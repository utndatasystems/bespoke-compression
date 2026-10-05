// Shared definitions for the Yelp-JSONL specialized codec.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <string>
#include <array>
#include <cstdio>
#include <algorithm>
#include <unordered_map>
#include <zstd.h>

// ---------------- bit writer (MSB-first) ----------------
struct BitW {
  std::vector<uint8_t>* out;
  uint64_t acc = 0;
  int n = 0;
  void put(uint64_t v, int bits) {
    v &= (bits >= 64 ? ~0ull : ((1ull << bits) - 1));
    acc = (acc << bits) | v;
    n += bits;
    while (n >= 8) { out->push_back((uint8_t)(acc >> (n - 8))); n -= 8; }
  }
  void align_byte() { if (n) { out->push_back((uint8_t)(acc << (8 - n))); acc = 0; n = 0; } }
};

// ---------------- bit reader (MSB-first) ----------------
struct BitR {
  const uint8_t* p;
  const uint8_t* pend;
  uint64_t acc;
  int n;
  void init(const uint8_t* q, const uint8_t* end) { p = q; pend = end; acc = 0; n = 0; refill(); }
  inline void refill() {
    if (n <= 32) {
      uint64_t w = 0;
      int avail = (int)(pend - p);
      if (avail > 0) {
        if (avail >= 4) memcpy(&w, p, 4);
        else memcpy(&w, p, (size_t)avail);
        p += (avail >= 4 ? 4 : avail);
      }
      acc = (acc << 32) | __builtin_bswap32((uint32_t)w);
      n += 32;
    }
  }
  inline uint64_t peek(int b) { if (n < b) refill(); return (acc >> (n - b)) & ((1ull << b) - 1); }
  inline void skip(int b) { n -= b; }
  inline uint64_t get(int b) { if (n < b) refill(); uint64_t v = (acc >> (n - b)) & ((1ull << b) - 1); n -= b; return v; }
};

// ---------------- LEB128 varint ----------------
static inline void put_uv(std::vector<uint8_t>& v, uint64_t x) {
  while (x >= 128) { v.push_back((uint8_t)(x | 128)); x >>= 7; }
  v.push_back((uint8_t)x);
}
static inline uint64_t get_uv(const uint8_t*& p) {
  uint64_t x = 0; int s = 0;
  while (true) { uint8_t b = *p++; x |= (uint64_t)(b & 127) << s; if (!(b & 128)) break; s += 7; }
  return x;
}

// ---------------- Huffman (canonical, lengths limited to <= HUFF_LIMIT) ----------------
#define HUFF_LIMIT 13

static void huff_lengths(const uint32_t* freq, int n, std::vector<uint8_t>& len) {
  len.assign(n, 0);
  if (n == 1) { len[0] = 1; return; }
  // heap of (freq, node)
  std::vector<uint32_t> hf;
  std::vector<int> hn;
  hf.reserve(2 * n); hn.reserve(2 * n);
  std::vector<int> parent(2 * n, -1);
  auto cmp = [&](int a, int b) { return hf[a] > hf[b] || (hf[a] == hf[b] && a > b); };
  std::vector<int> heap;
  for (int i = 0; i < n; i++) {
    if (freq[i] == 0) continue;
    hf.push_back(freq[i]); hn.push_back(i); heap.push_back((int)hf.size() - 1);
  }
  int m = (int)hf.size();
  if (m == 0) { len.assign(n, 0); return; }
  if (m == 1) { len[hn[heap[0]]] = 1; return; }
  std::make_heap(heap.begin(), heap.end(), cmp);
  int next = m;
  while (heap.size() > 1) {
    std::pop_heap(heap.begin(), heap.end(), cmp); int a = heap.back(); heap.pop_back();
    std::pop_heap(heap.begin(), heap.end(), cmp); int b = heap.back(); heap.pop_back();
    hf.push_back(hf[a] + hf[b]); hn.push_back(-1);
    parent[a] = next; parent[b] = next;
    heap.push_back(next); std::push_heap(heap.begin(), heap.end(), cmp);
    next++;
  }
  // depth of each leaf
  int maxsym = 0;
  for (int i = 0; i < m; i++) {
    int d = 0, cur = i;
    while (parent[cur] >= 0) { cur = parent[cur]; d++; }
    len[hn[i]] = (uint8_t)d;
    if (d > HUFF_LIMIT) maxsym = 1;
  }
  // limit lengths
  if (maxsym) {
    for (int i = 0; i < n; i++) if (len[i] > HUFF_LIMIT) len[i] = HUFF_LIMIT;
    // repair Kraft sum: push the deepest extendable symbols deeper until K <= 2^LIMIT
    while (true) {
      uint64_t k = 0;
      for (int i = 0; i < n; i++) if (len[i]) k += 1ull << (HUFF_LIMIT - len[i]);
      if (k <= (1ull << HUFF_LIMIT)) break;
      int j = -1;
      for (int i = 0; i < n; i++) if (len[i] > 0 && len[i] < HUFF_LIMIT && (j < 0 || len[i] > len[j])) j = i;
      if (j < 0) break; // all at limit: n <= 2^LIMIT guarantees this cannot happen
      len[j]++;
    }
  }
}

static void huff_codes(const std::vector<uint8_t>& len, std::vector<uint32_t>& code) {
  int n = (int)len.size();
  code.assign(n, 0);
  uint32_t bl_count[HUFF_LIMIT + 2] = {0};
  for (int i = 0; i < n; i++) bl_count[len[i]]++;
  bl_count[0] = 0;
  uint32_t next_code[HUFF_LIMIT + 2];
  uint32_t c = 0;
  for (int bits = 1; bits <= HUFF_LIMIT; bits++) { c = (c + bl_count[bits - 1]) << 1; next_code[bits] = c; }
  for (int i = 0; i < n; i++) if (len[i]) code[i] = next_code[len[i]]++;
}

struct HuffTable {
  std::vector<uint32_t> map; // entry: len<<16 | sym
  int maxlen = 0;
  int nsym = 0;
};

static void huff_build_table(const std::vector<uint8_t>& len, HuffTable& t) {
  std::vector<uint32_t> code;
  huff_codes(len, code);
  int maxlen = 0, nsym = (int)len.size();
  for (int i = 0; i < nsym; i++) if (len[i] > maxlen) maxlen = len[i];
  t.maxlen = maxlen; t.nsym = nsym;
  if (maxlen == 0 || maxlen > 16) { t.map.clear(); t.maxlen = 0; return; }
  t.map.assign(1u << maxlen, 0);
  for (int i = 0; i < nsym; i++) {
    if (!len[i]) continue;
    uint32_t base = code[i] << (maxlen - len[i]);
    uint32_t cnt = 1u << (maxlen - len[i]);
    uint32_t e = ((uint32_t)len[i] << 16) | (uint32_t)i;
    for (uint32_t j = 0; j < cnt; j++) t.map[base + j] = e;
  }
}

// ---------------- zstd helpers ----------------
static void zstd_compress_to(std::vector<uint8_t>& dst, const void* src, size_t n, int level) {
  size_t bound = ZSTD_compressBound(n);
  std::vector<uint8_t> tmp(bound);
  size_t r = ZSTD_compress(tmp.data(), bound, src, n, level);
  assert(!ZSTD_isError(r));
  dst.insert(dst.end(), (uint8_t*)&n, (uint8_t*)&n + 8);
  dst.insert(dst.end(), tmp.data(), tmp.data() + r);
}
static bool zstd_decompress_buf(const uint8_t*& p, const uint8_t* end, std::vector<uint8_t>& out) {
  if (end - p < 8) return false;
  uint64_t n;
  memcpy(&n, p, 8); p += 8;
  if (n > (1ull << 31)) return false;
  size_t used = ZSTD_findFrameCompressedSize(p, (size_t)(end - p));
  if (ZSTD_isError(used)) return false;
  if (used > (uint64_t)(end - p)) return false;
  out.resize((size_t)n);
  size_t r = ZSTD_decompress(out.data(), (size_t)n, p, used);
  if (ZSTD_isError(r) || r != n) return false;
  p += used;
  return true;
}

// ---------------- archive section table ----------------
#define NSEC 8
enum {
  SEC_SKEL = 0, SEC_BID, SEC_LOC, SEC_NUM3, SEC_LL, SEC_ATTR, SEC_CAT, SEC_HOURS
};

// pool+ids section layout (city/state/postal/rc):
//   u32 n; u32 offs[n+1]; u8 lens[n]; concat bytes; huff lens[n] u8; bitstream
// stars: u32 n; u32 offs[n+1]; concat; flat stream
