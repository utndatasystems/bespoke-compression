#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>
#include <string>
#include <algorithm>

static inline uint8_t* put_uv(uint8_t* p, uint64_t v) {
  while (v >= 128) { *p++ = (uint8_t)(128u | (v & 127)); v >>= 7; }
  *p++ = (uint8_t)v; return p;
}
static inline const uint8_t* get_uv(const uint8_t* p, const uint8_t* end, uint64_t& v) {
  v = 0; int s = 0;
  while (p < end) { uint8_t b = *p++; v |= (uint64_t)(b & 127) << s; if (!(b & 128)) break; s += 7; }
  return p;
}

// LSB-first bit writer
struct BitW {
  std::vector<uint8_t>& buf; uint64_t acc = 0; int cnt = 0;
  explicit BitW(std::vector<uint8_t>& b) : buf(b) {}
  inline void put(uint64_t v, int n) {
    if (n < 64) v &= ((1ULL << n) - 1);
    acc |= v << cnt; cnt += n;
    while (cnt >= 8) { buf.push_back((uint8_t)acc); acc >>= 8; cnt -= 8; }
  }
  inline void flush() { if (cnt) buf.push_back((uint8_t)acc); acc = 0; cnt = 0; }
  inline size_t bitlen() const { return buf.size() * 8 + (size_t)cnt; }
};

// codec ids
enum { C_CNAME = 1, C_GENOME = 2, C_HEX = 3, C_UUID = 4, C_LOC = 5, C_TOKEN = 6 };

static inline void split_rows(const uint8_t* d, size_t n, std::vector<std::string>& rows, bool& final_nl) {
  final_nl = (n > 0 && d[n - 1] == '\n');
  size_t start = 0;
  for (size_t i = 0; i < n; i++) {
    if (d[i] == '\n') { rows.emplace_back((const char*)d + start, i - start); start = i + 1; }
  }
  if (!final_nl && start < n) rows.emplace_back((const char*)d + start, n - start);
}
