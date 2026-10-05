#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#ifndef DECODE_ONLY
#include <vector>
#include <algorithm>
#endif

namespace ent {
static constexpr uint32_t SCALE = 12, TOTAL = 1u << SCALE, LOW = 1u << 15;
static inline uint32_t read32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
static inline bool decode(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t n) {
    if (!srcSize) return false;
    if (src[0] == 0) {
        if (srcSize - 1 != n) return false;
        if (n) memcpy(dst, src + 1, n);
        return true;
    }
    if (src[0] == 2) {
        if (srcSize != 2 || !n) return false;
        memset(dst, src[1], n);
        return true;
    }
    if (src[0] != 1 || srcSize < 2) return false;
    const unsigned count = unsigned(src[1]) + 1;
    const size_t headerSize = 2 + 3 * size_t(count) + 16;
    if (count < 2 || srcSize < headerSize) return false;
    uint32_t table[TOTAL];
    unsigned sum = 0;
    int previous = -1;
    const uint8_t* p = src + 2;
    for (unsigned j = 0; j < count; ++j, p += 3) {
        const unsigned sym = p[0], freq = unsigned(p[1]) | (unsigned(p[2]) << 8);
        if (int(sym) <= previous || !freq || freq >= TOTAL || sum + freq > TOTAL) return false;
        previous = int(sym);
        for (unsigned k = 0; k < freq; ++k) table[sum + k] = (freq << 20) | (k << 8) | sym;
        sum += freq;
    }
    if (sum != TOTAL) return false;
    uint32_t x0 = read32(p), x1 = read32(p + 4), x2 = read32(p + 8), x3 = read32(p + 12);
    p += 16;
    if (x0 < LOW || x1 < LOW || x2 < LOW || x3 < LOW || ((x0 | x1 | x2 | x3) >> 31)) return false;
    const uint8_t* end = src + srcSize;
    size_t i = 0;
    while (n - i >= 4 && size_t(end - p) >= 8) {
        const uint32_t a = table[x0 & (TOTAL - 1)], b = table[x1 & (TOTAL - 1)];
        const uint32_t c = table[x2 & (TOTAL - 1)], d = table[x3 & (TOTAL - 1)];
        x0 = (a >> 20) * (x0 >> SCALE) + ((a >> 8) & (TOTAL - 1));
        x1 = (b >> 20) * (x1 >> SCALE) + ((b >> 8) & (TOTAL - 1));
        x2 = (c >> 20) * (x2 >> SCALE) + ((c >> 8) & (TOTAL - 1));
        x3 = (d >> 20) * (x3 >> SCALE) + ((d >> 8) & (TOTAL - 1));
        const uint32_t symbols = (a & 255) | ((b & 255) << 8) | ((c & 255) << 16) | (d << 24);
        memcpy(dst + i, &symbols, 4);
        if (x0 < LOW) { x0 = (x0 << 16) | uint32_t(p[0]) | (uint32_t(p[1]) << 8); p += 2; }
        if (x1 < LOW) { x1 = (x1 << 16) | uint32_t(p[0]) | (uint32_t(p[1]) << 8); p += 2; }
        if (x2 < LOW) { x2 = (x2 << 16) | uint32_t(p[0]) | (uint32_t(p[1]) << 8); p += 2; }
        if (x3 < LOW) { x3 = (x3 << 16) | uint32_t(p[0]) | (uint32_t(p[1]) << 8); p += 2; }
        i += 4;
    }
    uint32_t states[4] = {x0, x1, x2, x3};
    for (; i < n; ++i) {
        uint32_t& x = states[i & 3];
        const uint32_t e = table[x & (TOTAL - 1)];
        dst[i] = uint8_t(e);
        x = (e >> 20) * (x >> SCALE) + ((e >> 8) & (TOTAL - 1));
        while (x < LOW) {
            if (size_t(end - p) < 2) return false;
            x = (x << 16) | uint32_t(p[0]) | (uint32_t(p[1]) << 8); p += 2;
        }
    }
    return p == end && states[0] == LOW && states[1] == LOW && states[2] == LOW && states[3] == LOW;
}
#ifndef DECODE_ONLY
static inline std::vector<uint8_t> encode(const std::vector<uint8_t>& input) {
    std::vector<uint8_t> raw;
    raw.reserve(input.size() + 1);
    raw.push_back(0);
    raw.insert(raw.end(), input.begin(), input.end());
    if (input.empty()) return raw;
    uint64_t counts[256] = {};
    for (uint8_t x : input) ++counts[x];
    uint32_t freq[256] = {}, start[256] = {};
    unsigned present = 0, last = 0, total = 0;
    for (unsigned j = 0; j < 256; ++j) if (counts[j]) {
        ++present; last = j;
        freq[j] = std::max<uint32_t>(1, uint32_t((counts[j] * TOTAL) / input.size()));
        total += freq[j];
    }
    if (present == 1) return std::vector<uint8_t>{2, uint8_t(last)};
    while (total < TOTAL) {
        int best = -1;
        int64_t error = INT64_MIN;
        for (unsigned j = 0; j < 256; ++j) if (counts[j]) {
            int64_t e = int64_t(counts[j] * TOTAL) - int64_t(uint64_t(freq[j]) * input.size());
            if (e > error) { error = e; best = int(j); }
        }
        ++freq[best]; ++total;
    }
    while (total > TOTAL) {
        int best = -1;
        int64_t error = INT64_MIN;
        for (unsigned j = 0; j < 256; ++j) if (freq[j] > 1) {
            int64_t e = int64_t(uint64_t(freq[j]) * input.size()) - int64_t(counts[j] * TOTAL);
            if (e > error) { error = e; best = int(j); }
        }
        --freq[best]; --total;
    }
    unsigned cumulative = 0;
    for (unsigned j = 0; j < 256; ++j) { start[j] = cumulative; cumulative += freq[j]; }
    std::vector<uint8_t> bytes;
    bytes.reserve(input.size());
    uint32_t states[4] = {LOW, LOW, LOW, LOW};
    for (size_t i = input.size(); i-- > 0;) {
        const unsigned sym = input[i], f = freq[sym];
        uint32_t x = states[i & 3];
        const uint32_t limit = ((LOW >> SCALE) << 16) * f;
        while (x >= limit) { bytes.push_back(uint8_t(x >> 8)); bytes.push_back(uint8_t(x)); x >>= 16; }
        states[i & 3] = ((x / f) << SCALE) + x % f + start[sym];
    }
    const size_t encodedSize = 2 + 3 * size_t(present) + 16 + bytes.size();
    if (encodedSize >= raw.size()) return raw;
    std::vector<uint8_t> result;
    result.reserve(encodedSize);
    result.push_back(1); result.push_back(uint8_t(present - 1));
    for (unsigned j = 0; j < 256; ++j) if (freq[j]) {
        result.push_back(uint8_t(j)); result.push_back(uint8_t(freq[j])); result.push_back(uint8_t(freq[j] >> 8));
    }
    for (uint32_t x : states) for (unsigned k = 0; k < 4; ++k) result.push_back(uint8_t(x >> (8*k)));
    result.insert(result.end(), bytes.rbegin(), bytes.rend());
    return result;
}
#endif
}
