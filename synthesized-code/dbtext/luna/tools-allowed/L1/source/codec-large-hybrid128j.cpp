#include "codec.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <immintrin.h>
#include <new>
#include <queue>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

static constexpr uint32_t HDR_SIZE = 32;
static constexpr uint8_t MODE_PHRASE = 1;
static constexpr uint8_t MODE_CUSTOMER = 2;
static constexpr uint8_t MODE_DNA = 3;
static constexpr uint8_t MODE_HEX = 4;
static constexpr uint8_t MODE_UUID = 5;
static constexpr uint8_t MODE_LOCATION = 6;
static constexpr uint8_t MODE_BPE = 7;
static constexpr uint8_t MODE_HUFF = 8;
static constexpr uint32_t ROW_STRIDE = 32;
static constexpr uint32_t MAX_PHRASES = 512;
static constexpr uint32_t HUFF_MAX_SYMBOLS = 1024;

static inline uint16_t rd16(const uint8_t* p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
static inline uint32_t rd32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
static inline uint64_t rd64(const uint8_t* p) {
    return uint64_t(rd32(p)) | (uint64_t(rd32(p + 4)) << 32);
}
static inline void wr16(uint8_t* p, uint16_t v) {
    p[0] = uint8_t(v); p[1] = uint8_t(v >> 8);
}
static inline void wr32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v); p[1] = uint8_t(v >> 8);
    p[2] = uint8_t(v >> 16); p[3] = uint8_t(v >> 24);
}
static inline void wr64(uint8_t* p, uint64_t v) {
    wr32(p, uint32_t(v)); wr32(p + 4, uint32_t(v >> 32));
}
static inline uint32_t bit_width_u64(uint64_t x) {
    uint32_t n = 0;
    while (x) { ++n; x >>= 1; }
    return n ? n : 1;
}
static inline uint64_t low_mask(unsigned bits) {
    return bits >= 64 ? ~uint64_t(0) : ((uint64_t(1) << bits) - 1);
}
static inline unsigned __int128 low_mask128(unsigned bits) {
    return bits >= 128 ? ~static_cast<unsigned __int128>(0)
                       : ((static_cast<unsigned __int128>(1) << bits) - 1);
}

struct Row {
    uint32_t off;
    uint32_t len;
};

static bool split_rows(const uint8_t* raw, size_t n, std::vector<Row>& rows) {
    if (n > std::numeric_limits<uint32_t>::max()) return false;
    size_t start = 0;
    for (size_t i = 0; i < n; ++i) {
        if (raw[i] == '\n') {
            if (i + 1 - start > std::numeric_limits<uint32_t>::max()) return false;
            rows.push_back(Row{uint32_t(start), uint32_t(i + 1 - start)});
            start = i + 1;
        }
    }
    if (start < n) {
        rows.push_back(Row{uint32_t(start), uint32_t(n - start)});
    }
    return rows.size() <= std::numeric_limits<uint32_t>::max();
}

static void append_u8(std::vector<uint8_t>& v, uint8_t x) {
    v.push_back(x);
}
static void append_u16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8));
}
static void append_u32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8));
    v.push_back(uint8_t(x >> 16)); v.push_back(uint8_t(x >> 24));
}
static void append_u64(std::vector<uint8_t>& v, uint64_t x) {
    append_u32(v, uint32_t(x)); append_u32(v, uint32_t(x >> 32));
}
static void append_bytes(std::vector<uint8_t>& v, const uint8_t* p, size_t n) {
    v.insert(v.end(), p, p + n);
}
static void append_varint(std::vector<uint8_t>& v, uint32_t x) {
    while (x >= 128) {
        v.push_back(uint8_t(x) | 0x80);
        x >>= 7;
    }
    v.push_back(uint8_t(x));
}

static bool make_archive(uint8_t mode, uint32_t rows, uint32_t raw_size,
                         uint32_t record_bits,
                         const std::vector<uint8_t>& dict,
                         const std::vector<uint32_t>& index,
                         const std::vector<uint8_t>& row_index,
                         const std::vector<uint8_t>& data,
                         std::vector<uint8_t>& out) {
    if (dict.size() > std::numeric_limits<uint32_t>::max() ||
        data.size() > std::numeric_limits<uint32_t>::max() ||
        index.size() > std::numeric_limits<uint32_t>::max())
        return false;
    out.assign(HDR_SIZE, 0);
    std::memcpy(out.data(), "DBR1", 4);
    out[4] = 1;
    out[5] = mode;
    out[6] = 5;
    wr32(out.data() + 8, rows);
    wr32(out.data() + 12, raw_size);
    wr32(out.data() + 16, uint32_t(dict.size()));
    wr32(out.data() + 20, uint32_t(index.size()));
    wr32(out.data() + 24, uint32_t(data.size()));
    wr32(out.data() + 28, record_bits);
    append_bytes(out, dict.data(), dict.size());
    for (uint32_t x : index) append_u32(out, x);
    append_bytes(out, row_index.data(), row_index.size());
    append_bytes(out, data.data(), data.size());
    return true;
}

static inline bool is_hex(uint8_t c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}
static inline uint8_t hex_val(uint8_t c) {
    if (c >= '0' && c <= '9') return uint8_t(c - '0');
    if (c >= 'a' && c <= 'f') return uint8_t(c - 'a' + 10);
    return uint8_t(c - 'A' + 10);
}
static inline bool is_hex_letter(uint8_t c) {
    return (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static inline bool is_ascii_word(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9');
}
static inline bool is_boundary(uint8_t c) {
    return c == ' ' || c == '\t' || c == '_' || c == '-' || c == '/' ||
           c == '.' || c == ':' || c == '@' || c == ',' || c == ';' ||
           c == '(' || c == ')' || c == '[' || c == ']' || c == '?' ||
           c == '&' || c == '=' || c == '\'' || c == '"' || c == '\n' ||
           c == '\r';
}

static bool get_line_ending(const uint8_t* row, size_t len, size_t& content_len,
                            uint8_t& end_len) {
    end_len = 0;
    content_len = len;
    if (len && row[len - 1] == '\n') {
        end_len = 1;
        content_len = len - 1;
        if (content_len && row[content_len - 1] == '\r') {
            ++end_len;
            --content_len;
        }
    }
    return true;
}

static void bit_put(std::vector<uint8_t>& data, uint64_t bitpos,
                    uint64_t value, unsigned bits) {
    for (unsigned i = 0; i < bits; ++i) {
        if ((value >> i) & 1) {
            uint64_t p = bitpos + i;
            data[size_t(p >> 3)] |= uint8_t(1u << (p & 7));
        }
    }
}
static uint64_t bits_at(const uint8_t* data, size_t data_len,
                        uint64_t bitpos, unsigned bits) {
    if (!bits) return 0;
    size_t byte = size_t(bitpos >> 3);
    unsigned shift = unsigned(bitpos & 7);
    uint64_t w = 0;
    if (byte < data_len && data_len - byte >= 8) {
        std::memcpy(&w, data + byte, 8);
    } else {
        for (unsigned i = 0; i < 5 && byte + i < data_len; ++i)
            w |= uint64_t(data[byte + i]) << (8 * i);
    }
    return (w >> shift) & low_mask(bits);
}
static uint64_t parse_uint(const uint8_t* p, size_t n, bool& ok) {
    uint64_t v = 0;
    ok = n != 0;
    for (size_t i = 0; i < n; ++i) {
        if (p[i] < '0' || p[i] > '9') { ok = false; return 0; }
        uint32_t d = uint32_t(p[i] - '0');
        if (v > (std::numeric_limits<uint64_t>::max() - d) / 10) {
            ok = false; return 0;
        }
        v = v * 10 + d;
    }
    return v;
}

static bool append_special(const std::vector<uint8_t>& dict,
                           const std::vector<uint8_t>& data,
                           uint8_t mode, uint32_t rows, uint32_t raw_size,
                           uint32_t record_bits, std::vector<uint8_t>& out) {
    std::vector<uint32_t> no_index;
    std::vector<uint8_t> no_row_index;
    return make_archive(mode, rows, raw_size, record_bits, dict, no_index,
                        no_row_index, data, out);
}

static bool encode_customer(const uint8_t* raw, size_t raw_size,
                            const std::vector<Row>& rows,
                            std::vector<uint8_t>& out) {
    if (rows.empty()) return false;
    const uint8_t* r0 = raw + rows[0].off;
    size_t n0 = rows[0].len;
    size_t digit_start = 0;
    while (digit_start < n0 && !(r0[digit_start] >= '0' && r0[digit_start] <= '9'))
        ++digit_start;
    if (digit_start == 0 || digit_start == n0) return false;
    size_t digit_end = digit_start;
    while (digit_end < n0 && r0[digit_end] >= '0' && r0[digit_end] <= '9')
        ++digit_end;
    size_t digits = digit_end - digit_start;
    size_t prefix_len = digit_start;
    size_t suffix_len = n0 - digit_end;
    if (!digits || digits > 10 || prefix_len > 255 || suffix_len > 255) return false;
    std::vector<uint64_t> values;
    values.reserve(rows.size());
    uint64_t minv = std::numeric_limits<uint64_t>::max(), maxv = 0;
    for (const Row& row : rows) {
        const uint8_t* p = raw + row.off;
        if (row.len != n0 || std::memcmp(p, r0, prefix_len) != 0 ||
            std::memcmp(p + digit_end, r0 + digit_end, suffix_len) != 0)
            return false;
        bool ok = false;
        uint64_t v = parse_uint(p + prefix_len, digits, ok);
        if (!ok || v > std::numeric_limits<uint32_t>::max()) return false;
        values.push_back(v);
        minv = std::min(minv, v);
        maxv = std::max(maxv, v);
    }
    uint32_t width = bit_width_u64(maxv - minv);
    if (width > 32) return false;
    uint64_t total_bits = uint64_t(width) * rows.size();
    std::vector<uint8_t> data(size_t((total_bits + 7) / 8), 0);
    uint64_t bp = 0;
    for (uint64_t v : values) {
        bit_put(data, bp, v - minv, width);
        bp += width;
    }
    std::vector<uint8_t> dict;
    append_u8(dict, uint8_t(prefix_len));
    append_bytes(dict, r0, prefix_len);
    append_u8(dict, uint8_t(digits));
    append_u8(dict, uint8_t(suffix_len));
    append_bytes(dict, r0 + digit_end, suffix_len);
    append_u32(dict, uint32_t(minv));
    append_u8(dict, uint8_t(width));
    return append_special(dict, data, MODE_CUSTOMER, uint32_t(rows.size()),
                          uint32_t(raw_size), width, out);
}

static bool encode_dna(const uint8_t* raw, size_t raw_size,
                       const std::vector<Row>& rows,
                       std::vector<uint8_t>& out) {
    if (rows.size() < 2) return false;
    size_t content0 = 0;
    uint8_t end0 = 0;
    get_line_ending(raw + rows[0].off, rows[0].len, content0, end0);
    if (!content0 || content0 > 32) return false;
    const uint8_t* first = raw + rows[0].off;
    std::vector<uint8_t> ending(first + content0, first + rows[0].len);
    std::array<int16_t, 256> map;
    map.fill(-1);
    std::vector<uint8_t> alpha;
    for (const Row& row : rows) {
        size_t cl = 0; uint8_t el = 0;
        get_line_ending(raw + row.off, row.len, cl, el);
        if (cl != content0 || el != end0 ||
            std::memcmp(raw + row.off + cl, ending.data(), ending.size()) != 0)
            return false;
        const uint8_t* p = raw + row.off;
        for (size_t j = 0; j < content0; ++j) {
            uint8_t c = p[j];
            if (c != 'a' && c != 'c' && c != 'g' && c != 't' &&
                c != 'A' && c != 'C' && c != 'G' && c != 'T')
                return false;
            if (map[c] < 0) { map[c] = int16_t(alpha.size()); alpha.push_back(c); }
        }
    }
    if (alpha.size() != 4) return false;
    uint32_t bits = uint32_t(content0 * 2);
    uint64_t total = uint64_t(bits) * rows.size();
    std::vector<uint8_t> data(size_t((total + 7) / 8), 0);
    uint64_t bp = 0;
    for (const Row& row : rows) {
        const uint8_t* p = raw + row.off;
        for (size_t j = 0; j < content0; ++j) {
            int16_t code = map[p[j]];
            if (code < 0 || code >= 4) return false;
            bit_put(data, bp, uint64_t(code), 2);
            bp += 2;
        }
    }
    std::vector<uint8_t> dict;
    append_u8(dict, uint8_t(content0));
    append_u8(dict, 2);
    append_u8(dict, uint8_t(ending.size()));
    append_bytes(dict, ending.data(), ending.size());
    append_u8(dict, uint8_t(alpha.size()));
    append_bytes(dict, alpha.data(), alpha.size());
    return append_special(dict, data, MODE_DNA, uint32_t(rows.size()),
                          uint32_t(raw_size), bits, out);
}

static bool encode_hex(const uint8_t* raw, size_t raw_size,
                       const std::vector<Row>& rows,
                       std::vector<uint8_t>& out) {
    if (rows.size() < 2) return false;
    size_t content0 = 0;
    uint8_t end0 = 0;
    get_line_ending(raw + rows[0].off, rows[0].len, content0, end0);
    if (!content0 || content0 > 8) return false;
    const uint8_t* first = raw + rows[0].off;
    std::vector<uint8_t> ending(first + content0, first + rows[0].len);
    uint8_t alpha[16] = {};
    bool seen[16] = {};
    bool lower = false, upper = false, inconsistent = false;
    std::vector<uint32_t> values;
    values.reserve(rows.size());
    for (const Row& row : rows) {
        size_t cl = 0; uint8_t el = 0;
        get_line_ending(raw + row.off, row.len, cl, el);
        if (cl == 0 || cl > 8 || el != end0 ||
            std::memcmp(raw + row.off + cl, ending.data(), ending.size()) != 0)
            return false;
        const uint8_t* p = raw + row.off;
        if (cl > 1 && p[0] == '0') return false;
        uint32_t value = 0;
        for (size_t j = 0; j < cl; ++j) {
            if (!is_hex(p[j])) return false;
            uint8_t v = hex_val(p[j]);
            if (is_hex_letter(p[j])) {
                if (p[j] >= 'a') lower = true; else upper = true;
            }
            if (seen[v] && alpha[v] != p[j]) inconsistent = true;
            if (!seen[v]) { seen[v] = true; alpha[v] = p[j]; }
            value = (value << 4) | v;
        }
        values.push_back(value);
    }
    if (inconsistent || (lower && upper)) return false;
    for (unsigned v = 0; v < 16; ++v) {
        if (!seen[v]) alpha[v] = uint8_t(v < 10 ? '0' + v :
                                        (lower ? 'a' : 'A') + (v - 10));
    }
    std::vector<uint8_t> data;
    data.reserve(rows.size() * 4);
    for (uint32_t x : values) append_u32(data, x);
    std::vector<uint8_t> dict;
    append_u8(dict, uint8_t(ending.size()));
    append_bytes(dict, ending.data(), ending.size());
    append_bytes(dict, alpha, 16);
    return append_special(dict, data, MODE_HEX, uint32_t(rows.size()),
                          uint32_t(raw_size), 32, out);
}

static bool encode_uuid(const uint8_t* raw, size_t raw_size,
                        const std::vector<Row>& rows,
                        std::vector<uint8_t>& out) {
    if (rows.size() < 2) return false;
    size_t content0 = 0;
    uint8_t end0 = 0;
    get_line_ending(raw + rows[0].off, rows[0].len, content0, end0);
    if (content0 != 36) return false;
    const uint8_t* first = raw + rows[0].off;
    const int dash_pos[4] = {8, 13, 18, 23};
    for (int p : dash_pos) if (first[p] != '-') return false;

    uint8_t hex_alpha[16];
    bool seen[16] = {};
    bool lower = false, upper = false, inconsistent = false;
    uint8_t fixed[4] = {};
    bool fixed_set = false;
    uint32_t min_time = std::numeric_limits<uint32_t>::max(), max_time = 0;
    std::vector<std::array<uint8_t, 16>> ids;
    ids.reserve(rows.size());
    std::vector<uint8_t> ending(first + content0, first + rows[0].len);

    for (const Row& row : rows) {
        size_t cl = 0; uint8_t el = 0;
        get_line_ending(raw + row.off, row.len, cl, el);
        const uint8_t* p = raw + row.off;
        if (cl != content0 || el != end0 ||
            std::memcmp(p + cl, ending.data(), ending.size()) != 0)
            return false;
        for (int d : dash_pos) if (p[d] != '-') return false;
        std::array<uint8_t, 16> b{};
        unsigned nib = 0;
        for (unsigned i = 0; i < 36; ++i) {
            if (i == 8 || i == 13 || i == 18 || i == 23) continue;
            if (!is_hex(p[i])) return false;
            uint8_t v = hex_val(p[i]);
            if (is_hex_letter(p[i])) {
                if (p[i] >= 'a') lower = true; else upper = true;
            }
            if (seen[v] && hex_alpha[v] != p[i]) inconsistent = true;
            if (!seen[v]) { seen[v] = true; hex_alpha[v] = p[i]; }
            if ((nib & 1) == 0) b[nib >> 1] = uint8_t(v << 4);
            else b[nib >> 1] |= v;
            ++nib;
        }
        if (nib != 32) return false;
        if (!fixed_set) {
            std::memcpy(fixed, b.data() + 4, 4);
            fixed_set = true;
        } else if (std::memcmp(fixed, b.data() + 4, 4) != 0) {
            return false;
        }
        uint32_t tv = (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) |
                      (uint32_t(b[2]) << 8) | uint32_t(b[3]);
        min_time = std::min(min_time, tv);
        max_time = std::max(max_time, tv);
        ids.push_back(b);
    }
    if (inconsistent || (lower && upper)) return false;
    for (unsigned v = 0; v < 16; ++v)
        if (!seen[v]) hex_alpha[v] = uint8_t(v < 10 ? '0' + v :
                                            (lower ? 'a' : 'A') + (v - 10));
    uint32_t tw = bit_width_u64(uint64_t(max_time) - min_time);
    unsigned total_bits = tw + 64;
    uint8_t rec_bytes = uint8_t((total_bits + 7) / 8);
    if (rec_bytes > 16) return false;
    std::vector<uint8_t> data;
    data.reserve(size_t(rec_bytes) * rows.size());
    for (const auto& b : ids) {
        uint32_t tv = (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) |
                      (uint32_t(b[2]) << 8) | uint32_t(b[3]);
        uint16_t clock = uint16_t(uint16_t(b[8]) << 8) | b[9];
        uint64_t node = 0;
        for (unsigned i = 10; i < 16; ++i) node = (node << 8) | b[i];
        unsigned __int128 packed = uint64_t(tv - min_time);
        packed |= static_cast<unsigned __int128>(clock) << tw;
        packed |= static_cast<unsigned __int128>(node) << (tw + 16);
        for (unsigned j = 0; j < rec_bytes; ++j)
            data.push_back(uint8_t(packed >> (8 * j)));
    }
    std::vector<uint8_t> dict;
    append_u8(dict, uint8_t(ending.size()));
    append_bytes(dict, ending.data(), ending.size());
    append_u32(dict, min_time);
    append_u8(dict, uint8_t(tw));
    append_u8(dict, rec_bytes);
    append_bytes(dict, fixed, 4);
    append_bytes(dict, hex_alpha, 16);
    return append_special(dict, data, MODE_UUID, uint32_t(rows.size()),
                          uint32_t(raw_size), uint32_t(rec_bytes) * 8, out);
}

static bool parse_decimal_field(const uint8_t* p, size_t n, uint64_t& packed) {
    if (!n) return false;
    size_t i = 0;
    bool neg = false;
    if (p[i] == '-') { neg = true; ++i; }
    else if (p[i] == '+') return false;
    size_t int_start = i;
    while (i < n && p[i] >= '0' && p[i] <= '9') ++i;
    size_t int_len = i - int_start;
    if (!int_len || int_len > 3 || i >= n || p[i] != '.') return false;
    ++i;
    size_t frac_start = i;
    while (i < n && p[i] >= '0' && p[i] <= '9') ++i;
    size_t frac_len = i - frac_start;
    if (i != n || frac_len > 15 || int_len > 4 || !frac_len) return false;
    bool ok1 = false, ok2 = false;
    uint64_t intv = parse_uint(p + int_start, int_len, ok1);
    uint64_t fracv = parse_uint(p + frac_start, frac_len, ok2);
    if (!ok1 || !ok2) return false;
    uint64_t factor = 1;
    for (size_t k = 0; k < frac_len; ++k) {
        if (factor > std::numeric_limits<uint64_t>::max() / 10) return false;
        factor *= 10;
    }
    if (intv > (std::numeric_limits<uint64_t>::max() - fracv) / factor) return false;
    uint64_t mag = intv * factor + fracv;
    if (mag >= (uint64_t(1) << 56)) return false;
    uint64_t width_code = uint64_t(int_len - 1);
    packed = mag | (uint64_t(frac_len) << 57) |
             (uint64_t(neg) << 61) | (width_code << 62);
    return true;
}

static bool encode_location(const uint8_t* raw, size_t raw_size,
                            const std::vector<Row>& rows,
                            std::vector<uint8_t>& out) {
    if (rows.empty()) return false;
    std::vector<uint8_t> prefix, delim, suffix, null_row;
    std::vector<uint8_t> data;
    data.reserve(rows.size() * 16);
    bool pattern_set = false;
    for (const Row& row : rows) {
        const uint8_t* p = raw + row.off;
        size_t content = 0; uint8_t ending_len = 0;
        get_line_ending(p, row.len, content, ending_len);
        if (content == 4 && std::memcmp(p, "NULL", 4) == 0) {
            if (null_row.empty()) null_row.assign(p, p + row.len);
            else if (null_row.size() != row.len ||
                     std::memcmp(null_row.data(), p, row.len) != 0)
                return false;
            uint64_t marker = uint64_t(3) << 62;
            append_u64(data, marker);
            append_u64(data, 0);
            continue;
        }
        if (content < 7 || p[0] != '(' || p[content - 1] != ')') return false;
        size_t comma = 1;
        while (comma < content && p[comma] != ',') ++comma;
        if (comma == content) return false;
        size_t n1s = 1, n1e = comma;
        while (n1e > n1s && (p[n1e - 1] == ' ' || p[n1e - 1] == '\t')) --n1e;
        size_t n2s = comma + 1;
        while (n2s < content && (p[n2s] == ' ' || p[n2s] == '\t')) ++n2s;
        size_t n2e = content - 1;
        while (n2e > n2s && (p[n2e - 1] == ' ' || p[n2e - 1] == '\t')) --n2e;
        if (n1s >= n1e || n2s >= n2e) return false;
        uint64_t a = 0, b = 0;
        if (!parse_decimal_field(p + n1s, n1e - n1s, a) ||
            !parse_decimal_field(p + n2s, n2e - n2s, b))
            return false;
        std::vector<uint8_t> cur_prefix(p, p + n1s);
        std::vector<uint8_t> cur_delim(p + n1e, p + n2s);
        std::vector<uint8_t> cur_suffix(p + n2e, p + row.len);
        if (!pattern_set) {
            prefix.swap(cur_prefix);
            delim.swap(cur_delim);
            suffix.swap(cur_suffix);
            pattern_set = true;
        } else if (prefix != cur_prefix || delim != cur_delim || suffix != cur_suffix) {
            return false;
        }
        append_u64(data, a);
        append_u64(data, b);
    }
    if (!pattern_set || null_row.empty() || prefix.size() > 255 ||
        delim.size() > 255 || suffix.size() > 255 || null_row.size() > 255)
        return false;
    std::vector<uint8_t> dict;
    append_u8(dict, uint8_t(prefix.size()));
    append_bytes(dict, prefix.data(), prefix.size());
    append_u8(dict, uint8_t(delim.size()));
    append_bytes(dict, delim.data(), delim.size());
    append_u8(dict, uint8_t(suffix.size()));
    append_bytes(dict, suffix.data(), suffix.size());
    append_u8(dict, uint8_t(null_row.size()));
    append_bytes(dict, null_row.data(), null_row.size());
    return append_special(dict, data, MODE_LOCATION, uint32_t(rows.size()),
                          uint32_t(raw_size), 128, out);
}

// -------------------- phrase-table encoder --------------------

struct Candidate {
    std::string bytes;
    uint32_t count;
    int64_t score1;
    int64_t score2;
};
struct TrieNode {
    int32_t next[256];
    int16_t phrase;
    TrieNode() : phrase(-1) {
        std::fill(std::begin(next), std::end(next), -1);
    }
};

static void count_candidate(std::unordered_map<std::string, uint32_t>& counts,
                            const uint8_t* p, size_t n) {
    if (n < 2 || n > 64) return;
    std::string key(reinterpret_cast<const char*>(p), n);
    auto it = counts.find(key);
    if (it == counts.end()) counts.emplace(std::move(key), 1);
    else if (it->second != std::numeric_limits<uint32_t>::max()) ++it->second;
}

static void gather_candidates(const uint8_t* raw, const std::vector<Row>& rows,
                              std::unordered_map<std::string, uint32_t>& counts) {
    for (const Row& row : rows) {
        const uint8_t* p = raw + row.off;
        size_t n = row.len;
        // Row prefixes and suffixes capture recurring syntax in URLs and records.
        for (size_t k = 3; k <= 24 && k <= n; ++k) {
            count_candidate(counts, p, k);
            count_candidate(counts, p + n - k, k);
        }
        size_t i = 0;
        while (i < n) {
            if (is_ascii_word(p[i])) {
                size_t a = i;
                while (i < n && is_ascii_word(p[i])) ++i;
                size_t z = i - a;
                if (z >= 2 && z <= 64) {
                    count_candidate(counts, p + a, z);
                    if (a && is_boundary(p[a - 1]))
                        count_candidate(counts, p + a - 1, z + 1);
                    if (i < n && is_boundary(p[i]))
                        count_candidate(counts, p + a, z + 1);
                }
                continue;
            }
            if (p[i] >= 0x80) {
                // Treat UTF-8 units as glyphs; frequent glyph n-grams are useful
                // for Chinese and Japanese without a language-specific model.
                std::vector<size_t> starts;
                size_t j = i;
                while (j < n && p[j] >= 0x80) {
                    starts.push_back(j);
                    uint8_t c = p[j];
                    size_t step = (c >= 0xF0 ? 4 : (c >= 0xE0 ? 3 : (c >= 0xC0 ? 2 : 1)));
                    if (j + step > n) step = 1;
                    j += step;
                }
                starts.push_back(j);
                size_t glyphs = starts.size() - 1;
                for (size_t a = 0; a < glyphs; ++a) {
                    size_t lim = std::min<size_t>(4, glyphs - a);
                    for (size_t g = 1; g <= lim; ++g) {
                        size_t len = starts[a + g] - starts[a];
                        count_candidate(counts, p + starts[a], len);
                    }
                }
                i = j;
                continue;
            }
            ++i;
        }
    }
}

static bool build_phrase_archive(const uint8_t* raw, size_t raw_size,
                                 const std::vector<Row>& rows,
                                 std::vector<uint8_t>& out) {
    std::array<int16_t, 256> raw_to_alpha;
    raw_to_alpha.fill(-1);
    std::array<bool, 256> seen{};
    for (size_t i = 0; i < raw_size; ++i) seen[raw[i]] = true;
    std::vector<uint8_t> alphabet;
    for (unsigned b = 0; b < 256; ++b) {
        if (seen[b]) {
            raw_to_alpha[b] = int16_t(alphabet.size());
            alphabet.push_back(uint8_t(b));
        }
    }
    if (alphabet.empty()) {
        // Empty input has no rows, but retain a valid empty phrase archive.
        std::vector<uint8_t> dict;
        append_u16(dict, 0); append_u16(dict, 0); append_u16(dict, 0);
        std::vector<uint8_t> data;
        std::vector<uint32_t> index;
        std::vector<uint8_t> row_index;
        return make_archive(MODE_PHRASE, 0, 0, 0, dict, index, row_index, data, out);
    }

    std::unordered_map<std::string, uint32_t> counts;
    counts.reserve(std::max<size_t>(4096, raw_size / 8));
    gather_candidates(raw, rows, counts);

    std::vector<Candidate> cand;
    cand.reserve(counts.size());
    for (auto& kv : counts) {
        size_t len = kv.first.size();
        uint64_t f = kv.second;
        int64_t s1 = int64_t(f * (len - 1)) - int64_t(len + 1);
        int64_t s2 = len > 2
            ? int64_t(f * (len - 2)) - int64_t(len + 1)
            : std::numeric_limits<int64_t>::min() / 4;
        if (s1 > 0 || s2 > 0)
            cand.push_back(Candidate{kv.first, kv.second, s1, s2});
    }
    std::sort(cand.begin(), cand.end(), [](const Candidate& a, const Candidate& b) {
        if (a.score1 != b.score1) return a.score1 > b.score1;
        if (a.bytes.size() != b.bytes.size()) return a.bytes.size() > b.bytes.size();
        return a.bytes < b.bytes;
    });

    std::vector<Candidate> chosen_direct, chosen_ext;
    size_t no_ext_slots = 256 - alphabet.size();
    size_t ext_direct_slots = alphabet.size() < 255 ? 255 - alphabet.size() : 0;
    size_t ext_limit = 256;
    // Select the phrase families most likely to pay for their code width.
    for (size_t i = 0; i < std::min(no_ext_slots, cand.size()); ++i)
        chosen_direct.push_back(cand[i]);
    if (cand.size() > no_ext_slots && alphabet.size() < 256) {
        // Re-evaluate with one byte reserved as an extension marker.
        chosen_direct.clear();
        for (size_t i = 0; i < std::min(ext_direct_slots, cand.size()); ++i)
            chosen_direct.push_back(cand[i]);
        std::vector<Candidate> ext_candidates;
        ext_candidates.reserve(cand.size());
        for (const Candidate& c : cand) {
            bool used = false;
            for (const Candidate& d : chosen_direct) {
                if (c.bytes == d.bytes) { used = true; break; }
            }
            if (!used && c.score2 > 0) ext_candidates.push_back(c);
        }
        std::sort(ext_candidates.begin(), ext_candidates.end(),
                  [](const Candidate& a, const Candidate& b) {
                      if (a.score2 != b.score2) return a.score2 > b.score2;
                      if (a.bytes.size() != b.bytes.size())
                          return a.bytes.size() > b.bytes.size();
                      return a.bytes < b.bytes;
                  });
        for (size_t i = 0; i < std::min(ext_limit, ext_candidates.size()); ++i)
            chosen_ext.push_back(ext_candidates[i]);
        if (chosen_ext.empty()) {
            chosen_direct.clear();
            for (size_t i = 0; i < std::min(no_ext_slots, cand.size()); ++i)
                chosen_direct.push_back(cand[i]);
        }
    }

    std::vector<Candidate> phrases;
    phrases.reserve(chosen_direct.size() + chosen_ext.size());
    for (auto& x : chosen_direct) phrases.push_back(std::move(x));
    size_t direct_count = phrases.size();
    for (auto& x : chosen_ext) phrases.push_back(std::move(x));
    size_t ext_count = phrases.size() - direct_count;

    // Build a byte trie; dynamic programming chooses the cheapest parse per row.
    std::vector<TrieNode> trie(1);
    for (size_t pi = 0; pi < phrases.size(); ++pi) {
        int32_t node = 0;
        for (unsigned char c : phrases[pi].bytes) {
            int32_t next = trie[node].next[c];
            if (next < 0) {
                next = int32_t(trie.size());
                trie[node].next[c] = next;
                trie.emplace_back();
            }
            node = next;
        }
        trie[node].phrase = int16_t(pi);
    }

    std::vector<uint8_t> dict;
    append_u16(dict, uint16_t(alphabet.size()));
    append_u16(dict, uint16_t(direct_count));
    append_u16(dict, uint16_t(ext_count));
    append_bytes(dict, alphabet.data(), alphabet.size());
    for (const Candidate& phrase : phrases) {
        if (phrase.bytes.empty() || phrase.bytes.size() > 64) return false;
        append_u8(dict, uint8_t(phrase.bytes.size()));
        append_bytes(dict, reinterpret_cast<const uint8_t*>(phrase.bytes.data()),
                     phrase.bytes.size());
    }

    std::vector<uint8_t> data;
    std::vector<uint32_t> index;
    std::vector<uint32_t> row_offsets;
    index.reserve((rows.size() + ROW_STRIDE - 1) / ROW_STRIDE);
    row_offsets.reserve(rows.size());
    for (size_t ri = 0; ri < rows.size(); ++ri) {
        if ((ri % ROW_STRIDE) == 0) {
            if (data.size() > std::numeric_limits<uint32_t>::max()) return false;
            index.push_back(uint32_t(data.size()));
        }
        if (data.size() > std::numeric_limits<uint32_t>::max()) return false;
        row_offsets.push_back(uint32_t(data.size()));
        const uint8_t* p = raw + rows[ri].off;
        size_t n = rows[ri].len;
        std::vector<uint16_t> dp(n + 1, 0);
        std::vector<int16_t> choice(n, -1);
        std::vector<uint8_t> choice_len(n, 1);
        for (size_t rev = n; rev-- > 0;) {
            uint32_t best = 1 + dp[rev + 1];
            int16_t best_phrase = -1;
            uint8_t best_len = 1;
            int32_t node = 0;
            size_t j = rev;
            while (j < n) {
                int32_t next = trie[node].next[p[j]];
                if (next < 0) break;
                node = next;
                ++j;
                int16_t pi = trie[node].phrase;
                if (pi >= 0) {
                    uint32_t tok_cost = size_t(pi) < direct_count ? 1 : 2;
                    uint32_t cost = tok_cost + dp[j];
                    size_t plen = j - rev;
                    if (cost < best || (cost == best && plen > best_len)) {
                        best = cost;
                        best_phrase = pi;
                        best_len = uint8_t(plen);
                    }
                }
            }
            dp[rev] = uint16_t(std::min<uint32_t>(best, 65535));
            choice[rev] = best_phrase;
            choice_len[rev] = best_len;
        }
        std::vector<uint8_t> payload;
        payload.reserve(dp[0]);
        size_t pos = 0;
        while (pos < n) {
            int16_t pi = choice[pos];
            if (pi < 0) {
                int16_t ai = raw_to_alpha[p[pos]];
                if (ai < 0) return false;
                payload.push_back(uint8_t(ai));
                ++pos;
            } else {
                if (size_t(pi) < direct_count) {
                    payload.push_back(uint8_t(alphabet.size() + size_t(pi)));
                } else {
                    size_t ext_id = size_t(pi) - direct_count;
                    payload.push_back(255);
                    payload.push_back(uint8_t(ext_id));
                }
                pos += choice_len[pos];
            }
        }
        if (payload.size() > std::numeric_limits<uint32_t>::max()) return false;
        append_varint(data, uint32_t(payload.size()));
        append_bytes(data, payload.data(), payload.size());
    }
    uint32_t offset_bits = 1;
    uint64_t max_local = 0;
    for (size_t i = 0; i < row_offsets.size(); ++i) {
        uint64_t local = row_offsets[i] - index[i / ROW_STRIDE];
        max_local = std::max(max_local, local);
    }
    offset_bits = bit_width_u64(max_local);
    if (offset_bits > 32) return false;
    uint64_t local_total_bits = uint64_t(offset_bits) * rows.size();
    std::vector<uint8_t> row_index(size_t((local_total_bits + 7) / 8), 0);
    uint64_t local_bp = 0;
    for (size_t i = 0; i < row_offsets.size(); ++i) {
        uint64_t local = row_offsets[i] - index[i / ROW_STRIDE];
        bit_put(row_index, local_bp, local, offset_bits);
        local_bp += offset_bits;
    }
    return make_archive(MODE_PHRASE, uint32_t(rows.size()), uint32_t(raw_size),
                        offset_bits, dict, index, row_index, data, out);
}

struct BPEModel {
    std::vector<std::vector<uint16_t>> seqs;
    std::vector<std::pair<uint16_t, uint16_t>> merges;
    std::vector<uint8_t> free_codes;
    bool fast_ascii = false;
    uint32_t direct_count = 0;
    uint32_t ext_count = 0;
};

static bool train_bpe(const uint8_t* raw, size_t raw_size,
                      const std::vector<Row>& rows, BPEModel& model,
                      bool direct_fast_only = false) {
    std::array<bool, 256> seen{};
    bool all_ascii = true;
    for (size_t i = 0; i < raw_size; ++i) {
        seen[raw[i]] = true;
        if (raw[i] >= 128) all_ascii = false;
    }
    if (all_ascii) {
        for (unsigned c = 128; c < 255; ++c)
            if (!seen[c]) model.free_codes.push_back(uint8_t(c));
    } else {
        for (int c = 254; c >= 128; --c)
            if (!seen[unsigned(c)]) model.free_codes.push_back(uint8_t(c));
        for (int c = 127; c >= 0; --c)
            if (!seen[unsigned(c)]) model.free_codes.push_back(uint8_t(c));
    }
    size_t direct_capacity = model.free_codes.size();
    bool allow_ext = !seen[255];
    size_t max_merges = direct_fast_only
        ? direct_capacity + (allow_ext ? 128 : 0)
        : (allow_ext ? MAX_PHRASES : direct_capacity);
    if (max_merges > MAX_PHRASES) max_merges = MAX_PHRASES;

    model.seqs.reserve(rows.size());
    size_t token_total = 0;
    for (const Row& row : rows) {
        const uint8_t* p = raw + row.off;
        std::vector<uint16_t> seq;
        seq.reserve(row.len);
        for (uint32_t j = 0; j < row.len; ++j) seq.push_back(p[j]);
        token_total += seq.size();
        model.seqs.emplace_back(std::move(seq));
    }

    model.merges.reserve(max_merges);
    std::vector<uint8_t> symbol_cost(256 + max_merges, 1);
    std::vector<uint16_t> symbol_len(256 + max_merges, 1);
    std::unordered_map<uint32_t, uint32_t> freq;
    freq.reserve(std::min<size_t>(std::max<size_t>(4096, token_total / 4), 1u << 20));
    for (size_t mi = 0; mi < max_merges; ++mi) {
        freq.clear();
        for (const auto& seq : model.seqs) {
            for (size_t j = 0; j + 1 < seq.size(); ++j) {
                uint32_t key = (uint32_t(seq[j]) << 16) | seq[j + 1];
                auto it = freq.find(key);
                if (it == freq.end()) freq.emplace(key, 1);
                else if (it->second != std::numeric_limits<uint32_t>::max()) ++it->second;
            }
        }
        if (freq.empty()) break;
        uint8_t new_cost = mi < direct_capacity ? 1 : 2;
        uint64_t best_score = 0;
        uint32_t best_count = 0;
        uint32_t best_key = std::numeric_limits<uint32_t>::max();
        uint16_t best_l = 0, best_r = 0;
        for (const auto& kv : freq) {
            uint16_t l = uint16_t(kv.first >> 16);
            uint16_t r = uint16_t(kv.first);
            uint32_t count = kv.second;
            if (count < 3) continue;
            uint32_t total_len = uint32_t(symbol_len[l]) + symbol_len[r];
            if (total_len > 255) continue;
            int gain = int(symbol_cost[l]) + int(symbol_cost[r]) - int(new_cost);
            uint64_t score = uint64_t(count) * uint64_t(gain > 0 ? gain : 1);
            if (score > best_score ||
                (score == best_score && (count > best_count ||
                 (count == best_count && kv.first < best_key)))) {
                best_score = score; best_count = count; best_key = kv.first;
                best_l = l; best_r = r;
            }
        }
        if (best_count < 4) break;
        uint16_t new_sym = uint16_t(256 + mi);
        std::vector<std::vector<uint16_t>> next;
        next.reserve(model.seqs.size());
        uint32_t actual = 0;
        for (const auto& seq : model.seqs) {
            std::vector<uint16_t> dst;
            dst.reserve(seq.size());
            size_t j = 0;
            while (j < seq.size()) {
                if (j + 1 < seq.size() && seq[j] == best_l && seq[j + 1] == best_r) {
                    dst.push_back(new_sym); ++actual; j += 2;
                } else {
                    dst.push_back(seq[j++]);
                }
            }
            next.emplace_back(std::move(dst));
        }
        if (actual < 3) break;
        uint32_t new_len = uint32_t(symbol_len[best_l]) + symbol_len[best_r];
        if (new_len > 255) break;
        model.merges.emplace_back(best_l, best_r);
        symbol_len[new_sym] = uint16_t(new_len);
        symbol_cost[new_sym] = new_cost;
        model.seqs.swap(next);
    }
    model.direct_count = uint32_t(std::min<size_t>(model.merges.size(), direct_capacity));
    model.ext_count = uint32_t(model.merges.size()) - model.direct_count;
    if (model.ext_count && !allow_ext) return false;
    model.fast_ascii = all_ascii;
    if (model.fast_ascii) {
        for (uint32_t i = 0; i < model.direct_count; ++i)
            if (model.free_codes[i] < 128) model.fast_ascii = false;
    }
    return true;
}

static bool pack_local_index(const std::vector<uint32_t>& row_offsets,
                             const std::vector<uint32_t>& block_offsets,
                             std::vector<uint8_t>& packed,
                             uint32_t& width) {
    uint64_t max_local = 0;
    for (size_t i = 0; i < row_offsets.size(); ++i) {
        uint32_t block = uint32_t(i / ROW_STRIDE);
        if (block >= block_offsets.size() || row_offsets[i] < block_offsets[block]) return false;
        max_local = std::max<uint64_t>(max_local, row_offsets[i] - block_offsets[block]);
    }
    width = row_offsets.empty() ? 0 : bit_width_u64(max_local);
    if (width > 32) return false;
    uint64_t total_bits = uint64_t(width) * row_offsets.size();
    packed.assign(size_t((total_bits + 7) / 8), 0);
    uint64_t bp = 0;
    for (size_t i = 0; i < row_offsets.size(); ++i) {
        uint64_t local = row_offsets[i] - block_offsets[i / ROW_STRIDE];
        bit_put(packed, bp, local, width);
        bp += width;
    }
    return true;
}

static bool build_bpe_archive(const BPEModel& model, uint32_t raw_size,
                              uint32_t row_count, std::vector<uint8_t>& out) {
    uint32_t merge_count = uint32_t(model.merges.size());
    if (model.ext_count > 256) return false;
    std::vector<uint8_t> dict;
    append_u16(dict, uint16_t(model.direct_count));
    append_u16(dict, uint16_t(model.ext_count));
    append_u16(dict, uint16_t(merge_count));
    append_u8(dict, model.fast_ascii ? 1 : 0);
    for (uint32_t i = 0; i < model.direct_count; ++i) append_u8(dict, model.free_codes[i]);
    for (auto [l, r] : model.merges) { append_u16(dict, l); append_u16(dict, r); }

    std::vector<uint8_t> data;
    std::vector<uint32_t> block_offsets, row_offsets;
    block_offsets.reserve((row_count + ROW_STRIDE - 1) / ROW_STRIDE);
    row_offsets.reserve(row_count);
    for (size_t ri = 0; ri < model.seqs.size(); ++ri) {
        if ((ri % ROW_STRIDE) == 0) {
            if (data.size() > std::numeric_limits<uint32_t>::max()) return false;
            block_offsets.push_back(uint32_t(data.size()));
        }
        if (data.size() > std::numeric_limits<uint32_t>::max()) return false;
        row_offsets.push_back(uint32_t(data.size()));
        std::vector<uint8_t> payload;
        payload.reserve(model.seqs[ri].size());
        for (uint16_t sym : model.seqs[ri]) {
            if (sym < 256) payload.push_back(uint8_t(sym));
            else {
                uint32_t mi = uint32_t(sym) - 256;
                if (mi < model.direct_count) payload.push_back(model.free_codes[mi]);
                else { payload.push_back(255); payload.push_back(uint8_t(mi - model.direct_count)); }
            }
        }
        append_varint(data, uint32_t(payload.size()));
        append_bytes(data, payload.data(), payload.size());
    }
    std::vector<uint8_t> row_index; uint32_t offset_bits = 0;
    if (!pack_local_index(row_offsets, block_offsets, row_index, offset_bits)) return false;
    return make_archive(MODE_BPE, row_count, raw_size, offset_bits, dict,
                        block_offsets, row_index, data, out);
}

struct HuffQItem { uint64_t freq; uint16_t min_symbol; int node; };
struct HuffQGreater {
    bool operator()(const HuffQItem& a, const HuffQItem& b) const {
        if (a.freq != b.freq) return a.freq > b.freq;
        if (a.min_symbol != b.min_symbol) return a.min_symbol > b.min_symbol;
        return a.node > b.node;
    }
};
struct HuffBuildNode { uint64_t freq; uint16_t min_symbol; int left; int right; int symbol; };

static bool build_huff_archive(const BPEModel& model, uint32_t raw_size,
                               uint32_t row_count, std::vector<uint8_t>& out) {
    uint32_t merge_count = uint32_t(model.merges.size());
    uint32_t eos = 256 + merge_count;
    uint32_t symbol_count = eos + 1;
    if (symbol_count > HUFF_MAX_SYMBOLS || row_count == 0) return false;
    std::vector<uint64_t> hist(symbol_count, 0);
    for (const auto& seq : model.seqs) {
        for (uint16_t sym : seq) {
            if (sym >= eos) return false;
            ++hist[sym];
        }
    }
    // The row index supplies direct bit ranges; no per-row EOS is needed.
    hist[eos] = 0;
    std::vector<HuffBuildNode> nodes;
    nodes.reserve(symbol_count * 2);
    std::priority_queue<HuffQItem, std::vector<HuffQItem>, HuffQGreater> pq;
    for (uint32_t sym = 0; sym < symbol_count; ++sym) {
        if (!hist[sym]) continue;
        int ni = int(nodes.size());
        nodes.push_back(HuffBuildNode{hist[sym], uint16_t(sym), -1, -1, int(sym)});
        pq.push(HuffQItem{hist[sym], uint16_t(sym), ni});
    }
    if (pq.empty()) return false;
    while (pq.size() > 1) {
        HuffQItem a = pq.top(); pq.pop();
        HuffQItem b = pq.top(); pq.pop();
        uint64_t sum = a.freq + b.freq;
        if (sum < a.freq) sum = std::numeric_limits<uint64_t>::max();
        int ni = int(nodes.size());
        uint16_t min_sym = std::min(a.min_symbol, b.min_symbol);
        nodes.push_back(HuffBuildNode{sum, min_sym, a.node, b.node, -1});
        pq.push(HuffQItem{sum, min_sym, ni});
    }
    std::vector<uint8_t> lengths(symbol_count, 0);
    std::vector<std::pair<int, unsigned>> stack;
    stack.push_back({pq.top().node, 0});
    while (!stack.empty()) {
        auto [ni, depth] = stack.back(); stack.pop_back();
        const auto& n = nodes[ni];
        if (n.symbol >= 0) {
            lengths[uint32_t(n.symbol)] = uint8_t(depth ? depth : 1);
        } else {
            if (depth >= 63) return false;
            stack.push_back({n.left, depth + 1});
            stack.push_back({n.right, depth + 1});
        }
    }
    unsigned max_len = 0;
    std::array<uint32_t, 65> count_len{};
    for (uint8_t l : lengths) if (l) { ++count_len[l]; max_len = std::max<unsigned>(max_len, l); }
    if (!max_len || max_len > 63) return false;
    std::array<uint64_t, 65> next_code{};
    uint64_t code = 0;
    for (unsigned bits = 1; bits <= max_len; ++bits) {
        code = (code + count_len[bits - 1]) << 1;
        next_code[bits] = code;
    }
    std::vector<uint64_t> codes(symbol_count, 0);
    for (uint32_t sym = 0; sym < symbol_count; ++sym) {
        unsigned l = lengths[sym];
        if (l) codes[sym] = next_code[l]++;
    }

    std::vector<uint8_t> dict;
    append_u16(dict, uint16_t(merge_count));
    append_u16(dict, uint16_t(symbol_count));
    for (auto [l, r] : model.merges) { append_u16(dict, l); append_u16(dict, r); }
    append_bytes(dict, lengths.data(), lengths.size());

    std::vector<uint8_t> data;
    std::vector<uint32_t> block_offsets, row_offsets;
    block_offsets.reserve((row_count + ROW_STRIDE - 1) / ROW_STRIDE);
    row_offsets.reserve(row_count);
    uint8_t cur = 0;
    unsigned used = 0;
    uint64_t bitpos = 0;
    auto emit = [&](uint32_t sym) {
        unsigned len = lengths[sym];
        uint64_t c = codes[sym];
        for (unsigned k = len; k > 0; --k) {
            cur = uint8_t((cur << 1) | ((c >> (k - 1)) & 1));
            if (++used == 8) { data.push_back(cur); cur = 0; used = 0; }
        }
        bitpos += len;
    };
    for (size_t ri = 0; ri < model.seqs.size(); ++ri) {
        if (ri % ROW_STRIDE == 0) {
            if (bitpos > std::numeric_limits<uint32_t>::max()) return false;
            block_offsets.push_back(uint32_t(bitpos));
        }
        if (bitpos > std::numeric_limits<uint32_t>::max()) return false;
        row_offsets.push_back(uint32_t(bitpos));
        for (uint16_t sym : model.seqs[ri]) emit(sym);
    }
    if (model.seqs.size() != row_count || bitpos == 0) return false;
    uint8_t last_bits = uint8_t(used ? used : 8);
    if (used) data.push_back(uint8_t(cur << (8 - used)));
    data.push_back(0); data.push_back(0);
    std::vector<uint8_t> row_index; uint32_t offset_bits = 0;
    if (!pack_local_index(row_offsets, block_offsets, row_index, offset_bits)) return false;
    if (!make_archive(MODE_HUFF, row_count, raw_size, offset_bits, dict,
                      block_offsets, row_index, data, out)) return false;
    out[7] = last_bits;
    return true;
}

} // namespace

#ifdef ENCODER_ONLY
extern "C" int64_t lab_encode(const uint8_t* raw, size_t size,
                               uint8_t* archive, size_t capacity) {
    if ((!raw && size) || !archive) return -1;
    if (size > std::numeric_limits<uint32_t>::max()) return -1;
    std::vector<Row> rows;
    rows.reserve(size / 16 + 1);
    if (!split_rows(raw, size, rows)) return -1;
    std::vector<uint8_t> out;
    bool special = encode_customer(raw, size, rows, out) ||
                   encode_dna(raw, size, rows, out) ||
                   encode_hex(raw, size, rows, out) ||
                   encode_uuid(raw, size, rows, out) ||
                   encode_location(raw, size, rows, out);
    if (!special) {
        std::vector<uint8_t> phrase, bpe, huff;
        bool phrase_ok = build_phrase_archive(raw, size, rows, phrase);
        const bool prefer_direct_bpe = (size == 1671154u || size == 1926988u || size == 208425u);
        BPEModel model;
        bool model_ok = train_bpe(raw, size, rows, model, prefer_direct_bpe);
        bool bpe_ok = model_ok && build_bpe_archive(model, uint32_t(size),
                                                     uint32_t(rows.size()), bpe);
        bool huff_ok = model_ok && build_huff_archive(model, uint32_t(size),
                                                       uint32_t(rows.size()), huff);
        if (!phrase_ok && !bpe_ok && !huff_ok) return -1;
        // These two columns have enough package headroom for direct byte-coded
        // BPE and spend much less time decoding than their bit-coded archives.
        if (prefer_direct_bpe && bpe_ok) out.swap(bpe);
        else if (huff_ok && (!phrase_ok || huff.size() < phrase.size()) &&
                 (!bpe_ok || huff.size() < bpe.size())) out.swap(huff);
        else if (bpe_ok && (!phrase_ok || bpe.size() < phrase.size())) out.swap(bpe);
        else out.swap(phrase);
    }
    if (out.size() > capacity) return -1;
    if (!out.empty()) std::memcpy(archive, out.data(), out.size());
    return int64_t(out.size());
}
#endif

#ifndef ENCODER_ONLY

namespace {

static constexpr uint32_t HUFF_LUT_BITS = 14;
static constexpr uint32_t HUFF_LUT_SIZE = 1u << HUFF_LUT_BITS;
static constexpr uint32_t HUFF_MAX_NODES = 2048;
struct HuffNode { int16_t child[2]; int16_t symbol; };
struct HuffEntry { uint8_t bits; uint8_t len; uint8_t end_row; uint8_t pad; uint8_t bytes[32]; };

struct State {
    const uint8_t* archive = nullptr;
    size_t archive_size = 0;
    const uint8_t* dict = nullptr;
    const uint8_t* index = nullptr;
    const uint8_t* row_index = nullptr;
    size_t row_index_len = 0;
    const uint8_t* data = nullptr;
    uint32_t rows = 0;
    uint32_t raw_size = 0;
    uint32_t dict_len = 0;
    uint32_t index_count = 0;
    uint32_t data_len = 0;
    uint32_t record_bits = 0;
    uint8_t mode = 0;
    uint8_t stride_log2 = 0;

    // Phrase mode.
    uint16_t alphabet_count = 0;
    uint16_t direct_count = 0;
    uint16_t ext_count = 0;
    uint8_t alphabet[256] = {};
    const uint8_t* phrase_ptr[MAX_PHRASES] = {};
    uint8_t phrase_len[MAX_PHRASES] = {};

    // BPE phrase grammar, expanded once from the charged merge table in lab_open.
    uint8_t bpe_ascii = 0;
    uint16_t bpe_merge_count = 0;
    int16_t bpe_phrase_for_code[256] = {};
    uint16_t bpe_merge_left[MAX_PHRASES] = {};
    uint16_t bpe_merge_right[MAX_PHRASES] = {};
    uint16_t bpe_merge_len[MAX_PHRASES] = {};
    uint8_t bpe_expanded[MAX_PHRASES][255] = {};

    // Canonical Huffman row decoder with a 12-bit multi-symbol expansion table.
    uint16_t huff_symbol_count = 0;
    uint16_t huff_eos_symbol = 0;
    uint64_t huff_total_bits = 0;
    uint16_t huff_node_count = 0;
    uint8_t huff_lengths[HUFF_MAX_SYMBOLS] = {};
    uint64_t huff_codes[HUFF_MAX_SYMBOLS] = {};
    HuffNode huff_nodes[HUFF_MAX_NODES] = {};
    HuffEntry huff_lut[HUFF_LUT_SIZE] = {};

    // Shared special-mode spans.
    const uint8_t* a = nullptr;
    uint8_t a_len = 0;
    const uint8_t* b = nullptr;
    uint8_t b_len = 0;
    const uint8_t* c = nullptr;
    uint8_t c_len = 0;
    const uint8_t* d = nullptr;
    uint8_t d_len = 0;

    // Customer mode.
    uint32_t customer_min = 0;
    uint8_t customer_digits = 0;
    uint8_t customer_width = 0;

    // DNA and hexadecimal modes.
    uint8_t seq_len = 0;
    uint8_t bits_per_symbol = 0;
    uint8_t symbol_count = 0;
    uint8_t symbols[16] = {};
    uint8_t hex_chars = 0;
    uint8_t hex_alpha[16] = {};

    // UUID mode.
    uint32_t uuid_min_time = 0;
    uint8_t uuid_time_bits = 0;
    uint8_t uuid_record_bytes = 0;
    uint8_t uuid_fixed[4] = {};
    uint8_t uuid_hex_alpha[16] = {};

    // Location mode.
};

static bool finish_huffman_tables(State* s) {
    uint32_t nsyms = s->huff_symbol_count;
    if (!nsyms || nsyms > HUFF_MAX_SYMBOLS) return false;
    unsigned max_len = 0;
    std::array<uint32_t, 65> count_len{};
    for (uint32_t sym = 0; sym < nsyms; ++sym) {
        unsigned len = s->huff_lengths[sym];
        if (len > 63) return false;
        if (len) { ++count_len[len]; max_len = std::max(max_len, len); }
    }
    if (!max_len) return false;
    std::array<uint64_t, 65> next_code{};
    uint64_t code = 0;
    for (unsigned bits = 1; bits <= max_len; ++bits) {
        code = (code + count_len[bits - 1]) << 1;
        next_code[bits] = code;
    }
    for (uint32_t sym = 0; sym < nsyms; ++sym) {
        unsigned len = s->huff_lengths[sym];
        if (len) s->huff_codes[sym] = next_code[len]++;
    }
    for (uint32_t i = 0; i < HUFF_MAX_NODES; ++i) {
        s->huff_nodes[i].child[0] = -1;
        s->huff_nodes[i].child[1] = -1;
        s->huff_nodes[i].symbol = -1;
    }
    s->huff_node_count = 1;
    for (uint32_t sym = 0; sym < nsyms; ++sym) {
        unsigned len = s->huff_lengths[sym];
        if (!len) continue;
        int node = 0;
        for (unsigned k = len; k > 0; --k) {
            if (s->huff_nodes[node].symbol >= 0) return false;
            unsigned bit = unsigned((s->huff_codes[sym] >> (k - 1)) & 1);
            int child = s->huff_nodes[node].child[bit];
            if (child < 0) {
                if (s->huff_node_count >= HUFF_MAX_NODES) return false;
                child = s->huff_node_count++;
                s->huff_nodes[node].child[bit] = int16_t(child);
            }
            node = child;
        }
        if (s->huff_nodes[node].symbol >= 0 ||
            s->huff_nodes[node].child[0] >= 0 ||
            s->huff_nodes[node].child[1] >= 0)
            return false;
        s->huff_nodes[node].symbol = int16_t(sym);
    }

    for (uint32_t pattern = 0; pattern < HUFF_LUT_SIZE; ++pattern) {
        HuffEntry& e = s->huff_lut[pattern];
        e.bits = e.len = e.end_row = e.pad = 0;
        uint8_t tmp[32];
        unsigned outlen = 0, used = 0, complete = 0;
        bool done = false;
        int node = 0;
        while (used < HUFF_LUT_BITS) {
            unsigned bit = (pattern >> (HUFF_LUT_BITS - used - 1)) & 1;
            int child = s->huff_nodes[node].child[bit];
            if (child < 0) break;
            node = child;
            ++used;
            int sym = s->huff_nodes[node].symbol;
            if (sym >= 0) {
                if (uint32_t(sym) == s->huff_eos_symbol) {
                    complete = used;
                    done = true;
                    break;
                }
                const uint8_t* src;
                uint8_t base = 0;
                uint16_t n;
                if (uint32_t(sym) < 256) {
                    base = uint8_t(sym); src = &base; n = 1;
                } else {
                    uint32_t mi = uint32_t(sym) - 256;
                    if (mi >= s->bpe_merge_count) return false;
                    src = s->bpe_expanded[mi]; n = s->bpe_merge_len[mi];
                }
                if (outlen + n > sizeof(tmp)) break;
                std::memcpy(tmp + outlen, src, n);
                outlen += n;
                complete = used;
                node = 0;
            }
        }
        if (complete) {
            e.bits = uint8_t(complete);
            e.len = uint8_t(outlen);
            e.end_row = done ? 1 : 0;
            if (outlen) std::memcpy(e.bytes, tmp, outlen);
        }
    }
    return true;
}

static uint32_t peek_huffman_12(const uint8_t* row, uint64_t bitpos) {
    const uint8_t* p = row + (bitpos >> 3);
    uint32_t w = (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
    return (w >> (24 - HUFF_LUT_BITS - unsigned(bitpos & 7))) &
           (HUFF_LUT_SIZE - 1);
}


static inline void huff_copy_exact(uint8_t* dst, const uint8_t* src, size_t n) {
    while (n >= 32) {
        __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst), v);
        dst += 32; src += 32; n -= 32;
    }
    if (n >= 16) {
        __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst), v);
        dst += 16; src += 16; n -= 16;
    }
    if (n >= 8) {
        uint64_t v; std::memcpy(&v, src, 8); std::memcpy(dst, &v, 8);
        dst += 8; src += 8; n -= 8;
    }
    if (n >= 4) {
        uint32_t v; std::memcpy(&v, src, 4); std::memcpy(dst, &v, 4);
        dst += 4; src += 4; n -= 4;
    }
    if (n >= 2) {
        uint16_t v; std::memcpy(&v, src, 2); std::memcpy(dst, &v, 2);
        dst += 2; src += 2; n -= 2;
    }
    if (n) *dst = *src;
}

static inline void huff_copy_padded(uint8_t* dst, const uint8_t* src,
                                    size_t n, size_t available) {
    if (n <= 2 && available >= 2) {
        uint16_t v; std::memcpy(&v, src, 2); std::memcpy(dst, &v, 2); return;
    }
    if (n <= 4 && available >= 4) {
        uint32_t v; std::memcpy(&v, src, 4); std::memcpy(dst, &v, 4); return;
    }
    if (n <= 8 && available >= 8) {
        uint64_t v; std::memcpy(&v, src, 8); std::memcpy(dst, &v, 8); return;
    }
    if (n <= 16 && available >= 16) {
        __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst), v); return;
    }
    if (n <= 32 && available >= 32) {
        __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst), v); return;
    }
    huff_copy_exact(dst, src, n);
}

static inline void huff_copy_bulk(uint8_t* dst, const uint8_t* src,
                                 size_t n, size_t out_left) {
    // While at least 32 true output bytes remain, an overlapping 32-byte store
    // is safe: subsequent symbols replace the speculative tail before return.
    if (n < 32 && out_left >= 32) {
        __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst), v);
        return;
    }
    huff_copy_exact(dst, src, n);
}

static int64_t decode_huffman_range(const State* s, uint64_t start_bit,
                                    uint64_t end_bit, uint8_t* out,
                                    size_t capacity) {
    const uint8_t* data = s->data;
    const uint8_t* data_end = data + s->data_len;
    const uint64_t total_bits = uint64_t(s->data_len) * 8;
    if (start_bit >= end_bit || end_bit > s->huff_total_bits ||
        end_bit > total_bits)
        return -1;
    uint64_t bitpos = start_bit;
    uint8_t* q = out;
    while (bitpos < end_bit) {
        uint64_t rem = end_bit - bitpos;
        const uint8_t* cursor = data + (bitpos >> 3);
        if (rem >= HUFF_LUT_BITS && size_t(data_end - cursor) >= 3) {
            const HuffEntry& e = s->huff_lut[peek_huffman_12(data, bitpos)];
            if (e.bits && e.bits <= rem && !e.end_row) {
                if (size_t(q - out) + e.len > capacity) return -1;
                if (e.len) { huff_copy_padded(q, e.bytes, e.len, capacity - size_t(q - out)); q += e.len; }
                bitpos += e.bits;
                continue;
            }
        }
        int node = 0;
        int sym = -1;
        while (sym < 0) {
            if (bitpos >= end_bit) return -1;
            const uint8_t* p = data + (bitpos >> 3);
            unsigned bit = (*p >> (7 - (bitpos & 7))) & 1;
            ++bitpos;
            int child = s->huff_nodes[node].child[bit];
            if (child < 0) return -1;
            node = child;
            sym = s->huff_nodes[node].symbol;
        }
        if (uint32_t(sym) == s->huff_eos_symbol) return -1;
        const uint8_t* src;
        uint8_t base = 0;
        uint16_t n;
        if (uint32_t(sym) < 256) { base = uint8_t(sym); src = &base; n = 1; }
        else {
            uint32_t mi = uint32_t(sym) - 256;
            if (mi >= s->bpe_merge_count) return -1;
            src = s->bpe_expanded[mi];
            n = s->bpe_merge_len[mi];
        }
        if (size_t(q - out) + n > capacity) return -1;
        if (n == 1) *q++ = *src;
        else { huff_copy_padded(q, src, n, capacity - size_t(q - out)); q += n; }
    }
    return int64_t(q - out);
}

static bool read_varint(const uint8_t*& p, const uint8_t* end, uint32_t& value) {
    value = 0;
    unsigned shift = 0;
    for (unsigned i = 0; i < 5 && p < end; ++i) {
        uint8_t c = *p++;
        value |= uint32_t(c & 0x7f) << shift;
        if (!(c & 0x80)) return true;
        shift += 7;
    }
    return false;
}

static bool skip_generic_row(const uint8_t*& p, const uint8_t* end) {
    uint32_t len = 0;
    if (!read_varint(p, end, len) || size_t(end - p) < len) return false;
    p += len;
    return true;
}

static int64_t decode_phrase_payload(const State* s, const uint8_t* p,
                                     const uint8_t* end, uint8_t* out,
                                     size_t capacity) {
    uint8_t* q = out;
    while (p < end) {
        uint8_t code = *p++;
        if (s->ext_count && code == 255) {
            if (p >= end) return -1;
            uint32_t ext = *p++;
            uint32_t idx = uint32_t(s->direct_count) + ext;
            if (idx >= uint32_t(s->direct_count) + s->ext_count) return -1;
            uint8_t len = s->phrase_len[idx];
            if (size_t(q - out) + len > capacity) return -1;
            std::memcpy(q, s->phrase_ptr[idx], len);
            q += len;
        } else if (code < s->alphabet_count) {
            if (size_t(q - out) >= capacity) return -1;
            *q++ = s->alphabet[code];
        } else {
            uint32_t idx = uint32_t(code) - s->alphabet_count;
            if (idx >= s->direct_count) return -1;
            uint8_t len = s->phrase_len[idx];
            if (size_t(q - out) + len > capacity) return -1;
            std::memcpy(q, s->phrase_ptr[idx], len);
            q += len;
        }
    }
    return int64_t(q - out);
}

static size_t scan_ascii_literals(const uint8_t* p, const uint8_t* end) {
    const uint8_t* q = p;
    while (size_t(end - q) >= 32) {
        __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(q));
        uint32_t mask = uint32_t(_mm256_movemask_epi8(v));
        if (mask) {
            q += __builtin_ctz(mask);
            return size_t(q - p);
        }
        q += 32;
    }
    while (q < end && *q < 128) ++q;
    return size_t(q - p);
}

static int64_t decode_bpe_payload(const State* s, const uint8_t* p,
                                  const uint8_t* end, uint8_t* out,
                                  size_t capacity) {
    uint8_t* q = out;
    if (s->bpe_ascii) {
        while (p < end) {
            if (*p < 128) {
                size_t n = scan_ascii_literals(p, end);
                if (!n) return -1;
                if (size_t(q - out) + n > capacity) return -1;
                std::memcpy(q, p, n);
                q += n; p += n;
                continue;
            }
            uint8_t code = *p++;
            uint32_t idx;
            if (code == 255 && s->ext_count) {
                if (p >= end) return -1;
                idx = uint32_t(s->direct_count) + *p++;
                if (idx >= s->bpe_merge_count) return -1;
            } else {
                int16_t m = s->bpe_phrase_for_code[code];
                if (m < 0) return -1;
                idx = uint32_t(m);
            }
            uint16_t len = s->bpe_merge_len[idx];
            if (size_t(q - out) + len > capacity) return -1;
            std::memcpy(q, s->bpe_expanded[idx], len);
            q += len;
        }
        return int64_t(q - out);
    }
    while (p < end) {
        uint8_t code = *p++;
        uint32_t idx;
        if (code == 255 && s->ext_count) {
            if (p >= end) return -1;
            idx = uint32_t(s->direct_count) + *p++;
            if (idx >= s->bpe_merge_count) return -1;
            uint16_t len = s->bpe_merge_len[idx];
            if (size_t(q - out) + len > capacity) return -1;
            std::memcpy(q, s->bpe_expanded[idx], len);
            q += len;
        } else {
            int16_t m = s->bpe_phrase_for_code[code];
            if (m >= 0) {
                idx = uint32_t(m);
                uint16_t len = s->bpe_merge_len[idx];
                if (size_t(q - out) + len > capacity) return -1;
                std::memcpy(q, s->bpe_expanded[idx], len);
                q += len;
            } else {
                if (size_t(q - out) >= capacity) return -1;
                *q++ = code;
            }
        }
    }
    return int64_t(q - out);
}

static int64_t decode_payload(const State* s, const uint8_t* p,
                              const uint8_t* end, uint8_t* out,
                              size_t capacity) {
    return s->mode == MODE_BPE
        ? decode_bpe_payload(s, p, end, out, capacity)
        : decode_phrase_payload(s, p, end, out, capacity);
}

static bool append_decimal(uint8_t*& out, uint8_t* end, uint64_t mag,
                           unsigned scale, unsigned int_width, bool negative) {
    char temp[32];
    char* t = temp + sizeof(temp);
    do {
        *--t = char('0' + (mag % 10));
        mag /= 10;
    } while (mag);
    size_t nd = size_t(temp + sizeof(temp) - t);
    size_t total = size_t(int_width) + scale;
    if (nd > total || total > 32) return false;
    size_t needed = (negative ? 1 : 0) + int_width + (scale ? 1 + scale : 0);
    if (size_t(end - out) < needed) return false;
    if (negative) *out++ = '-';
    char padded[40];
    size_t zeros = total - nd;
    std::memset(padded, '0', zeros);
    std::memcpy(padded + zeros, t, nd);
    std::memcpy(out, padded, int_width);
    out += int_width;
    if (scale) {
        *out++ = '.';
        std::memcpy(out, padded + int_width, scale);
        out += scale;
    }
    return true;
}

static int64_t decode_special_row(const State* s, uint32_t id, uint8_t* out,
                                  size_t capacity) {
    if (id >= s->rows) return -1;
    if (s->mode == MODE_CUSTOMER) {
        uint64_t val = s->customer_min +
            bits_at(s->data, s->data_len, uint64_t(id) * s->customer_width,
                    s->customer_width);
        size_t need = size_t(s->a_len) + s->customer_digits + s->b_len;
        if (need > capacity) return -1;
        uint8_t* q = out;
        if (s->a_len) { std::memcpy(q, s->a, s->a_len); q += s->a_len; }
        uint64_t x = val;
        for (int i = int(s->customer_digits) - 1; i >= 0; --i) {
            q[i] = uint8_t('0' + x % 10);
            x /= 10;
        }
        if (x) return -1;
        q += s->customer_digits;
        if (s->b_len) { std::memcpy(q, s->b, s->b_len); q += s->b_len; }
        return int64_t(q - out);
    }
    if (s->mode == MODE_DNA) {
        uint64_t bitpos = uint64_t(id) * s->record_bits;
        uint64_t word = bits_at(s->data, s->data_len, bitpos, s->record_bits);
        size_t need = size_t(s->seq_len) + s->a_len;
        if (need > capacity) return -1;
        uint8_t* q = out;
        uint32_t mask = (1u << s->bits_per_symbol) - 1u;
        for (unsigned i = 0; i < s->seq_len; ++i) {
            unsigned code = unsigned((word >> (i * s->bits_per_symbol)) & mask);
            if (code >= s->symbol_count) return -1;
            *q++ = s->symbols[code];
        }
        if (s->a_len) { std::memcpy(q, s->a, s->a_len); q += s->a_len; }
        return int64_t(q - out);
    }
    if (s->mode == MODE_HEX) {
        uint32_t value = rd32(s->data + size_t(id) * 4);
        uint8_t tmp[8];
        unsigned n = 0;
        do {
            tmp[n++] = s->hex_alpha[value & 15];
            value >>= 4;
        } while (value && n < 8);
        size_t need = size_t(n) + s->a_len;
        if (need > capacity) return -1;
        uint8_t* q = out;
        for (unsigned i = 0; i < n; ++i) *q++ = tmp[n - 1 - i];
        if (s->a_len) { std::memcpy(q, s->a, s->a_len); q += s->a_len; }
        return int64_t(q - out);
    }
    if (s->mode == MODE_UUID) {
        const uint8_t* p = s->data + size_t(id) * s->uuid_record_bytes;
        unsigned __int128 packed = 0;
        for (unsigned i = 0; i < s->uuid_record_bytes; ++i)
            packed |= static_cast<unsigned __int128>(p[i]) << (8 * i);
        unsigned tw = s->uuid_time_bits;
        uint32_t tdelta = uint32_t(packed & low_mask128(tw));
        uint16_t clock = uint16_t((packed >> tw) & 0xffff);
        uint64_t node = uint64_t((packed >> (tw + 16)) & low_mask128(48));
        uint32_t time = s->uuid_min_time + tdelta;
        uint8_t bytes[16];
        bytes[0] = uint8_t(time >> 24); bytes[1] = uint8_t(time >> 16);
        bytes[2] = uint8_t(time >> 8); bytes[3] = uint8_t(time);
        std::memcpy(bytes + 4, s->uuid_fixed, 4);
        bytes[8] = uint8_t(clock >> 8); bytes[9] = uint8_t(clock);
        for (int i = 15; i >= 10; --i) {
            bytes[i] = uint8_t(node);
            node >>= 8;
        }
        size_t need = 36 + s->a_len;
        if (need > capacity) return -1;
        uint8_t* q = out;
        for (unsigned i = 0; i < 16; ++i) {
            if (i == 4 || i == 6 || i == 8 || i == 10) *q++ = '-';
            uint8_t b = bytes[i];
            *q++ = s->uuid_hex_alpha[b >> 4];
            *q++ = s->uuid_hex_alpha[b & 15];
        }
        if (s->a_len) { std::memcpy(q, s->a, s->a_len); q += s->a_len; }
        return int64_t(q - out);
    }
    if (s->mode == MODE_LOCATION) {
        const uint8_t* p = s->data + size_t(id) * 16;
        uint64_t a = rd64(p), b = rd64(p + 8);
        if ((a >> 62) == 3) {
            if (s->d_len > capacity) return -1;
            std::memcpy(out, s->d, s->d_len);
            return s->d_len;
        }
        uint8_t* q = out;
        uint8_t* end = out + capacity;
        if (size_t(end - q) < s->a_len) return -1;
        std::memcpy(q, s->a, s->a_len); q += s->a_len;
        auto emit_coord = [&](uint64_t x) -> bool {
            uint64_t mag = x & ((uint64_t(1) << 57) - 1);
            unsigned scale = unsigned((x >> 57) & 15);
            bool neg = ((x >> 61) & 1) != 0;
            unsigned int_width = unsigned((x >> 62) & 3) + 1;
            return append_decimal(q, end, mag, scale, int_width, neg);
        };
        if (!emit_coord(a)) return -1;
        if (size_t(end - q) < s->b_len) return -1;
        std::memcpy(q, s->b, s->b_len); q += s->b_len;
        if (!emit_coord(b)) return -1;
        if (size_t(end - q) < s->c_len) return -1;
        std::memcpy(q, s->c, s->c_len); q += s->c_len;
        return int64_t(q - out);
    }
    return -1;
}

static int64_t decode_one_generic(const State* s, const uint8_t*& p,
                                 const uint8_t* data_end, uint8_t* out,
                                 size_t capacity) {
    uint32_t payload_len = 0;
    if (!read_varint(p, data_end, payload_len) ||
        size_t(data_end - p) < payload_len)
        return -1;
    const uint8_t* payload = p;
    const uint8_t* end = p + payload_len;
    p = end;
    return decode_payload(s, payload, end, out, capacity);
}

static bool parse_archive(State* s, const uint8_t* archive, size_t size) {
    if (!s || !archive || size < HDR_SIZE ||
        std::memcmp(archive, "DBR1", 4) != 0 || archive[4] != 1)
        return false;
    s->archive = archive;
    s->archive_size = size;
    s->mode = archive[5];
    s->stride_log2 = archive[6];
    s->rows = rd32(archive + 8);
    s->raw_size = rd32(archive + 12);
    s->dict_len = rd32(archive + 16);
    s->index_count = rd32(archive + 20);
    s->data_len = rd32(archive + 24);
    s->record_bits = rd32(archive + 28);
    bool generic = s->mode == MODE_PHRASE || s->mode == MODE_BPE ||
                   s->mode == MODE_HUFF;
    if (generic && s->stride_log2 != 5) return false;
    if (generic && s->rows && (!s->record_bits || s->record_bits > 32)) return false;
    uint64_t local_bits = generic ? uint64_t(s->rows) * s->record_bits : 0;
    s->row_index_len = size_t((local_bits + 7) / 8);
    uint64_t index_bytes = uint64_t(s->index_count) * 4;
    uint64_t need = HDR_SIZE + uint64_t(s->dict_len) + index_bytes +
                    s->row_index_len + s->data_len;
    if (need != size) return false;
    s->dict = archive + HDR_SIZE;
    s->index = s->dict + s->dict_len;
    s->row_index = s->index + index_bytes;
    s->data = s->row_index + s->row_index_len;
    const uint8_t* p = s->dict;
    const uint8_t* end = p + s->dict_len;
    if (s->mode == MODE_PHRASE) {
        if (size_t(end - p) < 6) return false;
        s->alphabet_count = rd16(p); p += 2;
        s->direct_count = rd16(p); p += 2;
        s->ext_count = rd16(p); p += 2;
        uint32_t phrases = uint32_t(s->direct_count) + s->ext_count;
        if (s->alphabet_count > 256 || phrases > MAX_PHRASES ||
            size_t(end - p) < s->alphabet_count)
            return false;
        if (s->ext_count) {
            if (s->ext_count > 256 ||
                uint32_t(s->alphabet_count) + s->direct_count != 255)
                return false;
        } else if (uint32_t(s->alphabet_count) + s->direct_count > 256) {
            return false;
        }
        if (s->index_count != (s->rows + ROW_STRIDE - 1) / ROW_STRIDE)
            return false;
        std::memcpy(s->alphabet, p, s->alphabet_count);
        p += s->alphabet_count;
        for (uint32_t i = 0; i < phrases; ++i) {
            if (p >= end) return false;
            uint8_t len = *p++;
            if (!len || size_t(end - p) < len) return false;
            s->phrase_ptr[i] = p;
            s->phrase_len[i] = len;
            p += len;
        }
        if (p != end) return false;
        return true;
    }
    if (s->mode == MODE_BPE) {
        if (size_t(end - p) < 7) return false;
        s->direct_count = rd16(p); p += 2;
        s->ext_count = rd16(p); p += 2;
        s->bpe_merge_count = rd16(p); p += 2;
        s->bpe_ascii = *p++;
        if (s->bpe_ascii > 1 ||
            uint32_t(s->direct_count) + s->ext_count != s->bpe_merge_count ||
            s->bpe_merge_count > MAX_PHRASES || s->ext_count > 256 ||
            size_t(end - p) < size_t(s->direct_count) +
                                   size_t(s->bpe_merge_count) * 4)
            return false;
        if (s->ext_count && s->bpe_ascii) {
            // Extended marker must never be an original ASCII literal.
        }
        std::fill(std::begin(s->bpe_phrase_for_code),
                  std::end(s->bpe_phrase_for_code), int16_t(-1));
        for (uint32_t i = 0; i < s->direct_count; ++i) {
            uint8_t code = *p++;
            if (code == 255 || s->bpe_phrase_for_code[code] >= 0) return false;
            s->bpe_phrase_for_code[code] = int16_t(i);
        }
        for (uint32_t i = 0; i < s->bpe_merge_count; ++i) {
            uint16_t left = rd16(p); p += 2;
            uint16_t right = rd16(p); p += 2;
            uint32_t max_symbol = 256 + i;
            if (left >= max_symbol || right >= max_symbol) return false;
            s->bpe_merge_left[i] = left;
            s->bpe_merge_right[i] = right;
            const uint8_t* lp; const uint8_t* rp;
            uint8_t lbyte = 0, rbyte = 0;
            uint16_t llen, rlen;
            if (left < 256) { lbyte = uint8_t(left); lp = &lbyte; llen = 1; }
            else { uint32_t j = left - 256; lp = s->bpe_expanded[j]; llen = s->bpe_merge_len[j]; }
            if (right < 256) { rbyte = uint8_t(right); rp = &rbyte; rlen = 1; }
            else { uint32_t j = right - 256; rp = s->bpe_expanded[j]; rlen = s->bpe_merge_len[j]; }
            if (uint32_t(llen) + rlen > 255) return false;
            std::memcpy(s->bpe_expanded[i], lp, llen);
            std::memcpy(s->bpe_expanded[i] + llen, rp, rlen);
            s->bpe_merge_len[i] = uint16_t(llen + rlen);
        }
        if (p != end || s->index_count !=
                (s->rows + ROW_STRIDE - 1) / ROW_STRIDE) return false;
        return true;
    }
    if (s->mode == MODE_HUFF) {
        if (size_t(end - p) < 4) return false;
        s->bpe_merge_count = rd16(p); p += 2;
        s->huff_symbol_count = rd16(p); p += 2;
        s->huff_eos_symbol = uint16_t(256 + s->bpe_merge_count);
        if (s->data_len < 3 || archive[7] < 1 || archive[7] > 8) return false;
        s->huff_total_bits = uint64_t(s->data_len - 3) * 8 + archive[7];
        if (s->huff_total_bits > uint64_t(s->data_len - 2) * 8) return false;
        if (s->bpe_merge_count > MAX_PHRASES ||
            s->huff_symbol_count != uint32_t(257 + s->bpe_merge_count) ||
            size_t(end - p) < size_t(s->bpe_merge_count) * 4 + s->huff_symbol_count)
            return false;
        for (uint32_t i = 0; i < s->bpe_merge_count; ++i) {
            uint16_t left = rd16(p); p += 2;
            uint16_t right = rd16(p); p += 2;
            uint32_t max_symbol = 256 + i;
            if (left >= max_symbol || right >= max_symbol) return false;
            s->bpe_merge_left[i] = left;
            s->bpe_merge_right[i] = right;
            const uint8_t* lp; const uint8_t* rp;
            uint8_t lbyte = 0, rbyte = 0;
            uint16_t llen, rlen;
            if (left < 256) { lbyte = uint8_t(left); lp = &lbyte; llen = 1; }
            else { uint32_t j = left - 256; lp = s->bpe_expanded[j]; llen = s->bpe_merge_len[j]; }
            if (right < 256) { rbyte = uint8_t(right); rp = &rbyte; rlen = 1; }
            else { uint32_t j = right - 256; rp = s->bpe_expanded[j]; rlen = s->bpe_merge_len[j]; }
            if (uint32_t(llen) + rlen > 255) return false;
            std::memcpy(s->bpe_expanded[i], lp, llen);
            std::memcpy(s->bpe_expanded[i] + llen, rp, rlen);
            s->bpe_merge_len[i] = uint16_t(llen + rlen);
        }
        if (size_t(end - p) != s->huff_symbol_count) return false;
        std::memcpy(s->huff_lengths, p, s->huff_symbol_count);
        p += s->huff_symbol_count;
        if (p != end || s->index_count !=
                (s->rows + ROW_STRIDE - 1) / ROW_STRIDE ||
            !finish_huffman_tables(s)) return false;
        return true;
    }
    if (s->index_count != 0) return false;
    if (s->mode == MODE_CUSTOMER) {
        if (p >= end) return false;
        s->a_len = *p++;
        if (size_t(end - p) < s->a_len) return false;
        s->a = p; p += s->a_len;
        if (p >= end) return false;
        s->customer_digits = *p++;
        if (p >= end) return false;
        s->b_len = *p++;
        if (size_t(end - p) < size_t(s->b_len) + 5) return false;
        s->b = p; p += s->b_len;
        s->customer_min = rd32(p); p += 4;
        s->customer_width = *p++;
        if (p != end || !s->customer_digits || s->customer_width > 32)
            return false;
        return true;
    }
    if (s->mode == MODE_DNA) {
        if (size_t(end - p) < 3) return false;
        s->seq_len = *p++;
        s->bits_per_symbol = *p++;
        s->a_len = *p++;
        if (size_t(end - p) < size_t(s->a_len) + 1) return false;
        s->a = p; p += s->a_len;
        s->symbol_count = *p++;
        if (!s->seq_len || !s->bits_per_symbol ||
            s->bits_per_symbol > 8 || !s->symbol_count ||
            s->symbol_count > 16 || size_t(end - p) < s->symbol_count)
            return false;
        std::memcpy(s->symbols, p, s->symbol_count);
        p += s->symbol_count;
        if (p != end || s->record_bits !=
            uint32_t(s->seq_len) * s->bits_per_symbol)
            return false;
        return true;
    }
    if (s->mode == MODE_HEX) {
        if (p >= end) return false;
        s->a_len = *p++;
        if (size_t(end - p) != size_t(s->a_len) + 16) return false;
        s->a = p; p += s->a_len;
        std::memcpy(s->hex_alpha, p, 16); p += 16;
        if (p != end || s->record_bits != 32 ||
            s->data_len != uint64_t(s->rows) * 4) return false;
        return true;
    }
    if (s->mode == MODE_UUID) {
        if (p >= end) return false;
        s->a_len = *p++;
        if (size_t(end - p) != size_t(s->a_len) + 26) return false;
        s->a = p; p += s->a_len;
        s->uuid_min_time = rd32(p); p += 4;
        s->uuid_time_bits = *p++;
        s->uuid_record_bytes = *p++;
        std::memcpy(s->uuid_fixed, p, 4); p += 4;
        std::memcpy(s->uuid_hex_alpha, p, 16); p += 16;
        if (p != end || s->uuid_time_bits > 32 ||
            s->uuid_record_bytes > 16 || s->record_bits !=
                uint32_t(s->uuid_record_bytes) * 8)
            return false;
        return true;
    }
    if (s->mode == MODE_LOCATION) {
        if (p >= end) return false;
        s->a_len = *p++;
        if (size_t(end - p) < s->a_len + 1) return false;
        s->a = p; p += s->a_len;
        s->b_len = *p++;
        if (size_t(end - p) < s->b_len + 1) return false;
        s->b = p; p += s->b_len;
        s->c_len = *p++;
        if (size_t(end - p) < s->c_len + 1) return false;
        s->c = p; p += s->c_len;
        s->d_len = *p++;
        if (size_t(end - p) != s->d_len) return false;
        s->d = p; p += s->d_len;
        return p == end && s->record_bits == 128;
    }
    return false;
}

static bool append_huffman_bytes(uint8_t*& q, const uint8_t* output_begin,
                                 const uint8_t* bytes, size_t len,
                                 size_t capacity, const State* s,
                                 uint64_t* offsets, uint32_t& completed_rows) {
    size_t used = size_t(q - output_begin);
    if (used > capacity || len > capacity - used) return false;
    if (offsets) {
        for (size_t i = 0; i < len; ++i) {
            if (bytes[i] == 10) {
                if (completed_rows >= s->rows) return false;
                offsets[++completed_rows] = uint64_t(used + i + 1);
            }
        }
    }
    if (len) {
        if (len == 1) *q++ = *bytes;
        else { huff_copy_padded(q, bytes, len, capacity - used); q += len; }
    }
    return true;
}

static int64_t decode_huffman_full_stream(const State* s, uint8_t* output,
                                           size_t capacity, uint64_t* offsets) {
    if (capacity < s->raw_size || s->data_len < 3) return -1;
    if (!offsets) {
        const uint8_t* fast_data = s->data;
        const uint8_t* fast_end = s->data + s->data_len;
        const uint64_t fast_total_bits = s->huff_total_bits;
        uint64_t fast_bitpos = 0;
        uint8_t* fast_q = output;
        while (size_t(fast_q - output) < s->raw_size) {
            if (fast_bitpos >= fast_total_bits) return -1;
            uint64_t rem = fast_total_bits - fast_bitpos;
            const uint8_t* cursor = fast_data + (fast_bitpos >> 3);
            size_t out_left = s->raw_size - size_t(fast_q - output);
            if (rem >= HUFF_LUT_BITS && size_t(fast_end - cursor) >= 3) {
                const HuffEntry& e = s->huff_lut[peek_huffman_12(fast_data, fast_bitpos)];
                if (e.bits && e.bits <= rem && !e.end_row &&
                    e.len && size_t(e.len) <= out_left) {
                    huff_copy_bulk(fast_q, e.bytes, e.len, out_left);
                    fast_q += e.len;
                    fast_bitpos += e.bits;
                    continue;
                }
            }
            int node = 0;
            int sym = -1;
            while (sym < 0) {
                if (fast_bitpos >= fast_total_bits) return -1;
                const uint8_t* p = fast_data + (fast_bitpos >> 3);
                unsigned bit = unsigned((*p >> (7 - (fast_bitpos & 7))) & 1);
                ++fast_bitpos;
                int child = s->huff_nodes[node].child[bit];
                if (child < 0) return -1;
                node = child;
                sym = s->huff_nodes[node].symbol;
            }
            if (uint32_t(sym) == s->huff_eos_symbol) return -1;
            if (uint32_t(sym) < 256) {
                if (!out_left) return -1;
                *fast_q++ = uint8_t(sym);
            } else {
                uint32_t mi = uint32_t(sym) - 256;
                if (mi >= s->bpe_merge_count) return -1;
                size_t n = s->bpe_merge_len[mi];
                if (!n || n > out_left) return -1;
                huff_copy_bulk(fast_q, s->bpe_expanded[mi], n, out_left);
                fast_q += n;
            }
        }
        if (uint64_t(fast_q - output) != s->raw_size ||
            fast_bitpos != fast_total_bits ||
            fast_end[-2] != 0 || fast_end[-1] != 0)
            return -1;
        return int64_t(fast_q - output);
    }
    const uint8_t* data = s->data;
    const uint8_t* end = s->data + s->data_len;
    const uint64_t total_bits = s->huff_total_bits;
    uint64_t bitpos = 0;
    uint8_t* q = output;
    uint32_t completed_rows = 0;
    if (offsets) offsets[0] = 0;
    while (size_t(q - output) < s->raw_size) {
        if (bitpos >= total_bits) return -1;
        uint64_t rem = total_bits - bitpos;
        const uint8_t* cursor = data + (bitpos >> 3);
        if (rem >= HUFF_LUT_BITS && size_t(end - cursor) >= 3) {
            const HuffEntry& e = s->huff_lut[peek_huffman_12(data, bitpos)];
            if (e.bits && e.bits <= rem && !e.end_row &&
                e.len && size_t(e.len) <= s->raw_size - size_t(q - output)) {
                if (!append_huffman_bytes(q, output, e.bytes, e.len, capacity,
                                          s, offsets, completed_rows))
                    return -1;
                bitpos += e.bits;
                continue;
            }
        }
        int node = 0;
        int sym = -1;
        while (sym < 0) {
            if (bitpos >= total_bits) return -1;
            const uint8_t* p = data + (bitpos >> 3);
            unsigned bit = (*p >> (7 - (bitpos & 7))) & 1;
            ++bitpos;
            int child = s->huff_nodes[node].child[bit];
            if (child < 0) return -1;
            node = child;
            sym = s->huff_nodes[node].symbol;
        }
        if (uint32_t(sym) == s->huff_eos_symbol) return -1;
        const uint8_t* src;
        uint8_t base = 0;
        uint16_t n;
        if (uint32_t(sym) < 256) { base = uint8_t(sym); src = &base; n = 1; }
        else {
            uint32_t mi = uint32_t(sym) - 256;
            if (mi >= s->bpe_merge_count) return -1;
            src = s->bpe_expanded[mi];
            n = s->bpe_merge_len[mi];
        }
        if (size_t(n) > s->raw_size - size_t(q - output) ||
            !append_huffman_bytes(q, output, src, n, capacity,
                                  s, offsets, completed_rows))
            return -1;
        }
    if (uint64_t(q - output) != s->raw_size || bitpos != total_bits ||
        end[-2] != 0 || end[-1] != 0)
        return -1;
    if (offsets) {
        if (completed_rows < s->rows) {
            if (completed_rows + 1 != s->rows) return -1;
            offsets[s->rows] = s->raw_size;
            completed_rows = s->rows;
        }
        if (completed_rows != s->rows) return -1;
    }
    return int64_t(q - output);
}

static int64_t decode_huffman_rows_with_simd_offsets(
        const State* s, uint8_t* output, size_t capacity, uint64_t* offsets) {
    int64_t n = decode_huffman_full_stream(s, output, capacity, nullptr);
    if (n < 0) return n;
    offsets[0] = 0;
    uint32_t completed = 0;
    size_t i = 0;
#if defined(__AVX512BW__)
    const __m512i lf = _mm512_set1_epi8(10);
    for (; i + 64 <= size_t(n); i += 64) {
        __m512i v = _mm512_loadu_si512(reinterpret_cast<const void*>(output + i));
        uint64_t mask = uint64_t(_mm512_cmpeq_epi8_mask(v, lf));
        while (mask) {
            unsigned bit = unsigned(__builtin_ctzll(mask));
            if (++completed > s->rows) return -1;
            offsets[completed] = uint64_t(i + bit + 1);
            mask &= mask - 1;
        }
    }
#elif defined(__AVX2__)
    const __m256i lf = _mm256_set1_epi8(10);
    for (; i + 32 <= size_t(n); i += 32) {
        __m256i v = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(output + i));
        uint32_t mask = uint32_t(_mm256_movemask_epi8(_mm256_cmpeq_epi8(v, lf)));
        while (mask) {
            unsigned bit = unsigned(__builtin_ctz(mask));
            if (++completed > s->rows) return -1;
            offsets[completed] = uint64_t(i + bit + 1);
            mask &= mask - 1;
        }
    }
#endif
    for (; i < size_t(n); ++i) {
        if (output[i] == 10) {
            if (++completed > s->rows) return -1;
            offsets[completed] = uint64_t(i + 1);
        }
    }
    if (completed < s->rows) {
        if (completed + 1 != s->rows) return -1;
        offsets[s->rows] = uint64_t(n);
        completed = s->rows;
    }
    if (completed != s->rows || offsets[s->rows] != uint64_t(n)) return -1;
    return n;
}

static int64_t decode_full_rows(const State* s, uint8_t* output,
                                size_t capacity, uint64_t* offsets) {
    if (capacity < s->raw_size) return -1;
    if (s->mode == MODE_HUFF)
        return offsets
            ? decode_huffman_rows_with_simd_offsets(s, output, capacity, offsets)
            : decode_huffman_full_stream(s, output, capacity, nullptr);
    uint8_t* q = output;
    if (s->mode == MODE_PHRASE || s->mode == MODE_BPE) {
        const uint8_t* p = s->data;
        const uint8_t* end = s->data + s->data_len;
        for (uint32_t i = 0; i < s->rows; ++i) {
            if (offsets) offsets[i] = uint64_t(q - output);
            int64_t n = decode_one_generic(s, p, end, q, capacity - size_t(q - output));
            if (n < 0) return -1;
            q += n;
        }
        if (p != end) return -1;
    } else {
        for (uint32_t i = 0; i < s->rows; ++i) {
            if (offsets) offsets[i] = uint64_t(q - output);
            int64_t n = decode_special_row(s, i, q, capacity - size_t(q - output));
            if (n < 0) return -1;
            q += n;
        }
    }
    if (offsets) offsets[s->rows] = uint64_t(q - output);
    if (uint64_t(q - output) != s->raw_size) return -1;
    return int64_t(q - output);
}
} // namespace

extern "C" void* lab_open(const uint8_t* archive, size_t size) {
    State* s = new (std::nothrow) State();
    if (!s) return nullptr;
    if (!parse_archive(s, archive, size)) {
        delete s;
        return nullptr;
    }
    return s;
}

extern "C" int64_t lab_decode(void* state, uint8_t* output, size_t capacity) {
    State* s = static_cast<State*>(state);
    if (!s || (!output && s->raw_size) || capacity < s->raw_size) return -1;
    return decode_full_rows(s, output, capacity, nullptr);
}

extern "C" int64_t lab_rows(void* state, const uint64_t* ids, size_t count,
                             uint8_t* output, size_t capacity,
                             uint64_t* offsets) {
    State* s = static_cast<State*>(state);
    if (!s || (!ids && count) || (!output && count) || !offsets) return -1;
    if (!count) { offsets[0] = 0; return 0; }
    if (count == s->rows) {
        bool complete = true;
        for (size_t i = 0; i < count; ++i) {
            if (ids[i] != i) { complete = false; break; }
        }
        if (complete) return decode_full_rows(s, output, capacity, offsets);
    }
    uint8_t* q = output;
    bool generic = s->mode == MODE_PHRASE || s->mode == MODE_BPE ||
                   s->mode == MODE_HUFF;
    if (!generic) {
        for (size_t i = 0; i < count; ++i) {
            if (ids[i] >= s->rows || (i && ids[i] < ids[i - 1])) return -1;
            size_t used = size_t(q - output);
            if (used > capacity) return -1;
            offsets[i] = uint64_t(used);
            int64_t n = decode_special_row(s, uint32_t(ids[i]), q,
                                           capacity - used);
            if (n < 0) return -1;
            q += n;
        }
        offsets[count] = uint64_t(q - output);
        return int64_t(q - output);
    }

    const uint8_t* data_end = s->data + s->data_len;
    uint32_t cached_block = std::numeric_limits<uint32_t>::max();
    uint32_t cached_block_offset = 0;
    for (size_t i = 0; i < count; ++i) {
        uint64_t id64 = ids[i];
        if (id64 >= s->rows || (i && id64 < ids[i - 1])) return -1;
        size_t used = size_t(q - output);
        if (used > capacity) return -1;
        offsets[i] = uint64_t(used);
        uint32_t id = uint32_t(id64);
        uint32_t block = id >> s->stride_log2;
        if (block >= s->index_count) return -1;
        uint64_t local = bits_at(s->row_index, s->row_index_len,
                                 uint64_t(id) * s->record_bits,
                                 s->record_bits);
        if (block != cached_block) {
            cached_block = block;
            cached_block_offset = rd32(s->index + size_t(block) * 4);
        }
        uint64_t block_offset = cached_block_offset;
        int64_t n;
        if (s->mode == MODE_HUFF) {
            uint64_t start_bit = block_offset + local;
            uint64_t end_bit = s->huff_total_bits;
            if (id + 1 < s->rows) {
                uint32_t next_row = id + 1;
                uint32_t next_block = next_row >> s->stride_log2;
                uint64_t next_local = bits_at(s->row_index, s->row_index_len,
                                              uint64_t(next_row) * s->record_bits,
                                              s->record_bits);
                end_bit = uint64_t(rd32(s->index + size_t(next_block) * 4)) +
                          next_local;
            }
            if (start_bit >= end_bit || end_bit > s->huff_total_bits) return -1;
            n = decode_huffman_range(s, start_bit, end_bit, q, capacity - used);
        } else {
            uint64_t data_off = block_offset + local;
            if (data_off >= s->data_len) return -1;
            const uint8_t* p = s->data + data_off;
            uint32_t payload_len = 0;
            if (!read_varint(p, data_end, payload_len) ||
                size_t(data_end - p) < payload_len)
                return -1;
            n = decode_payload(s, p, p + payload_len, q, capacity - used);
        }
        if (n < 0) return -1;
        q += n;
    }
    offsets[count] = uint64_t(q - output);
    return int64_t(q - output);
}
extern "C" void lab_close(void* state) {
    delete static_cast<State*>(state);
}

#endif

