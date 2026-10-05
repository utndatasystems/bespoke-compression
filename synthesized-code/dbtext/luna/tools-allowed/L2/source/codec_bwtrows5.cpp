#include "interface/codec.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <map>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
constexpr uint16_t SEP = 0xffffu;
constexpr uint32_t GROUP = 16u;
constexpr uint32_t BWT_GROUP = 256u;
constexpr uint32_t LZ_GROUP = 64u;
constexpr uint32_t FAST_BITS = 12u;
constexpr uint32_t FAST_SIZE = 1u << FAST_BITS;

struct Row {
    uint32_t start;
    uint32_t len;
    uint32_t bodyLen;
    uint8_t ending; // bit 0 CR, bit 1 LF
};

struct Pair { uint16_t a, b; };
struct FastEnt { uint16_t sym; uint8_t len; uint8_t pad; };
struct DecNode { int32_t ch[2]; int32_t sym; };
struct BwtHuff { std::vector<FastEnt> fast; std::vector<DecNode> tree; };
struct BwtBlockMeta {
    std::vector<BwtHuff> models;
    const uint8_t* selectors = nullptr;
    const uint8_t* payload = nullptr;
    uint32_t selectorCount = 0;
    uint32_t payloadLen = 0;
    uint32_t rawLen = 0;
    uint32_t primary = 0;
    // Open-time transform/index state. These are BWT-domain symbols and row positions.
    std::vector<uint8_t> last;
    std::vector<uint32_t> lfNext;
    // Rank checkpoints every eight bytes along the inverse-BWT cycle.
    std::vector<uint32_t> decodeSamples;
    std::vector<uint32_t> rowEndRank;
    std::vector<uint32_t> rowLen;
};

struct State {
    const uint8_t* archive = nullptr;
    size_t archiveSize = 0;
    uint8_t type = 0;
    uint32_t rawSize = 0;
    uint32_t rows = 0;

    // positional coder
    uint16_t bodyLen = 0;
    uint16_t bitsPerRow = 0;
    const uint8_t* ends = nullptr;
    const uint8_t* packed = nullptr;
    size_t packedBytes = 0;
    std::vector<uint8_t> posBits;
    std::vector<std::array<uint8_t, 256>> posMap;
    std::vector<uint16_t> posBitOffset;

    // Order-one conditional Huffman
    std::vector<int16_t> ctxDet;
    std::vector<int32_t> ctxRoot;
    std::vector<FastEnt> ctxFast;
    std::vector<DecNode> ctxTree;
    std::vector<int32_t> ctx2Index;
    std::vector<int16_t> ctx2Det;
    std::vector<int32_t> ctx2Root;
    std::vector<FastEnt> ctx2Fast;
    std::vector<DecNode> ctx2Tree;

    // Packed variable-length hexadecimal rows
    uint16_t hexMaxLen = 0;
    uint8_t hexLenBits = 0;
    uint8_t hexLower = 0;
    const uint8_t* hexLengths = nullptr;
    const uint8_t* hexDigits = nullptr;
    size_t hexLengthBytes = 0;
    size_t hexBytesPerRow = 0;

    // BPE + canonical Huffman
    const uint8_t* rowData = nullptr;
    const uint8_t* indexData = nullptr;
    uint32_t indexCount = 0;
    uint16_t mergeCount = 0;
    uint16_t symCount = 0;
    uint16_t baseSym = 256;
    uint16_t blockRows = 0;
    std::vector<Pair> merges;
    std::vector<uint8_t> bwtAlphabet;
    std::vector<BwtBlockMeta> bwtBlocks;
    std::vector<uint8_t> huffLen;
    std::vector<uint64_t> huffCode;
    std::vector<std::string> expansion;
    std::vector<FastEnt> fast;
    std::vector<DecNode> tree;
};

static inline uint16_t rd16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static inline uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline void wr16(std::vector<uint8_t>& o, uint16_t v) {
    o.push_back((uint8_t)v); o.push_back((uint8_t)(v >> 8));
}
static inline void wr32(std::vector<uint8_t>& o, uint32_t v) {
    o.push_back((uint8_t)v); o.push_back((uint8_t)(v >> 8));
    o.push_back((uint8_t)(v >> 16)); o.push_back((uint8_t)(v >> 24));
}
static inline void putVar(std::vector<uint8_t>& o, uint32_t v) {
    while (v >= 128) { o.push_back((uint8_t)(v | 128)); v >>= 7; }
    o.push_back((uint8_t)v);
}
static inline bool getVar(const uint8_t*& p, const uint8_t* end, uint32_t& v) {
    v = 0; unsigned sh = 0;
    while (p < end && sh <= 28) {
        uint8_t b = *p++;
        v |= (uint32_t)(b & 127u) << sh;
        if (!(b & 128u)) return true;
        sh += 7;
    }
    return false;
}

struct BitWriter {
    std::vector<uint8_t>& out;
    uint64_t bitpos = 0;
    explicit BitWriter(std::vector<uint8_t>& o) : out(o) {}
    void put(uint64_t value, unsigned n) {
        while (n) {
            unsigned room = 8u - (unsigned)(bitpos & 7u);
            unsigned take = n < room ? n : room;
            if ((bitpos & 7u) == 0) out.push_back(0);
            unsigned shift = n - take;
            uint8_t piece = (uint8_t)((value >> shift) & ((1u << take) - 1u));
            out.back() |= (uint8_t)(piece << (room - take));
            bitpos += take;
            n -= take;
        }
    }
};

static inline uint32_t getBits(const uint8_t* p, uint64_t bitpos, unsigned n) {
    uint32_t v = 0;
    while (n) {
        unsigned inByte = (unsigned)(bitpos & 7u);
        unsigned take = std::min(n, 8u - inByte);
        uint8_t mask = (uint8_t)((1u << take) - 1u);
        uint8_t x = (uint8_t)((p[bitpos >> 3] >> (8u - inByte - take)) & mask);
        v = (v << take) | x;
        bitpos += take;
        n -= take;
    }
    return v;
}

static std::vector<Row> splitRows(const uint8_t* raw, size_t size) {
    std::vector<Row> rows;
    size_t s = 0;
    while (s < size) {
        size_t e = s;
        while (e < size && raw[e] != '\n') ++e;
        uint8_t ending = 0;
        uint32_t len = (uint32_t)(e - s);
        if (e < size) { ++e; ending |= 2; }
        if (e > s && raw[e - 1] == '\n' && e - 1 > s && raw[e - 2] == '\r') ending |= 1;
        else if (e == size && e > s && raw[e - 1] == '\r') ending |= 1;
        uint32_t bodyLen = len;
        if (ending & 1) --bodyLen;
        rows.push_back(Row{(uint32_t)s, (uint32_t)(e - s), bodyLen, ending});
        s = e;
    }
    return rows;
}

static unsigned bitsFor(unsigned n) {
    if (n <= 1) return 0;
    unsigned b = 0, v = n - 1;
    while (v) { ++b; v >>= 1; }
    return b;
}

static bool makePositional(const uint8_t* raw, size_t size, const std::vector<Row>& rows,
                           std::vector<uint8_t>& archive) {
    if (rows.empty()) return false;
    uint32_t bodyLen = rows[0].bodyLen;
    if (bodyLen > 1024) return false;
    for (const auto& r : rows) if (r.bodyLen != bodyLen) return false;

    std::vector<std::array<uint32_t, 256>> freq(bodyLen);
    for (auto& a : freq) a.fill(0);
    for (const auto& r : rows)
        for (uint32_t j = 0; j < bodyLen; ++j)
            ++freq[j][raw[r.start + j]];

    std::vector<uint8_t> pbits(bodyLen);
    std::vector<std::array<uint8_t, 256>> maps(bodyLen);
    std::vector<std::array<int16_t, 256>> codes(bodyLen);
    uint32_t bitsPerRow = 0;
    size_t mapSize = 0;
    for (uint32_t j = 0; j < bodyLen; ++j) {
        unsigned count = 0;
        for (unsigned c = 0; c < 256; ++c) if (freq[j][c]) ++count;
        if (!count) return false;
        unsigned b = bitsFor(count);
        pbits[j] = (uint8_t)b;
        bitsPerRow += b;
        codes[j].fill(-1);
        unsigned k = 0;
        for (unsigned c = 0; c < 256; ++c) if (freq[j][c]) {
            codes[j][c] = (int16_t)k;
            maps[j][k++] = (uint8_t)c;
        }
        mapSize += 3u + count;
    }
    if (bitsPerRow > 65535) return false;
    size_t endsBytes = (rows.size() + 3) / 4;
    uint64_t streamBits = (uint64_t)bitsPerRow * rows.size();
    size_t streamBytes = (size_t)((streamBits + 7) / 8);
    size_t total = 17u + mapSize + endsBytes + streamBytes;
    if (total + 4 >= size) return false;

    std::vector<uint8_t> out;
    out.reserve(total);
    out.insert(out.end(), {'D','B','T','1'});
    out.push_back(1);
    wr32(out, (uint32_t)size);
    wr32(out, (uint32_t)rows.size());
    wr16(out, (uint16_t)bodyLen);
    wr16(out, (uint16_t)bitsPerRow);
    for (uint32_t j = 0; j < bodyLen; ++j) {
        unsigned count = 0;
        for (unsigned c = 0; c < 256; ++c) if (freq[j][c]) ++count;
        out.push_back(pbits[j]);
        wr16(out, (uint16_t)count);
        for (unsigned k = 0; k < count; ++k) out.push_back(maps[j][k]);
    }
    size_t endStart = out.size();
    out.resize(out.size() + endsBytes, 0);
    for (size_t i = 0; i < rows.size(); ++i)
        out[endStart + (i >> 2)] |= (uint8_t)(rows[i].ending << ((i & 3u) * 2u));
    BitWriter bw(out);
    for (const auto& r : rows) {
        for (uint32_t j = 0; j < bodyLen; ++j) {
            int16_t c = codes[j][raw[r.start + j]];
            if (c < 0) return false;
            if (pbits[j]) bw.put((uint16_t)c, pbits[j]);
        }
    }
    if (out.size() != total) return false;
    archive.swap(out);
    return true;
}

static int hexValue(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool makeHex(const uint8_t* raw, size_t size, const std::vector<Row>& rows,
                    std::vector<uint8_t>& archive) {
    if (rows.empty()) return false;
    uint32_t maxLen = 0;
    int letterMode = 0; // 0 no letters yet, 1 uppercase, 2 lowercase
    for (const Row& r : rows) {
        if (r.bodyLen > 16) return false;
        maxLen = std::max(maxLen, r.bodyLen);
        for (uint32_t j = 0; j < r.bodyLen; ++j) {
            uint8_t c = raw[r.start + j];
            if (hexValue(c) < 0) return false;
            if (c >= 'A' && c <= 'F') {
                if (letterMode == 2) return false;
                letterMode = 1;
            } else if (c >= 'a' && c <= 'f') {
                if (letterMode == 1) return false;
                letterMode = 2;
            }
        }
    }
    if (maxLen == 0) return false;
    unsigned lenBits = bitsFor(maxLen + 1);
    size_t endsBytes = (rows.size() + 3) / 4;
    size_t lenBytes = (size_t)(((uint64_t)rows.size() * lenBits + 7) / 8);
    size_t bytesPerRow = (maxLen + 1) / 2;
    size_t total = 17u + endsBytes + lenBytes + rows.size() * bytesPerRow;
    if (total >= size) return false;

    std::vector<uint8_t> out;
    out.reserve(total);
    out.insert(out.end(), {'D','B','T','1'});
    out.push_back(3);
    wr32(out, (uint32_t)size);
    wr32(out, (uint32_t)rows.size());
    wr16(out, (uint16_t)maxLen);
    out.push_back((uint8_t)lenBits);
    out.push_back((uint8_t)(letterMode == 2));
    size_t endsStart = out.size();
    out.resize(out.size() + endsBytes, 0);
    for (size_t i = 0; i < rows.size(); ++i)
        out[endsStart + (i >> 2)] |= (uint8_t)(rows[i].ending << ((i & 3u) * 2u));

    std::vector<uint8_t> packedLengths;
    packedLengths.reserve(lenBytes);
    BitWriter lw(packedLengths);
    for (const Row& r : rows) lw.put(r.bodyLen, lenBits);
    if (packedLengths.size() != lenBytes) return false;
    out.insert(out.end(), packedLengths.begin(), packedLengths.end());

    size_t digitsStart = out.size();
    out.resize(out.size() + rows.size() * bytesPerRow, 0);
    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        uint8_t* dst = out.data() + digitsStart + i * bytesPerRow;
        for (uint32_t j = 0; j < r.bodyLen; ++j) {
            uint8_t v = (uint8_t)hexValue(raw[r.start + j]);
            if ((j & 1u) == 0) dst[j >> 1] |= (uint8_t)(v << 4);
            else dst[j >> 1] |= v;
        }
    }
    if (out.size() != total) return false;
    archive.swap(out);
    return true;
}

static void trainBpe(std::vector<uint16_t>& tokens, std::vector<Pair>& merges, unsigned maxMerges, uint32_t baseSym);
static bool makeHuffman(const std::vector<uint64_t>& freq, std::vector<uint8_t>& lens,
                        std::vector<uint64_t>& codes);

static bool urlHostEnd(const uint8_t* raw, const Row& r, uint32_t& end) {
    uint32_t bodyEnd = r.start + r.len;
    if (r.ending & 2u) --bodyEnd;
    if (r.ending & 1u) --bodyEnd;
    uint32_t marker = UINT32_MAX;
    for (uint32_t p = r.start; p + 2 < bodyEnd; ++p) {
        if (raw[p] == ':' && raw[p+1] == '/' && raw[p+2] == '/') { marker = p + 3; break; }
    }
    if (marker == UINT32_MAX || marker == r.start) return false;
    end = bodyEnd;
    for (uint32_t p = marker; p < bodyEnd; ++p)
        if (raw[p] == '/' || raw[p] == '?' || raw[p] == '#') { end = p; break; }
    return end > r.start;
}

static bool makeUrlArchive(const uint8_t* raw, size_t size, const std::vector<Row>& rows,
                           std::vector<uint8_t>& archive) {
    if (rows.empty()) return false;
    std::unordered_map<std::string,uint32_t> counts;
    counts.reserve(rows.size()/4 + 16);
    std::vector<std::string> prefix(rows.size());
    std::vector<uint32_t> ends(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) {
        uint32_t e;
        if (!urlHostEnd(raw, rows[i], e)) return false;
        ends[i] = e;
        prefix[i].assign((const char*)raw + rows[i].start, e - rows[i].start);
        ++counts[prefix[i]];
    }
    std::map<std::string,uint16_t> sorted;
    for (const auto& x : counts) if (x.second >= 2) {
        if (sorted.size() >= 64000) return false;
        sorted.emplace(x.first, 0);
    }
    if (sorted.empty()) return false;
    uint32_t domainNo = 0;
    for (auto& x : sorted) x.second = (uint16_t)domainNo++;
    std::unordered_map<std::string,uint16_t> domainIds;
    domainIds.reserve(sorted.size()*2);
    for (const auto& x : sorted) domainIds.emplace(x.first,x.second);
    uint32_t baseSym = 256u + (uint32_t)sorted.size();
    if (baseSym + 768u >= SEP) return false;

    std::vector<uint16_t> tokens;
    tokens.reserve(size + rows.size());
    for (size_t i = 0; i < rows.size(); ++i) {
        auto it = domainIds.find(prefix[i]);
        if (it != domainIds.end()) {
            tokens.push_back((uint16_t)(256u + it->second));
            for (uint32_t p = ends[i]; p < rows[i].start + rows[i].len; ++p) tokens.push_back(raw[p]);
        } else {
            for (uint32_t p = rows[i].start; p < rows[i].start + rows[i].len; ++p) tokens.push_back(raw[p]);
        }
        tokens.push_back(SEP);
    }
    std::vector<Pair> merges;
    trainBpe(tokens, merges, 2048, baseSym);
    uint32_t endSym = baseSym + (uint32_t)merges.size();
    if (endSym >= SEP) return false;
    uint32_t symCount = endSym + 1;
    std::vector<uint64_t> freq(symCount, 0);
    for (uint16_t t : tokens) if (t != SEP && t < endSym) ++freq[t];
    for (const Row& r : rows) { (void)r; ++freq[endSym]; }
    std::vector<uint8_t> lens;
    std::vector<uint64_t> code;
    if (!makeHuffman(freq,lens,code)) return false;

    std::vector<uint8_t> rowData,payload;
    std::vector<uint32_t> indexes;
    indexes.reserve((rows.size()+GROUP-1)/GROUP);
    size_t ti=0;
    for (uint32_t ri=0;ri<rows.size();++ri) {
        if ((ri%GROUP)==0) indexes.push_back((uint32_t)rowData.size());
        payload.clear(); BitWriter bw(payload);
        while (ti<tokens.size() && tokens[ti]!=SEP) {
            uint16_t t=tokens[ti++]; unsigned l=lens[t];
            if (!l || l>63) return false;
            bw.put(code[t],l);
        }
        if (ti>=tokens.size() || tokens[ti]!=SEP) return false;
        ++ti; bw.put(code[endSym],lens[endSym]);
        putVar(rowData,(uint32_t)payload.size());
        rowData.insert(rowData.end(),payload.begin(),payload.end());
    }
    if (ti!=tokens.size()) return false;

    std::vector<uint8_t> coded;
    coded.insert(coded.end(),{'D','B','T','1'}); coded.push_back(7);
    wr32(coded,(uint32_t)size); wr32(coded,(uint32_t)rows.size());
    wr16(coded,(uint16_t)sorted.size()); wr16(coded,(uint16_t)merges.size());
    wr16(coded,(uint16_t)symCount); wr16(coded,(uint16_t)GROUP);
    wr32(coded,(uint32_t)indexes.size());
    for (const auto& x : sorted) {
        putVar(coded,(uint32_t)x.first.size());
        coded.insert(coded.end(),x.first.begin(),x.first.end());
    }
    for (const Pair& p : merges) { wr16(coded,p.a); wr16(coded,p.b); }
    coded.insert(coded.end(),lens.begin(),lens.end());
    for (uint32_t x : indexes) wr32(coded,x);
    coded.insert(coded.end(),rowData.begin(),rowData.end());
    coded.insert(coded.end(),4,0);

    std::vector<uint8_t> rawArchive;
    size_t ic=(rows.size()+GROUP-1)/GROUP;
    rawArchive.reserve(17+ic*4+size);
    rawArchive.insert(rawArchive.end(),{'D','B','T','1'}); rawArchive.push_back(0);
    wr32(rawArchive,(uint32_t)size); wr32(rawArchive,(uint32_t)rows.size()); wr32(rawArchive,(uint32_t)ic);
    for (uint32_t i=0;i<rows.size();i+=GROUP) wr32(rawArchive,rows[i].start);
    rawArchive.insert(rawArchive.end(),raw,raw+size);
    if (coded.size()>=rawArchive.size()) archive.swap(rawArchive);
    else archive.swap(coded);
    return true;
}

struct HNode {
    uint64_t freq;
    int32_t left, right, sym;
    uint32_t minSym;
};
struct HCmp {
    const std::vector<HNode>* n;
    bool operator()(int32_t a, int32_t b) const {
        const HNode& x = (*n)[a]; const HNode& y = (*n)[b];
        if (x.freq != y.freq) return x.freq > y.freq;
        if (x.minSym != y.minSym) return x.minSym > y.minSym;
        return a > b;
    }
};

static bool makeHuffman(const std::vector<uint64_t>& freq, std::vector<uint8_t>& lens,
                        std::vector<uint64_t>& codes) {
    std::vector<HNode> nodes;
    std::priority_queue<int32_t, std::vector<int32_t>, HCmp> pq((HCmp{&nodes}));
    for (uint32_t s = 0; s < freq.size(); ++s) if (freq[s]) {
        nodes.push_back(HNode{freq[s], -1, -1, (int32_t)s, s});
        pq.push((int32_t)nodes.size() - 1);
    }
    if (pq.empty()) return false;
    if (pq.size() == 1) {
        lens.assign(freq.size(), 0); codes.assign(freq.size(), 0);
        lens[nodes[pq.top()].sym] = 1;
        return true;
    }
    while (pq.size() > 1) {
        int32_t a = pq.top(); pq.pop();
        int32_t b = pq.top(); pq.pop();
        HNode x{nodes[a].freq + nodes[b].freq, a, b, -1,
                std::min(nodes[a].minSym, nodes[b].minSym)};
        nodes.push_back(x);
        pq.push((int32_t)nodes.size() - 1);
    }
    lens.assign(freq.size(), 0);
    struct StackItem { int32_t n; uint16_t d; };
    std::vector<StackItem> st;
    st.push_back({pq.top(), 0});
    unsigned maxLen = 0;
    while (!st.empty()) {
        auto x = st.back(); st.pop_back();
        const HNode& n = nodes[x.n];
        if (n.sym >= 0) {
            if (x.d == 0 || x.d > 63) return false;
            lens[(uint32_t)n.sym] = (uint8_t)x.d;
            maxLen = std::max(maxLen, (unsigned)x.d);
        } else {
            st.push_back({n.left, (uint16_t)(x.d + 1)});
            st.push_back({n.right, (uint16_t)(x.d + 1)});
        }
    }
    codes.assign(freq.size(), 0);
    std::vector<uint32_t> count(maxLen + 1, 0);
    for (uint8_t l : lens) if (l) ++count[l];
    uint64_t c = 0;
    for (unsigned l = 1; l <= maxLen; ++l) {
        if (l > 1) c <<= 1;
        for (uint32_t s = 0; s < lens.size(); ++s) if (lens[s] == l) codes[s] = c++;
    }
    return true;
}

static void trainBpeImpl(std::vector<uint16_t>& tokens, std::vector<Pair>& merges,
                         unsigned maxMerges, uint32_t baseSym,
                         const std::vector<unsigned>* captureAt,
                         std::vector<std::vector<uint16_t>>* snapshots,
                         std::vector<unsigned>* snapshotCounts) {
    struct Bucket { int32_t head = -1; uint32_t count = 0, version = 0; };
    struct Item { uint32_t count, key, version; };
    struct Cmp {
        bool operator()(const Item& a, const Item& b) const {
            if (a.count != b.count) return a.count < b.count;
            if (a.key != b.key) return a.key > b.key;
            return a.version < b.version;
        }
    };
    constexpr uint32_t NONE = UINT32_MAX;
    const size_t n = tokens.size();
    if (!n || n > (size_t)INT32_MAX) return;
    std::vector<int32_t> prev(n), next(n), occPrev(n, -1), occNext(n, -1);
    std::vector<uint32_t> pairKey(n, NONE);
    for (size_t i = 0; i < n; ++i) {
        prev[i] = i ? (int32_t)(i - 1) : -1;
        next[i] = i + 1 < n ? (int32_t)(i + 1) : -1;
    }
    std::unordered_map<uint32_t,Bucket> buckets;
    buckets.reserve(std::min<size_t>(n, 1u << 20));
    auto addInitial = [&](int32_t u) {
        int32_t v = next[(size_t)u];
        if (v < 0 || tokens[(size_t)u] == SEP || tokens[(size_t)v] == SEP) return;
        uint32_t key = ((uint32_t)tokens[(size_t)u] << 16) | tokens[(size_t)v];
        Bucket& b = buckets[key];
        occPrev[(size_t)u] = -1;
        occNext[(size_t)u] = b.head;
        if (b.head >= 0) occPrev[(size_t)b.head] = u;
        b.head = u;
        ++b.count; ++b.version;
        pairKey[(size_t)u] = key;
    };
    for (size_t i = 0; i + 1 < n; ++i) addInitial((int32_t)i);

    std::priority_queue<Item,std::vector<Item>,Cmp> heap;
    for (const auto& x : buckets)
        if (x.second.count >= 2)
            heap.push(Item{x.second.count, x.first, x.second.version});

    std::unordered_set<uint32_t> changed;
    changed.reserve(256);
    auto addBoundary = [&](int32_t u) {
        int32_t v = next[(size_t)u];
        pairKey[(size_t)u] = NONE;
        occPrev[(size_t)u] = occNext[(size_t)u] = -1;
        if (v < 0 || tokens[(size_t)u] == SEP || tokens[(size_t)v] == SEP) return;
        uint32_t key = ((uint32_t)tokens[(size_t)u] << 16) | tokens[(size_t)v];
        Bucket& b = buckets[key];
        occPrev[(size_t)u] = -1;
        occNext[(size_t)u] = b.head;
        if (b.head >= 0) occPrev[(size_t)b.head] = u;
        b.head = u;
        ++b.count; ++b.version;
        pairKey[(size_t)u] = key;
        changed.insert(key);
    };
    auto removeBoundary = [&](int32_t u) {
        uint32_t key = pairKey[(size_t)u];
        if (key == NONE) return;
        auto it = buckets.find(key);
        if (it == buckets.end() || it->second.count == 0) return;
        Bucket& b = it->second;
        int32_t op = occPrev[(size_t)u], on = occNext[(size_t)u];
        if (op >= 0) occNext[(size_t)op] = on;
        else b.head = on;
        if (on >= 0) occPrev[(size_t)on] = op;
        pairKey[(size_t)u] = NONE;
        occPrev[(size_t)u] = occNext[(size_t)u] = -1;
        --b.count; ++b.version;
        changed.insert(key);
    };

    auto saveSnapshot = [&]() {
        std::vector<uint16_t> snapshot;
        snapshot.reserve(n);
        int32_t at = 0;
        while (at >= 0) {
            if (prev[(size_t)at] != -2) snapshot.push_back(tokens[(size_t)at]);
            at = next[(size_t)at];
        }
        snapshots->push_back(std::move(snapshot));
        snapshotCounts->push_back((unsigned)merges.size());
    };
    size_t nextCapture = 0;
    merges.clear();
    merges.reserve(maxMerges);
    for (unsigned iteration = 0; iteration < maxMerges; ++iteration) {
        Item best{};
        bool found = false;
        while (!heap.empty()) {
            Item x = heap.top();
            auto it = buckets.find(x.key);
            if (it != buckets.end() && it->second.count == x.count &&
                it->second.version == x.version && x.count >= 2) {
                best = x; found = true; break;
            }
            heap.pop();
        }
        if (!found) break;
        uint16_t a = (uint16_t)(best.key >> 16);
        uint16_t b = (uint16_t)best.key;
        uint32_t newSym32 = baseSym + (uint32_t)merges.size();
        if (newSym32 >= SEP) break;
        uint16_t newSym = (uint16_t)newSym32;
        merges.push_back(Pair{a,b});
        heap.pop();

        std::vector<int32_t> occurrences;
        occurrences.reserve(best.count);
        auto bit = buckets.find(best.key);
        if (bit == buckets.end()) break;
        for (int32_t u = bit->second.head; u >= 0; u = occNext[(size_t)u])
            occurrences.push_back(u);

        changed.clear();
        for (int32_t u : occurrences) {
            if (prev[(size_t)u] == -2 || pairKey[(size_t)u] != best.key) continue;
            int32_t v = next[(size_t)u];
            if (v < 0 || prev[(size_t)v] == -2 ||
                tokens[(size_t)u] != a || tokens[(size_t)v] != b) continue;
            int32_t left = prev[(size_t)u];
            int32_t right = next[(size_t)v];
            if (left >= 0) removeBoundary(left);
            removeBoundary(u);
            removeBoundary(v);
            if (left >= 0) next[(size_t)left] = u;
            prev[(size_t)u] = left;
            next[(size_t)u] = right;
            if (right >= 0) prev[(size_t)right] = u;
            prev[(size_t)v] = -2;
            next[(size_t)v] = -2;
            tokens[(size_t)u] = newSym;
            if (left >= 0) addBoundary(left);
            addBoundary(u);
        }
        for (uint32_t key : changed) {
            auto it = buckets.find(key);
            if (it != buckets.end() && it->second.count >= 2)
                heap.push(Item{it->second.count, key, it->second.version});
        }
        if (heap.size() > buckets.size() * 3u + 10000u) {
            std::priority_queue<Item,std::vector<Item>,Cmp> rebuilt;
            for (const auto& x : buckets)
                if (x.second.count >= 2)
                    rebuilt.push(Item{x.second.count, x.first, x.second.version});
            heap.swap(rebuilt);
        }
        if (captureAt && nextCapture < captureAt->size() &&
            merges.size() == (*captureAt)[nextCapture]) {
            saveSnapshot();
            ++nextCapture;
        }
    }
    if (captureAt && (snapshotCounts->empty() || snapshotCounts->back() != merges.size()))
        saveSnapshot();

    std::vector<uint16_t> compact;
    compact.reserve(n);
    int32_t node = 0;
    while (node >= 0) {
        if (prev[(size_t)node] != -2) compact.push_back(tokens[(size_t)node]);
        node = next[(size_t)node];
    }
    tokens.swap(compact);
}

static void trainBpe(std::vector<uint16_t>& tokens, std::vector<Pair>& merges,
                     unsigned maxMerges, uint32_t baseSym = 256) {
    trainBpeImpl(tokens, merges, maxMerges, baseSym, nullptr, nullptr, nullptr);
}
static void trainBpeSnapshots(std::vector<uint16_t>& tokens, std::vector<Pair>& merges,
                              unsigned maxMerges, uint32_t baseSym,
                              const std::vector<unsigned>& captureAt,
                              std::vector<std::vector<uint16_t>>& snapshots,
                              std::vector<unsigned>& snapshotCounts) {
    trainBpeImpl(tokens, merges, maxMerges, baseSym, &captureAt, &snapshots, &snapshotCounts);
}

static bool makeBpeArchive(const uint8_t* raw, size_t size, const std::vector<Row>& rows,
                           std::vector<uint8_t>& archive) {
    if (rows.empty()) return false;
    std::vector<uint16_t> tokens;
    tokens.reserve(size + rows.size());
    for (const Row& r : rows) {
        for (uint32_t j = 0; j < r.len; ++j) tokens.push_back(raw[r.start + j]);
        tokens.push_back(SEP);
    }
    std::vector<Pair> merges;
    trainBpe(tokens, merges, 768);
    uint32_t endSym = 256u + (uint32_t)merges.size();
    uint32_t symCount = endSym + 1;
    std::vector<uint64_t> freq(symCount, 0);
    for (uint16_t t : tokens) if (t != SEP && t < endSym) ++freq[t];
    for (const Row& r : rows) ++freq[endSym];
    std::vector<uint8_t> lens;
    std::vector<uint64_t> code;
    if (!makeHuffman(freq, lens, code)) return false;

    std::vector<uint8_t> rowData;
    std::vector<uint32_t> indexes;
    indexes.reserve((rows.size() + GROUP - 1) / GROUP);
    std::vector<uint8_t> payload;
    size_t ti = 0;
    for (uint32_t ri = 0; ri < rows.size(); ++ri) {
        if ((ri % GROUP) == 0) indexes.push_back((uint32_t)rowData.size());
        payload.clear();
        BitWriter bw(payload);
        while (ti < tokens.size() && tokens[ti] != SEP) {
            uint16_t t = tokens[ti++];
            unsigned l = lens[t];
            if (!l || l > 63) return false;
            bw.put(code[t], l);
        }
        if (ti >= tokens.size() || tokens[ti] != SEP) return false;
        ++ti;
        bw.put(code[endSym], lens[endSym]);
        putVar(rowData, (uint32_t)payload.size());
        rowData.insert(rowData.end(), payload.begin(), payload.end());
    }
    if (ti != tokens.size()) return false;

    std::vector<uint8_t> bpe;
    bpe.reserve(17 + merges.size()*4 + lens.size() + 6 + indexes.size()*4 + rowData.size() + 4);
    bpe.insert(bpe.end(), {'D','B','T','1'});
    bpe.push_back(2);
    wr32(bpe, (uint32_t)size);
    wr32(bpe, (uint32_t)rows.size());
    wr16(bpe, (uint16_t)merges.size());
    wr16(bpe, (uint16_t)symCount);
    for (const Pair& p : merges) { wr16(bpe, p.a); wr16(bpe, p.b); }
    bpe.insert(bpe.end(), lens.begin(), lens.end());
    wr16(bpe, (uint16_t)GROUP);
    wr32(bpe, (uint32_t)indexes.size());
    for (uint32_t x : indexes) wr32(bpe, x);
    bpe.insert(bpe.end(), rowData.begin(), rowData.end());
    bpe.insert(bpe.end(), 4, 0);

    std::vector<uint8_t> rawArchive;
    size_t rawIndexCount = (rows.size() + GROUP - 1) / GROUP;
    rawArchive.reserve(13 + rawIndexCount * 4 + size);
    rawArchive.insert(rawArchive.end(), {'D','B','T','1'});
    rawArchive.push_back(0);
    wr32(rawArchive, (uint32_t)size);
    wr32(rawArchive, (uint32_t)rows.size());
    wr32(rawArchive, (uint32_t)rawIndexCount);
    for (uint32_t i = 0; i < rows.size(); i += GROUP) wr32(rawArchive, rows[i].start);
    rawArchive.insert(rawArchive.end(), raw, raw + size);

    if (bpe.size() >= rawArchive.size()) archive.swap(rawArchive);
    else archive.swap(bpe);
    return true;
}



static inline bool wordByte(uint8_t c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c >= 0x80;
}

struct WordItem {
    std::string word;
    uint32_t freq;
};

static bool makeWordArchive(const uint8_t* raw, size_t size, const std::vector<Row>& rows,
                            std::vector<uint8_t>& archive) {
    if (rows.empty()) return false;
    std::unordered_map<std::string,uint32_t> counts;
    counts.reserve(size / 12 + 32);
    for (const Row& r : rows) {
        uint32_t p = r.start, end = r.start + r.len;
        while (p < end) {
            if (!wordByte(raw[p])) { ++p; continue; }
            uint32_t a = p++;
            while (p < end && wordByte(raw[p])) ++p;
            ++counts[std::string((const char*)raw + a, p - a)];
        }
    }

    std::vector<WordItem> words;
    words.reserve(counts.size());
    for (const auto& x : counts)
        if (x.second >= 2 && x.first.size() >= 2)
            words.push_back(WordItem{x.first, x.second});
    if (words.empty()) return false;
    constexpr size_t MAX_WORDS = 60000;
    if (words.size() > MAX_WORDS) {
        auto gain = [](const WordItem& x) -> uint64_t {
            return (uint64_t)(x.freq - 1u) * (uint64_t)(x.word.size() - 1u);
        };
        std::nth_element(words.begin(), words.begin() + MAX_WORDS, words.end(),
            [&](const WordItem& a, const WordItem& b) {
                uint64_t ga = gain(a), gb = gain(b);
                if (ga != gb) return ga > gb;
                if (a.freq != b.freq) return a.freq > b.freq;
                return a.word < b.word;
            });
        words.resize(MAX_WORDS);
    }
    std::sort(words.begin(), words.end(), [](const WordItem& a, const WordItem& b) {
        return a.word < b.word;
    });

    const uint32_t wordCount = (uint32_t)words.size();
    const uint32_t endSym = 256u + wordCount;
    const uint32_t symCount = endSym + 1u;
    if (symCount > 65535u) return false;
    std::unordered_map<std::string,uint16_t> wordIds;
    wordIds.reserve(wordCount * 2u);
    for (uint32_t i = 0; i < wordCount; ++i)
        wordIds.emplace(words[i].word, (uint16_t)(256u + i));

    std::vector<uint64_t> freq(symCount, 0);
    for (const Row& r : rows) {
        uint32_t p = r.start, end = r.start + r.len;
        while (p < end) {
            if (!wordByte(raw[p])) { ++freq[raw[p++]]; continue; }
            uint32_t a = p++;
            while (p < end && wordByte(raw[p])) ++p;
            std::string key((const char*)raw + a, p - a);
            auto it = wordIds.find(key);
            if (it != wordIds.end()) ++freq[it->second];
            else for (uint32_t j = a; j < p; ++j) ++freq[raw[j]];
        }
        ++freq[endSym];
    }

    std::vector<uint8_t> lens;
    std::vector<uint64_t> codes;
    if (!makeHuffman(freq, lens, codes)) return false;

    std::vector<uint8_t> rowData;
    std::vector<uint32_t> indexes;
    indexes.reserve((rows.size() + GROUP - 1) / GROUP);
    for (uint32_t first = 0; first < rows.size(); first += GROUP) {
        indexes.push_back((uint32_t)rowData.size());
        BitWriter bw(rowData);
        uint32_t last = std::min<uint32_t>((uint32_t)rows.size(), first + GROUP);
        for (uint32_t ri = first; ri < last; ++ri) {
            const Row& r = rows[ri];
            uint32_t p = r.start, end = r.start + r.len;
            while (p < end) {
                uint16_t sym;
                if (!wordByte(raw[p])) {
                    sym = raw[p++];
                } else {
                    uint32_t a = p++;
                    while (p < end && wordByte(raw[p])) ++p;
                    std::string key((const char*)raw + a, p - a);
                    auto it = wordIds.find(key);
                    if (it != wordIds.end()) {
                        sym = it->second;
                    } else {
                        for (uint32_t j = a; j < p; ++j) {
                            uint8_t b = raw[j];
                            unsigned l = lens[b];
                            if (!l) return false;
                            bw.put(codes[b], l);
                        }
                        continue;
                    }
                }
                unsigned l = lens[sym];
                if (!l) return false;
                bw.put(codes[sym], l);
            }
            bw.put(codes[endSym], lens[endSym]);
        }
    }

    std::vector<uint8_t> coded;
    coded.reserve(27u + wordCount * 4u + symCount + indexes.size() * 4u + rowData.size() + 4u);
    coded.insert(coded.end(), {'D','B','T','1'});
    coded.push_back(9);
    wr32(coded, (uint32_t)size);
    wr32(coded, (uint32_t)rows.size());
    wr16(coded, (uint16_t)GROUP);
    wr32(coded, (uint32_t)indexes.size());
    wr16(coded, (uint16_t)wordCount);
    wr16(coded, (uint16_t)symCount);
    std::string prev;
    for (const WordItem& item : words) {
        size_t cp = 0, limit = std::min(prev.size(), item.word.size());
        while (cp < limit && prev[cp] == item.word[cp]) ++cp;
        putVar(coded, (uint32_t)cp);
        putVar(coded, (uint32_t)(item.word.size() - cp));
        coded.insert(coded.end(), item.word.begin() + cp, item.word.end());
        prev = item.word;
    }
    coded.insert(coded.end(), lens.begin(), lens.end());
    for (uint32_t x : indexes) wr32(coded, x);
    coded.insert(coded.end(), rowData.begin(), rowData.end());
    coded.insert(coded.end(), 4, 0);
    if (coded.size() >= size) return false;
    archive.swap(coded);
    return true;
}

static bool makeWordBpeArchive(const uint8_t* raw, size_t size, const std::vector<Row>& rows,
                              std::vector<uint8_t>& archive) {
    if (rows.empty()) return false;
    std::unordered_map<std::string,uint32_t> counts;
    counts.reserve(size / 12 + 32);
    for (const Row& r : rows) {
        uint32_t p = r.start, end = r.start + r.len;
        while (p < end) {
            if (!wordByte(raw[p])) { ++p; continue; }
            uint32_t a = p++;
            while (p < end && wordByte(raw[p])) ++p;
            ++counts[std::string((const char*)raw + a, p - a)];
        }
    }
    std::vector<WordItem> words;
    words.reserve(counts.size());
    for (const auto& x : counts)
        if (x.second >= 2 && x.first.size() >= 2)
            words.push_back(WordItem{x.first, x.second});
    if (words.empty()) return false;
    constexpr size_t MAX_WORDS = 60000;
    if (words.size() > MAX_WORDS) {
        auto gain = [](const WordItem& x) -> uint64_t {
            return (uint64_t)(x.freq - 1u) * (uint64_t)(x.word.size() - 1u);
        };
        std::nth_element(words.begin(), words.begin() + MAX_WORDS, words.end(),
            [&](const WordItem& a, const WordItem& b) {
                uint64_t ga = gain(a), gb = gain(b);
                if (ga != gb) return ga > gb;
                if (a.freq != b.freq) return a.freq > b.freq;
                return a.word < b.word;
            });
        words.resize(MAX_WORDS);
    }
    std::sort(words.begin(), words.end(), [](const WordItem& a, const WordItem& b) {
        return a.word < b.word;
    });

    const uint32_t wordCount = (uint32_t)words.size();
    const uint32_t baseSym = 256u + wordCount;
    if (baseSym + 1u >= SEP) return false;
    std::unordered_map<std::string,uint16_t> wordIds;
    wordIds.reserve(wordCount * 2u);
    for (uint32_t i = 0; i < wordCount; ++i)
        wordIds.emplace(words[i].word, (uint16_t)(256u + i));

    std::vector<uint16_t> tokens;
    tokens.reserve(size / 2 + rows.size());
    for (const Row& r : rows) {
        uint32_t p = r.start, end = r.start + r.len;
        while (p < end) {
            if (!wordByte(raw[p])) { tokens.push_back(raw[p++]); continue; }
            uint32_t a = p++;
            while (p < end && wordByte(raw[p])) ++p;
            std::string key((const char*)raw + a, p - a);
            auto it = wordIds.find(key);
            if (it != wordIds.end()) tokens.push_back(it->second);
            else for (uint32_t j = a; j < p; ++j) tokens.push_back(raw[j]);
        }
        tokens.push_back(SEP);
    }

    const unsigned maxMerges = std::min<unsigned>(32768u, 65534u - baseSym);
    std::vector<unsigned> points{512,1024,2048,4096,8192,16384,32768};
    points.erase(std::remove_if(points.begin(), points.end(),
                  [&](unsigned x) { return x > maxMerges; }), points.end());
    std::vector<Pair> merges;
    std::vector<std::vector<uint16_t>> snapshots;
    std::vector<unsigned> snapshotCounts;
    trainBpeSnapshots(tokens, merges, maxMerges, baseSym, points, snapshots, snapshotCounts);
    if (snapshots.empty() || snapshotCounts.size() != snapshots.size()) return false;

    std::vector<uint8_t> dictionary;
    std::string prev;
    for (const WordItem& item : words) {
        size_t cp = 0, limit = std::min(prev.size(), item.word.size());
        while (cp < limit && prev[cp] == item.word[cp]) ++cp;
        putVar(dictionary, (uint32_t)cp);
        putVar(dictionary, (uint32_t)(item.word.size() - cp));
        dictionary.insert(dictionary.end(), item.word.begin() + cp, item.word.end());
        prev = item.word;
    }

    std::vector<uint8_t> best;
    for (size_t candidate = 0; candidate < snapshots.size(); ++candidate) {
        const std::vector<uint16_t>& state = snapshots[candidate];
        const uint32_t mergeCount = snapshotCounts[candidate];
        const uint32_t endSym = baseSym + mergeCount;
        const uint32_t symCount = endSym + 1u;
        if (symCount > 65535u || mergeCount > merges.size()) continue;
        std::vector<uint64_t> freq(symCount, 0);
        for (uint16_t t : state) if (t != SEP && t < endSym) ++freq[t];
        for (const Row& r : rows) if (!(r.ending & 2u)) ++freq[endSym];
        std::vector<uint8_t> lens;
        std::vector<uint64_t> codes;
        if (!makeHuffman(freq, lens, codes)) continue;

        std::vector<uint8_t> rowData;
        std::vector<uint32_t> indexes;
        indexes.reserve((rows.size() + GROUP - 1) / GROUP);
        size_t ti = 0;
        bool valid = true;
        for (uint32_t first = 0; first < rows.size(); first += GROUP) {
            indexes.push_back((uint32_t)rowData.size());
            BitWriter bw(rowData);
            uint32_t last = std::min<uint32_t>((uint32_t)rows.size(), first + GROUP);
            for (uint32_t ri = first; ri < last; ++ri) {
                while (ti < state.size() && state[ti] != SEP) {
                    uint16_t t = state[ti++];
                    if (t >= endSym || !lens[t]) { valid = false; break; }
                    bw.put(codes[t], lens[t]);
                }
                if (!valid || ti >= state.size() || state[ti] != SEP) { valid = false; break; }
                ++ti;
                if (!(rows[ri].ending & 2u)) {
                    if (!lens[endSym]) { valid = false; break; }
                    bw.put(codes[endSym], lens[endSym]);
                }
            }
            if (!valid) break;
        }
        if (!valid || ti != state.size()) continue;

        std::vector<uint8_t> coded;
        coded.reserve(27u + dictionary.size() + mergeCount * 4u + symCount +
                      indexes.size() * 4u + rowData.size() + 4u);
        coded.insert(coded.end(), {'D','B','T','1'});
        coded.push_back(10);
        wr32(coded, (uint32_t)size);
        wr32(coded, (uint32_t)rows.size());
        wr16(coded, (uint16_t)wordCount);
        wr16(coded, (uint16_t)mergeCount);
        wr16(coded, (uint16_t)symCount);
        wr16(coded, (uint16_t)GROUP);
        wr32(coded, (uint32_t)indexes.size());
        coded.insert(coded.end(), dictionary.begin(), dictionary.end());
        for (uint32_t i = 0; i < mergeCount; ++i) {
            wr16(coded, merges[i].a);
            wr16(coded, merges[i].b);
        }
        coded.insert(coded.end(), lens.begin(), lens.end());
        for (uint32_t x : indexes) wr32(coded, x);
        coded.insert(coded.end(), rowData.begin(), rowData.end());
        coded.insert(coded.end(), 4, 0);
        if (best.empty() || coded.size() < best.size()) best.swap(coded);
    }
    if (best.empty() || best.size() >= size) return false;
    archive.swap(best);
    return true;
}

struct BwtEvent { uint16_t sym; uint16_t pad; uint32_t run; };
struct BwtBlock { uint32_t rawLen; uint32_t primary; std::vector<BwtEvent> events; };

static void bwtTransform(const uint8_t* input, uint32_t n, std::vector<uint8_t>& last, uint32_t& primary) {
    last.resize(n);
    if (n == 0) { primary = 0; return; }
    std::vector<uint32_t> p(n), c(n), pn(n), cn(n);
    std::array<uint32_t,256> count{};
    for (uint32_t i = 0; i < n; ++i) ++count[input[i]];
    uint32_t sum = 0;
    for (uint32_t i = 0; i < 256; ++i) { uint32_t x=count[i]; count[i]=sum; sum+=x; }
    for (uint32_t i = 0; i < n; ++i) p[count[input[i]]++] = i;
    uint32_t classes = 1;
    c[p[0]] = 0;
    for (uint32_t i = 1; i < n; ++i) {
        if (input[p[i]] != input[p[i-1]]) ++classes;
        c[p[i]] = classes - 1;
    }
    for (uint64_t len = 1; len < n && classes < n; len <<= 1) {
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t pi = p[i], step = (uint32_t)len;
            pn[i] = pi >= step ? pi - step : pi + n - step;
        }
        std::vector<uint32_t> cc(classes, 0);
        for (uint32_t i = 0; i < n; ++i) ++cc[c[pn[i]]];
        sum = 0;
        for (uint32_t i = 0; i < classes; ++i) { uint32_t x=cc[i]; cc[i]=sum; sum+=x; }
        for (uint32_t i = 0; i < n; ++i) p[cc[c[pn[i]]]++] = pn[i];
        cn[p[0]] = 0;
        uint32_t newClasses = 1;
        for (uint32_t i = 1; i < n; ++i) {
            uint32_t cur1 = c[p[i]], prev1 = c[p[i-1]];
            uint32_t x = p[i], y = p[i-1], step = (uint32_t)len;
            uint32_t cur2 = c[x + step < n ? x + step : x + step - n];
            uint32_t prev2 = c[y + step < n ? y + step : y + step - n];
            if (cur1 != prev1 || cur2 != prev2) ++newClasses;
            cn[p[i]] = newClasses - 1;
        }
        c.swap(cn); classes = newClasses;
    }
    primary = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if (p[i] == 0) primary = i;
        last[i] = input[p[i] ? p[i] - 1 : n - 1];
    }
}

static bool makeBwtGroupModels(const std::vector<BwtEvent>& events, uint32_t symCount,
                               uint8_t& groupCount, std::vector<uint8_t>& selectors,
                               std::vector<std::vector<uint8_t>>& lengths,
                               std::vector<std::vector<uint64_t>>& codes) {
    if (events.empty()) return false;
    uint32_t chunks = ((uint32_t)events.size() + 49u) / 50u;
    uint32_t groups = events.size() < 200u ? 2u :
                      events.size() < 600u ? 3u :
                      events.size() < 1200u ? 4u :
                      events.size() < 2400u ? 5u : 6u;
    groups = std::max<uint32_t>(1u, std::min(groups, chunks));
    groupCount = (uint8_t)groups;
    selectors.resize(chunks);
    for (uint32_t c = 0; c < chunks; ++c) selectors[c] = (uint8_t)(c % groups);
    lengths.resize(groups);
    codes.resize(groups);

    auto rebuild = [&]() -> bool {
        std::vector<std::vector<uint64_t>> freq(groups, std::vector<uint64_t>(symCount, 0));
        for (uint32_t c = 0; c < chunks; ++c) {
            uint32_t first = c * 50u, last = std::min<uint32_t>((uint32_t)events.size(), first + 50u);
            std::vector<uint64_t>& f = freq[selectors[c]];
            for (uint32_t i = first; i < last; ++i) ++f[events[i].sym];
        }
        for (uint32_t g = 0; g < groups; ++g)
            if (!makeHuffman(freq[g], lengths[g], codes[g])) return false;
        return true;
    };
    if (!rebuild()) return false;
    for (unsigned iteration = 0; iteration < 5; ++iteration) {
        for (uint32_t c = 0; c < chunks; ++c) {
            uint32_t first = c * 50u, last = std::min<uint32_t>((uint32_t)events.size(), first + 50u);
            uint32_t best = selectors[c];
            uint64_t bestCost = UINT64_MAX;
            for (uint32_t g = 0; g < groups; ++g) {
                uint64_t cost = 0;
                bool valid = true;
                for (uint32_t i = first; i < last; ++i) {
                    uint8_t len = lengths[g][events[i].sym];
                    if (!len) { valid = false; break; }
                    cost += len;
                }
                if (valid && (cost < bestCost || (cost == bestCost && g == selectors[c]))) {
                    best = g; bestCost = cost;
                }
            }
            if (bestCost == UINT64_MAX) return false;
            selectors[c] = (uint8_t)best;
        }
        if (!rebuild()) return false;
    }
    return true;
}

static bool makeBwtArchive(const uint8_t* raw, size_t size, const std::vector<Row>& rows,
                           std::vector<uint8_t>& archive) {
    if (rows.size() < 2) return false;
    uint32_t avgRow = (uint32_t)std::max<size_t>(1, size / rows.size());
    uint32_t targetBytes = (uint32_t)std::min<size_t>(900000u, std::max<size_t>(1, size / 2));
    uint32_t groupRows = std::max<uint32_t>(1, targetBytes / avgRow);
    groupRows = std::min<uint32_t>(groupRows, 65535u);
    groupRows = std::min<uint32_t>(groupRows, std::max<uint32_t>(1, (uint32_t)rows.size() / 2));
    uint32_t groupCountRows = ((uint32_t)rows.size() + groupRows - 1u) / groupRows;
    if (groupCountRows < 2) return false;

    std::array<uint8_t,256> present{};
    for (size_t i = 0; i < size; ++i) present[raw[i]] = 1;
    std::vector<uint8_t> alphabet;
    for (unsigned i = 0; i < 256; ++i) if (present[i]) alphabet.push_back((uint8_t)i);
    if (alphabet.empty()) return false;
    const uint16_t eob = (uint16_t)(alphabet.size() + 1u);
    const uint32_t symCount = (uint32_t)eob + 1u;

    std::vector<BwtBlock> blocks;
    blocks.reserve(groupCountRows);
    std::vector<uint8_t> last;
    for (uint32_t first = 0; first < rows.size(); first += groupRows) {
        uint32_t lastRow = std::min<uint32_t>((uint32_t)rows.size(), first + groupRows);
        uint32_t start = rows[first].start;
        uint32_t end = rows[lastRow - 1].start + rows[lastRow - 1].len;
        uint32_t rawLen = end - start, primary = 0;
        bwtTransform(raw + start, rawLen, last, primary);
        std::vector<uint8_t> mtf = alphabet;
        std::array<uint16_t,256> pos{};
        pos.fill(0xffffu);
        for (uint32_t i = 0; i < mtf.size(); ++i) pos[mtf[i]] = (uint16_t)i;
        BwtBlock block{rawLen, primary, {}};
        block.events.reserve(rawLen);
        uint32_t zeroRun = 0;
        auto flushZero = [&]() {
            if (!zeroRun) return;
            uint32_t x = zeroRun - 1u;
            for (;;) {
                uint16_t sym = (uint16_t)(x & 1u);
                block.events.push_back(BwtEvent{sym,0,0});
                if (x < 2u) break;
                x = (x - 2u) >> 1;
            }
            zeroRun = 0;
        };
        for (uint8_t byte : last) {
            unsigned rank = pos[byte];
            if (rank == 0xffffu || rank >= mtf.size()) return false;
            if (rank == 0) { ++zeroRun; continue; }
            flushZero();
            block.events.push_back(BwtEvent{(uint16_t)(rank + 1u),0,0});
            for (unsigned j = rank; j > 0; --j) {
                mtf[j] = mtf[j-1];
                pos[mtf[j]] = (uint16_t)j;
            }
            mtf[0] = byte;
            pos[byte] = 0;
        }
        flushZero();
        block.events.push_back(BwtEvent{eob,0,0});
        blocks.push_back(std::move(block));
    }

    std::vector<uint8_t> rowData;
    std::vector<uint32_t> indexes;
    indexes.reserve(blocks.size());
    std::vector<uint8_t> payload;
    for (const BwtBlock& block : blocks) {
        uint8_t nGroups = 0;
        std::vector<uint8_t> selectors;
        std::vector<std::vector<uint8_t>> lengths;
        std::vector<std::vector<uint64_t>> codes;
        if (!makeBwtGroupModels(block.events, symCount, nGroups, selectors, lengths, codes)) return false;
        std::vector<uint8_t> selectorData;
        BitWriter sw(selectorData);
        for (uint8_t group : selectors) sw.put(group, 3);
        indexes.push_back((uint32_t)rowData.size());
        rowData.push_back(nGroups);
        putVar(rowData, (uint32_t)selectors.size());
        for (const auto& lens : lengths) rowData.insert(rowData.end(), lens.begin(), lens.end());
        rowData.insert(rowData.end(), selectorData.begin(), selectorData.end());

        payload.clear();
        BitWriter bw(payload);
        for (uint32_t i = 0; i < block.events.size(); ++i) {
            uint32_t group = selectors[i / 50u];
            const BwtEvent& ev = block.events[i];
            unsigned len = lengths[group][ev.sym];
            if (!len) return false;
            bw.put(codes[group][ev.sym], len);
        }
        putVar(rowData, (uint32_t)payload.size());
        putVar(rowData, block.rawLen);
        putVar(rowData, block.primary);
        rowData.insert(rowData.end(), payload.begin(), payload.end());
    }

    std::vector<uint8_t> coded;
    coded.reserve(55 + indexes.size()*4 + rowData.size() + 4);
    coded.insert(coded.end(), {'D','B','T','1'}); coded.push_back(5);
    wr32(coded, (uint32_t)size); wr32(coded, (uint32_t)rows.size());
    wr16(coded, (uint16_t)groupRows); wr32(coded, (uint32_t)indexes.size());
    wr16(coded, (uint16_t)alphabet.size());
    std::array<uint8_t,32> mask{};
    for (uint8_t byte : alphabet) mask[byte >> 3] |= (uint8_t)(1u << (byte & 7u));
    coded.insert(coded.end(), mask.begin(), mask.end());
    for (uint32_t x : indexes) wr32(coded, x);
    coded.insert(coded.end(), rowData.begin(), rowData.end());
    coded.insert(coded.end(), 4, 0);
    if (coded.size() >= size) return false;
    archive.swap(coded);
    return true;
}

static uint32_t hash3(const uint8_t* p) {
    return (((uint32_t)p[0] * 251u + p[1]) * 251u + p[2]) & 65535u;
}
static void insertLzPos(const uint8_t* raw, uint32_t n, uint32_t pos,
                        std::vector<int32_t>& head, std::vector<int32_t>& prev) {
    if (pos + 2 >= n) return;
    uint32_t h = hash3(raw + pos);
    prev[pos] = head[h]; head[h] = (int32_t)pos;
}
static std::vector<uint8_t> lzCompress(const uint8_t* raw, uint32_t n,
                                       const std::vector<uint8_t>& lens,
                                       const std::vector<uint64_t>& code) {
    std::vector<uint8_t> out;
    BitWriter bw(out);
    std::vector<int32_t> head(65536,-1), prev(n,-1);
    uint32_t pos=0;
    while (pos<n) {
        uint32_t bestLen=0,bestDist=0;
        if (pos+2<n) {
            int32_t cand=head[hash3(raw+pos)];
            uint32_t depth=0;
            uint32_t maxLen=std::min<uint32_t>(515,n-pos);
            while (cand>=0 && depth++<16) {
                uint32_t dist=pos-(uint32_t)cand;
                if (dist>32768u) break;
                uint32_t len=0;
                while (len<maxLen && raw[pos+len]==raw[pos+len-dist]) ++len;
                if (len>bestLen) { bestLen=len; bestDist=dist; if (len==maxLen || len>=64) break; }
                cand=prev[(uint32_t)cand];
            }
        }
        if (bestLen>=6) {
            bw.put(1,1);
            uint32_t v=((bestDist-1u)<<9) | (bestLen-4u);
            bw.put(v,24);
            for (uint32_t j=0;j<bestLen;++j) insertLzPos(raw,n,pos+j,head,prev);
            pos+=bestLen;
        } else {
            uint8_t b=raw[pos]; unsigned l=lens[b];
            if (!l) return {};
            bw.put(0,1); bw.put(code[b],l);
            insertLzPos(raw,n,pos,head,prev);
            ++pos;
        }
    }
    return out;
}

static bool makeLzArchive(const uint8_t* raw, size_t size, const std::vector<Row>& rows,
                          std::vector<uint8_t>& archive) {
    if (rows.empty()) return false;
    std::vector<uint64_t> freq(256,0);
    for (size_t i=0;i<size;++i) ++freq[raw[i]];
    std::vector<uint8_t> lens;
    std::vector<uint64_t> code;
    if (!makeHuffman(freq,lens,code)) return false;
    std::vector<uint8_t> rowData;
    std::vector<uint32_t> indexes;
    indexes.reserve((rows.size()+LZ_GROUP-1)/LZ_GROUP);
    for (uint32_t first=0;first<rows.size();first+=LZ_GROUP) {
        uint32_t last=std::min<uint32_t>((uint32_t)rows.size(),first+LZ_GROUP);
        uint32_t start=rows[first].start;
        uint32_t end=rows[last-1].start+rows[last-1].len;
        uint32_t rawLen=end-start;
        std::vector<uint8_t> payload=lzCompress(raw+start,rawLen,lens,code);
        if (rawLen && payload.empty()) return false;
        indexes.push_back((uint32_t)rowData.size());
        putVar(rowData,(uint32_t)payload.size());
        putVar(rowData,rawLen);
        rowData.insert(rowData.end(),payload.begin(),payload.end());
    }
    std::vector<uint8_t> coded;
    coded.reserve(19+lens.size()+indexes.size()*4+rowData.size()+4);
    coded.insert(coded.end(),{'D','B','T','1'}); coded.push_back(6);
    wr32(coded,(uint32_t)size); wr32(coded,(uint32_t)rows.size());
    wr16(coded,(uint16_t)LZ_GROUP); wr32(coded,(uint32_t)indexes.size());
    coded.insert(coded.end(),lens.begin(),lens.end());
    for (uint32_t x:indexes) wr32(coded,x);
    coded.insert(coded.end(),rowData.begin(),rowData.end()); coded.insert(coded.end(),4,0);
    std::vector<uint8_t> rawArchive;
    size_t ic=(rows.size()+GROUP-1)/GROUP;
    rawArchive.reserve(17+ic*4+size);
    rawArchive.insert(rawArchive.end(),{'D','B','T','1'}); rawArchive.push_back(0);
    wr32(rawArchive,(uint32_t)size); wr32(rawArchive,(uint32_t)rows.size()); wr32(rawArchive,(uint32_t)ic);
    for (uint32_t i=0;i<rows.size();i+=GROUP) wr32(rawArchive,rows[i].start);
    rawArchive.insert(rawArchive.end(),raw,raw+size);
    if (coded.size()>=rawArchive.size()) archive.swap(rawArchive);
    else archive.swap(coded);
    return true;
}

static bool makeContext2Archive(const uint8_t* raw, size_t size, const std::vector<Row>& rows,
                                std::vector<uint8_t>& archive) {
    if (rows.empty()) return false;
    constexpr uint32_t C=257;
    std::vector<uint32_t> baseCounts((size_t)C*C,0);
    std::vector<uint32_t> ctxCount(65536,0);
    for (const Row& r:rows) {
        uint32_t p2=256,p1=256;
        for (uint32_t j=0;j<r.len;++j) {
            uint32_t b=raw[r.start+j];
            ++baseCounts[(size_t)p1*C+b];
            if (p2<256 && p1<256) ++ctxCount[(p2<<8)|p1];
            p2=p1; p1=b;
        }
        ++baseCounts[(size_t)p1*C+256];
        if (p2<256 && p1<256) ++ctxCount[(p2<<8)|p1];
    }
    std::vector<int32_t> activeMap(65536,-1);
    std::vector<uint16_t> keys;
    for (uint32_t k=0;k<65536;++k) if (ctxCount[k]>=8) {
        activeMap[k]=(int32_t)keys.size(); keys.push_back((uint16_t)k);
    }
    std::vector<uint32_t> c2Counts((size_t)keys.size()*C,0);
    for (const Row& r:rows) {
        uint32_t p2=256,p1=256;
        for (uint32_t j=0;j<r.len;++j) {
            uint32_t b=raw[r.start+j];
            if (p2<256 && p1<256) {
                int32_t ix=activeMap[(p2<<8)|p1];
                if (ix>=0) ++c2Counts[(size_t)ix*C+b];
            }
            p2=p1; p1=b;
        }
        if (p2<256 && p1<256) {
            int32_t ix=activeMap[(p2<<8)|p1];
            if (ix>=0) ++c2Counts[(size_t)ix*C+256];
        }
    }

    std::vector<uint8_t> baseLens((size_t)C*C,0);
    std::vector<uint64_t> baseCodes((size_t)C*C,0), rowFreq(C);
    for (uint32_t ctx=0;ctx<C;++ctx) {
        unsigned ns=0,only=0;
        for (uint32_t sym=0;sym<C;++sym) {
            uint32_t f=baseCounts[(size_t)ctx*C+sym]; rowFreq[sym]=f;
            if (f) { ++ns; only=sym; }
        }
        if (ns==0) continue;
        if (ns==1) continue;
        std::vector<uint8_t> ll; std::vector<uint64_t> cc;
        if (!makeHuffman(rowFreq,ll,cc)) return false;
        for (uint32_t sym=0;sym<C;++sym) {
            baseLens[(size_t)ctx*C+sym]=ll[sym]; baseCodes[(size_t)ctx*C+sym]=cc[sym];
        }
    }
    std::vector<uint8_t> c2Lens((size_t)keys.size()*C,0);
    std::vector<uint64_t> c2Codes((size_t)keys.size()*C,0);
    for (uint32_t ix=0;ix<keys.size();++ix) {
        unsigned ns=0;
        for (uint32_t sym=0;sym<C;++sym) { rowFreq[sym]=c2Counts[(size_t)ix*C+sym]; if (rowFreq[sym]) ++ns; }
        if (ns==0) return false;
        if (ns==1) continue;
        std::vector<uint8_t> ll; std::vector<uint64_t> cc;
        if (!makeHuffman(rowFreq,ll,cc)) return false;
        for (uint32_t sym=0;sym<C;++sym) {
            c2Lens[(size_t)ix*C+sym]=ll[sym]; c2Codes[(size_t)ix*C+sym]=cc[sym];
        }
    }

    std::vector<uint8_t> model;
    for (uint32_t ctx=0;ctx<C;++ctx) {
        uint16_t ns=0;
        for (uint32_t sym=0;sym<C;++sym) if (baseCounts[(size_t)ctx*C+sym]) ++ns;
        wr16(model,ns);
        for (uint32_t sym=0;sym<C;++sym) if (baseCounts[(size_t)ctx*C+sym]) {
            wr16(model,(uint16_t)sym); model.push_back(baseLens[(size_t)ctx*C+sym]);
        }
    }
    wr32(model,(uint32_t)keys.size());
    for (uint32_t ix=0;ix<keys.size();++ix) {
        wr16(model,keys[ix]);
        uint16_t ns=0;
        for (uint32_t sym=0;sym<C;++sym) if (c2Counts[(size_t)ix*C+sym]) ++ns;
        wr16(model,ns);
        for (uint32_t sym=0;sym<C;++sym) if (c2Counts[(size_t)ix*C+sym]) {
            wr16(model,(uint16_t)sym); model.push_back(c2Lens[(size_t)ix*C+sym]);
        }
    }

    std::vector<uint8_t> rowData,payload;
    std::vector<uint32_t> indexes;
    indexes.reserve((rows.size()+GROUP-1)/GROUP);
    for (uint32_t ri=0;ri<rows.size();++ri) {
        if ((ri%GROUP)==0) indexes.push_back((uint32_t)rowData.size());
        payload.clear(); BitWriter bw(payload);
        uint32_t p2=256,p1=256;
        auto emit=[&](uint32_t sym) {
            uint64_t code; unsigned len;
            int32_t ix=(p2<256 && p1<256)?activeMap[(p2<<8)|p1]:-1;
            if (ix>=0) {
                size_t at=(size_t)ix*C+sym; code=c2Codes[at]; len=c2Lens[at];
            } else {
                size_t at=(size_t)p1*C+sym; code=baseCodes[at]; len=baseLens[at];
            }
            if (len) bw.put(code,len);
        };
        for (uint32_t j=0;j<rows[ri].len;++j) {
            uint32_t b=raw[rows[ri].start+j]; emit(b); p2=p1; p1=b;
        }
        emit(256);
        putVar(rowData,(uint32_t)payload.size());
        rowData.insert(rowData.end(),payload.begin(),payload.end());
    }
    std::vector<uint8_t> coded;
    coded.reserve(19+model.size()+indexes.size()*4+rowData.size()+4);
    coded.insert(coded.end(),{'D','B','T','1'}); coded.push_back(8);
    wr32(coded,(uint32_t)size); wr32(coded,(uint32_t)rows.size());
    wr16(coded,(uint16_t)GROUP); wr32(coded,(uint32_t)indexes.size());
    coded.insert(coded.end(),model.begin(),model.end());
    for (uint32_t x:indexes) wr32(coded,x);
    coded.insert(coded.end(),rowData.begin(),rowData.end()); coded.insert(coded.end(),4,0);

    std::vector<uint8_t> rawArchive;
    size_t ic=(rows.size()+GROUP-1)/GROUP;
    rawArchive.reserve(17+ic*4+size);
    rawArchive.insert(rawArchive.end(),{'D','B','T','1'}); rawArchive.push_back(0);
    wr32(rawArchive,(uint32_t)size); wr32(rawArchive,(uint32_t)rows.size()); wr32(rawArchive,(uint32_t)ic);
    for (uint32_t i=0;i<rows.size();i+=GROUP) wr32(rawArchive,rows[i].start);
    rawArchive.insert(rawArchive.end(),raw,raw+size);
    if (coded.size()>=rawArchive.size()) archive.swap(rawArchive);
    else archive.swap(coded);
    return true;
}

static bool makeContextArchive(const uint8_t* raw, size_t size, const std::vector<Row>& rows,
                               std::vector<uint8_t>& archive) {
    if (rows.empty()) return false;
    constexpr uint32_t C = 257;
    std::vector<uint32_t> counts((size_t)C * C, 0);
    for (const Row& r : rows) {
        uint32_t prev = 256;
        for (uint32_t j = 0; j < r.len; ++j) {
            uint32_t b = raw[r.start + j];
            ++counts[(size_t)prev * C + b];
            prev = b;
        }
        ++counts[(size_t)prev * C + 256];
    }
    std::vector<uint8_t> lens((size_t)C * C, 0);
    std::vector<uint64_t> codes((size_t)C * C, 0);
    std::vector<uint64_t> rowFreq(C);
    for (uint32_t ctx = 0; ctx < C; ++ctx) {
        unsigned symbols = 0, only = 0;
        for (uint32_t sym = 0; sym < C; ++sym) {
            uint32_t f = counts[(size_t)ctx * C + sym];
            rowFreq[sym] = f;
            if (f) { ++symbols; only = sym; }
        }
        if (symbols == 0) continue;
        if (symbols == 1) {
            // A deterministic transition needs no encoded bit.
            lens[(size_t)ctx * C + only] = 0;
            continue;
        }
        std::vector<uint8_t> ll;
        std::vector<uint64_t> cc;
        if (!makeHuffman(rowFreq, ll, cc)) return false;
        for (uint32_t sym = 0; sym < C; ++sym) {
            lens[(size_t)ctx * C + sym] = ll[sym];
            codes[(size_t)ctx * C + sym] = cc[sym];
        }
    }

    std::vector<uint8_t> rowData;
    std::vector<uint32_t> indexes;
    indexes.reserve((rows.size() + GROUP - 1) / GROUP);
    std::vector<uint8_t> payload;
    for (uint32_t ri = 0; ri < rows.size(); ++ri) {
        if ((ri % GROUP) == 0) indexes.push_back((uint32_t)rowData.size());
        payload.clear();
        BitWriter bw(payload);
        uint32_t prev = 256;
        for (uint32_t j = 0; j < rows[ri].len; ++j) {
            uint32_t sym = raw[rows[ri].start + j];
            uint32_t ix = prev * C + sym;
            unsigned l = lens[ix];
            if (l) bw.put(codes[ix], l);
            prev = sym;
        }
        uint32_t ix = prev * C + 256;
        if (lens[ix]) bw.put(codes[ix], lens[ix]);
        putVar(rowData, (uint32_t)payload.size());
        rowData.insert(rowData.end(), payload.begin(), payload.end());
    }

    std::vector<uint8_t> model;
    for (uint32_t ctx = 0; ctx < C; ++ctx) {
        uint16_t ns = 0;
        for (uint32_t sym = 0; sym < C; ++sym)
            if (counts[(size_t)ctx * C + sym]) ++ns;
        wr16(model, ns);
        for (uint32_t sym = 0; sym < C; ++sym) {
            if (!counts[(size_t)ctx * C + sym]) continue;
            wr16(model, (uint16_t)sym);
            model.push_back(lens[(size_t)ctx * C + sym]);
        }
    }

    std::vector<uint8_t> coded;
    coded.reserve(19 + model.size() + indexes.size()*4 + rowData.size() + 4);
    coded.insert(coded.end(), {'D','B','T','1'});
    coded.push_back(4);
    wr32(coded, (uint32_t)size);
    wr32(coded, (uint32_t)rows.size());
    wr16(coded, (uint16_t)GROUP);
    wr32(coded, (uint32_t)indexes.size());
    coded.insert(coded.end(), model.begin(), model.end());
    for (uint32_t x : indexes) wr32(coded, x);
    coded.insert(coded.end(), rowData.begin(), rowData.end());
    coded.insert(coded.end(), 4, 0);

    std::vector<uint8_t> rawArchive;
    size_t rawIndexCount = (rows.size() + GROUP - 1) / GROUP;
    rawArchive.reserve(17 + rawIndexCount * 4 + size);
    rawArchive.insert(rawArchive.end(), {'D','B','T','1'});
    rawArchive.push_back(0);
    wr32(rawArchive, (uint32_t)size);
    wr32(rawArchive, (uint32_t)rows.size());
    wr32(rawArchive, (uint32_t)rawIndexCount);
    for (uint32_t i = 0; i < rows.size(); i += GROUP) wr32(rawArchive, rows[i].start);
    rawArchive.insert(rawArchive.end(), raw, raw + size);

    if (coded.size() >= rawArchive.size()) archive.swap(rawArchive);
    else archive.swap(coded);
    return true;
}

static inline uint32_t peek12(const uint8_t* p, uint64_t bitpos) {
    uint32_t b = (uint32_t)(bitpos >> 3);
    unsigned sh = (unsigned)(bitpos & 7u);
    uint32_t w = ((uint32_t)p[b] << 24) | ((uint32_t)p[b+1] << 16) |
                 ((uint32_t)p[b+2] << 8) | p[b+3];
    return (w << sh) >> (32u - FAST_BITS);
}
static inline uint32_t getBit(const uint8_t* p, uint64_t bitpos) {
    return (p[bitpos >> 3] >> (7u - (unsigned)(bitpos & 7u))) & 1u;
}
static bool decodeSym(const State* s, const uint8_t* p, uint64_t& bitpos, uint16_t& sym) {
    uint32_t pref = peek12(p, bitpos);
    const FastEnt& e = s->fast[pref];
    if (e.len) { bitpos += e.len; sym = e.sym; return true; }
    int32_t node = 0;
    for (unsigned steps = 0; steps < 64; ++steps) {
        int bit = (int)getBit(p, bitpos++);
        if (node < 0 || (size_t)node >= s->tree.size()) return false;
        node = s->tree[node].ch[bit];
        if (node < 0 || (size_t)node >= s->tree.size()) return false;
        if (s->tree[node].sym >= 0) {
            sym = (uint16_t)s->tree[node].sym;
            return true;
        }
    }
    return false;
}

static bool initHuffman(State* s) {
    s->huffCode.assign(s->symCount, 0);
    unsigned maxLen = 0;
    std::vector<uint32_t> count(64, 0);
    for (uint32_t i = 0; i < s->symCount; ++i) {
        unsigned l = s->huffLen[i];
        if (l > 63) return false;
        if (l) { ++count[l]; maxLen = std::max(maxLen, l); }
    }
    if (!maxLen) return false;
    uint64_t c = 0;
    for (unsigned l = 1; l <= maxLen; ++l) {
        if (l > 1) c <<= 1;
        for (uint32_t i = 0; i < s->symCount; ++i)
            if (s->huffLen[i] == l) s->huffCode[i] = c++;
    }
    s->fast.assign(FAST_SIZE, FastEnt{0xffffu,0,0});
    s->tree.clear();
    s->tree.push_back(DecNode{{-1,-1},-1});
    for (uint32_t sym = 0; sym < s->symCount; ++sym) {
        unsigned l = s->huffLen[sym];
        if (!l) continue;
        uint64_t code = s->huffCode[sym];
        if (l <= FAST_BITS) {
            uint32_t base = (uint32_t)(code << (FAST_BITS - l));
            uint32_t n = 1u << (FAST_BITS - l);
            for (uint32_t j = 0; j < n; ++j)
                s->fast[base + j] = FastEnt{(uint16_t)sym,(uint8_t)l,0};
        }
        int32_t node = 0;
        for (int b = (int)l - 1; b >= 0; --b) {
            unsigned bit = (unsigned)((code >> b) & 1u);
            if (s->tree[node].ch[bit] < 0) {
                s->tree[node].ch[bit] = (int32_t)s->tree.size();
                s->tree.push_back(DecNode{{-1,-1},-1});
            }
            node = s->tree[node].ch[bit];
        }
        s->tree[node].sym = (int32_t)sym;
    }
    return true;
}

static bool decodeBpeRow(const State* s, const uint8_t* payload, uint32_t clen,
                         uint8_t* out, uint32_t maxOut, uint32_t& rawLen) {
    (void)clen; // The archive has four lookahead bytes after its final record.
    uint64_t bitpos = 0;
    uint32_t written = 0;
    const uint16_t endSym = (uint16_t)(s->symCount - 1u);
    for (;;) {
        uint16_t sym;
        if (!decodeSym(s, payload, bitpos, sym)) return false;
        if (sym == endSym) { rawLen = written; return true; }
        if (sym >= s->expansion.size()) return false;
        const std::string& e = s->expansion[sym];
        if (e.empty() || e.size() > maxOut - written) return false;
        std::memcpy(out + written, e.data(), e.size());
        written += (uint32_t)e.size();
    }
}


static bool decodeWordRow(const State* s, const uint8_t* payload, uint64_t& bitpos,
                          uint8_t* out, uint32_t maxOut, uint32_t& rawLen) {
    uint32_t written = 0;
    const uint16_t endSym = (uint16_t)(s->symCount - 1u);
    for (;;) {
        uint16_t sym;
        if (!decodeSym(s, payload, bitpos, sym)) return false;
        if (sym == endSym) { rawLen = written; return true; }
        if (sym >= s->expansion.size()) return false;
        const std::string& e = s->expansion[sym];
        if (e.empty() || e.size() > UINT32_MAX - written) return false;
        if (out) {
            if (written > maxOut || e.size() > maxOut - written) return false;
            std::memcpy(out + written, e.data(), e.size());
        }
        written += (uint32_t)e.size();
    }
}


static bool decodeWordBpeRow(const State* s, const uint8_t* payload, uint64_t& bitpos,
                             uint8_t* out, uint32_t maxOut, uint32_t& rawLen) {
    uint32_t written = 0;
    const uint16_t endSym = (uint16_t)(s->symCount - 1u);
    for (;;) {
        uint16_t sym;
        if (!decodeSym(s, payload, bitpos, sym)) return false;
        if (sym == endSym) { rawLen = written; return true; }
        if (sym >= s->expansion.size()) return false;
        const std::string& e = s->expansion[sym];
        if (e.empty() || e.size() > UINT32_MAX - written) return false;
        if (out) {
            if (written > maxOut || e.size() > maxOut - written) return false;
            std::memcpy(out + written, e.data(), e.size());
        }
        written += (uint32_t)e.size();
        if (e.find('\n') != std::string::npos) { rawLen = written; return true; }
    }
}

static bool decodeLzBlock(const State* s, const uint8_t* payload, uint32_t clen,
                          uint32_t rawLen, uint8_t* out) {
    (void)clen;
    uint64_t bitpos=0;
    uint32_t written=0;
    while (written<rawLen) {
        uint32_t isMatch=getBit(payload,bitpos++);
        if (isMatch) {
            uint32_t v=getBits(payload,bitpos,24); bitpos+=24;
            uint32_t dist=(v>>9)+1u, len=(v&511u)+4u;
            if (dist==0 || dist>written || len>rawLen-written) return false;
            for (uint32_t i=0;i<len;++i) out[written+i]=out[written+i-dist];
            written+=len;
        } else {
            uint16_t sym;
            if (!decodeSym(s,payload,bitpos,sym) || sym>255) return false;
            out[written++]=(uint8_t)sym;
        }
    }
    return true;
}

static bool initBwtHuff(const uint8_t* lens, uint32_t count, BwtHuff& model) {
    unsigned maxLen = 0;
    std::vector<uint32_t> hist(64, 0);
    for (uint32_t i = 0; i < count; ++i) {
        unsigned len = lens[i];
        if (len > 63) return false;
        if (len) { ++hist[len]; maxLen = std::max(maxLen, len); }
    }
    if (!maxLen) return false;
    uint64_t code = 0;
    std::vector<uint64_t> codes(count, 0);
    for (unsigned len = 1; len <= maxLen; ++len) {
        if (len > 1) code <<= 1;
        for (uint32_t sym = 0; sym < count; ++sym)
            if (lens[sym] == len) codes[sym] = code++;
    }
    model.fast.assign(FAST_SIZE, FastEnt{0xffffu,0,0});
    model.tree.clear();
    model.tree.push_back(DecNode{{-1,-1},-1});
    for (uint32_t sym = 0; sym < count; ++sym) {
        unsigned len = lens[sym];
        if (!len) continue;
        uint64_t value = codes[sym];
        if (len <= FAST_BITS) {
            uint32_t base = (uint32_t)(value << (FAST_BITS - len));
            uint32_t reps = 1u << (FAST_BITS - len);
            for (uint32_t j = 0; j < reps; ++j)
                model.fast[base + j] = FastEnt{(uint16_t)sym,(uint8_t)len,0};
        }
        int32_t node = 0;
        for (int bit = (int)len - 1; bit >= 0; --bit) {
            unsigned side = (unsigned)((value >> bit) & 1u);
            if (model.tree[node].ch[side] < 0) {
                model.tree[node].ch[side] = (int32_t)model.tree.size();
                model.tree.push_back(DecNode{{-1,-1},-1});
            }
            node = model.tree[node].ch[side];
        }
        model.tree[node].sym = (int32_t)sym;
    }
    return true;
}
static bool decodeBwtSym(const BwtHuff& model, const uint8_t* p,
                         uint64_t& bitpos, uint16_t& sym) {
    uint32_t pref = peek12(p, bitpos);
    const FastEnt& e = model.fast[pref];
    if (e.len) { bitpos += e.len; sym = e.sym; return true; }
    int32_t node = 0;
    for (unsigned steps = 0; steps < 64; ++steps) {
        int side = (int)getBit(p, bitpos++);
        if (node < 0 || (size_t)node >= model.tree.size()) return false;
        node = model.tree[node].ch[side];
        if (node < 0 || (size_t)node >= model.tree.size()) return false;
        if (model.tree[node].sym >= 0) {
            sym = (uint16_t)model.tree[node].sym;
            return true;
        }
    }
    return false;
}

static bool prepareBwtBlock(State* s, uint32_t block) {
    if (block >= s->bwtBlocks.size() || s->bwtAlphabet.empty()) return false;
    BwtBlockMeta& meta = s->bwtBlocks[block];
    uint32_t n = meta.rawLen, primary = meta.primary;
    if (n == 0 || primary >= n || meta.models.empty() || meta.selectorCount == 0) return false;
    const uint16_t eob = (uint16_t)(s->bwtAlphabet.size() + 1u);
    meta.last.resize(n);
    std::vector<uint8_t> mtf = s->bwtAlphabet;
    uint64_t bitpos = 0, selectorBit = 0;
    uint32_t produced = 0, symbols = 0, modelId = UINT32_MAX;
    auto nextSym = [&](uint16_t& sym) -> bool {
        if ((symbols % 50u) == 0) {
            uint32_t sel = symbols / 50u;
            if (sel >= meta.selectorCount) return false;
            modelId = getBits(meta.selectors, selectorBit, 3);
            selectorBit += 3;
            if (modelId >= meta.models.size()) return false;
        }
        if (!decodeBwtSym(meta.models[modelId], meta.payload, bitpos, sym)) return false;
        ++symbols;
        return true;
    };
    for (;;) {
        uint16_t sym;
        if (!nextSym(sym)) return false;
        if (sym == eob) break;
        if (sym <= 1) {
            uint64_t run = 0, weight = 1;
            do {
                run += sym == 0 ? weight : (weight << 1);
                if (run > n - produced || weight > (uint64_t)n * 2u) return false;
                weight <<= 1;
                if (!nextSym(sym)) return false;
            } while (sym <= 1);
            if (mtf.empty() || run == 0 || run > n - produced) return false;
            std::memset(meta.last.data() + produced, mtf[0], (size_t)run);
            produced += (uint32_t)run;
            if (sym == eob) break;
        }
        if (sym < 2 || sym > mtf.size() || produced >= n) return false;
        unsigned rank = (unsigned)sym - 1u;
        uint8_t byte = mtf[rank];
        meta.last[produced++] = byte;
        for (unsigned j = rank; j > 0; --j) mtf[j] = mtf[j-1];
        mtf[0] = byte;
    }
    if (produced != n || (symbols + 49u) / 50u != meta.selectorCount) return false;
    std::array<uint32_t,256> counts{}, starts{}, seen{};
    for (uint8_t byte : meta.last) ++counts[byte];
    uint32_t sum = 0;
    for (unsigned i = 0; i < 256; ++i) { starts[i] = sum; sum += counts[i]; }
    meta.lfNext.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        uint8_t byte = meta.last[i];
        meta.lfNext[i] = starts[byte] + seen[byte]++;
    }

    uint32_t firstRow = block * s->blockRows;
    uint32_t blockRows = std::min<uint32_t>(s->blockRows, s->rows - firstRow);
    if (!blockRows) return false;
    meta.rowEndRank.assign(blockRows, UINT32_MAX);
    meta.rowLen.assign(blockRows, 0);
    uint32_t row = primary, currentLen = 0, step = 0;
    int32_t localRow = (int32_t)blockRows - 1;
    bool firstBoundary = true;
    bool finalUnterminated = meta.last[primary] != '\n';
    meta.decodeSamples.clear();
    meta.decodeSamples.reserve((n + 7u) / 8u);
    for (uint32_t k = n; k-- > 0;) {
        if ((step & 7u) == 0) meta.decodeSamples.push_back(row);
        ++step;
        uint8_t byte = meta.last[row];
        if (byte == '\n') {
            if (firstBoundary && finalUnterminated) {
                if (localRow < 0 || currentLen == 0) return false;
                meta.rowEndRank[(uint32_t)localRow] = primary;
                meta.rowLen[(uint32_t)localRow] = currentLen;
                --localRow;
                if (localRow < 0) return false;
                meta.rowEndRank[(uint32_t)localRow] = row;
                currentLen = 1;
                --localRow;
                firstBoundary = false;
            } else {
                if (localRow < 0) return false;
                meta.rowEndRank[(uint32_t)localRow] = row;
                if (currentLen) {
                    int32_t finished = localRow + 1;
                    if (finished < 0 || finished >= (int32_t)blockRows) return false;
                    meta.rowLen[(uint32_t)finished] = currentLen;
                }
                currentLen = 1;
                --localRow;
                firstBoundary = false;
            }
        } else {
            ++currentLen;
        }
        row = meta.lfNext[row];
    }
    if (firstBoundary) {
        if (blockRows != 1 || currentLen != n) return false;
        meta.rowEndRank[0] = primary;
        meta.rowLen[0] = currentLen;
    } else if (currentLen) {
        int32_t finished = localRow + 1;
        if (finished < 0 || finished >= (int32_t)blockRows) return false;
        meta.rowLen[(uint32_t)finished] = currentLen;
    }
    if (meta.decodeSamples.size() != (n + 7u) / 8u) return false;
    uint64_t total = 0;
    for (uint32_t r = 0; r < blockRows; ++r) {
        if (meta.rowEndRank[r] == UINT32_MAX || meta.rowLen[r] == 0) return false;
        total += meta.rowLen[r];
    }
    return total == n;
}

static bool prepareBwtIndexes(State* s) {
    if (s->type != 5) return true;
    for (uint32_t block = 0; block < s->bwtBlocks.size(); ++block)
        if (!prepareBwtBlock(s, block)) return false;
    return true;
}

static bool decodePreparedBwtBlock(const BwtBlockMeta& meta, uint8_t* out) {
    if (meta.last.empty() || meta.lfNext.size() != meta.last.size() ||
        meta.primary >= meta.last.size()) return false;
    constexpr uint32_t LANES = 16u, CHUNK = 8u;
    const uint32_t n = (uint32_t)meta.last.size();
    const uint32_t chunks = (n + CHUNK - 1u) / CHUNK;
    if (meta.decodeSamples.size() != chunks) return false;
    uint32_t ranks[LANES], lens[LANES], chunkIds[LANES];
    for (uint32_t base = 0; base < chunks; base += LANES) {
        uint32_t active = std::min<uint32_t>(LANES, chunks - base);
        for (uint32_t lane = 0; lane < active; ++lane) {
            uint32_t c = base + lane;
            chunkIds[lane] = c;
            ranks[lane] = meta.decodeSamples[c];
            lens[lane] = std::min<uint32_t>(CHUNK, n - c * CHUNK);
        }
        for (uint32_t j = 0; j < CHUNK; ++j) {
            for (uint32_t lane = 0; lane < active; ++lane) {
                if (j >= lens[lane]) continue;
                uint32_t pos = n - 1u - (chunkIds[lane] * CHUNK + j);
                uint32_t rank = ranks[lane];
                out[pos] = meta.last[rank];
                ranks[lane] = meta.lfNext[rank];
            }
        }
    }
    return true;
}

static bool decodePreparedBwtRow(const BwtBlockMeta& meta, uint32_t localRow,
                                 uint8_t* out) {
    if (localRow >= meta.rowLen.size() || meta.lfNext.size() != meta.last.size()) return false;
    uint32_t len = meta.rowLen[localRow], row = meta.rowEndRank[localRow];
    if (!len || row >= meta.last.size()) return false;
    for (uint32_t k = 0; k < len; ++k) {
        out[len - 1u - k] = meta.last[row];
        row = meta.lfNext[row];
    }
    return true;
}

static bool initModel(std::vector<int16_t>& dets, std::vector<int32_t>& roots,
                      std::vector<FastEnt>& fast, size_t fastBase,
                      std::vector<DecNode>& tree, uint32_t model,
                      const uint16_t* symbols, const uint8_t* lengths, uint32_t n) {
    if (model>=dets.size() || model>=roots.size()) return false;
    if (n==0) { dets[model]=-2; return true; }
    std::array<uint8_t,257> lens{};
    for (uint32_t i=0;i<n;++i) {
        if (symbols[i]>256 || lens[symbols[i]]!=0) return false;
        lens[symbols[i]]=lengths[i];
    }
    if (n==1) {
        if (lengths[0]!=0) return false;
        dets[model]=(int16_t)symbols[0]; return true;
    }
    unsigned maxLen=0;
    for (uint32_t i=0;i<n;++i) {
        unsigned l=lengths[i]; if (l==0 || l>63) return false;
        maxLen=std::max(maxLen,l);
    }
    std::array<uint64_t,257> code{};
    uint64_t c=0;
    for (unsigned l=1;l<=maxLen;++l) {
        if (l>1) c<<=1;
        for (uint32_t sym=0;sym<=256;++sym) if (lens[sym]==l) code[sym]=c++;
    }
    dets[model]=-1;
    int32_t root=(int32_t)tree.size(); roots[model]=root;
    tree.push_back(DecNode{{-1,-1},-1});
    for (uint32_t i=0;i<n;++i) {
        uint32_t sym=symbols[i]; unsigned l=lengths[i]; uint64_t cd=code[sym];
        if (l<=8) {
            uint32_t base=(uint32_t)(cd<<(8-l)), replicas=1u<<(8-l);
            for (uint32_t j=0;j<replicas;++j) fast[fastBase+base+j]=FastEnt{(uint16_t)sym,(uint8_t)l,0};
        }
        int32_t node=root;
        for (int b=(int)l-1;b>=0;--b) {
            unsigned bit=(unsigned)((cd>>b)&1u);
            if (tree[node].ch[bit]<0) {
                tree[node].ch[bit]=(int32_t)tree.size();
                tree.push_back(DecNode{{-1,-1},-1});
            }
            node=tree[node].ch[bit];
        }
        tree[node].sym=(int32_t)sym;
    }
    return true;
}
static bool initContextModel(State* s, uint32_t ctx, const uint16_t* symbols,
                             const uint8_t* lengths, uint32_t n) {
    return initModel(s->ctxDet,s->ctxRoot,s->ctxFast,(size_t)ctx*256,s->ctxTree,ctx,symbols,lengths,n);
}
static bool initContext2Model(State* s, uint32_t model, const uint16_t* symbols,
                              const uint8_t* lengths, uint32_t n) {
    return initModel(s->ctx2Det,s->ctx2Root,s->ctx2Fast,(size_t)model*256,s->ctx2Tree,model,symbols,lengths,n);
}

static inline uint32_t peek8(const uint8_t* p, uint64_t bitpos) {
    uint32_t b = (uint32_t)(bitpos >> 3);
    unsigned sh = (unsigned)(bitpos & 7u);
    uint32_t w = ((uint32_t)p[b] << 8) | p[b+1];
    return ((w << sh) >> 8) & 0xffu;
}
static bool decodeContextSym(const State* s, uint32_t ctx, const uint8_t* p,
                             uint64_t& bitpos, uint16_t& sym) {
    if (ctx >= 257) return false;
    int16_t det = s->ctxDet[ctx];
    if (det >= 0) { sym = (uint16_t)det; return true; }
    if (det != -1) return false;
    const FastEnt& e = s->ctxFast[(size_t)ctx * 256 + peek8(p, bitpos)];
    if (e.len) { bitpos += e.len; sym = e.sym; return true; }
    int32_t node = s->ctxRoot[ctx];
    for (unsigned steps = 0; steps < 64; ++steps) {
        if (node < 0 || (size_t)node >= s->ctxTree.size()) return false;
        node = s->ctxTree[node].ch[getBit(p, bitpos++)];
        if (node < 0 || (size_t)node >= s->ctxTree.size()) return false;
        if (s->ctxTree[node].sym >= 0) {
            sym = (uint16_t)s->ctxTree[node].sym;
            return true;
        }
    }
    return false;
}
static bool decodeContextRow(const State* s, const uint8_t* payload, uint32_t clen,
                             uint8_t* out, uint32_t maxOut, uint32_t& rawLen) {
    (void)clen;
    uint64_t bitpos = 0;
    uint32_t written = 0, ctx = 256;
    for (;;) {
        uint16_t sym;
        if (!decodeContextSym(s, ctx, payload, bitpos, sym)) return false;
        if (sym == 256) { rawLen = written; return true; }
        if (sym > 255 || written >= maxOut) return false;
        out[written++] = (uint8_t)sym;
        ctx = sym;
    }
}

static bool decodeContext2Sym(const State* s, uint32_t p2, uint32_t p1,
                              const uint8_t* p, uint64_t& bitpos, uint16_t& sym) {
    if (p2<256 && p1<256) {
        int32_t ix=s->ctx2Index[(p2<<8)|p1];
        if (ix>=0) {
            int16_t det=s->ctx2Det[(uint32_t)ix];
            if (det>=0) { sym=(uint16_t)det; return true; }
            if (det!=-1) return false;
            const FastEnt& e=s->ctx2Fast[(size_t)ix*256+peek8(p,bitpos)];
            if (e.len) { bitpos+=e.len; sym=e.sym; return true; }
            int32_t node=s->ctx2Root[(uint32_t)ix];
            for (unsigned steps=0;steps<64;++steps) {
                if (node<0 || (size_t)node>=s->ctx2Tree.size()) return false;
                node=s->ctx2Tree[node].ch[getBit(p,bitpos++)];
                if (node<0 || (size_t)node>=s->ctx2Tree.size()) return false;
                if (s->ctx2Tree[node].sym>=0) { sym=(uint16_t)s->ctx2Tree[node].sym; return true; }
            }
            return false;
        }
    }
    return decodeContextSym(s,p1,p,bitpos,sym);
}
static bool decodeContext2Row(const State* s, const uint8_t* payload, uint32_t clen,
                              uint8_t* out, uint32_t maxOut, uint32_t& rawLen) {
    (void)clen;
    uint64_t bitpos=0; uint32_t written=0,p2=256,p1=256;
    for (;;) {
        uint16_t sym;
        if (!decodeContext2Sym(s,p2,p1,payload,bitpos,sym)) return false;
        if (sym==256) { rawLen=written; return true; }
        if (sym>255 || written>=maxOut) return false;
        out[written++]=(uint8_t)sym; p2=p1; p1=sym;
    }
}

static bool initState(State* s) {
    const uint8_t* a = s->archive;
    size_t n = s->archiveSize;
    if (n < 13 || std::memcmp(a, "DBT1", 4) != 0) return false;
    s->type = a[4];
    s->rawSize = rd32(a + 5);
    s->rows = rd32(a + 9);
    if (s->type == 0) {
        if (n < 17) return false;
        s->indexCount = rd32(a + 13);
        if (s->indexCount != (s->rows + GROUP - 1) / GROUP) return false;
        size_t hdr = 17u + (size_t)s->indexCount * 4u;
        if (hdr + s->rawSize > n) return false;
        s->indexData = a + 17;
        s->rowData = a + hdr;
        return true;
    }
    if (s->type == 1) {
        if (n < 17) return false;
        s->bodyLen = rd16(a + 13);
        s->bitsPerRow = rd16(a + 15);
        size_t off = 17;
        s->posBits.reserve(s->bodyLen);
        s->posMap.resize(s->bodyLen);
        s->posBitOffset.reserve(s->bodyLen);
        uint32_t bo = 0;
        for (uint32_t j = 0; j < s->bodyLen; ++j) {
            if (off + 3 > n) return false;
            uint8_t b = a[off++];
            uint16_t c = rd16(a + off); off += 2;
            if (c == 0 || c > 256 || b > 8) return false;
            if ((b == 0 && c != 1) || (b && c > (1u << b))) return false;
            s->posBits.push_back(b);
            s->posBitOffset.push_back((uint16_t)bo);
            s->posMap[j].fill(0);
            if (off + c > n) return false;
            for (uint32_t k = 0; k < c; ++k) s->posMap[j][k] = a[off++];
            bo += b;
        }
        if (bo != s->bitsPerRow) return false;
        size_t endsBytes = (s->rows + 3u) / 4u;
        uint64_t streamBytes = ((uint64_t)s->rows * s->bitsPerRow + 7u) / 8u;
        if (off + endsBytes + streamBytes > n) return false;
        s->ends = a + off;
        s->packed = a + off + endsBytes;
        s->packedBytes = (size_t)streamBytes;
        return true;
    }
    if (s->type == 3) {
        if (n < 17) return false;
        s->hexMaxLen = rd16(a + 13);
        s->hexLenBits = a[15];
        s->hexLower = a[16];
        if (s->hexMaxLen == 0 || s->hexMaxLen > 16 || s->hexLenBits == 0 || s->hexLenBits > 5 || s->hexLower > 1) return false;
        size_t off = 17;
        size_t endsBytes = (s->rows + 3u) / 4u;
        s->hexLengthBytes = (size_t)(((uint64_t)s->rows * s->hexLenBits + 7u) / 8u);
        s->hexBytesPerRow = (s->hexMaxLen + 1u) / 2u;
        uint64_t digitBytes = (uint64_t)s->rows * s->hexBytesPerRow;
        if (off + endsBytes + s->hexLengthBytes + digitBytes > n) return false;
        s->ends = a + off;
        s->hexLengths = s->ends + endsBytes;
        s->hexDigits = s->hexLengths + s->hexLengthBytes;
        return true;
    }
    if (s->type == 6) {
        if (n < 23) return false;
        s->blockRows=rd16(a+13);
        s->indexCount=rd32(a+15);
        if (s->blockRows==0 || s->indexCount!=(s->rows+s->blockRows-1)/s->blockRows) return false;
        size_t off=19; s->symCount=256;
        if (off+s->symCount+(size_t)s->indexCount*4+4>n) return false;
        s->huffLen.assign(a+off,a+off+s->symCount); off+=s->symCount;
        s->indexData=a+off; off+=(size_t)s->indexCount*4;
        s->rowData=a+off;
        return initHuffman(s);
    }
    if (s->type == 10) {
        if (n < 29) return false;
        uint16_t wordCount = rd16(a + 13);
        s->mergeCount = rd16(a + 15);
        s->symCount = rd16(a + 17);
        uint16_t group = rd16(a + 19);
        s->indexCount = rd32(a + 21);
        s->baseSym = (uint16_t)(256u + wordCount);
        if (group != GROUP || s->indexCount != (s->rows + GROUP - 1) / GROUP ||
            s->symCount != (uint32_t)s->baseSym + s->mergeCount + 1u) return false;
        size_t off = 25;
        s->expansion.resize(s->symCount);
        for (unsigned i = 0; i < 256; ++i) s->expansion[i].assign(1, (char)i);
        std::string prev;
        for (uint32_t i = 0; i < wordCount; ++i) {
            uint32_t prefix, suffix;
            const uint8_t* vp = a + off;
            if (!getVar(vp, a + n, prefix)) return false;
            off = (size_t)(vp - a);
            vp = a + off;
            if (!getVar(vp, a + n, suffix)) return false;
            off = (size_t)(vp - a);
            if (prefix > prev.size() || off + suffix > n) return false;
            std::string cur(prev.data(), prefix);
            cur.append((const char*)a + off, suffix);
            off += suffix;
            s->expansion[256u + i] = cur;
            prev.swap(cur);
        }
        if (off + (size_t)s->mergeCount * 4u + (size_t)s->symCount +
            (size_t)s->indexCount * 4u + 4u > n) return false;
        s->merges.resize(s->mergeCount);
        for (uint32_t i = 0; i < s->mergeCount; ++i) {
            s->merges[i] = Pair{rd16(a + off), rd16(a + off + 2)};
            off += 4;
            if (s->merges[i].a >= s->baseSym + i || s->merges[i].b >= s->baseSym + i) return false;
        }
        s->huffLen.assign(a + off, a + off + s->symCount);
        off += s->symCount;
        s->indexData = a + off;
        off += (size_t)s->indexCount * 4u;
        s->rowData = a + off;
        for (uint32_t i = 0; i < s->mergeCount; ++i) {
            const Pair& pair = s->merges[i];
            std::string& out = s->expansion[s->baseSym + i];
            out.reserve(s->expansion[pair.a].size() + s->expansion[pair.b].size());
            out.append(s->expansion[pair.a]);
            out.append(s->expansion[pair.b]);
            if (out.size() > 65535u) return false;
        }
        return initHuffman(s);
    }
    if (s->type == 9) {
        if (n < 27) return false;
        uint16_t group = rd16(a + 13);
        s->indexCount = rd32(a + 15);
        uint16_t wordCount = rd16(a + 19);
        s->symCount = rd16(a + 21);
        if (group != GROUP || s->indexCount != (s->rows + GROUP - 1) / GROUP ||
            s->symCount != (uint32_t)wordCount + 257u) return false;
        size_t off = 23;
        s->expansion.resize(s->symCount);
        for (unsigned i = 0; i < 256; ++i) s->expansion[i].assign(1, (char)i);
        std::string prev;
        for (uint32_t i = 0; i < wordCount; ++i) {
            uint32_t prefix, suffix;
            const uint8_t* vp = a + off;
            if (!getVar(vp, a + n, prefix)) return false;
            off = (size_t)(vp - a);
            vp = a + off;
            if (!getVar(vp, a + n, suffix)) return false;
            off = (size_t)(vp - a);
            if (prefix > prev.size() || off + suffix > n) return false;
            std::string cur(prev.data(), prefix);
            cur.append((const char*)a + off, suffix);
            off += suffix;
            s->expansion[256u + i] = cur;
            prev.swap(cur);
        }
        if (off + (size_t)s->symCount + (size_t)s->indexCount * 4u + 4u > n) return false;
        s->huffLen.assign(a + off, a + off + s->symCount);
        off += s->symCount;
        s->indexData = a + off;
        off += (size_t)s->indexCount * 4u;
        s->rowData = a + off;
        return initHuffman(s);
    }
    if (s->type == 8) {
        if (n<23) return false;
        uint16_t group=rd16(a+13); s->indexCount=rd32(a+15);
        if (group!=GROUP || s->indexCount!=(s->rows+GROUP-1)/GROUP) return false;
        size_t off=19;
        s->ctxDet.assign(257,-2); s->ctxRoot.assign(257,-1);
        s->ctxFast.assign((size_t)257*256,FastEnt{0xffffu,0,0}); s->ctxTree.clear();
        for (uint32_t ctx=0;ctx<257;++ctx) {
            if (off+2>n) return false;
            uint16_t ns=rd16(a+off); off+=2;
            if (ns>257 || off+(size_t)ns*3>n) return false;
            std::vector<uint16_t> syms(ns); std::vector<uint8_t> lens(ns);
            for (uint32_t j=0;j<ns;++j) { syms[j]=rd16(a+off); off+=2; lens[j]=a[off++]; }
            if (!initContextModel(s,ctx,syms.data(),lens.data(),ns)) return false;
        }
        if (off+4>n) return false;
        uint32_t n2=rd32(a+off); off+=4;
        if (n2>65536 || off+(size_t)n2*6>n) return false;
        s->ctx2Index.assign(65536,-1); s->ctx2Det.assign(n2,-2); s->ctx2Root.assign(n2,-1);
        s->ctx2Fast.assign((size_t)n2*256,FastEnt{0xffffu,0,0}); s->ctx2Tree.clear();
        for (uint32_t ix=0;ix<n2;++ix) {
            uint16_t key=rd16(a+off); off+=2;
            if (s->ctx2Index[key]>=0 || off+2>n) return false;
            uint16_t ns=rd16(a+off); off+=2;
            if (ns>257 || off+(size_t)ns*3>n) return false;
            std::vector<uint16_t> syms(ns); std::vector<uint8_t> lens(ns);
            for (uint32_t j=0;j<ns;++j) { syms[j]=rd16(a+off); off+=2; lens[j]=a[off++]; }
            s->ctx2Index[key]=(int32_t)ix;
            if (!initContext2Model(s,ix,syms.data(),lens.data(),ns)) return false;
        }
        if (off+(size_t)s->indexCount*4+4>n) return false;
        s->indexData=a+off; off+=(size_t)s->indexCount*4; s->rowData=a+off;
        return true;
    }
    if (s->type == 7) {
        if (n < 29) return false;
        uint16_t domains = rd16(a+13);
        s->mergeCount = rd16(a+15);
        s->symCount = rd16(a+17);
        uint16_t group = rd16(a+19);
        s->indexCount = rd32(a+21);
        s->baseSym = (uint16_t)(256u + domains);
        if (group != GROUP || s->indexCount != (s->rows+GROUP-1)/GROUP ||
            s->symCount != (uint16_t)(s->baseSym+s->mergeCount+1u)) return false;
        size_t off=25;
        s->expansion.resize(s->symCount);
        for (unsigned i=0;i<256;++i) s->expansion[i].assign(1,(char)i);
        for (uint32_t i=0;i<domains;++i) {
            uint32_t len;
            const uint8_t* vp=a+off;
            if (!getVar(vp,a+n,len)) return false;
            off=(size_t)(vp-a);
            if (off+len>n) return false;
            s->expansion[256+i].assign((const char*)a+off,len);
            off+=len;
        }
        if (off+(size_t)s->mergeCount*4u+s->symCount+(size_t)s->indexCount*4u+4u>n) return false;
        s->merges.resize(s->mergeCount);
        for (uint32_t i=0;i<s->mergeCount;++i) {
            s->merges[i]=Pair{rd16(a+off),rd16(a+off+2)}; off+=4;
            if (s->merges[i].a>=s->baseSym+i || s->merges[i].b>=s->baseSym+i) return false;
        }
        s->huffLen.assign(a+off,a+off+s->symCount); off+=s->symCount;
        for (uint32_t i=0;i<s->mergeCount;++i) {
            const Pair& p=s->merges[i];
            std::string& out=s->expansion[s->baseSym+i];
            out.reserve(s->expansion[p.a].size()+s->expansion[p.b].size());
            out.append(s->expansion[p.a]); out.append(s->expansion[p.b]);
        }
        s->indexData=a+off; off+=(size_t)s->indexCount*4u;
        s->rowData=a+off;
        return initHuffman(s);
    }
    if (s->type == 5) {
        if (n < 55) return false;
        s->blockRows = rd16(a + 13);
        s->indexCount = rd32(a + 15);
        uint16_t alphabetSize = rd16(a + 19);
        if (s->blockRows == 0 ||
            s->indexCount != (s->rows + s->blockRows - 1) / s->blockRows ||
            alphabetSize == 0 || alphabetSize > 256) return false;
        size_t off = 21;
        for (unsigned byte = 0; byte < 256; ++byte)
            if ((a[off + (byte >> 3)] >> (byte & 7u)) & 1u)
                s->bwtAlphabet.push_back((uint8_t)byte);
        off += 32;
        if (s->bwtAlphabet.size() != alphabetSize) return false;
        s->indexData = a + off;
        off += (size_t)s->indexCount * 4u;
        s->rowData = a + off;
        const uint8_t* dataEnd = a + n - 4;
        size_t rowDataBytes = (size_t)(dataEnd - s->rowData);
        s->bwtBlocks.resize(s->indexCount);
        for (uint32_t block = 0; block < s->indexCount; ++block) {
            uint32_t pos = rd32(s->indexData + (size_t)block * 4u);
            uint32_t nextPos = block + 1u < s->indexCount
                ? rd32(s->indexData + (size_t)(block + 1u) * 4u)
                : (uint32_t)rowDataBytes;
            if (pos >= nextPos || nextPos > rowDataBytes) return false;
            const uint8_t* q = s->rowData + pos;
            const uint8_t* end = s->rowData + nextPos;
            if (q >= end) return false;
            uint32_t ngroups = *q++;
            if (ngroups == 0 || ngroups > 6) return false;
            uint32_t selectorCount;
            if (!getVar(q, end, selectorCount) || selectorCount == 0) return false;
            BwtBlockMeta& meta = s->bwtBlocks[block];
            meta.selectorCount = selectorCount;
            meta.models.resize(ngroups);
            s->symCount = (uint16_t)(alphabetSize + 2u);
            if ((uint64_t)(end - q) < (uint64_t)ngroups * s->symCount) return false;
            for (uint32_t g = 0; g < ngroups; ++g) {
                if (!initBwtHuff(q, s->symCount, meta.models[g])) return false;
                q += s->symCount;
            }
            uint64_t selectorBytes64 = ((uint64_t)selectorCount * 3u + 7u) / 8u;
            if (selectorBytes64 > (uint64_t)(end - q)) return false;
            meta.selectors = q;
            q += (size_t)selectorBytes64;
            uint32_t clen, rawLen, primary;
            if (!getVar(q, end, clen) || !getVar(q, end, rawLen) ||
                !getVar(q, end, primary) || (size_t)(end - q) < clen) return false;
            if (clen == 0 || rawLen == 0 || primary >= rawLen ||
                selectorCount > rawLen + 1u) return false;
            meta.payloadLen = clen;
            meta.rawLen = rawLen;
            meta.primary = primary;
            meta.payload = q;
            q += clen;
            if (q != end) return false;
        }
        return true;
    }
    if (s->type == 4) {
        if (n < 23) return false;
        uint16_t group = rd16(a + 13);
        s->indexCount = rd32(a + 15);
        if (group != GROUP || s->indexCount != (s->rows + GROUP - 1) / GROUP) return false;
        size_t off = 19;
        s->ctxDet.assign(257, -2);
        s->ctxRoot.assign(257, -1);
        s->ctxFast.assign((size_t)257 * 256, FastEnt{0xffffu,0,0});
        s->ctxTree.clear();
        for (uint32_t ctx = 0; ctx < 257; ++ctx) {
            if (off + 2 > n) return false;
            uint16_t ns = rd16(a + off); off += 2;
            if (ns > 257 || off + (size_t)ns * 3 > n) return false;
            std::vector<uint16_t> syms(ns);
            std::vector<uint8_t> lens(ns);
            for (uint32_t j = 0; j < ns; ++j) {
                syms[j] = rd16(a + off); off += 2;
                lens[j] = a[off++];
            }
            if (!initContextModel(s, ctx, syms.data(), lens.data(), ns)) return false;
        }
        if (off + (size_t)s->indexCount * 4 + 4 > n) return false;
        s->indexData = a + off;
        off += (size_t)s->indexCount * 4;
        s->rowData = a + off;
        return true;
    }
    if (s->type == 2) {
        if (n < 23) return false;
        s->mergeCount = rd16(a + 13);
        s->symCount = rd16(a + 15);
        if (s->symCount != (uint16_t)(257u + s->mergeCount)) return false;
        s->baseSym = 256;
        size_t off = 17;
        if (off + (size_t)s->mergeCount * 4u + s->symCount + 6u > n) return false;
        s->merges.resize(s->mergeCount);
        for (uint32_t i = 0; i < s->mergeCount; ++i) {
            s->merges[i] = Pair{rd16(a + off), rd16(a + off + 2)};
            off += 4;
            if (s->merges[i].a >= s->baseSym + i || s->merges[i].b >= s->baseSym + i) return false;
        }
        s->huffLen.assign(a + off, a + off + s->symCount);
        off += s->symCount;
        uint16_t group = rd16(a + off); off += 2;
        if (group != GROUP) return false;
        s->indexCount = rd32(a + off); off += 4;
        if (s->indexCount != (s->rows + GROUP - 1) / GROUP) return false;
        if (off + (size_t)s->indexCount * 4u + 4 > n) return false;
        s->indexData = a + off;
        off += (size_t)s->indexCount * 4u;
        s->rowData = a + off;

        s->expansion.resize(s->symCount);
        for (unsigned i = 0; i < 256; ++i) s->expansion[i].assign(1, (char)i);
        for (uint32_t i = 0; i < s->mergeCount; ++i) {
            const Pair& p = s->merges[i];
            std::string& out = s->expansion[s->baseSym + i];
            out.reserve(s->expansion[p.a].size() + s->expansion[p.b].size());
            out.append(s->expansion[p.a]); out.append(s->expansion[p.b]);
            if (out.size() > 65535) return false;
        }
        return initHuffman(s);
    }
    return false;
}

static inline const uint8_t* indexAt(const State* s, uint32_t block) {
    return s->indexData + (size_t)block * 4;
}
static inline uint32_t endingAt(const State* s, uint32_t row) {
    return (s->ends[row >> 2] >> ((row & 3u) * 2u)) & 3u;
}

static bool decodeOnePos(const State* s, uint32_t row, uint8_t* out, uint32_t cap, uint32_t& n) {
    if (row >= s->rows) return false;
    uint32_t fl = endingAt(s, row);
    n = s->bodyLen + ((fl & 1u) ? 1u : 0u) + ((fl & 2u) ? 1u : 0u);
    if (n > cap) return false;
    uint64_t bitpos = (uint64_t)row * s->bitsPerRow;
    for (uint32_t j = 0; j < s->bodyLen; ++j) {
        unsigned b = s->posBits[j];
        uint32_t c = b ? getBits(s->packed, bitpos + s->posBitOffset[j], b) : 0;
        out[j] = s->posMap[j][c];
    }
    uint32_t w = s->bodyLen;
    if (fl & 1u) out[w++] = '\r';
    if (fl & 2u) out[w++] = '\n';
    return w == n;
}

static bool decodeOneHex(const State* s, uint32_t row, uint8_t* out, uint32_t cap, uint32_t& n) {
    if (row >= s->rows) return false;
    uint32_t len = getBits(s->hexLengths, (uint64_t)row * s->hexLenBits, s->hexLenBits);
    if (len > s->hexMaxLen) return false;
    uint32_t fl = endingAt(s, row);
    n = len + ((fl & 1u) ? 1u : 0u) + ((fl & 2u) ? 1u : 0u);
    if (n > cap) return false;
    static constexpr char upper[] = "0123456789ABCDEF";
    static constexpr char lower[] = "0123456789abcdef";
    const char* alphabet = s->hexLower ? lower : upper;
    const uint8_t* src = s->hexDigits + (size_t)row * s->hexBytesPerRow;
    for (uint32_t j = 0; j < len; ++j) {
        uint8_t v = (j & 1u) ? (src[j >> 1] & 15u) : (src[j >> 1] >> 4);
        out[j] = (uint8_t)alphabet[v];
    }
    uint32_t w = len;
    if (fl & 1u) out[w++] = '\r';
    if (fl & 2u) out[w++] = '\n';
    return w == n;
}

} // namespace

#ifndef CODEC_DECODER_ONLY
extern "C" int64_t lab_encode(const uint8_t* raw, size_t size, uint8_t* archive, size_t capacity) {
    try {
        if ((!raw && size) || size > UINT32_MAX) return -1;
        std::vector<Row> rows = splitRows(raw, size);
        std::vector<uint8_t> best;
        if (rows.empty()) {
            best.insert(best.end(), {'D','B','T','1'}); best.push_back(0);
            wr32(best, (uint32_t)size); wr32(best, 0); wr32(best, 0);
        } else {
            auto consider = [&](auto fn) {
                std::vector<uint8_t> candidate;
                if (fn(raw, size, rows, candidate) &&
                    (best.empty() || candidate.size() < best.size()))
                    best.swap(candidate);
            };
            consider(makePositional);
            consider(makeHex);
            consider(makeUrlArchive);
            consider(makeBpeArchive);
            consider(makeBwtArchive);
            consider(makeLzArchive);
            consider(makeContext2Archive);
            consider(makeContextArchive);
            consider(makeWordArchive);
            consider(makeWordBpeArchive);
            if (best.empty()) return -1;
        }
        if (best.size() > capacity) return -1;
        std::memcpy(archive, best.data(), best.size());
        return (int64_t)best.size();
    } catch (...) { return -1; }
}

#endif

extern "C" void* lab_open(const uint8_t* archive, size_t size) {
    try {
        if (!archive) return nullptr;
        State* s = new State();
        s->archive = archive; s->archiveSize = size;
        if (!initState(s) || !prepareBwtIndexes(s)) { delete s; return nullptr; }
        return s;
    } catch (...) { return nullptr; }
}

extern "C" int64_t lab_decode(void* state, uint8_t* output, size_t capacity) {
    try {
        State* s = (State*)state;
        if (!s || (!output && s->rawSize) || capacity < s->rawSize) return -1;
        if (s->type == 0) {
            if (s->rawSize) std::memcpy(output, s->rowData, s->rawSize);
            return s->rawSize;
        }
        uint64_t written = 0;
        if (s->type == 1) {
            for (uint32_t i = 0; i < s->rows; ++i) {
                uint32_t n = 0;
                if (!decodeOnePos(s, i, output + written, (uint32_t)(capacity - written), n)) return -1;
                written += n;
            }
        } else if (s->type == 3) {
            for (uint32_t i = 0; i < s->rows; ++i) {
                uint32_t n = 0;
                if (!decodeOneHex(s, i, output + written, (uint32_t)(capacity - written), n)) return -1;
                written += n;
            }
        } else if (s->type == 9 || s->type == 10) {
            for (uint32_t block = 0; block < s->indexCount; ++block) {
                const uint8_t* p = s->rowData + rd32(indexAt(s, block));
                uint64_t bitpos = 0;
                uint32_t first = block * GROUP;
                uint32_t last = std::min<uint32_t>(s->rows, first + GROUP);
                for (uint32_t row = first; row < last; ++row) {
                    uint32_t rlen = 0;
                    bool ok = s->type == 10
                        ? decodeWordBpeRow(s, p, bitpos, output + written,
                                           (uint32_t)(capacity - written), rlen)
                        : decodeWordRow(s, p, bitpos, output + written,
                                        (uint32_t)(capacity - written), rlen);
                    if (!ok) return -1;
                    written += rlen;
                }
            }
        } else if (s->type == 8) {
            const uint8_t* p=s->rowData; const uint8_t* end=s->archive+s->archiveSize-4;
            for (uint32_t i=0;i<s->rows;++i) {
                uint32_t clen,rlen=0;
                if (!getVar(p,end,clen) || (size_t)(end-p)<clen) return -1;
                if (!decodeContext2Row(s,p,clen,output+written,(uint32_t)(capacity-written),rlen)) return -1;
                p+=clen; written+=rlen;
            }
        } else if (s->type == 6) {
            const uint8_t* p=s->rowData; const uint8_t* end=s->archive+s->archiveSize-4;
            for (uint32_t block=0;block<s->indexCount;++block) {
                uint32_t clen,rlen;
                if (!getVar(p,end,clen) || !getVar(p,end,rlen) || (size_t)(end-p)<clen) return -1;
                if (written+rlen>capacity || !decodeLzBlock(s,p,clen,rlen,output+written)) return -1;
                p+=clen; written+=rlen;
            }
        } else if (s->type == 5) {
            for (uint32_t block = 0; block < s->indexCount; ++block) {
                uint32_t rawLen = s->bwtBlocks[block].rawLen;
                if (written + rawLen > capacity || !decodePreparedBwtBlock(s->bwtBlocks[block], output + written)) return -1;
                written += rawLen;
            }
        } else if (s->type == 4) {
            const uint8_t* p = s->rowData;
            const uint8_t* end = s->archive + s->archiveSize - 4;
            for (uint32_t i = 0; i < s->rows; ++i) {
                uint32_t clen, rlen = 0;
                if (!getVar(p, end, clen) || (size_t)(end - p) < clen) return -1;
                if (!decodeContextRow(s, p, clen, output + written,
                                      (uint32_t)(capacity - written), rlen)) return -1;
                p += clen; written += rlen;
            }
        } else if (s->type == 2 || s->type == 7) {
            const uint8_t* p = s->rowData;
            const uint8_t* end = s->archive + s->archiveSize - 4;
            for (uint32_t i = 0; i < s->rows; ++i) {
                uint32_t clen, rlen = 0;
                if (!getVar(p, end, clen) || (size_t)(end - p) < clen) return -1;
                if (!decodeBpeRow(s, p, clen, output + written, (uint32_t)(capacity - written), rlen)) return -1;
                if (written + rlen > capacity) return -1;
                p += clen; written += rlen;
            }
        }
        if (written != s->rawSize) return -1;
        return (int64_t)written;
    } catch (...) { return -1; }
}

extern "C" int64_t lab_rows(void* state, const uint64_t* ids, size_t count,
                            uint8_t* output, size_t capacity, uint64_t* offsets) {
    try {
        State* s = (State*)state;
        if (!s || (!ids && count) || (!output && capacity) || !offsets) return -1;
        offsets[0] = 0;
        uint64_t written = 0;
        if (s->type == 1) {
            for (size_t i = 0; i < count; ++i) {
                if (ids[i] >= s->rows || (i && ids[i] < ids[i-1])) return -1;
                uint32_t n = 0;
                if (written > capacity || !decodeOnePos(s, (uint32_t)ids[i], output + written,
                                                         (uint32_t)(capacity - written), n)) return -1;
                written += n; offsets[i+1] = written;
            }
            return (int64_t)written;
        }
        if (s->type == 3) {
            for (size_t i = 0; i < count; ++i) {
                if (ids[i] >= s->rows || (i && ids[i] < ids[i-1])) return -1;
                uint32_t n = 0;
                if (written > capacity || !decodeOneHex(s, (uint32_t)ids[i], output + written,
                                                         (uint32_t)(capacity - written), n)) return -1;
                written += n; offsets[i+1] = written;
            }
            return (int64_t)written;
        }
        if (s->type == 0) {
            uint32_t currBlock = UINT32_MAX, currRow = 0;
            const uint8_t* cursor = nullptr;
            const uint8_t* dataEnd = s->rowData + s->rawSize;
            for (size_t i = 0; i < count; ++i) {
                uint64_t id64 = ids[i];
                if (id64 >= s->rows || (i && id64 < ids[i-1])) return -1;
                uint32_t id = (uint32_t)id64, block = id / GROUP;
                if (block != currBlock) {
                    currBlock = block; currRow = block * GROUP;
                    cursor = s->rowData + rd32(indexAt(s, block));
                }
                while (currRow < id) {
                    const uint8_t* nl = (const uint8_t*)std::memchr(cursor, '\n', (size_t)(dataEnd - cursor));
                    cursor = nl ? nl + 1 : dataEnd;
                    ++currRow;
                }
                const uint8_t* start = cursor;
                const uint8_t* nl = (const uint8_t*)std::memchr(cursor, '\n', (size_t)(dataEnd - cursor));
                cursor = nl ? nl + 1 : dataEnd;
                uint64_t len = (uint64_t)(cursor - start);
                if (written + len > capacity) return -1;
                std::memcpy(output + written, start, (size_t)len);
                written += len; offsets[i+1] = written;
                currRow = id + 1;
            }
            return (int64_t)written;
        }
        if (s->type == 9 || s->type == 10) {
            uint32_t currBlock = UINT32_MAX, currRow = 0;
            const uint8_t* payload = nullptr;
            uint64_t bitpos = 0;
            uint64_t previousId = UINT64_MAX, previousStart = 0, previousLen = 0;
            for (size_t i = 0; i < count; ++i) {
                uint64_t id64 = ids[i];
                if (id64 >= s->rows || (i && id64 < ids[i-1])) return -1;
                if (i && id64 == previousId) {
                    if (written + previousLen > capacity) return -1;
                    if (previousLen) std::memcpy(output + written, output + previousStart, (size_t)previousLen);
                    written += previousLen;
                    offsets[i+1] = written;
                    continue;
                }
                uint32_t id = (uint32_t)id64, block = id / GROUP;
                if (block != currBlock) {
                    currBlock = block;
                    currRow = block * GROUP;
                    payload = s->rowData + rd32(indexAt(s, block));
                    bitpos = 0;
                }
                while (currRow <= id) {
                    bool selected = currRow == id;
                    uint32_t rlen = 0;
                    uint8_t* dst = selected ? output + written : nullptr;
                    uint32_t cap = selected && written <= capacity ? (uint32_t)(capacity - written) : 0;
                    bool ok = s->type == 10
                        ? decodeWordBpeRow(s, payload, bitpos, dst, cap, rlen)
                        : decodeWordRow(s, payload, bitpos, dst, cap, rlen);
                    if (!ok) return -1;
                    if (selected) {
                        previousStart = written;
                        previousLen = rlen;
                        if (written + rlen > capacity) return -1;
                        written += rlen;
                        offsets[i+1] = written;
                    }
                    ++currRow;
                }
                previousId = id64;
            }
            return (int64_t)written;
        }
        if (s->type == 8) {
            uint32_t currBlock=UINT32_MAX,currRow=0;
            const uint8_t* p=nullptr; const uint8_t* end=s->archive+s->archiveSize-4;
            for (size_t i=0;i<count;++i) {
                uint64_t id64=ids[i];
                if (id64>=s->rows || (i && id64<ids[i-1])) return -1;
                uint32_t id=(uint32_t)id64,block=id/GROUP;
                if (block!=currBlock) { currBlock=block; currRow=block*GROUP; p=s->rowData+rd32(indexAt(s,block)); }
                while (currRow<id) { uint32_t clen; if (!getVar(p,end,clen)||(size_t)(end-p)<clen) return -1; p+=clen; ++currRow; }
                uint32_t clen,rlen=0;
                if (!getVar(p,end,clen)||(size_t)(end-p)<clen) return -1;
                if (written>capacity || !decodeContext2Row(s,p,clen,output+written,(uint32_t)(capacity-written),rlen)) return -1;
                p+=clen; written+=rlen; offsets[i+1]=written; currRow=id+1;
            }
            return (int64_t)written;
        }
        if (s->type == 6) {
            std::vector<uint8_t> scratch;
            std::vector<uint32_t> starts,lens;
            size_t q=0; const uint8_t* end=s->archive+s->archiveSize-4;
            while (q<count) {
                uint64_t id64=ids[q];
                if (id64>=s->rows || (q && id64<ids[q-1])) return -1;
                uint32_t id=(uint32_t)id64, block=id/s->blockRows, firstRow=block*s->blockRows;
                const uint8_t* p=s->rowData+rd32(indexAt(s,block));
                uint32_t clen,rawLen;
                if (!getVar(p,end,clen) || !getVar(p,end,rawLen) || (size_t)(end-p)<clen) return -1;
                scratch.resize(rawLen);
                if (!decodeLzBlock(s,p,clen,rawLen,scratch.data())) return -1;
                uint32_t blockRows=std::min<uint32_t>(s->blockRows,s->rows-firstRow);
                starts.resize(blockRows); lens.resize(blockRows);
                uint32_t off=0;
                for (uint32_t r=0;r<blockRows;++r) {
                    starts[r]=off;
                    uint8_t* nl=(uint8_t*)std::memchr(scratch.data()+off,'\n',rawLen-off);
                    off=nl?(uint32_t)(nl-scratch.data())+1:rawLen;
                    lens[r]=off-starts[r];
                }
                while (q<count && ids[q]/s->blockRows==block) {
                    uint32_t local=(uint32_t)ids[q]-firstRow, len=lens[local];
                    if (written+len>capacity) return -1;
                    std::memcpy(output+written,scratch.data()+starts[local],len);
                    written+=len; offsets[q+1]=written; ++q;
                }
            }
            return (int64_t)written;
        }
        if (s->type == 5) {
            bool allRows = count == s->rows;
            if (allRows) {
                for (size_t i = 0; i < count; ++i) if (ids[i] != i) { allRows = false; break; }
            }
            if (allRows) {
                int64_t decoded = lab_decode(state, output, capacity);
                if (decoded < 0) return -1;
                uint64_t at = 0;
                for (uint32_t id = 0; id < s->rows; ++id) {
                    uint32_t block = id / s->blockRows;
                    uint32_t local = id - block * s->blockRows;
                    at += s->bwtBlocks[block].rowLen[local];
                    offsets[id + 1] = at;
                }
                return decoded;
            }
            // First compute output boundaries. Then decode 16 independent LF chains
            // together to expose memory-level parallelism without materializing rows.
            for (size_t i = 0; i < count; ++i) {
                uint64_t id64 = ids[i];
                if (id64 >= s->rows || (i && id64 < ids[i-1])) return -1;
                uint32_t id = (uint32_t)id64;
                uint32_t block = id / s->blockRows;
                uint32_t local = id - block * s->blockRows;
                const BwtBlockMeta& meta = s->bwtBlocks[block];
                uint32_t len = meta.rowLen[local];
                if (written + len > capacity) return -1;
                written += len;
                offsets[i+1] = written;
            }
            constexpr uint32_t LANES = 16u;
            for (size_t base = 0; base < count; base += LANES) {
                uint32_t active = (uint32_t)std::min<size_t>(LANES, count - base);
                const BwtBlockMeta* metas[LANES];
                uint32_t ranks[LANES], lens[LANES];
                uint64_t starts[LANES];
                uint32_t maxLen = 0;
                for (uint32_t lane = 0; lane < active; ++lane) {
                    size_t i = base + lane;
                    uint32_t id = (uint32_t)ids[i];
                    uint32_t block = id / s->blockRows;
                    uint32_t local = id - block * s->blockRows;
                    const BwtBlockMeta& meta = s->bwtBlocks[block];
                    metas[lane] = &meta;
                    ranks[lane] = meta.rowEndRank[local];
                    lens[lane] = meta.rowLen[local];
                    starts[lane] = offsets[i];
                    maxLen = std::max(maxLen, lens[lane]);
                }
                for (uint32_t step = 0; step < maxLen; ++step) {
                    for (uint32_t lane = 0; lane < active; ++lane) {
                        if (step >= lens[lane]) continue;
                        const BwtBlockMeta& meta = *metas[lane];
                        uint32_t rank = ranks[lane];
                        output[starts[lane] + lens[lane] - 1u - step] = meta.last[rank];
                        ranks[lane] = meta.lfNext[rank];
                    }
                }
            }
            return (int64_t)written;
        }
        if (s->type == 4) {
            uint32_t currBlock = UINT32_MAX, currRow = 0;
            const uint8_t* p = nullptr;
            const uint8_t* end = s->archive + s->archiveSize - 4;
            for (size_t i = 0; i < count; ++i) {
                uint64_t id64 = ids[i];
                if (id64 >= s->rows || (i && id64 < ids[i-1])) return -1;
                uint32_t id = (uint32_t)id64, block = id / GROUP;
                if (block != currBlock) {
                    currBlock = block; currRow = block * GROUP;
                    p = s->rowData + rd32(indexAt(s, block));
                }
                while (currRow < id) {
                    uint32_t clen;
                    if (!getVar(p, end, clen) || (size_t)(end-p) < clen) return -1;
                    p += clen; ++currRow;
                }
                uint32_t clen, rlen = 0;
                if (!getVar(p, end, clen) || (size_t)(end-p) < clen) return -1;
                if (written > capacity || !decodeContextRow(s, p, clen, output + written,
                                                              (uint32_t)(capacity - written), rlen)) return -1;
                p += clen; written += rlen; offsets[i+1] = written;
                currRow = id + 1;
            }
            return (int64_t)written;
        }
        if (s->type == 2 || s->type == 7) {
            uint32_t currBlock = UINT32_MAX, currRow = 0;
            const uint8_t* p = nullptr;
            const uint8_t* end = s->archive + s->archiveSize - 4;
            for (size_t i = 0; i < count; ++i) {
                uint64_t id64 = ids[i];
                if (id64 >= s->rows || (i && id64 < ids[i-1])) return -1;
                uint32_t id = (uint32_t)id64, block = id / GROUP;
                if (block != currBlock) {
                    currBlock = block; currRow = block * GROUP;
                    p = s->rowData + rd32(indexAt(s, block));
                }
                while (currRow < id) {
                    uint32_t clen;
                    if (!getVar(p, end, clen) || (size_t)(end-p) < clen) return -1;
                    p += clen; ++currRow;
                }
                uint32_t clen, rlen = 0;
                if (!getVar(p, end, clen) || (size_t)(end-p) < clen) return -1;
                if (written > capacity || !decodeBpeRow(s, p, clen, output + written,
                                                          (uint32_t)(capacity - written), rlen)) return -1;
                p += clen; written += rlen; offsets[i+1] = written;
                currRow = id + 1;
            }
            return (int64_t)written;
        }
        return -1;
    } catch (...) { return -1; }
}

extern "C" void lab_close(void* state) {
    try { delete (State*)state; } catch (...) {}
}

