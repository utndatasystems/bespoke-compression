#include <codec.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <immintrin.h>
#include <cstring>
#include <limits>
#include <new>
#include <queue>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

namespace {
constexpr uint8_t TYPE_BPE = 0;
constexpr uint8_t TYPE_CNAME = 1;
constexpr uint8_t TYPE_DNA = 2;
constexpr uint8_t TYPE_UUID = 3;
constexpr uint8_t TYPE_HEX = 4;
constexpr uint8_t TYPE_EXT = 5;
constexpr uint8_t TYPE_CTX = 6;
constexpr uint8_t TYPE_CTX_DIRECT = 7;
constexpr uint8_t TYPE_FIXEDTOK = 8;
constexpr size_t HEADER_SIZE = 32;

static inline uint16_t rd16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
static inline uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
static inline void wr16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x));
    v.push_back(static_cast<uint8_t>(x >> 8));
}
static inline void wr32at(std::vector<uint8_t>& v, size_t p, uint32_t x) {
    v[p + 0] = static_cast<uint8_t>(x);
    v[p + 1] = static_cast<uint8_t>(x >> 8);
    v[p + 2] = static_cast<uint8_t>(x >> 16);
    v[p + 3] = static_cast<uint8_t>(x >> 24);
}
static inline void wrvar(std::vector<uint8_t>& v, uint32_t x) {
    while (x >= 128) {
        v.push_back(static_cast<uint8_t>(x | 128));
        x >>= 7;
    }
    v.push_back(static_cast<uint8_t>(x));
}
static inline uint8_t get3(const uint8_t* p, uint32_t i) {
    uint32_t bit = i * 3;
    uint32_t byte = bit >> 3;
    uint32_t shift = bit & 7;
    uint32_t x = p[byte];
    if (shift > 5) x |= static_cast<uint32_t>(p[byte + 1]) << 8;
    return static_cast<uint8_t>((x >> shift) & 7);
}
static inline uint32_t getbits(const uint8_t* p, uint64_t i, uint32_t width) {
    uint64_t bit = i * width;
    uint32_t byte = static_cast<uint32_t>(bit >> 3);
    uint32_t shift = static_cast<uint32_t>(bit & 7);
    uint32_t count = (shift + width + 7) >> 3;
    uint64_t word = 0;
    for (uint32_t k = 0; k < count; ++k) word |= static_cast<uint64_t>(p[byte + k]) << (8 * k);
    return static_cast<uint32_t>((word >> shift) & ((uint64_t{1} << width) - 1));
}
static inline uint32_t getbits_at(const uint8_t* p, uint64_t bit, uint32_t width) {
    uint32_t byte = static_cast<uint32_t>(bit >> 3);
    uint32_t shift = static_cast<uint32_t>(bit & 7);
    uint32_t count = (shift + width + 7) >> 3;
    uint64_t word = 0;
    for (uint32_t k = 0; k < count; ++k) word |= static_cast<uint64_t>(p[byte + k]) << (8 * k);
    return static_cast<uint32_t>((word >> shift) & ((uint64_t{1} << width) - 1));
}
static inline void put3(std::vector<uint8_t>& p, uint32_t i, uint8_t x) {
    uint32_t bit = i * 3;
    uint32_t byte = bit >> 3;
    uint32_t shift = bit & 7;
    uint16_t v = static_cast<uint16_t>(p[byte]);
    if (shift > 5) v |= static_cast<uint16_t>(p[byte + 1]) << 8;
    v = static_cast<uint16_t>((v & ~(static_cast<uint16_t>(7) << shift)) |
                              (static_cast<uint16_t>(x & 7) << shift));
    p[byte] = static_cast<uint8_t>(v);
    if (shift > 5) p[byte + 1] = static_cast<uint8_t>(v >> 8);
}
static inline int hexval(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

struct Row { uint32_t off; uint32_t len; };
static bool make_rows(const uint8_t* raw, size_t size, std::vector<Row>& rows) {
    if (size > UINT32_MAX) return false;
    size_t start = 0;
    for (size_t i = 0; i < size; ++i) {
        if (raw[i] == '\n') {
            rows.push_back({static_cast<uint32_t>(start), static_cast<uint32_t>(i + 1 - start)});
            start = i + 1;
        }
    }
    if (start < size) rows.push_back({static_cast<uint32_t>(start), static_cast<uint32_t>(size - start)});
    return rows.size() <= UINT32_MAX;
}

static bool finish_archive(uint8_t type, uint8_t flags, uint32_t raw_size,
                           uint32_t rows, uint32_t record_size,
                           const std::vector<uint8_t>& dict,
                           const std::vector<uint8_t>& index,
                           const std::vector<uint8_t>& payload,
                           uint8_t* dst, size_t capacity) {
    if (dict.size() > UINT32_MAX || index.size() > UINT32_MAX || payload.size() > UINT32_MAX) return false;
    size_t total = HEADER_SIZE + dict.size() + index.size() + payload.size();
    if (total > capacity || total > static_cast<size_t>(INT64_MAX)) return false;
    std::vector<uint8_t> h(HEADER_SIZE, 0);
    h[0] = 'D'; h[1] = 'B'; h[2] = 'T'; h[3] = 'X';
    h[4] = type; h[5] = 1; h[6] = flags;
    wr32at(h, 8, raw_size);
    wr32at(h, 12, rows);
    wr32at(h, 16, static_cast<uint32_t>(payload.size()));
    wr32at(h, 20, static_cast<uint32_t>(index.size()));
    wr32at(h, 24, static_cast<uint32_t>(dict.size()));
    wr32at(h, 28, record_size);
    size_t pos = 0;
    std::memcpy(dst + pos, h.data(), h.size()); pos += h.size();
    if (!dict.empty()) { std::memcpy(dst + pos, dict.data(), dict.size()); pos += dict.size(); }
    if (!index.empty()) { std::memcpy(dst + pos, index.data(), index.size()); pos += index.size(); }
    if (!payload.empty()) { std::memcpy(dst + pos, payload.data(), payload.size()); pos += payload.size(); }
    return pos == total;
}
}

#ifdef LAB_ENCODER
namespace {
struct BpeNode {
    uint16_t left = 0;
    uint16_t right = 0;
    uint32_t len = 1;
    std::string text;
};
struct Candidate {
    std::string text;
    uint64_t score = 0;
};
struct TrieNode {
    std::array<int16_t, 256> next;
    int16_t code;
    TrieNode() : code(-1) { next.fill(-1); }
};
struct Codebook {
    std::vector<uint8_t> literals;
    std::vector<std::string> phrases;
    std::vector<TrieNode> trie;
    uint64_t sampled_cost = UINT64_MAX;
    uint64_t dict_cost = UINT64_MAX;
};

static uint32_t sample_step(uint32_t raw_size) {
    constexpr uint32_t SAMPLE_LIMIT = 262144;
    if (raw_size <= SAMPLE_LIMIT) return 1;
    return (raw_size + SAMPLE_LIMIT - 1) / SAMPLE_LIMIT;
}
static bool train_bpe(const uint8_t* raw, const std::vector<Row>& rows,
                      std::vector<Candidate>& candidates,
                      std::vector<uint64_t>& byte_freq, uint32_t max_merges) {
    const uint32_t step = sample_step(static_cast<uint32_t>(rows.empty() ? 0 : rows.back().off + rows.back().len));
    std::vector<std::vector<uint16_t>> seqs;
    seqs.reserve((rows.size() + step - 1) / step);
    for (uint32_t i = 0; i < rows.size(); i += step) {
        const Row& r = rows[i];
        std::vector<uint16_t> s;
        s.reserve(r.len);
        for (uint32_t j = 0; j < r.len; ++j) s.push_back(raw[r.off + j]);
        seqs.emplace_back(std::move(s));
    }
    byte_freq.assign(256, 0);
    for (const auto& s : seqs) for (uint16_t c : s) ++byte_freq[c];

    std::vector<BpeNode> nodes;
    nodes.reserve(static_cast<size_t>(256) + max_merges);
    for (uint32_t i = 0; i < 256; ++i) {
        BpeNode n; n.text.assign(1, static_cast<char>(i));
        nodes.emplace_back(std::move(n));
    }
    const uint32_t max_nodes = 256 + max_merges;
    std::vector<uint32_t> counts(static_cast<size_t>(max_nodes) * max_nodes);
    for (uint32_t merge = 0; merge < max_merges; ++merge) {
        const uint32_t stride = static_cast<uint32_t>(nodes.size());
        std::fill(counts.begin(), counts.begin() + static_cast<size_t>(stride) * stride, 0);
        uint32_t best_key = 0, best_count = 0;
        for (const auto& s : seqs) {
            for (size_t j = 1; j < s.size(); ++j) {
                uint32_t key = static_cast<uint32_t>(s[j - 1]) * stride + s[j];
                uint32_t c = ++counts[key];
                if (c > best_count) { best_count = c; best_key = key; }
            }
        }
        if (best_count < 2) break;
        uint16_t a = static_cast<uint16_t>(best_key / stride);
        uint16_t b = static_cast<uint16_t>(best_key % stride);
        BpeNode n;
        n.left = a; n.right = b;
        uint64_t length = static_cast<uint64_t>(nodes[a].len) + nodes[b].len;
        n.len = length > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(length);
        if (n.len <= 64 && !nodes[a].text.empty() && !nodes[b].text.empty()) {
            n.text.reserve(n.len);
            n.text = nodes[a].text;
            n.text += nodes[b].text;
        }
        uint16_t new_id = static_cast<uint16_t>(nodes.size());
        if (!n.text.empty() && n.len >= 2) {
            Candidate c;
            c.text = n.text;
            c.score = static_cast<uint64_t>(best_count) * (n.len - 1);
            candidates.emplace_back(std::move(c));
        }
        nodes.emplace_back(std::move(n));
        for (auto& s : seqs) {
            if (s.size() < 2) continue;
            std::vector<uint16_t> out;
            out.reserve(s.size());
            for (size_t j = 0; j < s.size();) {
                if (j + 1 < s.size() && s[j] == a && s[j + 1] == b) {
                    out.push_back(new_id); j += 2;
                } else {
                    out.push_back(s[j]); ++j;
                }
            }
            s.swap(out);
        }
    }
    return true;
}

static void build_trie(Codebook& cb) {
    cb.trie.clear();
    cb.trie.emplace_back();
    for (uint32_t i = 0; i < cb.literals.size(); ++i) {
        uint8_t c = cb.literals[i];
        int16_t child = cb.trie[0].next[c];
        if (child < 0) {
            child = static_cast<int16_t>(cb.trie.size());
            cb.trie[0].next[c] = child;
            cb.trie.emplace_back();
        }
        cb.trie[static_cast<size_t>(child)].code = static_cast<int16_t>(i);
    }
    for (uint32_t i = 0; i < cb.phrases.size(); ++i) {
        const std::string& s = cb.phrases[i];
        int16_t node = 0;
        for (unsigned char c : s) {
            int16_t child = cb.trie[static_cast<size_t>(node)].next[c];
            if (child < 0) {
                if (cb.trie.size() >= 32760) return;
                child = static_cast<int16_t>(cb.trie.size());
                cb.trie[static_cast<size_t>(node)].next[c] = child;
                cb.trie.emplace_back();
            }
            node = child;
        }
        cb.trie[static_cast<size_t>(node)].code = static_cast<int16_t>(cb.literals.size() + i);
    }
}
static uint64_t cost_sample(const uint8_t* raw, const std::vector<Row>& rows,
                            uint32_t step, const Codebook& cb) {
    uint64_t cost = 0;
    for (uint32_t ri = 0; ri < rows.size(); ri += step) {
        const Row& r = rows[ri];
        uint32_t p = r.off, end = r.off + r.len;
        while (p < end) {
            int16_t node = 0, best_code = -1;
            uint32_t best_len = 0, q = p;
            while (q < end) {
                int16_t child = cb.trie[static_cast<size_t>(node)].next[raw[q]];
                if (child < 0) break;
                node = child; ++q;
                int16_t c = cb.trie[static_cast<size_t>(node)].code;
                if (c >= 0) { best_code = c; best_len = q - p; }
            }
            if (best_code >= 0) { ++cost; p += best_len; }
            else { cost += 2; ++p; }
        }
    }
    return cost;
}
static bool choose_codebook(const uint8_t* raw, const std::vector<Row>& rows,
                            uint32_t raw_size, const std::vector<Candidate>& raw_candidates,
                            const std::vector<uint64_t>& sample_byte_freq,
                            Codebook& best) {
    std::vector<Candidate> candidates = raw_candidates;
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.text.size() != b.text.size()) return a.text.size() > b.text.size();
        return a.text < b.text;
    });
    std::vector<Candidate> unique;
    unique.reserve(candidates.size());
    for (auto& c : candidates) {
        bool found = false;
        for (auto& u : unique) {
            if (u.text == c.text) { if (c.score > u.score) u.score = c.score; found = true; break; }
        }
        if (!found && c.text.size() >= 2 && c.text.size() <= 64) unique.emplace_back(std::move(c));
    }
    std::array<uint64_t, 256> full_freq{};
    for (uint32_t i = 0; i < raw_size; ++i) ++full_freq[raw[i]];
    std::vector<uint8_t> byte_order;
    for (uint32_t i = 0; i < 256; ++i) if (full_freq[i]) byte_order.push_back(static_cast<uint8_t>(i));
    std::sort(byte_order.begin(), byte_order.end(), [&](uint8_t a, uint8_t b) {
        if (full_freq[a] != full_freq[b]) return full_freq[a] > full_freq[b];
        return a < b;
    });
    uint32_t step = sample_step(raw_size);
    uint64_t sample_bytes = 0;
    for (uint32_t i = 0; i < rows.size(); i += step) sample_bytes += rows[i].len;
    if (sample_bytes == 0) sample_bytes = 1;
    const uint32_t maxp = static_cast<uint32_t>(std::min<size_t>(unique.size(), 254));
    std::vector<uint32_t> trials;
    for (uint32_t p = 0; p <= maxp; p += 8) trials.push_back(p);
    if (trials.empty() || trials.back() != maxp) trials.push_back(maxp);
    uint64_t best_total = UINT64_MAX;
    uint32_t bestp = 0;
    for (uint32_t p : trials) {
        Codebook cb;
        cb.phrases.reserve(p);
        for (uint32_t i = 0; i < p; ++i) cb.phrases.push_back(unique[i].text);
        uint32_t nlit = std::min<uint32_t>(static_cast<uint32_t>(byte_order.size()), 254 - p);
        cb.literals.assign(byte_order.begin(), byte_order.begin() + nlit);
        build_trie(cb);
        uint64_t scost = cost_sample(raw, rows, step, cb);
        uint64_t scaled = static_cast<uint64_t>((static_cast<long double>(scost) * raw_size) / sample_bytes);
        uint64_t dcost = 4 + cb.literals.size();
        for (const auto& s : cb.phrases) dcost += 2 + s.size();
        uint64_t total = scaled + dcost;
        if (total < best_total) { best_total = total; bestp = p; }
    }
    best.phrases.clear();
    best.literals.clear();
    for (uint32_t i = 0; i < bestp; ++i) best.phrases.push_back(unique[i].text);
    uint32_t nlit = std::min<uint32_t>(static_cast<uint32_t>(byte_order.size()), 254 - bestp);
    best.literals.assign(byte_order.begin(), byte_order.begin() + nlit);
    build_trie(best);
    best.sampled_cost = best_total;
    best.dict_cost = 4 + best.literals.size();
    for (const auto& s : best.phrases) best.dict_cost += 2 + s.size();
    return true;
}
static void build_dictionary(const Codebook& cb, std::vector<uint8_t>& dict) {
    dict.clear();
    wr16(dict, static_cast<uint16_t>(cb.literals.size()));
    wr16(dict, static_cast<uint16_t>(cb.phrases.size()));
    dict.insert(dict.end(), cb.literals.begin(), cb.literals.end());
    for (const auto& s : cb.phrases) {
        wr16(dict, static_cast<uint16_t>(s.size()));
        dict.insert(dict.end(), s.begin(), s.end());
    }
}
static bool encode_generic(const uint8_t* raw, uint32_t raw_size, const std::vector<Row>& rows,
                           uint8_t* archive, size_t capacity) {
    std::vector<Candidate> candidates;
    std::vector<uint64_t> sample_byte_freq;
    if (!train_bpe(raw, rows, candidates, sample_byte_freq, 240)) return false;
    Codebook cb;
    if (!choose_codebook(raw, rows, raw_size, candidates, sample_byte_freq, cb)) return false;
    std::vector<uint8_t> dict, index, payload;
    std::vector<uint32_t> row_lens;
    row_lens.reserve(rows.size());
    build_dictionary(cb, dict);
    payload.reserve(raw_size);
    for (const Row& r : rows) {
        uint32_t p = r.off, end = r.off + r.len;
        uint32_t before = static_cast<uint32_t>(payload.size());
        while (p < end) {
            int16_t node = 0, best_code = -1;
            uint32_t best_len = 0, q = p;
            while (q < end) {
                int16_t child = cb.trie[static_cast<size_t>(node)].next[raw[q]];
                if (child < 0) break;
                node = child; ++q;
                int16_t c = cb.trie[static_cast<size_t>(node)].code;
                if (c >= 0) { best_code = c; best_len = q - p; }
            }
            if (best_code >= 0) {
                payload.push_back(static_cast<uint8_t>(best_code));
                p += best_len;
            } else {
                payload.push_back(255);
                payload.push_back(raw[p++]);
            }
        }
        row_lens.push_back(static_cast<uint32_t>(payload.size()) - before);
    }
    uint32_t max_len = 0;
    for (uint32_t n : row_lens) if (n > max_len) max_len = n;
    uint32_t width = 1;
    while (width < 8 && max_len >= (1u << width)) ++width;
    if (width < 8) {
        index.assign((static_cast<uint64_t>(row_lens.size()) * width + 7) / 8, 0);
        for (uint32_t i = 0; i < row_lens.size(); ++i) {
            uint64_t bit = static_cast<uint64_t>(i) * width;
            uint32_t byte = static_cast<uint32_t>(bit >> 3);
            uint32_t shift = static_cast<uint32_t>(bit & 7);
            uint16_t word = index[byte];
            if (shift + width > 8) word |= static_cast<uint16_t>(index[byte + 1]) << 8;
            word = static_cast<uint16_t>(word | (static_cast<uint16_t>(row_lens[i]) << shift));
            index[byte] = static_cast<uint8_t>(word);
            if (shift + width > 8) index[byte + 1] = static_cast<uint8_t>(word >> 8);
        }
    } else {
        width = 0;
        for (uint32_t n : row_lens) wrvar(index, n);
    }
    return finish_archive(TYPE_BPE, 0, raw_size, static_cast<uint32_t>(rows.size()), width,
                          dict, index, payload, archive, capacity);
}



static uint32_t hybrid_plan_row(const uint8_t* raw, const Row& r, const Codebook& cb,
                                uint32_t base_count, std::vector<uint32_t>& dp,
                                std::vector<int16_t>& choice, std::vector<uint8_t>& choice_len) {
    uint32_t n = r.len;
    dp.resize(static_cast<size_t>(n) + 1);
    choice.resize(n);
    choice_len.resize(n);
    dp[n] = 0;
    for (uint32_t i = n; i-- > 0;) {
        uint8_t b = raw[r.off + i];
        dp[i] = (b < 128 ? 2u : 3u) + dp[i + 1];
        choice[i] = -1;
        choice_len[i] = 1;
        int16_t node = 0;
        for (uint32_t j = i; j < n && j - i < 64; ++j) {
            int16_t child = cb.trie[static_cast<size_t>(node)].next[raw[r.off + j]];
            if (child < 0) break;
            node = child;
            int16_t code = cb.trie[static_cast<size_t>(node)].code;
            if (code >= 0) {
                uint32_t token_cost = static_cast<uint32_t>(code) < base_count ? 1u : 2u;
                uint32_t cost = token_cost + dp[j + 1];
                if (cost < dp[i]) {
                    dp[i] = cost;
                    choice[i] = code;
                    choice_len[i] = static_cast<uint8_t>(j + 1 - i);
                }
            }
        }
    }
    return dp[0];
}
static uint64_t cost_sample_hybrid(const uint8_t* raw, const std::vector<Row>& rows,
                                   uint32_t step, const Codebook& cb, uint32_t base_count,
                                   uint32_t& max_row_bytes) {
    std::vector<uint32_t> dp;
    std::vector<int16_t> choice;
    std::vector<uint8_t> choice_len;
    uint64_t total = 0;
    max_row_bytes = 0;
    for (uint32_t i = 0; i < rows.size(); i += step) {
        uint32_t n = hybrid_plan_row(raw, rows[i], cb, base_count, dp, choice, choice_len);
        total += n;
        if (n > max_row_bytes) max_row_bytes = n;
    }
    return total;
}
static bool encode_hybrid(const uint8_t* raw, uint32_t raw_size, const std::vector<Row>& rows,
                          uint8_t* archive, size_t capacity) {
    std::vector<Candidate> candidates;
    std::vector<uint64_t> sample_byte_freq;
    if (!train_bpe(raw, rows, candidates, sample_byte_freq, 1000)) return false;
    Codebook base;
    if (!choose_codebook(raw, rows, raw_size, candidates, sample_byte_freq, base)) return false;
    const uint32_t base_phrases = static_cast<uint32_t>(base.phrases.size());
    const uint32_t base_count = static_cast<uint32_t>(base.literals.size()) + base_phrases;

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.text.size() != b.text.size()) return a.text.size() > b.text.size();
        return a.text < b.text;
    });
    std::vector<Candidate> extra;
    extra.reserve(candidates.size());
    for (auto& c : candidates) {
        if (c.text.size() < 4 || c.text.size() > 64) continue;
        bool found = false;
        for (const auto& phrase : base.phrases) if (phrase == c.text) { found = true; break; }
        if (found) continue;
        for (auto& u : extra) {
            if (u.text == c.text) {
                if (c.score > u.score) u.score = c.score;
                found = true;
                break;
            }
        }
        if (!found) extra.emplace_back(std::move(c));
    }
    auto benefit = [](const Candidate& c) -> int64_t {
        uint64_t len = c.text.size();
        uint64_t freq = c.score / (len - 1);
        return static_cast<int64_t>(freq * (len - 2)) - static_cast<int64_t>(len);
    };
    std::sort(extra.begin(), extra.end(), [&](const Candidate& a, const Candidate& b) {
        int64_t ga = benefit(a), gb = benefit(b);
        if (ga != gb) return ga > gb;
        if (a.score != b.score) return a.score > b.score;
        if (a.text.size() != b.text.size()) return a.text.size() > b.text.size();
        return a.text < b.text;
    });
    extra.erase(std::remove_if(extra.begin(), extra.end(),
                               [&](const Candidate& c) { return benefit(c) <= 0; }), extra.end());
    const uint32_t max_extra = static_cast<uint32_t>(std::min<size_t>(extra.size(), 127));
    const uint32_t step = sample_step(raw_size);
    uint64_t sample_bytes = 0;
    for (uint32_t i = 0; i < rows.size(); i += step) sample_bytes += rows[i].len;
    if (!sample_bytes) sample_bytes = 1;
    std::vector<uint32_t> trials{0};
    for (uint32_t k = 16; k < max_extra; k += 16) trials.push_back(k);
    if (trials.back() != max_extra) trials.push_back(max_extra);
    uint64_t best_total = UINT64_MAX;
    uint32_t best_extra = 0;
    for (uint32_t k : trials) {
        Codebook cb = base;
        cb.phrases.reserve(base_phrases + k);
        for (uint32_t i = 0; i < k; ++i) cb.phrases.push_back(extra[i].text);
        build_trie(cb);
        uint32_t max_row = 0;
        uint64_t sample_cost = cost_sample_hybrid(raw, rows, step, cb, base_count, max_row);
        uint64_t scaled = static_cast<uint64_t>((static_cast<long double>(sample_cost) * raw_size) / sample_bytes);
        uint32_t width = 1;
        while (width < 8 && max_row >= (1u << width)) ++width;
        uint64_t index_cost = width < 8 ? (static_cast<uint64_t>(rows.size()) * width + 7) / 8
                                        : static_cast<uint64_t>(rows.size());
        uint64_t dict_cost = 4 + cb.literals.size();
        for (uint32_t i = 0; i < base_phrases; ++i) dict_cost += 2 + cb.phrases[i].size();
        if (k) {
            dict_cost += 2;
            for (uint32_t i = 0; i < k; ++i) dict_cost += 2 + cb.phrases[base_phrases + i].size();
        }
        uint64_t total = scaled + index_cost + dict_cost;
        if (total < best_total) { best_total = total; best_extra = k; }
    }

    Codebook cb = base;
    cb.phrases.reserve(base_phrases + best_extra);
    for (uint32_t i = 0; i < best_extra; ++i) cb.phrases.push_back(extra[i].text);
    build_trie(cb);
    std::vector<uint8_t> dict, index, payload;
    wr16(dict, static_cast<uint16_t>(cb.literals.size()));
    wr16(dict, static_cast<uint16_t>(base_phrases));
    dict.insert(dict.end(), cb.literals.begin(), cb.literals.end());
    for (uint32_t i = 0; i < base_phrases; ++i) {
        const std::string& phrase = cb.phrases[i];
        wr16(dict, static_cast<uint16_t>(phrase.size()));
        dict.insert(dict.end(), phrase.begin(), phrase.end());
    }
    if (best_extra) {
        wr16(dict, static_cast<uint16_t>(best_extra));
        for (uint32_t i = 0; i < best_extra; ++i) {
            const std::string& phrase = cb.phrases[base_phrases + i];
            wr16(dict, static_cast<uint16_t>(phrase.size()));
            dict.insert(dict.end(), phrase.begin(), phrase.end());
        }
    }
    std::vector<uint32_t> row_lens;
    row_lens.reserve(rows.size());
    payload.reserve(raw_size);
    std::vector<uint32_t> dp;
    std::vector<int16_t> choice;
    std::vector<uint8_t> choice_len;
    for (const Row& r : rows) {
        uint32_t before = static_cast<uint32_t>(payload.size());
        hybrid_plan_row(raw, r, cb, base_count, dp, choice, choice_len);
        for (uint32_t i = 0; i < r.len;) {
            int16_t code = choice[i];
            uint8_t len = choice_len[i];
            if (code < 0) {
                uint8_t b = raw[r.off + i];
                payload.push_back(255);
                if (b < 128) payload.push_back(b);
                else { payload.push_back(255); payload.push_back(b); }
            } else if (static_cast<uint32_t>(code) < base_count) {
                payload.push_back(static_cast<uint8_t>(code));
            } else {
                uint32_t id = static_cast<uint32_t>(code) - base_count;
                if (id >= best_extra) return false;
                payload.push_back(255);
                payload.push_back(static_cast<uint8_t>(128 + id));
            }
            i += len;
        }
        row_lens.push_back(static_cast<uint32_t>(payload.size()) - before);
    }
    uint32_t max_len = 0;
    for (uint32_t n : row_lens) if (n > max_len) max_len = n;
    uint32_t width = 1;
    while (width < 8 && max_len >= (1u << width)) ++width;
    if (width < 8) {
        index.assign((static_cast<uint64_t>(row_lens.size()) * width + 7) / 8, 0);
        for (uint32_t i = 0; i < row_lens.size(); ++i) {
            uint64_t bit = static_cast<uint64_t>(i) * width;
            uint32_t byte = static_cast<uint32_t>(bit >> 3);
            uint32_t shift = static_cast<uint32_t>(bit & 7);
            uint16_t word = index[byte];
            if (shift + width > 8) word |= static_cast<uint16_t>(index[byte + 1]) << 8;
            word = static_cast<uint16_t>(word | (static_cast<uint16_t>(row_lens[i]) << shift));
            index[byte] = static_cast<uint8_t>(word);
            if (shift + width > 8) index[byte + 1] = static_cast<uint8_t>(word >> 8);
        }
    } else {
        width = 0;
        for (uint32_t n : row_lens) wrvar(index, n);
    }
    return finish_archive(TYPE_BPE, best_extra ? 2 : 0, raw_size,
                          static_cast<uint32_t>(rows.size()), width,
                          dict, index, payload, archive, capacity);
}

static uint64_t cost_sample_extended(const uint8_t* raw, const std::vector<Row>& rows,
                                     uint32_t step, const Codebook& cb,
                                     uint32_t& max_code_row) {
    uint64_t cost = 0;
    max_code_row = 0;
    for (uint32_t ri = 0; ri < rows.size(); ri += step) {
        const Row& r = rows[ri];
        uint32_t p = r.off, end = r.off + r.len, row_cost = 0;
        while (p < end) {
            int16_t node = 0, best_code = -1;
            uint32_t best_len = 0, q = p;
            while (q < end) {
                int16_t child = cb.trie[static_cast<size_t>(node)].next[raw[q]];
                if (child < 0) break;
                node = child; ++q;
                int16_t c = cb.trie[static_cast<size_t>(node)].code;
                if (c >= 0) { best_code = c; best_len = q - p; }
            }
            if (best_code >= 0) { row_cost += 3; p += best_len; }
            else { row_cost += raw[p] == 255 ? 3 : 1; ++p; }
        }
        cost += row_cost;
        if (row_cost > max_code_row) max_code_row = row_cost;
    }
    return cost;
}
static bool encode_extended(const uint8_t* raw, uint32_t raw_size,
                            const std::vector<Row>& rows,
                            uint8_t* archive, size_t capacity) {
    std::vector<Candidate> candidates;
    std::vector<uint64_t> sample_byte_freq;
    if (!train_bpe(raw, rows, candidates, sample_byte_freq, 1000)) return false;
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.text.size() != b.text.size()) return a.text.size() > b.text.size();
        return a.text < b.text;
    });
    std::vector<Candidate> unique;
    unique.reserve(candidates.size());
    for (auto& c : candidates) {
        if (c.text.size() < 4 || c.text.size() > 64) continue;
        bool found = false;
        for (auto& u : unique) {
            if (u.text == c.text) {
                if (c.score > u.score) u.score = c.score;
                found = true;
                break;
            }
        }
        if (!found) unique.emplace_back(std::move(c));
    }
    auto benefit = [](const Candidate& c) -> int64_t {
        uint64_t len = c.text.size();
        uint64_t freq = c.score / (len - 1);
        return static_cast<int64_t>(freq * (len - 3)) - static_cast<int64_t>(len);
    };
    std::sort(unique.begin(), unique.end(), [&](const Candidate& a, const Candidate& b) {
        int64_t ga = benefit(a), gb = benefit(b);
        if (ga != gb) return ga > gb;
        if (a.score != b.score) return a.score > b.score;
        if (a.text.size() != b.text.size()) return a.text.size() > b.text.size();
        return a.text < b.text;
    });
    const uint32_t maxp = static_cast<uint32_t>(std::min<size_t>(unique.size(), 400));
    uint32_t step = sample_step(raw_size);
    uint64_t sample_bytes = 0;
    for (uint32_t i = 0; i < rows.size(); i += step) sample_bytes += rows[i].len;
    if (!sample_bytes) sample_bytes = 1;
    std::vector<uint32_t> trials{0};
    for (uint32_t p = 32; p < maxp; p += 32) trials.push_back(p);
    if (trials.back() != maxp) trials.push_back(maxp);
    uint64_t best_total = UINT64_MAX;
    uint32_t bestp = 0;
    for (uint32_t p : trials) {
        Codebook cb;
        cb.phrases.reserve(p);
        for (uint32_t i = 0; i < p; ++i) cb.phrases.push_back(unique[i].text);
        build_trie(cb);
        uint32_t maxrow = 0;
        uint64_t scost = cost_sample_extended(raw, rows, step, cb, maxrow);
        uint64_t scaled = static_cast<uint64_t>((static_cast<long double>(scost) * raw_size) / sample_bytes);
        uint32_t width = 1;
        while (width < 8 && maxrow >= (1u << width)) ++width;
        uint64_t index_cost = width < 8 ? (static_cast<uint64_t>(rows.size()) * width + 7) / 8
                                        : static_cast<uint64_t>(rows.size());
        uint64_t dict_cost = 2;
        for (const auto& phrase : cb.phrases) dict_cost += 2 + phrase.size();
        uint64_t total = scaled + index_cost + dict_cost;
        if (total < best_total) { best_total = total; bestp = p; }
    }
    Codebook cb;
    cb.phrases.reserve(bestp);
    for (uint32_t i = 0; i < bestp; ++i) cb.phrases.push_back(unique[i].text);
    build_trie(cb);
    std::vector<uint8_t> dict, index, payload;
    wr16(dict, static_cast<uint16_t>(cb.phrases.size()));
    for (const auto& phrase : cb.phrases) {
        wr16(dict, static_cast<uint16_t>(phrase.size()));
        dict.insert(dict.end(), phrase.begin(), phrase.end());
    }
    std::vector<uint32_t> row_lens;
    row_lens.reserve(rows.size());
    payload.reserve(raw_size);
    for (const Row& r : rows) {
        uint32_t p = r.off, end = r.off + r.len;
        uint32_t before = static_cast<uint32_t>(payload.size());
        while (p < end) {
            int16_t node = 0, best_code = -1;
            uint32_t best_len = 0, q = p;
            while (q < end) {
                int16_t child = cb.trie[static_cast<size_t>(node)].next[raw[q]];
                if (child < 0) break;
                node = child; ++q;
                int16_t c = cb.trie[static_cast<size_t>(node)].code;
                if (c >= 0) { best_code = c; best_len = q - p; }
            }
            if (best_code >= 0) {
                payload.push_back(255);
                wr16(payload, static_cast<uint16_t>(best_code));
                p += best_len;
            } else {
                uint8_t b = raw[p++];
                if (b == 255) { payload.push_back(255); wr16(payload, 0xffff); }
                else payload.push_back(b);
            }
        }
        row_lens.push_back(static_cast<uint32_t>(payload.size()) - before);
    }
    uint32_t max_len = 0;
    for (uint32_t n : row_lens) if (n > max_len) max_len = n;
    uint32_t width = 1;
    while (width < 8 && max_len >= (1u << width)) ++width;
    if (width < 8) {
        index.assign((static_cast<uint64_t>(row_lens.size()) * width + 7) / 8, 0);
        for (uint32_t i = 0; i < row_lens.size(); ++i) {
            uint64_t bit = static_cast<uint64_t>(i) * width;
            uint32_t byte = static_cast<uint32_t>(bit >> 3);
            uint32_t shift = static_cast<uint32_t>(bit & 7);
            uint16_t word = index[byte];
            if (shift + width > 8) word |= static_cast<uint16_t>(index[byte + 1]) << 8;
            word = static_cast<uint16_t>(word | (static_cast<uint16_t>(row_lens[i]) << shift));
            index[byte] = static_cast<uint8_t>(word);
            if (shift + width > 8) index[byte + 1] = static_cast<uint8_t>(word >> 8);
        }
    } else {
        width = 0;
        for (uint32_t n : row_lens) wrvar(index, n);
    }
    return finish_archive(TYPE_EXT, 0, raw_size, static_cast<uint32_t>(rows.size()), width,
                          dict, index, payload, archive, capacity);
}


struct BpeView {
    const uint8_t* archive = nullptr;
    const uint8_t* dict = nullptr;
    const uint8_t* index = nullptr;
    const uint8_t* payload = nullptr;
    size_t size = 0;
    uint32_t raw_size = 0, rows = 0, payload_size = 0, index_size = 0, dict_size = 0, index_width = 0;
    uint8_t flags = 0;
    uint16_t nlit = 0, nphr = 0, next = 0;
    uint32_t base_count = 0, alphabet = 0, eos = 0, start = 0;
    std::vector<uint32_t> starts;
};
static bool local_read_var(const uint8_t* p, size_t end, size_t& at, uint32_t& value) {
    value = 0;
    uint32_t shift = 0;
    for (uint32_t i = 0; i < 5 && at < end; ++i) {
        uint8_t b = p[at++];
        value |= static_cast<uint32_t>(b & 127) << shift;
        if (!(b & 128)) return true;
        shift += 7;
    }
    return false;
}
static bool parse_bpe_view(const uint8_t* archive, size_t size, BpeView& v) {
    if (!archive || size < HEADER_SIZE || archive[0] != 'D' || archive[1] != 'B' ||
        archive[2] != 'T' || archive[3] != 'X' || archive[4] != TYPE_BPE || archive[5] != 1) return false;
    uint64_t need = static_cast<uint64_t>(HEADER_SIZE) + rd32(archive + 24) +
                    rd32(archive + 20) + rd32(archive + 16);
    if (need != size || (archive[6] & ~uint8_t{2})) return false;
    v.archive = archive; v.size = size; v.flags = archive[6];
    v.raw_size = rd32(archive + 8); v.rows = rd32(archive + 12);
    v.payload_size = rd32(archive + 16); v.index_size = rd32(archive + 20);
    v.dict_size = rd32(archive + 24); v.index_width = rd32(archive + 28);
    v.dict = archive + HEADER_SIZE;
    v.index = v.dict + v.dict_size;
    v.payload = v.index + v.index_size;
    if (v.dict_size < 4 || v.index_width > 7) return false;
    v.nlit = rd16(v.dict); v.nphr = rd16(v.dict + 2);
    if (static_cast<uint32_t>(v.nlit) + v.nphr > 255) return false;
    size_t at = 4 + v.nlit;
    if (at > v.dict_size) return false;
    for (uint32_t i = 0; i < v.nphr; ++i) {
        if (at + 2 > v.dict_size) return false;
        uint16_t len = rd16(v.dict + at); at += 2;
        if (len < 2 || len > 64 || at + len > v.dict_size) return false;
        at += len;
    }
    v.base_count = static_cast<uint32_t>(v.nlit) + v.nphr;
    if (v.flags & 2) {
        if (at + 2 > v.dict_size) return false;
        v.next = rd16(v.dict + at); at += 2;
        if (v.next > 127) return false;
        for (uint32_t i = 0; i < v.next; ++i) {
            if (at + 2 > v.dict_size) return false;
            uint16_t len = rd16(v.dict + at); at += 2;
            if (len < 4 || len > 64 || at + len > v.dict_size) return false;
            at += len;
        }
    }
    if (at != v.dict_size) return false;
    v.alphabet = v.base_count + 256 + v.next + 1;
    v.eos = v.alphabet - 1;
    v.start = v.alphabet;
    v.starts.resize(static_cast<size_t>(v.rows) + 1);
    size_t ip = 0; uint64_t off = 0; v.starts[0] = 0;
    if (v.index_width &&
        (static_cast<uint64_t>(v.rows) * v.index_width + 7) / 8 != v.index_size) return false;
    for (uint32_t i = 0; i < v.rows; ++i) {
        uint32_t len;
        if (v.index_width) len = getbits(v.index, i, v.index_width);
        else if (!local_read_var(v.index, v.index_size, ip, len)) return false;
        off += len;
        if (off > v.payload_size || off > UINT32_MAX) return false;
        v.starts[i + 1] = static_cast<uint32_t>(off);
    }
    return (v.index_width || ip == v.index_size) && off == v.payload_size;
}
struct EncHNode { uint64_t freq; int32_t left, right, symbol; };
struct EncBitWriter {
    std::vector<uint8_t>& out;
    uint8_t cur = 0;
    uint32_t used = 0;
    explicit EncBitWriter(std::vector<uint8_t>& v) : out(v) {}
    void put(uint32_t code, uint32_t bits) {
        for (uint32_t i = bits; i > 0; --i) {
            cur = static_cast<uint8_t>((cur << 1) | ((code >> (i - 1)) & 1));
            if (++used == 8) { out.push_back(cur); cur = 0; used = 0; }
        }
    }
    void flush() {
        if (used) { cur = static_cast<uint8_t>(cur << (8 - used)); out.push_back(cur); cur = 0; used = 0; }
    }
};
struct EncLsbWriter {
    std::vector<uint8_t>& out;
    uint64_t bits = 0;
    uint32_t used = 0;
    explicit EncLsbWriter(std::vector<uint8_t>& v) : out(v) {}
    void put(uint32_t value, uint32_t width) {
        if (width) { bits |= static_cast<uint64_t>(value) << used; used += width; }
        while (used >= 8) {
            out.push_back(static_cast<uint8_t>(bits));
            bits >>= 8; used -= 8;
        }
    }
    void flush() {
        if (used) { out.push_back(static_cast<uint8_t>(bits)); bits = 0; used = 0; }
    }
};
static bool token_symbol(const BpeView& v, uint32_t row_end, uint32_t& at, uint32_t& symbol) {
    if (at >= row_end) return false;
    uint8_t c = v.payload[at++];
    if (c != 255) symbol = c;
    else if (!(v.flags & 2)) {
        if (at >= row_end) return false;
        symbol = v.base_count + v.payload[at++];
    } else {
        if (at >= row_end) return false;
        uint8_t esc = v.payload[at++];
        if (esc < 128) symbol = v.base_count + esc;
        else if (esc == 255) {
            if (at >= row_end) return false;
            symbol = v.base_count + v.payload[at++];
        } else symbol = v.base_count + 256 + (esc - 128);
    }
    return symbol < v.eos;
}
static bool make_ctx_model(const std::vector<uint32_t>& freq, uint32_t alphabet,
                           std::vector<uint8_t>& lengths, std::vector<uint32_t>* codes,
                           uint16_t& nctx, uint32_t& nnz);
static void append_context_tables(const std::vector<uint32_t>& freq,
                                  const std::vector<uint8_t>& lengths,
                                  uint32_t alphabet, std::vector<uint8_t>& dict);
static bool encode_context_archive(const uint8_t* source, size_t source_size,
                                   uint8_t* archive, size_t capacity) {
    BpeView v;
    if (!parse_bpe_view(source, source_size, v)) return false;
    const uint32_t A = v.alphabet, contexts = A + 1;
    if (!A || A > 2048 || contexts > UINT16_MAX) return false;
    std::vector<uint32_t> freq(static_cast<size_t>(contexts) * A, 0);
    for (uint32_t r = 0; r < v.rows; ++r) {
        uint32_t at = v.starts[r], end = v.starts[r + 1], prev = v.start, sym;
        while (at < end) {
            if (!token_symbol(v, end, at, sym)) return false;
            uint32_t& f = freq[static_cast<size_t>(prev) * A + sym];
            if (f == UINT32_MAX) return false;
            ++f; prev = sym;
        }
        uint32_t& f = freq[static_cast<size_t>(prev) * A + v.eos];
        if (f == UINT32_MAX) return false;
        ++f;
    }
    std::vector<uint8_t> lengths;
    std::vector<uint32_t> codes;
    uint16_t nctx = 0;
    uint32_t nnz = 0;
    if (!make_ctx_model(freq, A, lengths, &codes, nctx, nnz)) return false;
    std::vector<uint8_t> dict(source + HEADER_SIZE,
                              source + HEADER_SIZE + v.dict_size);
    wr16(dict, static_cast<uint16_t>(A));
    wr16(dict, nctx);
    append_context_tables(freq, lengths, A, dict);
    std::vector<uint8_t> payload, index;
    payload.reserve(v.payload_size);
    std::vector<uint32_t> row_lens;
    row_lens.reserve(v.rows);
    for (uint32_t r = 0; r < v.rows; ++r) {
        uint32_t at = v.starts[r], end = v.starts[r + 1], prev = v.start, sym;
        uint32_t before = static_cast<uint32_t>(payload.size());
        EncBitWriter writer(payload);
        while (at < end) {
            if (!token_symbol(v, end, at, sym)) return false;
            size_t ci = static_cast<size_t>(prev) * A + sym;
            uint8_t bits = lengths[ci];
            if (!freq[ci]) return false;
            if (bits) writer.put(codes[ci], bits);
            prev = sym;
        }
        size_t ei = static_cast<size_t>(prev) * A + v.eos;
        if (!freq[ei]) return false;
        if (lengths[ei]) writer.put(codes[ei], lengths[ei]);
        writer.flush();
        row_lens.push_back(static_cast<uint32_t>(payload.size()) - before);
    }
    uint32_t max_len = 0;
    for (uint32_t n : row_lens) if (n > max_len) max_len = n;
    uint32_t width = 1;
    while (width < 8 && max_len >= (1u << width)) ++width;
    if (width < 8) {
        index.assign((static_cast<uint64_t>(row_lens.size()) * width + 7) / 8, 0);
        for (uint32_t i = 0; i < row_lens.size(); ++i) {
            uint64_t bit = static_cast<uint64_t>(i) * width;
            uint32_t byte = static_cast<uint32_t>(bit >> 3);
            uint32_t shift = static_cast<uint32_t>(bit & 7);
            uint16_t word = index[byte];
            if (shift + width > 8) word |= static_cast<uint16_t>(index[byte + 1]) << 8;
            word = static_cast<uint16_t>(word | (static_cast<uint16_t>(row_lens[i]) << shift));
            index[byte] = static_cast<uint8_t>(word);
            if (shift + width > 8) index[byte + 1] = static_cast<uint8_t>(word >> 8);
        }
    } else {
        width = 0;
        for (uint32_t n : row_lens) wrvar(index, n);
    }
    return finish_archive(TYPE_CTX, (v.flags & 2) ? 1 : 0, v.raw_size, v.rows, width,
                          dict, index, payload, archive, capacity);
}

static uint16_t direct_next_symbol(const uint8_t* raw, uint32_t end, uint32_t& p,
                                   const Codebook& cb) {
    uint32_t q = p, best_len = 0;
    int16_t node = 0, best_code = -1;
    while (q < end) {
        int16_t child = cb.trie[static_cast<size_t>(node)].next[raw[q]];
        if (child < 0) break;
        node = child; ++q;
        int16_t code = cb.trie[static_cast<size_t>(node)].code;
        if (code >= 0) { best_code = code; best_len = q - p; }
    }
    if (best_code >= 0) {
        p += best_len;
        return static_cast<uint16_t>(best_code);
    }
    return static_cast<uint16_t>(cb.phrases.size() + raw[p++]);
}
static bool direct_frequencies(const uint8_t* raw, const std::vector<Row>& rows,
                               const Codebook& cb, uint32_t alphabet,
                               std::vector<uint32_t>& freq) {
    const uint32_t contexts = alphabet + 1;
    freq.assign(static_cast<size_t>(contexts) * alphabet, 0);
    const uint32_t eos = alphabet - 1;
    for (const Row& r : rows) {
        uint32_t p = r.off, end = r.off + r.len;
        uint32_t prev = alphabet;
        while (p < end) {
            uint32_t sym = direct_next_symbol(raw, end, p, cb);
            uint32_t& f = freq[static_cast<size_t>(prev) * alphabet + sym];
            if (f == UINT32_MAX) return false;
            ++f; prev = sym;
        }
        uint32_t& f = freq[static_cast<size_t>(prev) * alphabet + eos];
        if (f == UINT32_MAX) return false;
        ++f;
    }
    return true;
}
static bool make_ctx_model(const std::vector<uint32_t>& freq, uint32_t alphabet,
                           std::vector<uint8_t>& lengths, std::vector<uint32_t>* codes,
                           uint16_t& nctx, uint32_t& nnz) {
    const uint32_t contexts = alphabet + 1;
    lengths.assign(static_cast<size_t>(contexts) * alphabet, 0);
    if (codes) codes->assign(static_cast<size_t>(contexts) * alphabet, 0);
    nctx = 0; nnz = 0;
    for (uint32_t ctx = 0; ctx < contexts; ++ctx) {
        const size_t base = static_cast<size_t>(ctx) * alphabet;
        std::vector<EncHNode> nodes;
        nodes.reserve(static_cast<size_t>(alphabet) * 2);
        using Entry = std::pair<uint64_t, int32_t>;
        std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> heap;
        for (uint32_t sym = 0; sym < alphabet; ++sym) {
            uint32_t f = freq[base + sym];
            if (!f) continue;
            ++nnz;
            nodes.push_back({f, -1, -1, static_cast<int32_t>(sym)});
            heap.push({f, static_cast<int32_t>(nodes.size() - 1)});
        }
        if (heap.empty()) continue;
        ++nctx;
        if (heap.size() == 1) {
            lengths[base + static_cast<uint32_t>(nodes[heap.top().second].symbol)] = 0;
            continue;
        }
        while (heap.size() > 1) {
            Entry a = heap.top(); heap.pop();
            Entry b = heap.top(); heap.pop();
            nodes.push_back({a.first + b.first, a.second, b.second, -1});
            heap.push({a.first + b.first, static_cast<int32_t>(nodes.size() - 1)});
        }
        std::vector<std::pair<int32_t, uint32_t>> stack{{heap.top().second, 0}};
        while (!stack.empty()) {
            auto [id, depth] = stack.back(); stack.pop_back();
            const EncHNode& node = nodes[static_cast<size_t>(id)];
            if (node.symbol >= 0) {
                if (!depth || depth > 31) return false;
                lengths[base + static_cast<uint32_t>(node.symbol)] = static_cast<uint8_t>(depth);
            } else {
                stack.push_back({node.left, depth + 1});
                stack.push_back({node.right, depth + 1});
            }
        }
        uint32_t count[32] = {};
        uint32_t max_len = 0;
        for (uint32_t sym = 0; sym < alphabet; ++sym) {
            uint32_t len = lengths[base + sym];
            if (len) { ++count[len]; if (len > max_len) max_len = len; }
        }
        uint64_t code = 0;
        uint64_t next_code[32] = {};
        for (uint32_t len = 1; len <= max_len; ++len) {
            code = (code + count[len - 1]) << 1;
            if (code + count[len] > (uint64_t{1} << len)) return false;
            next_code[len] = code;
        }
        if (codes) {
            for (uint32_t len = 1; len <= max_len; ++len)
                for (uint32_t sym = 0; sym < alphabet; ++sym)
                    if (lengths[base + sym] == len)
                        (*codes)[base + sym] = static_cast<uint32_t>(next_code[len]++);
        }
    }
    return true;
}
static uint64_t context_table_bytes(const std::vector<uint32_t>& freq, uint32_t alphabet) {
    uint64_t total = 0;
    for (uint32_t ctx = 0; ctx <= alphabet; ++ctx) {
        size_t base = static_cast<size_t>(ctx) * alphabet;
        uint32_t nsym = 0, prev = 0, max_gap = 0;
        for (uint32_t sym = 0; sym < alphabet; ++sym) if (freq[base + sym]) {
            uint32_t gap = sym - prev;
            if (gap > max_gap) max_gap = gap;
            prev = sym; ++nsym;
        }
        if (!nsym) continue;
        uint32_t gap_width = 0;
        while ((uint32_t{1} << gap_width) <= max_gap) ++gap_width;
        uint64_t packed = 1 + (static_cast<uint64_t>(nsym) * (gap_width + 5) + 7) / 8;
        uint64_t plain = static_cast<uint64_t>(nsym) * 2;
        total += packed < plain ? packed : plain;
    }
    return total;
}
static void append_context_tables(const std::vector<uint32_t>& freq,
                                  const std::vector<uint8_t>& lengths,
                                  uint32_t alphabet, std::vector<uint8_t>& dict) {
    for (uint32_t ctx = 0; ctx <= alphabet; ++ctx) {
        size_t base = static_cast<size_t>(ctx) * alphabet;
        uint16_t nsym = 0; uint32_t prev = 0, max_gap = 0;
        for (uint32_t sym = 0; sym < alphabet; ++sym) if (freq[base + sym]) {
            uint32_t gap = sym - prev;
            if (gap > max_gap) max_gap = gap;
            prev = sym; ++nsym;
        }
        if (!nsym) continue;
        uint32_t gap_width = 0;
        while ((uint32_t{1} << gap_width) <= max_gap) ++gap_width;
        uint64_t packed_size = 1 + (static_cast<uint64_t>(nsym) * (gap_width + 5) + 7) / 8;
        uint64_t plain_size = static_cast<uint64_t>(nsym) * 2;
        bool compact = packed_size < plain_size;
        wr16(dict, static_cast<uint16_t>(ctx));
        wr16(dict, static_cast<uint16_t>(nsym | (compact ? 0x8000u : 0u)));
        if (compact) {
            dict.push_back(static_cast<uint8_t>(gap_width));
            EncLsbWriter writer(dict);
            prev = 0;
            for (uint32_t sym = 0; sym < alphabet; ++sym) if (freq[base + sym]) {
                writer.put(sym - prev, gap_width);
                writer.put(lengths[base + sym], 5);
                prev = sym;
            }
            writer.flush();
        } else {
            for (uint32_t sym = 0; sym < alphabet; ++sym) if (freq[base + sym])
                wr16(dict, static_cast<uint16_t>((sym << 5) | lengths[base + sym]));
        }
    }
}
static uint32_t direct_var_size(uint32_t x) {
    uint32_t n = 1;
    while (x >= 128) { x >>= 7; ++n; }
    return n;
}
static bool measure_direct_size(const uint8_t* raw, const std::vector<Row>& rows,
                                const std::vector<Candidate>& phrases, uint32_t count,
                                uint64_t& total) {
    Codebook cb;
    cb.phrases.reserve(count);
    for (uint32_t i = 0; i < count; ++i) cb.phrases.push_back(phrases[i].text);
    build_trie(cb);
    const uint32_t alphabet = count + 257, eos = alphabet - 1;
    if (alphabet > 2048) return false;
    std::vector<uint32_t> freq;
    if (!direct_frequencies(raw, rows, cb, alphabet, freq)) return false;
    std::vector<uint8_t> lengths;
    uint16_t nctx = 0; uint32_t nnz = 0;
    if (!make_ctx_model(freq, alphabet, lengths, nullptr, nctx, nnz)) return false;
    uint64_t payload_size = 0;
    uint32_t max_row = 0;
    uint64_t var_size = 0;
    for (const Row& r : rows) {
        uint32_t p = r.off, end = r.off + r.len, prev = alphabet;
        uint64_t bits = 0;
        while (p < end) {
            uint32_t sym = direct_next_symbol(raw, end, p, cb);
            size_t ci = static_cast<size_t>(prev) * alphabet + sym;
            uint8_t len = lengths[ci];
            if (!freq[ci]) return false;
            bits += len; prev = sym;
        }
        size_t ei = static_cast<size_t>(prev) * alphabet + eos;
        uint8_t len = lengths[ei];
        if (!freq[ei]) return false;
        bits += len;
        uint64_t bytes64 = (bits + 7) >> 3;
        if (bytes64 > UINT32_MAX) return false;
        uint32_t bytes = static_cast<uint32_t>(bytes64);
        payload_size += bytes;
        if (bytes > max_row) max_row = bytes;
        var_size += direct_var_size(bytes);
    }
    uint32_t width = 1;
    while (width < 8 && max_row >= (1u << width)) ++width;
    uint64_t index_size = width < 8 ? (static_cast<uint64_t>(rows.size()) * width + 7) / 8
                                    : var_size;
    uint64_t dict_size = 2;
    for (uint32_t i = 0; i < count; ++i) dict_size += 2 + phrases[i].text.size();
    dict_size += 4 + static_cast<uint64_t>(nctx) * 4 + context_table_bytes(freq, alphabet);
    total = HEADER_SIZE + dict_size + index_size + payload_size;
    return total <= UINT32_MAX * uint64_t{3};
}
static bool emit_direct_archive(const uint8_t* raw, uint32_t raw_size,
                                const std::vector<Row>& rows,
                                const std::vector<Candidate>& phrases, uint32_t count,
                                uint8_t* archive, size_t capacity) {
    Codebook cb;
    cb.phrases.reserve(count);
    for (uint32_t i = 0; i < count; ++i) cb.phrases.push_back(phrases[i].text);
    build_trie(cb);
    const uint32_t alphabet = count + 257, eos = alphabet - 1;
    std::vector<uint32_t> freq;
    if (!direct_frequencies(raw, rows, cb, alphabet, freq)) return false;
    std::vector<uint8_t> lengths;
    std::vector<uint32_t> codes;
    uint16_t nctx = 0; uint32_t nnz = 0;
    if (!make_ctx_model(freq, alphabet, lengths, &codes, nctx, nnz)) return false;
    std::vector<uint8_t> dict;
    wr16(dict, static_cast<uint16_t>(count));
    for (uint32_t i = 0; i < count; ++i) {
        wr16(dict, static_cast<uint16_t>(phrases[i].text.size()));
        dict.insert(dict.end(), phrases[i].text.begin(), phrases[i].text.end());
    }
    wr16(dict, static_cast<uint16_t>(alphabet));
    wr16(dict, nctx);
    append_context_tables(freq, lengths, alphabet, dict);
    std::vector<uint8_t> payload, index;
    std::vector<uint32_t> row_lens;
    row_lens.reserve(rows.size());
    for (const Row& r : rows) {
        uint32_t p = r.off, end = r.off + r.len, prev = alphabet;
        uint32_t before = static_cast<uint32_t>(payload.size());
        EncBitWriter writer(payload);
        while (p < end) {
            uint32_t sym = direct_next_symbol(raw, end, p, cb);
            size_t ci = static_cast<size_t>(prev) * alphabet + sym;
            uint8_t len = lengths[ci];
            if (!freq[ci]) return false;
            if (len) writer.put(codes[ci], len);
            prev = sym;
        }
        size_t ei = static_cast<size_t>(prev) * alphabet + eos;
        if (!freq[ei]) return false;
        if (lengths[ei]) writer.put(codes[ei], lengths[ei]);
        writer.flush();
        row_lens.push_back(static_cast<uint32_t>(payload.size()) - before);
    }
    uint32_t max_len = 0;
    for (uint32_t n : row_lens) if (n > max_len) max_len = n;
    uint32_t width = 1;
    while (width < 8 && max_len >= (1u << width)) ++width;
    if (width < 8) {
        index.assign((static_cast<uint64_t>(row_lens.size()) * width + 7) / 8, 0);
        for (uint32_t i = 0; i < row_lens.size(); ++i) {
            uint64_t bit = static_cast<uint64_t>(i) * width;
            uint32_t byte = static_cast<uint32_t>(bit >> 3);
            uint32_t shift = static_cast<uint32_t>(bit & 7);
            uint16_t word = index[byte];
            if (shift + width > 8) word |= static_cast<uint16_t>(index[byte + 1]) << 8;
            word = static_cast<uint16_t>(word | (static_cast<uint16_t>(row_lens[i]) << shift));
            index[byte] = static_cast<uint8_t>(word);
            if (shift + width > 8) index[byte + 1] = static_cast<uint8_t>(word >> 8);
        }
    } else {
        width = 0;
        for (uint32_t n : row_lens) wrvar(index, n);
    }
    return finish_archive(TYPE_CTX_DIRECT, 0, raw_size, static_cast<uint32_t>(rows.size()),
                          width, dict, index, payload, archive, capacity);
}
static bool collect_direct_vocab(const uint8_t* raw, const std::vector<Row>& rows,
                                 std::vector<Candidate>& unique) {
    std::vector<Candidate> candidates;
    std::vector<uint64_t> sample_byte_freq;
    if (!train_bpe(raw, rows, candidates, sample_byte_freq, 1000)) return false;
    std::unordered_map<std::string, uint32_t> words;
    for (const Row& r : rows) {
        uint32_t p = r.off, end = r.off + r.len;
        while (p < end) {
            uint8_t c = raw[p];
            bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_';
            if (!word) { ++p; continue; }
            uint32_t begin = p++;
            while (p < end) {
                c = raw[p];
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_')) break;
                ++p;
            }
            uint32_t len = p - begin;
            if (len >= 2 && len <= 64)
                ++words[std::string(reinterpret_cast<const char*>(raw + begin), len)];
        }
    }
    std::vector<Candidate> word_candidates;
    word_candidates.reserve(words.size());
    for (const auto& kv : words) {
        if (kv.second < 2) continue;
        Candidate c; c.text = kv.first;
        c.score = static_cast<uint64_t>(kv.second) * (kv.first.size() - 1);
        word_candidates.emplace_back(std::move(c));
    }
    std::sort(word_candidates.begin(), word_candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.text.size() != b.text.size()) return a.text.size() > b.text.size();
        return a.text < b.text;
    });
    if (word_candidates.size() > 2048) word_candidates.resize(2048);
    for (auto& c : word_candidates) candidates.emplace_back(std::move(c));
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.text.size() != b.text.size()) return a.text.size() > b.text.size();
        return a.text < b.text;
    });
    unique.clear();
    unique.reserve(candidates.size());
    for (auto& c : candidates) {
        if (c.text.size() < 2 || c.text.size() > 64) continue;
        bool found = false;
        for (auto& u : unique) {
            if (u.text == c.text) {
                if (c.score > u.score) u.score = c.score;
                found = true; break;
            }
        }
        if (!found) unique.emplace_back(std::move(c));
    }
    return true;
}
static uint32_t fixed_token_width(uint32_t phrase_count) {
    uint32_t symbols = phrase_count + 256, width = 0;
    while ((uint32_t{1} << width) < symbols) ++width;
    return width;
}
static bool measure_fixedtok_size(const uint8_t* raw, const std::vector<Row>& rows,
                                  const std::vector<Candidate>& phrases, uint32_t count,
                                  uint64_t& total) {
    Codebook cb;
    cb.phrases.reserve(count);
    for (uint32_t i = 0; i < count; ++i) cb.phrases.push_back(phrases[i].text);
    build_trie(cb);
    uint64_t token_count = 0, var_size = 0;
    uint32_t max_row = 0;
    for (const Row& r : rows) {
        uint32_t p = r.off, end = r.off + r.len, n = 0;
        while (p < end) { (void)direct_next_symbol(raw, end, p, cb); ++n; }
        token_count += n;
        if (n > max_row) max_row = n;
        var_size += direct_var_size(n);
    }
    uint32_t idx_width = 1;
    while (idx_width < 8 && max_row >= (1u << idx_width)) ++idx_width;
    uint64_t index_size = idx_width < 8
        ? (static_cast<uint64_t>(rows.size()) * idx_width + 7) / 8 : var_size;
    uint64_t dict_size = 2;
    for (uint32_t i = 0; i < count; ++i) dict_size += 2 + phrases[i].text.size();
    uint64_t payload_size = (token_count * fixed_token_width(count) + 7) / 8 + 4;
    total = HEADER_SIZE + dict_size + index_size + payload_size;
    return total <= UINT32_MAX * uint64_t{3};
}
static bool emit_fixedtok_archive(const uint8_t* raw, uint32_t raw_size,
                                  const std::vector<Row>& rows,
                                  const std::vector<Candidate>& phrases, uint32_t count,
                                  uint8_t* archive, size_t capacity) {
    Codebook cb;
    cb.phrases.reserve(count);
    for (uint32_t i = 0; i < count; ++i) cb.phrases.push_back(phrases[i].text);
    build_trie(cb);
    const uint32_t tok_width = fixed_token_width(count);
    std::vector<uint8_t> dict, index;
    wr16(dict, static_cast<uint16_t>(count));
    for (uint32_t i = 0; i < count; ++i) {
        wr16(dict, static_cast<uint16_t>(phrases[i].text.size()));
        dict.insert(dict.end(), phrases[i].text.begin(), phrases[i].text.end());
    }
    std::vector<uint32_t> row_lens;
    row_lens.reserve(rows.size());
    uint64_t token_count = 0;
    for (const Row& r : rows) {
        uint32_t p = r.off, end = r.off + r.len, n = 0;
        while (p < end) { (void)direct_next_symbol(raw, end, p, cb); ++n; }
        row_lens.push_back(n); token_count += n;
    }
    uint64_t packed_size64 = (token_count * tok_width + 7) / 8;
    if (packed_size64 + 4 > UINT32_MAX || packed_size64 + 4 > capacity) return false;
    std::vector<uint8_t> payload(static_cast<size_t>(packed_size64) + 4, 0);
    uint32_t token = 0;
    for (const Row& r : rows) {
        uint32_t p = r.off, end = r.off + r.len;
        while (p < end) {
            uint32_t sym = direct_next_symbol(raw, end, p, cb);
            uint64_t bit = static_cast<uint64_t>(token++) * tok_width;
            uint32_t byte = static_cast<uint32_t>(bit >> 3);
            uint32_t shift = static_cast<uint32_t>(bit & 7);
            uint32_t z = sym << shift;
            payload[byte] |= static_cast<uint8_t>(z);
            payload[byte + 1] |= static_cast<uint8_t>(z >> 8);
            payload[byte + 2] |= static_cast<uint8_t>(z >> 16);
        }
    }
    uint32_t max_len = 0;
    for (uint32_t n : row_lens) if (n > max_len) max_len = n;
    uint32_t width = 1;
    while (width < 8 && max_len >= (1u << width)) ++width;
    if (width < 8) {
        index.assign((static_cast<uint64_t>(row_lens.size()) * width + 7) / 8, 0);
        for (uint32_t i = 0; i < row_lens.size(); ++i) {
            uint64_t bit = static_cast<uint64_t>(i) * width;
            uint32_t byte = static_cast<uint32_t>(bit >> 3);
            uint32_t shift = static_cast<uint32_t>(bit & 7);
            uint16_t word = index[byte];
            if (shift + width > 8) word |= static_cast<uint16_t>(index[byte + 1]) << 8;
            word = static_cast<uint16_t>(word | (static_cast<uint16_t>(row_lens[i]) << shift));
            index[byte] = static_cast<uint8_t>(word);
            if (shift + width > 8) index[byte + 1] = static_cast<uint8_t>(word >> 8);
        }
    } else {
        width = 0;
        for (uint32_t n : row_lens) wrvar(index, n);
    }
    return finish_archive(TYPE_FIXEDTOK, 0, raw_size, static_cast<uint32_t>(rows.size()),
                          width, dict, index, payload, archive, capacity);
}
static bool encode_fixedtok(const uint8_t* raw, uint32_t raw_size,
                            const std::vector<Row>& rows,
                            uint8_t* archive, size_t capacity) {
    std::vector<Candidate> unique;
    if (!collect_direct_vocab(raw, rows, unique)) return false;
    uint32_t max_count = 0, trie_bytes = 1;
    for (const Candidate& c : unique) {
        if (trie_bytes + c.text.size() > 30000) break;
        trie_bytes += static_cast<uint32_t>(c.text.size());
        ++max_count;
    }
    max_count = std::min<uint32_t>(max_count, 1791);
    std::vector<uint32_t> trials{0};
    for (uint32_t n = 64; n < max_count; n += 64) trials.push_back(n);
    if (trials.back() != max_count) trials.push_back(max_count);
    std::vector<uint8_t> tested(static_cast<size_t>(max_count) + 1, 0);
    uint64_t best_size = UINT64_MAX;
    uint32_t best_count = 0;
    for (uint32_t n : trials) {
        uint64_t z;
        if (measure_fixedtok_size(raw, rows, unique, n, z)) {
            tested[n] = 1;
            if (z < best_size) { best_size = z; best_count = n; }
        }
    }
    uint32_t lo = best_count > 64 ? best_count - 64 : 0;
    uint32_t hi = std::min<uint32_t>(max_count, best_count + 64);
    for (uint32_t n = lo; n <= hi; n += 16) {
        if (tested[n]) continue;
        uint64_t z;
        if (measure_fixedtok_size(raw, rows, unique, n, z)) {
            if (z < best_size) { best_size = z; best_count = n; }
        }
    }
    return emit_fixedtok_archive(raw, raw_size, rows, unique, best_count, archive, capacity);
}

static bool encode_context_direct(const uint8_t* raw, uint32_t raw_size,
                                  const std::vector<Row>& rows,
                                  uint8_t* archive, size_t capacity) {
    std::vector<Candidate> unique;
    if (!collect_direct_vocab(raw, rows, unique)) return false;
    uint32_t max_count = 0, trie_bytes = 1;
    for (const Candidate& c : unique) {
        if (trie_bytes + c.text.size() > 30000) break;
        trie_bytes += static_cast<uint32_t>(c.text.size());
        ++max_count;
    }
    max_count = std::min<uint32_t>(max_count, 1791);
    std::vector<uint32_t> trials{0};
    for (uint32_t n = 64; n < max_count; n += 64) trials.push_back(n);
    if (trials.back() != max_count) trials.push_back(max_count);
    std::vector<uint8_t> tested(static_cast<size_t>(max_count) + 1, 0);
    uint64_t best_size = UINT64_MAX;
    uint32_t best_count = 0;
    for (uint32_t n : trials) {
        uint64_t z;
        if (measure_direct_size(raw, rows, unique, n, z)) {
            tested[n] = 1;
            if (z < best_size) { best_size = z; best_count = n; }
        }
    }
    uint32_t lo = best_count > 64 ? best_count - 64 : 0;
    uint32_t hi = std::min<uint32_t>(max_count, best_count + 64);
    for (uint32_t n = lo; n <= hi; n += 16) {
        if (tested[n]) continue;
        uint64_t z;
        if (measure_direct_size(raw, rows, unique, n, z)) {
            if (z < best_size) { best_size = z; best_count = n; }
        }
    }
    return emit_direct_archive(raw, raw_size, rows, unique, best_count, archive, capacity);
}

static bool encode_cname(const uint8_t* raw, uint32_t raw_size, const std::vector<Row>& rows,
                         uint8_t* archive, size_t capacity) {
    static const uint8_t prefix[] = {'C','u','s','t','o','m','e','r','#'};
    if (rows.size() != 100000) return false;
    uint32_t maxv = 0;
    for (const Row& r : rows) {
        if (r.len != 19 || std::memcmp(raw + r.off, prefix, 9) != 0 || raw[r.off + 18] != '\n') return false;
        uint32_t v = 0;
        for (uint32_t j = 9; j < 18; ++j) {
            uint8_t c = raw[r.off + j];
            if (c < '0' || c > '9') return false;
            v = v * 10 + (c - '0');
        }
        if (v > maxv) maxv = v;
    }
    uint32_t width = maxv < (1u << 18) ? 18 : (maxv < 0x1000000u ? 3 : 4);
    std::vector<uint8_t> dict(prefix, prefix + 9), payload;
    if (width == 18) payload.assign((rows.size() * 18 + 7) / 8, 0);
    else payload.reserve(rows.size() * width);
    for (uint32_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        uint32_t v = 0;
        for (uint32_t j = 9; j < 18; ++j) v = v * 10 + (raw[r.off + j] - '0');
        if (width == 18) {
            uint64_t bit = static_cast<uint64_t>(i) * 18;
            uint32_t byte = static_cast<uint32_t>(bit >> 3), shift = static_cast<uint32_t>(bit & 7);
            uint64_t z = static_cast<uint64_t>(v) << shift;
            payload[byte] |= static_cast<uint8_t>(z);
            payload[byte + 1] |= static_cast<uint8_t>(z >> 8);
            if (byte + 2 < payload.size()) payload[byte + 2] |= static_cast<uint8_t>(z >> 16);
        } else {
            payload.push_back(static_cast<uint8_t>(v));
            payload.push_back(static_cast<uint8_t>(v >> 8));
            payload.push_back(static_cast<uint8_t>(v >> 16));
            if (width == 4) payload.push_back(static_cast<uint8_t>(v >> 24));
        }
    }
    std::vector<uint8_t> empty;
    return finish_archive(TYPE_CNAME, 0, raw_size, static_cast<uint32_t>(rows.size()), width,
                          dict, empty, payload, archive, capacity);
}
static bool encode_dna(const uint8_t* raw, uint32_t raw_size, const std::vector<Row>& rows,
                       uint8_t* archive, size_t capacity) {
    if (rows.size() != 100000) return false;
    std::vector<uint8_t> payload((rows.size() * 18 + 7) / 8, 0);
    for (uint32_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        if (r.len != 10 || raw[r.off + 9] != '\n') return false;
        uint32_t bits = 0;
        for (uint32_t j = 0; j < 9; ++j) {
            uint32_t v;
            switch (raw[r.off + j]) {
                case 'a': v = 0; break;
                case 'c': v = 1; break;
                case 'g': v = 2; break;
                case 't': v = 3; break;
                default: return false;
            }
            bits |= v << (2 * j);
        }
        uint64_t bit = static_cast<uint64_t>(i) * 18;
        uint32_t byte = static_cast<uint32_t>(bit >> 3), shift = static_cast<uint32_t>(bit & 7);
        uint64_t z = static_cast<uint64_t>(bits) << shift;
        payload[byte] |= static_cast<uint8_t>(z);
        payload[byte + 1] |= static_cast<uint8_t>(z >> 8);
        if (byte + 2 < payload.size()) payload[byte + 2] |= static_cast<uint8_t>(z >> 16);
    }
    std::vector<uint8_t> empty;
    return finish_archive(TYPE_DNA, 0, raw_size, static_cast<uint32_t>(rows.size()), 18,
                          empty, empty, payload, archive, capacity);
}
static bool encode_uuid(const uint8_t* raw, uint32_t raw_size, const std::vector<Row>& rows,
                        uint8_t* archive, size_t capacity) {
    static const uint8_t varpos[22] = {2,3,4,5,6,7,19,20,21,22,24,25,26,27,28,29,30,31,32,33,34,35};
    if (rows.size() != 100000) return false;
    for (const Row& r : rows) if (r.len != 37 || raw[r.off + 36] != '\n') return false;
    std::vector<uint8_t> dict(raw, raw + 37), payload;
    for (uint8_t p : varpos) dict[p] = 0;
    bool upper = false, lower = false;
    for (const Row& r : rows) {
        for (uint8_t p : varpos) {
            int v = hexval(raw[r.off + p]);
            if (v < 0) return false;
            if (raw[r.off + p] >= 'A' && raw[r.off + p] <= 'F') upper = true;
            if (raw[r.off + p] >= 'a' && raw[r.off + p] <= 'f') lower = true;
        }
        for (uint32_t p = 0; p < 37; ++p) {
            bool var = false;
            for (uint32_t k = 0; k < 22; ++k) if (varpos[k] == p) { var = true; break; }
            if (!var && raw[r.off + p] != dict[p]) return false;
        }
    }
    if (upper && lower) return false;
    uint8_t flags = upper ? 1 : 0;
    payload.reserve(rows.size() * 11);
    for (const Row& r : rows) {
        uint8_t packed[11] = {};
        for (uint32_t k = 0; k < 22; ++k) {
            int v = hexval(raw[r.off + varpos[k]]);
            if ((k & 1) == 0) packed[k >> 1] = static_cast<uint8_t>(v << 4);
            else packed[k >> 1] |= static_cast<uint8_t>(v);
        }
        payload.insert(payload.end(), packed, packed + 11);
    }
    std::vector<uint8_t> empty;
    return finish_archive(TYPE_UUID, flags, raw_size, static_cast<uint32_t>(rows.size()), 11,
                          dict, empty, payload, archive, capacity);
}
static bool encode_hex(const uint8_t* raw, uint32_t raw_size, const std::vector<Row>& rows,
                       uint8_t* archive, size_t capacity) {
    if (rows.size() != 100000) return false;
    bool upper = false, lower = false;
    for (const Row& r : rows) {
        if (r.len < 5 || r.len > 9 || raw[r.off + r.len - 1] != '\n') return false;
        for (uint32_t j = 0; j + 1 < r.len; ++j) {
            uint8_t c = raw[r.off + j];
            if (hexval(c) < 0) return false;
            if (c >= 'A' && c <= 'F') upper = true;
            if (c >= 'a' && c <= 'f') lower = true;
        }
    }
    if (upper && lower) return false;
    std::vector<uint8_t> index((rows.size() * 3 + 7) / 8, 0), payload;
    payload.reserve(rows.size() * 4);
    for (uint32_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        uint32_t digits = r.len - 1;
        put3(index, i, static_cast<uint8_t>(digits - 4));
        for (uint32_t j = 0; j < digits; j += 2) {
            uint8_t hi = static_cast<uint8_t>(hexval(raw[r.off + j]));
            uint8_t lo = (j + 1 < digits) ? static_cast<uint8_t>(hexval(raw[r.off + j + 1])) : 0;
            payload.push_back(static_cast<uint8_t>((hi << 4) | lo));
        }
    }
    std::vector<uint8_t> empty;
    return finish_archive(TYPE_HEX, upper ? 1 : 0, raw_size, static_cast<uint32_t>(rows.size()), 0,
                          empty, index, payload, archive, capacity);
}
}

extern "C" int64_t lab_encode(const uint8_t* raw, size_t size, uint8_t* archive, size_t capacity) {
    if ((!raw && size) || !archive || size > UINT32_MAX) return -1;
    try {
        std::vector<Row> rows;
        if (!make_rows(raw, size, rows)) return -1;
        if (encode_cname(raw, static_cast<uint32_t>(size), rows, archive, capacity)) return static_cast<int64_t>(HEADER_SIZE + rd32(archive + 24) + rd32(archive + 20) + rd32(archive + 16));
        if (encode_dna(raw, static_cast<uint32_t>(size), rows, archive, capacity)) return static_cast<int64_t>(HEADER_SIZE + rd32(archive + 24) + rd32(archive + 20) + rd32(archive + 16));
        if (encode_uuid(raw, static_cast<uint32_t>(size), rows, archive, capacity)) return static_cast<int64_t>(HEADER_SIZE + rd32(archive + 24) + rd32(archive + 20) + rd32(archive + 16));
        if (encode_hex(raw, static_cast<uint32_t>(size), rows, archive, capacity)) return static_cast<int64_t>(HEADER_SIZE + rd32(archive + 24) + rd32(archive + 20) + rd32(archive + 16));
        if (!encode_generic(raw, static_cast<uint32_t>(size), rows, archive, capacity)) return -1;
        size_t chosen_size = HEADER_SIZE + rd32(archive + 24) + rd32(archive + 20) + rd32(archive + 16);
        const size_t bpe_size = chosen_size;
        std::vector<uint8_t> bpe_archive(archive, archive + bpe_size);
        try {
            std::vector<uint8_t> extended(capacity);
            if (encode_hybrid(raw, static_cast<uint32_t>(size), rows, extended.data(), extended.size())) {
                size_t extended_size = HEADER_SIZE + rd32(extended.data() + 24) +
                                       rd32(extended.data() + 20) + rd32(extended.data() + 16);
                if (extended_size < chosen_size) {
                    std::memcpy(archive, extended.data(), extended_size);
                    chosen_size = extended_size;
                }
            }
        } catch (...) {}
        try {
            std::vector<uint8_t> conditional(capacity);
            if (encode_context_archive(archive, chosen_size, conditional.data(), conditional.size())) {
                size_t conditional_size = HEADER_SIZE + rd32(conditional.data() + 24) +
                                          rd32(conditional.data() + 20) + rd32(conditional.data() + 16);
                if (conditional_size < chosen_size) {
                    std::memcpy(archive, conditional.data(), conditional_size);
                    chosen_size = conditional_size;
                }
            }
        } catch (...) {}
        try {
            std::vector<uint8_t> direct(capacity);
            if (encode_context_direct(raw, static_cast<uint32_t>(size), rows,
                                      direct.data(), direct.size())) {
                size_t direct_size = HEADER_SIZE + rd32(direct.data() + 24) +
                                     rd32(direct.data() + 20) + rd32(direct.data() + 16);
                if (direct_size < chosen_size) {
                    std::memcpy(archive, direct.data(), direct_size);
                    chosen_size = direct_size;
                }
            }
        } catch (...) {}
        try {
            std::vector<uint8_t> fixedtok(capacity);
            if (encode_fixedtok(raw, static_cast<uint32_t>(size), rows,
                                fixedtok.data(), fixedtok.size())) {
                size_t fixedtok_size = HEADER_SIZE + rd32(fixedtok.data() + 24) +
                                       rd32(fixedtok.data() + 20) + rd32(fixedtok.data() + 16);
                if (fixedtok_size < chosen_size) {
                    std::memcpy(archive, fixedtok.data(), fixedtok_size);
                    chosen_size = fixedtok_size;
                }
            }
        } catch (...) {}
        if (bpe_size > chosen_size && bpe_size - chosen_size <= 40000) {
            std::memcpy(archive, bpe_archive.data(), bpe_size);
            chosen_size = bpe_size;
        }
        return static_cast<int64_t>(chosen_size);
    } catch (...) { return -1; }
}

#else
namespace {
struct Code {
    const uint8_t* p = nullptr;
    uint16_t len = 0;
    uint64_t first = 0;
};
struct CtxDecode {
    // For codes up to nine bits, pack (symbol << 4) | code length.
    // The alphabet is capped at 2048 and fast lengths at nine bits.
    std::array<uint16_t, 512> fast9{};
    bool deterministic = false;
    uint16_t single_symbol = 0;
    std::array<uint16_t, 33> count{};
    std::array<uint16_t, 33> first_index{};
    std::array<uint32_t, 33> first_code{};
    uint8_t max_len = 0;
    std::vector<uint16_t> symbols;
};
struct State {
    const uint8_t* archive = nullptr;
    size_t archive_size = 0;
    uint8_t type = 0, flags = 0;
    uint32_t raw_size = 0, rows = 0, payload_size = 0, index_size = 0, dict_size = 0, record_size = 0;
    const uint8_t* dict = nullptr;
    const uint8_t* index = nullptr;
    const uint8_t* payload = nullptr;
    uint32_t* starts = nullptr;
    uint16_t code_count = 0;
    Code codes[255];
    std::vector<Code> ext_codes;
    std::vector<Code> ctx_codes;
    std::vector<uint64_t> pair_first;
    std::vector<uint8_t> pair_len;
    bool ctx_direct = false;
    uint8_t token_width = 0;
    uint16_t ctx_alphabet = 0, ctx_eos = 0, ctx_start = 0;
    std::vector<CtxDecode> contexts;
    ~State() { delete[] starts; }
};
static bool read_var(const uint8_t* p, size_t end, size_t& at, uint32_t& value) {
    value = 0;
    uint32_t shift = 0;
    for (uint32_t i = 0; i < 5 && at < end; ++i) {
        uint8_t b = p[at++];
        value |= static_cast<uint32_t>(b & 127) << shift;
        if (!(b & 128)) return true;
        shift += 7;
    }
    return false;
}

static bool build_ctx_decode(CtxDecode& out,
                             std::vector<std::pair<uint16_t, uint8_t>>& entries,
                             uint32_t alphabet) {
    if (entries.empty() || alphabet > 2048) return false;
    if (entries.size() == 1 && entries[0].second == 0) {
        if (entries[0].first >= alphabet) return false;
        out.deterministic = true;
        out.single_symbol = entries[0].first;
        out.symbols.push_back(entries[0].first);
        return true;
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second < b.second;
        return a.first < b.first;
    });
    std::vector<uint8_t> seen(alphabet, 0);
    uint32_t max_len = 0;
    for (const auto& e : entries) {
        if (e.first >= alphabet || e.second == 0 || e.second > 31 || seen[e.first]) return false;
        seen[e.first] = 1;
        ++out.count[e.second];
        if (e.second > max_len) max_len = e.second;
    }
    out.max_len = static_cast<uint8_t>(max_len);
    uint32_t index = 0;
    uint64_t first = 0;
    for (uint32_t len = 1; len <= max_len; ++len) {
        first = (first + out.count[len - 1]) << 1;
        out.first_code[len] = static_cast<uint32_t>(first);
        out.first_index[len] = static_cast<uint16_t>(index);
        index += out.count[len];
        if (first + out.count[len] > (uint64_t{1} << len)) return false;
    }
    out.symbols.reserve(entries.size());
    uint64_t code = 0;
    uint32_t prev_len = 0;
    for (const auto& e : entries) {
        uint32_t len = e.second;
        code <<= (len - prev_len);
        if (code >= (uint64_t{1} << len)) return false;
        uint32_t this_code = static_cast<uint32_t>(code++);
        prev_len = len;
        out.symbols.push_back(e.first);
        if (len <= 9) {
            uint32_t base = this_code << (9 - len);
            uint32_t n = 1u << (9 - len);
            uint16_t value = static_cast<uint16_t>((static_cast<uint32_t>(e.first) << 4) | len);
            for (uint32_t i = 0; i < n; ++i) {
                if (out.fast9[base + i]) return false;
                out.fast9[base + i] = value;
            }
        }
    }
    return true;
}
static bool build_fixedtok_starts(State* s) {
    if (s->record_size > 7 || s->payload_size < 4 ||
        (s->record_size && (static_cast<uint64_t>(s->rows) * s->record_size + 7) / 8 != s->index_size) ||
        (!s->record_size && s->index_size < s->rows)) return false;
    uint32_t symbols = static_cast<uint32_t>(s->code_count) + 256;
    uint32_t width = 0;
    while ((uint32_t{1} << width) < symbols) ++width;
    if (width < 8 || width > 11) return false;
    s->token_width = static_cast<uint8_t>(width);
    s->starts = new (std::nothrow) uint32_t[static_cast<size_t>(s->rows) + 1];
    if (!s->starts) return false;
    size_t pos = 0;
    uint64_t tokens = 0;
    s->starts[0] = 0;
    for (uint32_t i = 0; i < s->rows; ++i) {
        uint32_t n;
        if (s->record_size) n = getbits(s->index, i, s->record_size);
        else if (!read_var(s->index, s->index_size, pos, n)) return false;
        tokens += n;
        if (tokens > UINT32_MAX) return false;
        s->starts[i + 1] = static_cast<uint32_t>(tokens);
    }
    if ((s->record_size || pos == s->index_size) == false) return false;
    uint64_t packed = (tokens * width + 7) / 8;
    if (packed + 4 != s->payload_size) return false;
    for (uint64_t i = packed; i < s->payload_size; ++i) if (s->payload[i]) return false;
    return true;
}
static bool build_row_starts(State* s) {
    if (s->record_size > 7 ||
        (s->record_size && (static_cast<uint64_t>(s->rows) * s->record_size + 7) / 8 != s->index_size) ||
        (!s->record_size && s->index_size < s->rows)) return false;
    s->starts = new (std::nothrow) uint32_t[static_cast<size_t>(s->rows) + 1];
    if (!s->starts) return false;
    size_t pos = 0;
    uint64_t off = 0;
    s->starts[0] = 0;
    for (uint32_t i = 0; i < s->rows; ++i) {
        uint32_t len;
        if (s->record_size) len = getbits(s->index, i, s->record_size);
        else if (!read_var(s->index, s->index_size, pos, len)) return false;
        off += len;
        if (off > s->payload_size || off > UINT32_MAX) return false;
        s->starts[i + 1] = static_cast<uint32_t>(off);
    }
    return (s->record_size || pos == s->index_size) && off == s->payload_size;
}
static inline uint8_t nibble_char(uint8_t v, bool upper) {
    static const char lo[] = "0123456789abcdef";
    static const char up[] = "0123456789ABCDEF";
    return static_cast<uint8_t>((upper ? up : lo)[v & 15]);
}
static constexpr uint8_t uuid_varpos[22] = {2,3,4,5,6,7,19,20,21,22,24,25,26,27,28,29,30,31,32,33,34,35};

static State* open_state(const uint8_t* archive, size_t size) {
    if (!archive || size < HEADER_SIZE) return nullptr;
    if (archive[0] != 'D' || archive[1] != 'B' || archive[2] != 'T' || archive[3] != 'X' || archive[5] != 1) return nullptr;
    uint32_t raw_size = rd32(archive + 8), rows = rd32(archive + 12);
    uint32_t payload_size = rd32(archive + 16), index_size = rd32(archive + 20), dict_size = rd32(archive + 24), record_size = rd32(archive + 28);
    uint64_t needed = static_cast<uint64_t>(HEADER_SIZE) + dict_size + index_size + payload_size;
    if (needed != size) return nullptr;
    State* s = new (std::nothrow) State();
    if (!s) return nullptr;
    s->archive = archive; s->archive_size = size;
    s->type = archive[4]; s->flags = archive[6]; s->raw_size = raw_size; s->rows = rows;
    s->payload_size = payload_size; s->index_size = index_size; s->dict_size = dict_size; s->record_size = record_size;
    s->dict = archive + HEADER_SIZE;
    s->index = s->dict + dict_size;
    s->payload = s->index + index_size;
    if (s->type == TYPE_BPE) {
        if (dict_size < 4) { delete s; return nullptr; }
        uint16_t nlit = rd16(s->dict), nphr = rd16(s->dict + 2);
        if (static_cast<uint32_t>(nlit) + nphr > 255) { delete s; return nullptr; }
        size_t at = 4;
        if (at + nlit > dict_size) { delete s; return nullptr; }
        for (uint32_t i = 0; i < nlit; ++i) {
            Code& c = s->codes[i]; c.p = s->dict + at + i; c.len = 1; c.first = s->dict[at + i];
        }
        at += nlit;
        for (uint32_t i = 0; i < nphr; ++i) {
            if (at + 2 > dict_size) { delete s; return nullptr; }
            uint16_t len = rd16(s->dict + at); at += 2;
            if (len < 2 || len > 64 || at + len > dict_size) { delete s; return nullptr; }
            Code& c = s->codes[nlit + i]; c.p = s->dict + at; c.len = len; c.first = 0;
            std::memcpy(&c.first, c.p, std::min<size_t>(8, len));
            at += len;
        }
        s->code_count = static_cast<uint16_t>(nlit + nphr);
        if (s->flags & 2) {
            if (at + 2 > dict_size) { delete s; return nullptr; }
            uint16_t next = rd16(s->dict + at); at += 2;
            if (next > 127) { delete s; return nullptr; }
            s->ext_codes.resize(next);
            for (uint32_t i = 0; i < next; ++i) {
                if (at + 2 > dict_size) { delete s; return nullptr; }
                uint16_t len = rd16(s->dict + at); at += 2;
                if (len < 4 || len > 64 || at + len > dict_size) { delete s; return nullptr; }
                Code& c = s->ext_codes[i];
                c.p = s->dict + at; c.len = len; c.first = 0;
                std::memcpy(&c.first, c.p, std::min<size_t>(8, len));
                at += len;
            }
        }
        if (at != dict_size || (s->flags & ~uint8_t{2})) { delete s; return nullptr; }
        if (!build_row_starts(s)) { delete s; return nullptr; }
        s->pair_first.assign(65536, 0);
        s->pair_len.assign(65536, 0);
        for (uint32_t a = 0; a < s->code_count; ++a) {
            for (uint32_t b = 0; b < s->code_count; ++b) {
                const Code& ca = s->codes[a];
                const Code& cb = s->codes[b];
                uint32_t len = static_cast<uint32_t>(ca.len) + cb.len;
                if (len <= 8) {
                    uint32_t key = (a << 8) | b;
                    s->pair_first[key] = ca.first | (cb.first << (ca.len * 8));
                    s->pair_len[key] = static_cast<uint8_t>(len);
                }
            }
        }
    } else if (s->type == TYPE_EXT) {
        if (dict_size < 2) { delete s; return nullptr; }
        uint16_t nphr = rd16(s->dict);
        size_t at = 2;
        s->ext_codes.resize(nphr);
        for (uint32_t i = 0; i < nphr; ++i) {
            if (at + 2 > dict_size) { delete s; return nullptr; }
            uint16_t len = rd16(s->dict + at); at += 2;
            if (len < 4 || len > 64 || at + len > dict_size) { delete s; return nullptr; }
            Code& c = s->ext_codes[i];
            c.p = s->dict + at; c.len = len; c.first = 0;
            std::memcpy(&c.first, c.p, std::min<size_t>(8, len));
            at += len;
        }
        if (at != dict_size || !build_row_starts(s)) { delete s; return nullptr; }
    } else if (s->type == TYPE_FIXEDTOK) {
        if (dict_size < 2 || s->flags) { delete s; return nullptr; }
        uint16_t nphr = rd16(s->dict);
        if (nphr > 1791) { delete s; return nullptr; }
        size_t at = 2;
        s->code_count = nphr;
        s->ctx_codes.resize(nphr);
        for (uint32_t i = 0; i < nphr; ++i) {
            if (at + 2 > dict_size) { delete s; return nullptr; }
            uint16_t len = rd16(s->dict + at); at += 2;
            if (len < 2 || len > 64 || at + len > dict_size) { delete s; return nullptr; }
            Code& c = s->ctx_codes[i];
            c.p = s->dict + at; c.len = len; c.first = 0;
            std::memcpy(&c.first, c.p, std::min<size_t>(8, len));
            at += len;
        }
        if (at != dict_size || !build_fixedtok_starts(s)) { delete s; return nullptr; }
    } else if (s->type == TYPE_CTX || s->type == TYPE_CTX_DIRECT) {
        bool direct = s->type == TYPE_CTX_DIRECT;
        s->ctx_direct = direct;
        if (direct) {
            if (dict_size < 2 || s->flags) { delete s; return nullptr; }
            uint16_t nphr = rd16(s->dict);
            if (nphr > 1791) { delete s; return nullptr; }
            size_t at = 2;
            s->ctx_codes.resize(nphr);
            for (uint32_t i = 0; i < nphr; ++i) {
                if (at + 2 > dict_size) { delete s; return nullptr; }
                uint16_t len = rd16(s->dict + at); at += 2;
                if (len < 2 || len > 64 || at + len > dict_size) { delete s; return nullptr; }
                Code& c = s->ctx_codes[i];
                c.p = s->dict + at; c.len = len; c.first = 0;
                std::memcpy(&c.first, c.p, std::min<size_t>(8, len));
                at += len;
            }
            s->code_count = nphr;
            uint32_t alphabet = static_cast<uint32_t>(nphr) + 257;
            if (alphabet > 2048 || at + 4 > dict_size || rd16(s->dict + at) != alphabet) { delete s; return nullptr; }
            s->ctx_alphabet = static_cast<uint16_t>(alphabet);
            s->ctx_eos = static_cast<uint16_t>(alphabet - 1);
            s->ctx_start = static_cast<uint16_t>(alphabet);
            at += 2;
            uint16_t nctx = rd16(s->dict + at); at += 2;
            if (nctx > alphabet + 1) { delete s; return nullptr; }
            s->contexts.resize(alphabet + 1);
            std::vector<uint8_t> seen_ctx(alphabet + 1, 0);
            for (uint32_t ci = 0; ci < nctx; ++ci) {
                if (at + 4 > dict_size) { delete s; return nullptr; }
                uint16_t ctx = rd16(s->dict + at); at += 2;
                uint16_t nsym_field = rd16(s->dict + at); at += 2;
                bool compact = (nsym_field & 0x8000u) != 0;
                uint16_t nsym = nsym_field & 0x7fffu;
                if (ctx > alphabet || !nsym || nsym > alphabet || seen_ctx[ctx]) { delete s; return nullptr; }
                seen_ctx[ctx] = 1;
                std::vector<std::pair<uint16_t, uint8_t>> entries;
                entries.reserve(nsym);
                if (compact) {
                    if (at >= dict_size) { delete s; return nullptr; }
                    uint32_t gap_width = s->dict[at++];
                    if (gap_width > 11) { delete s; return nullptr; }
                    size_t data_size = (static_cast<size_t>(nsym) * (gap_width + 5) + 7) / 8;
                    if (at + data_size > dict_size) { delete s; return nullptr; }
                    const uint8_t* packed_data = s->dict + at;
                    uint64_t bit = 0; uint32_t prev_sym = 0;
                    for (uint32_t j = 0; j < nsym; ++j) {
                        uint32_t gap = getbits_at(packed_data, bit, gap_width); bit += gap_width;
                        uint32_t len = getbits_at(packed_data, bit, 5); bit += 5;
                        uint32_t sym = prev_sym + gap;
                        if (sym >= alphabet) { delete s; return nullptr; }
                        entries.emplace_back(static_cast<uint16_t>(sym), static_cast<uint8_t>(len));
                        prev_sym = sym;
                    }
                    at += data_size;
                } else {
                    if (at + static_cast<size_t>(nsym) * 2 > dict_size) { delete s; return nullptr; }
                    for (uint32_t j = 0; j < nsym; ++j) {
                        uint16_t packed = rd16(s->dict + at); at += 2;
                        entries.emplace_back(static_cast<uint16_t>(packed >> 5), static_cast<uint8_t>(packed & 31));
                    }
                }
                if (!build_ctx_decode(s->contexts[ctx], entries, alphabet)) { delete s; return nullptr; }
            }
            if (at != dict_size || !build_row_starts(s)) { delete s; return nullptr; }
        } else {
            if (dict_size < 4 || (s->flags & ~uint8_t{1})) { delete s; return nullptr; }
            uint16_t nlit = rd16(s->dict), nphr = rd16(s->dict + 2);
            if (static_cast<uint32_t>(nlit) + nphr > 255) { delete s; return nullptr; }
            size_t at = 4;
            if (at + nlit > dict_size) { delete s; return nullptr; }
            for (uint32_t i = 0; i < nlit; ++i) {
                Code& c = s->codes[i];
                c.p = s->dict + at + i; c.len = 1; c.first = s->dict[at + i];
            }
            at += nlit;
            for (uint32_t i = 0; i < nphr; ++i) {
                if (at + 2 > dict_size) { delete s; return nullptr; }
                uint16_t len = rd16(s->dict + at); at += 2;
                if (len < 2 || len > 64 || at + len > dict_size) { delete s; return nullptr; }
                Code& c = s->codes[nlit + i];
                c.p = s->dict + at; c.len = len; c.first = 0;
                std::memcpy(&c.first, c.p, std::min<size_t>(8, len));
                at += len;
            }
            s->code_count = static_cast<uint16_t>(nlit + nphr);
            if (s->flags & 1) {
                if (at + 2 > dict_size) { delete s; return nullptr; }
                uint16_t next = rd16(s->dict + at); at += 2;
                if (next > 127) { delete s; return nullptr; }
                s->ext_codes.resize(next);
                for (uint32_t i = 0; i < next; ++i) {
                    if (at + 2 > dict_size) { delete s; return nullptr; }
                    uint16_t len = rd16(s->dict + at); at += 2;
                    if (len < 4 || len > 64 || at + len > dict_size) { delete s; return nullptr; }
                    Code& c = s->ext_codes[i];
                    c.p = s->dict + at; c.len = len; c.first = 0;
                    std::memcpy(&c.first, c.p, std::min<size_t>(8, len));
                    at += len;
                }
            }
            uint32_t alphabet = static_cast<uint32_t>(s->code_count) + 256 +
                                static_cast<uint32_t>(s->ext_codes.size()) + 1;
            if (alphabet > 2048 || at + 4 > dict_size || rd16(s->dict + at) != alphabet) { delete s; return nullptr; }
            s->ctx_alphabet = static_cast<uint16_t>(alphabet);
            s->ctx_eos = static_cast<uint16_t>(alphabet - 1);
            s->ctx_start = static_cast<uint16_t>(alphabet);
            at += 2;
            uint16_t nctx = rd16(s->dict + at); at += 2;
            if (nctx > alphabet + 1) { delete s; return nullptr; }
            s->contexts.resize(alphabet + 1);
            std::vector<uint8_t> seen_ctx(alphabet + 1, 0);
            for (uint32_t ci = 0; ci < nctx; ++ci) {
                if (at + 4 > dict_size) { delete s; return nullptr; }
                uint16_t ctx = rd16(s->dict + at); at += 2;
                uint16_t nsym_field = rd16(s->dict + at); at += 2;
                bool compact = (nsym_field & 0x8000u) != 0;
                uint16_t nsym = nsym_field & 0x7fffu;
                if (ctx > alphabet || !nsym || nsym > alphabet || seen_ctx[ctx]) { delete s; return nullptr; }
                seen_ctx[ctx] = 1;
                std::vector<std::pair<uint16_t, uint8_t>> entries;
                entries.reserve(nsym);
                if (compact) {
                    if (at >= dict_size) { delete s; return nullptr; }
                    uint32_t gap_width = s->dict[at++];
                    if (gap_width > 11) { delete s; return nullptr; }
                    size_t data_size = (static_cast<size_t>(nsym) * (gap_width + 5) + 7) / 8;
                    if (at + data_size > dict_size) { delete s; return nullptr; }
                    const uint8_t* packed_data = s->dict + at;
                    uint64_t bit = 0; uint32_t prev_sym = 0;
                    for (uint32_t j = 0; j < nsym; ++j) {
                        uint32_t gap = getbits_at(packed_data, bit, gap_width); bit += gap_width;
                        uint32_t len = getbits_at(packed_data, bit, 5); bit += 5;
                        uint32_t sym = prev_sym + gap;
                        if (sym >= alphabet) { delete s; return nullptr; }
                        entries.emplace_back(static_cast<uint16_t>(sym), static_cast<uint8_t>(len));
                        prev_sym = sym;
                    }
                    at += data_size;
                } else {
                    if (at + static_cast<size_t>(nsym) * 2 > dict_size) { delete s; return nullptr; }
                    for (uint32_t j = 0; j < nsym; ++j) {
                        uint16_t packed = rd16(s->dict + at); at += 2;
                        entries.emplace_back(static_cast<uint16_t>(packed >> 5), static_cast<uint8_t>(packed & 31));
                    }
                }
                if (!build_ctx_decode(s->contexts[ctx], entries, alphabet)) { delete s; return nullptr; }
            }
            if (at != dict_size || !build_row_starts(s)) { delete s; return nullptr; }
        }
    } else if (s->type == TYPE_CNAME) {
        uint64_t expected = record_size == 18 ? (static_cast<uint64_t>(rows) * 18 + 7) / 8 :
                            static_cast<uint64_t>(rows) * record_size;
        if (rows != 100000 || raw_size != rows * 19u ||
            (record_size != 18 && record_size != 3 && record_size != 4) ||
            dict_size != 9 || index_size != 0 || expected != payload_size) { delete s; return nullptr; }
    } else if (s->type == TYPE_DNA) {
        if (rows != 100000 || raw_size != rows * 10u || record_size != 18 || dict_size || index_size ||
            (static_cast<uint64_t>(rows) * 18 + 7) / 8 != payload_size) { delete s; return nullptr; }
    } else if (s->type == TYPE_UUID) {
        if (rows != 100000 || raw_size != rows * 37u || record_size != 11 || dict_size != 37 || index_size ||
            static_cast<uint64_t>(rows) * 11 != payload_size) { delete s; return nullptr; }
    } else if (s->type == TYPE_HEX) {
        if (rows != 100000 || record_size || dict_size || index_size != (static_cast<uint64_t>(rows) * 3 + 7) / 8) { delete s; return nullptr; }
        s->starts = new (std::nothrow) uint32_t[static_cast<size_t>(rows) + 1];
        if (!s->starts) { delete s; return nullptr; }
        uint64_t off = 0; s->starts[0] = 0;
        for (uint32_t i = 0; i < rows; ++i) {
            uint32_t d = 4 + get3(s->index, i);
            if (d > 8) { delete s; return nullptr; }
            off += (d + 1) / 2;
            if (off > payload_size || off > UINT32_MAX) { delete s; return nullptr; }
            s->starts[i + 1] = static_cast<uint32_t>(off);
        }
        if (off != payload_size) { delete s; return nullptr; }
    } else { delete s; return nullptr; }
    return s;
}

struct Stage {
    uint8_t* output;
    size_t capacity;
    size_t written = 0;
    size_t total = 0;
    Stage(uint8_t* o, size_t c) : output(o), capacity(c) {}
    bool flush() { return written == total; }
    bool append(const uint8_t* p, uint16_t len, uint64_t first) {
        if (total > capacity) return false;
        size_t remaining = capacity - total;
        if (remaining < len) return false;
        uint8_t* dst = output + total;
        if (len <= 8) {
            if (remaining >= 8) std::memcpy(dst, &first, 8);
            else std::memcpy(dst, &first, len);
        } else {
            std::memcpy(dst, p, len);
        }
        total += len; written = total;
        return true;
    }
    bool byte(uint8_t b) {
        uint64_t x = b;
        return append(nullptr, 1, x);
    }
};
static bool decode_bpe_row(const State* s, uint32_t id, Stage& stage) {
    uint32_t p = s->starts[id], end = s->starts[id + 1];
    while (p < end) {
        if (p + 1 < end) {
            uint8_t a = s->payload[p], b = s->payload[p + 1];
            if (a != 255 && b != 255 && a < s->code_count && b < s->code_count) {
                uint32_t key = (static_cast<uint32_t>(a) << 8) | b;
                uint8_t len = s->pair_len[key];
                if (len) {
                    if (!stage.append(nullptr, len, s->pair_first[key])) return false;
                    p += 2;
                    continue;
                }
            }
        }
        uint8_t code = s->payload[p++];
        if (code == 255) {
            if (p >= end) return false;
            uint8_t esc = s->payload[p++];
            if (!(s->flags & 2)) {
                if (!stage.byte(esc)) return false;
            } else if (esc < 128) {
                if (!stage.byte(esc)) return false;
            } else if (esc == 255) {
                if (p >= end || !stage.byte(s->payload[p++])) return false;
            } else {
                uint32_t id = esc - 128;
                if (id >= s->ext_codes.size()) return false;
                const Code& c = s->ext_codes[id];
                if (!stage.append(c.p, c.len, c.first)) return false;
            }
        } else {
            if (code >= s->code_count) return false;
            const Code& c = s->codes[code];
            if (!stage.append(c.p, c.len, c.first)) return false;
        }
    }
    return true;
}
static bool decode_extended_row(const State* s, uint32_t id, Stage& stage) {
    uint32_t p = s->starts[id], end = s->starts[id + 1];
    while (p < end) {
        uint8_t token = s->payload[p++];
        if (token != 255) {
            if (!stage.byte(token)) return false;
        } else {
            if (end - p < 2) return false;
            uint16_t code = rd16(s->payload + p); p += 2;
            if (code == 0xffff) {
                if (!stage.byte(255)) return false;
            } else {
                if (code >= s->ext_codes.size()) return false;
                const Code& c = s->ext_codes[code];
                if (!stage.append(c.p, c.len, c.first)) return false;
            }
        }
    }
    return true;
}
struct BitReader {
    const uint8_t* p;
    const uint8_t* end;
    uint64_t bits = 0;
    uint32_t available = 0;
    BitReader(const uint8_t* begin, const uint8_t* finish) : p(begin), end(finish) {}
    inline void fill_fast() {
        if (available >= 9 || p == end) return;
        size_t room = (64 - available) >> 3;
        size_t remaining = static_cast<size_t>(end - p);
        size_t n;
        uint64_t chunk = 0;
        if (remaining >= 8) {
            std::memcpy(&chunk, p, 8);
            n = room;
        } else {
            n = std::min<size_t>(room, remaining);
            std::memcpy(&chunk, p, n);
        }
        chunk = __builtin_bswap64(chunk);
        bits |= chunk >> available;
        p += n;
        available += static_cast<uint32_t>(n * 8);
    }
    inline bool fill_one() {
        if (!available) fill_fast();
        return available != 0;
    }
    inline uint32_t peek9() const {
        return static_cast<uint32_t>(bits >> 55);
    }
    inline void consume(uint32_t n) {
        bits <<= n;
        available -= n;
    }
    inline bool getbit(uint32_t& bit) {
        if (!fill_one()) return false;
        bit = static_cast<uint32_t>(bits >> 63);
        consume(1);
        return true;
    }
};
static inline __attribute__((always_inline)) bool decode_ctx_symbol(
    const CtxDecode& ctx, BitReader& reader, uint16_t& symbol) {
    if (ctx.deterministic) { symbol = ctx.single_symbol; return true; }
    reader.fill_fast();
    if (reader.available) {
        uint16_t fast = ctx.fast9[reader.peek9()];
        if (fast) {
            uint32_t len = fast & 15;
            if (reader.available >= len) {
                symbol = static_cast<uint16_t>(fast >> 4);
                reader.consume(len);
                return true;
            }
        }
    }
    uint32_t code = 0;
    for (uint32_t len = 1; len <= ctx.max_len; ++len) {
        uint32_t bit;
        if (!reader.getbit(bit)) return false;
        code = (code << 1) | bit;
        uint32_t first = ctx.first_code[len];
        uint32_t count = ctx.count[len];
        if (count && code >= first && code - first < count) {
            uint32_t index = static_cast<uint32_t>(ctx.first_index[len]) + code - first;
            if (index >= ctx.symbols.size()) return false;
            symbol = ctx.symbols[index];
            return true;
        }
    }
    return false;
}
static bool append_ctx_token(const State* s, uint16_t sym, Stage& stage) {
    if (sym < s->code_count) {
        const Code& c = s->ctx_direct ? s->ctx_codes[sym] : s->codes[sym];
        return stage.append(c.p, c.len, c.first);
    }
    if (sym < static_cast<uint32_t>(s->code_count) + 256)
        return stage.byte(static_cast<uint8_t>(sym - s->code_count));
    uint32_t x = sym - static_cast<uint32_t>(s->code_count) - 256;
    if (x >= s->ext_codes.size()) return false;
    const Code& c = s->ext_codes[x];
    return stage.append(c.p, c.len, c.first);
}
static bool decode_ctx_row(const State* s, uint32_t id, Stage& stage) {
    const uint8_t* begin = s->payload + s->starts[id];
    const uint8_t* end = s->payload + s->starts[id + 1];
    BitReader reader(begin, end);
    uint16_t prev = s->ctx_start;
    for (;;) {
        if (prev >= s->contexts.size()) return false;
        uint16_t sym;
        if (!decode_ctx_symbol(s->contexts[prev], reader, sym)) return false;
        if (sym == s->ctx_eos) return true;
        if (sym >= s->ctx_eos || !append_ctx_token(s, sym, stage)) return false;
        prev = sym;
    }
}
static bool decode_ctx_direct_row(const State* s, uint32_t id, Stage& stage) {
    const uint8_t* begin = s->payload + s->starts[id];
    const uint8_t* end = s->payload + s->starts[id + 1];
    BitReader reader(begin, end);
    uint16_t prev = s->ctx_start;
    for (;;) {
        if (prev >= s->contexts.size()) return false;
        uint16_t sym;
        if (!decode_ctx_symbol(s->contexts[prev], reader, sym)) return false;
        if (sym == s->ctx_eos) return true;
        if (sym < s->code_count) {
            const Code& c = s->ctx_codes[sym];
            if (!stage.append(c.p, c.len, c.first)) return false;
        } else if (sym < static_cast<uint32_t>(s->code_count) + 256) {
            if (!stage.byte(static_cast<uint8_t>(sym - s->code_count))) return false;
        } else {
            return false;
        }
        prev = sym;
    }
}
static inline uint32_t fixedtok_symbol(const State* s, uint32_t token) {
    uint64_t bit = static_cast<uint64_t>(token) * s->token_width;
    uint32_t byte = static_cast<uint32_t>(bit >> 3);
    uint32_t shift = static_cast<uint32_t>(bit & 7);
    uint32_t word = rd32(s->payload + byte);
    return (word >> shift) & ((uint32_t{1} << s->token_width) - 1);
}
static bool decode_fixedtok_row(const State* s, uint32_t id, Stage& stage) {
    uint32_t begin = s->starts[id], end = s->starts[id + 1];
    uint32_t count = s->code_count;
    for (uint32_t i = begin; i < end; ++i) {
        uint32_t sym = fixedtok_symbol(s, i);
        if (sym < count) {
            const Code& c = s->ctx_codes[sym];
            if (!stage.append(c.p, c.len, c.first)) return false;
        } else {
            if (sym >= count + 256 || !stage.byte(static_cast<uint8_t>(sym - count))) return false;
        }
    }
    return true;
}
static uint32_t fixed_row_size(const State* s, uint32_t id) {
    if (s->type == TYPE_CNAME) return 19;
    if (s->type == TYPE_DNA) return 10;
    if (s->type == TYPE_UUID) return 37;
    if (s->type == TYPE_HEX) return 5 + get3(s->index, id);
    return 0;
}
static uint32_t decode_fixed_row(const State* s, uint32_t id, uint8_t* out) {
    if (s->type == TYPE_CNAME) {
        const uint8_t* p = s->payload;
        uint32_t v;
        if (s->record_size == 18) v = getbits(p, id, 18);
        else {
            p += static_cast<size_t>(id) * s->record_size;
            v = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16);
            if (s->record_size == 4) v |= static_cast<uint32_t>(p[3]) << 24;
        }
        if (v >= 1000000000u) return 0;
        std::memcpy(out, s->dict, 9);
        for (int k = 8; k >= 0; --k) { out[9 + k] = static_cast<uint8_t>('0' + v % 10); v /= 10; }
        out[18] = '\n';
        return 19;
    }
    if (s->type == TYPE_DNA) {
        static const uint8_t base[4] = {'a','c','g','t'};
        uint32_t bits = getbits(s->payload, id, 18);
        for (uint32_t j = 0; j < 9; ++j) out[j] = base[(bits >> (2 * j)) & 3];
        out[9] = '\n';
        return 10;
    }
    if (s->type == TYPE_UUID) {
        std::memcpy(out, s->dict, 37);
        const uint8_t* p = s->payload + static_cast<size_t>(id) * 11;
        alignas(16) uint8_t tail[16];
        if (id + 1 == s->rows) {
            std::memcpy(tail, p, 11);
            std::memset(tail + 11, 0, 5);
            p = tail;
        }
        __m128i packed = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
        __m128i mask = _mm_set1_epi8(15);
        __m128i high = _mm_and_si128(_mm_srli_epi16(packed, 4), mask);
        __m128i low = _mm_and_si128(packed, mask);
        alignas(16) static const uint8_t lower_chars[16] = {
            '0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'
        };
        alignas(16) static const uint8_t upper_chars[16] = {
            '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'
        };
        __m128i table = _mm_load_si128(reinterpret_cast<const __m128i*>(
            (s->flags & 1) ? upper_chars : lower_chars));
        __m128i hchars = _mm_shuffle_epi8(table, high);
        __m128i lchars = _mm_shuffle_epi8(table, low);
        alignas(16) uint8_t digits[32];
        _mm_store_si128(reinterpret_cast<__m128i*>(digits), _mm_unpacklo_epi8(hchars, lchars));
        _mm_store_si128(reinterpret_cast<__m128i*>(digits + 16), _mm_unpackhi_epi8(hchars, lchars));
        std::memcpy(out + 2, digits, 6);
        std::memcpy(out + 19, digits + 6, 4);
        std::memcpy(out + 24, digits + 10, 12);
        return 37;
    }
    if (s->type == TYPE_HEX) {
        uint32_t digits = 4 + get3(s->index, id);
        const uint8_t* p = s->payload + s->starts[id];
        bool upper = (s->flags & 1) != 0;
        for (uint32_t j = 0; j < digits; ++j) {
            uint8_t b = p[j >> 1];
            uint8_t n = (j & 1) ? (b & 15) : (b >> 4);
            out[j] = nibble_char(n, upper);
        }
        out[digits] = '\n';
        return digits + 1;
    }
    return 0;
}
static int64_t decode_all(const State* s, uint8_t* output, size_t capacity, uint64_t* offsets) {
    if (!output && s->raw_size) return -1;
    if (capacity < s->raw_size) return -1;
    if (offsets) offsets[0] = 0;
    if (s->type == TYPE_BPE || s->type == TYPE_EXT || s->type == TYPE_CTX || s->type == TYPE_CTX_DIRECT) {
        Stage stage(output, capacity);
        for (uint32_t i = 0; i < s->rows; ++i) {
            bool ok = s->type == TYPE_BPE ? decode_bpe_row(s, i, stage)
                     : s->type == TYPE_EXT ? decode_extended_row(s, i, stage)
                     : s->type == TYPE_CTX_DIRECT ? decode_ctx_direct_row(s, i, stage)
                                           : decode_ctx_row(s, i, stage);
            if (!ok) return -1;
            if (offsets) offsets[i + 1] = stage.total;
        }
        if (stage.total != s->raw_size || !stage.flush() || stage.written != s->raw_size) return -1;
        return static_cast<int64_t>(s->raw_size);
    }
    if (s->type == TYPE_FIXEDTOK) {
        Stage stage(output, capacity);
        for (uint32_t i = 0; i < s->rows; ++i) {
            if (!decode_fixedtok_row(s, i, stage)) return -1;
            if (offsets) offsets[i + 1] = stage.total;
        }
        if (stage.total != s->raw_size || !stage.flush()) return -1;
        return static_cast<int64_t>(stage.total);
    }
    size_t total = 0;
    for (uint32_t i = 0; i < s->rows; ++i) {
        uint32_t n = decode_fixed_row(s, i, output + total);
        if (!n || total + n > capacity) return -1;
        total += n;
        if (offsets) offsets[i + 1] = total;
    }
    if (total != s->raw_size) return -1;
    return static_cast<int64_t>(total);
}
}

extern "C" void* lab_open(const uint8_t* archive, size_t size) {
    try { return open_state(archive, size); } catch (...) { return nullptr; }
}
extern "C" int64_t lab_decode(void* state, uint8_t* output, size_t capacity) {
    if (!state) return -1;
    try { return decode_all(static_cast<State*>(state), output, capacity, nullptr); }
    catch (...) { return -1; }
}
extern "C" int64_t lab_rows(void* state, const uint64_t* ids, size_t count,
                            uint8_t* output, size_t capacity, uint64_t* offsets) {
    if (!state || (!ids && count) || !offsets || (!output && capacity)) return -1;
    State* s = static_cast<State*>(state);
    try {
        if (count == s->rows) {
            bool all = true;
            for (size_t i = 0; i < count; ++i) if (ids[i] != i) { all = false; break; }
            if (all) return decode_all(s, output, capacity, offsets);
        }
        offsets[0] = 0;
        if (s->type == TYPE_BPE || s->type == TYPE_EXT || s->type == TYPE_CTX || s->type == TYPE_CTX_DIRECT) {
            Stage stage(output, capacity);
            for (size_t i = 0; i < count; ++i) {
                if (ids[i] >= s->rows) return -1;
                bool ok = s->type == TYPE_BPE ? decode_bpe_row(s, static_cast<uint32_t>(ids[i]), stage)
                         : s->type == TYPE_EXT ? decode_extended_row(s, static_cast<uint32_t>(ids[i]), stage)
                         : s->type == TYPE_CTX_DIRECT ? decode_ctx_direct_row(s, static_cast<uint32_t>(ids[i]), stage)
                                               : decode_ctx_row(s, static_cast<uint32_t>(ids[i]), stage);
                if (!ok) return -1;
                offsets[i + 1] = stage.total;
            }
            if (!stage.flush()) return -1;
            return static_cast<int64_t>(stage.total);
        }
        if (s->type == TYPE_FIXEDTOK) {
            Stage stage(output, capacity);
            for (size_t i = 0; i < count; ++i) {
                if (ids[i] >= s->rows || !decode_fixedtok_row(s, static_cast<uint32_t>(ids[i]), stage)) return -1;
                offsets[i + 1] = stage.total;
            }
            if (!stage.flush()) return -1;
            return static_cast<int64_t>(stage.total);
        }
        size_t total = 0;
        for (size_t i = 0; i < count; ++i) {
            if (ids[i] >= s->rows || total > capacity) return -1;
            uint32_t expected = fixed_row_size(s, static_cast<uint32_t>(ids[i]));
            if (!expected || expected > capacity - total) return -1;
            uint32_t n = decode_fixed_row(s, static_cast<uint32_t>(ids[i]), output + total);
            if (n != expected) return -1;
            total += n; offsets[i + 1] = total;
        }
        return static_cast<int64_t>(total);
    } catch (...) { return -1; }
}
extern "C" void lab_close(void* state) { delete static_cast<State*>(state); }
#endif
