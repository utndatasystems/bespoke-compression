#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>
// Minimal static canonical Huffman for byte streams (depth <= 15, 12-bit LUT decode).
// Stream: [u16 table_bytes][len table: 256 bytes][MSB-first bitstream]
static inline size_t huff_bound(size_t n) { return n + 660 + 16; }

static inline size_t huff_compress(const uint8_t* src, size_t n, uint8_t* dst, size_t cap) {
  if (n == 0 || cap < 700) return 0;
  uint32_t freq[256] = {0};
  for (size_t i = 0; i < n; i++) freq[src[i]]++;
  uint32_t nz = 0, last = 0;
  for (uint32_t s = 0; s < 256; s++) if (freq[s]) { nz++; last = s; }
  if (nz == 0) return 0;
  uint8_t len[256] = {0};
  uint64_t fwork[256];
  for (uint32_t s = 0; s < 256; s++) fwork[s] = freq[s];
  for (int pass = 0; pass < 40; pass++) {
    uint32_t cnt = 0;
    for (uint32_t s = 0; s < 256; s++) if (fwork[s]) cnt++;
    if (cnt == 0) return 0;
    if (cnt == 1) { for (uint32_t s = 0; s < 256; s++) if (fwork[s]) len[s] = 1; break; }
    uint64_t f[512]; int32_t ch[512][2]; uint8_t nsym[512];
    uint32_t k = 0;
    for (uint32_t s = 0; s < 256; s++) if (fwork[s]) { f[k] = fwork[s]; ch[k][0] = ch[k][1] = -1; nsym[k] = (uint8_t)s; k++; }
    bool alive[512]; memset(alive, 1, sizeof(bool) * k);
    uint32_t remain = k;
    uint32_t maxlen = 0;
    while (remain > 1) {
      uint32_t a = 0xFFFFFFFFu, b = 0xFFFFFFFFu;
      for (uint32_t i = 0; i < k; i++) {
        if (!alive[i]) continue;
        if (a == 0xFFFFFFFFu || f[i] < f[a]) { b = a; a = i; }
        else if (b == 0xFFFFFFFFu || f[i] < f[b]) b = i;
      }
      f[k] = f[a] + f[b]; ch[k][0] = (int32_t)a; ch[k][1] = (int32_t)b; alive[k] = true;
      alive[a] = false; alive[b] = false;
      k++; remain--;
    }
    uint32_t stack[600]; uint8_t dep[600]; uint32_t sp = 0;
    stack[sp] = k - 1; dep[sp] = 0; sp++;
    while (sp) {
      sp--;
      uint32_t ni = stack[sp]; uint8_t dp = dep[sp];
      if (dp > maxlen) maxlen = dp;
      if (ch[ni][0] < 0) { for (uint32_t s = 0; s < 256; s++) if (nsym[ni] == s) len[s] = dp ? dp : 1; continue; }
      stack[sp] = (uint32_t)ch[ni][1]; dep[sp] = dp + 1; sp++;
      stack[sp] = (uint32_t)ch[ni][0]; dep[sp] = dp + 1; sp++;
    }
    if (maxlen <= 15) break;
    for (uint32_t s = 0; s < 256; s++) if (fwork[s]) fwork[s] = (fwork[s] + 1) >> 1;
  }
  uint32_t bl_count[16] = {0};
  for (uint32_t s = 0; s < 256; s++) bl_count[len[s]]++;
  bl_count[0] = 0;
  uint32_t code_acc = 0, next_code[16]; next_code[0] = 0;
  for (int b = 1; b <= 15; b++) { code_acc = (code_acc + bl_count[b - 1]) << 1; next_code[b] = code_acc; }
  uint32_t codes[256];
  for (int b = 1; b <= 15; b++)
    for (uint32_t s = 0; s < 256; s++) if (len[s] == (uint32_t)b) codes[s] = next_code[b]++;
  uint8_t* out = dst;
  *out++ = 0; *out++ = 0;
  for (uint32_t s = 0; s < 256; s++) *out++ = len[s];
  size_t tbl = (size_t)(out - dst) - 2;
  dst[0] = (uint8_t)(tbl >> 8); dst[1] = (uint8_t)(tbl & 0xFF);
  uint64_t acc = 0; int nbits = 0;
  uint8_t* w = out;
  uint64_t total_bits = 0;
  for (uint32_t s = 0; s < 256; s++) total_bits += (uint64_t)freq[s] * len[s];
  if (total_bits + 8 > (uint64_t)(cap - (size_t)(out - dst)) * 8) return 0;
  for (size_t i = 0; i < n; i++) {
    uint8_t s = src[i];
    acc = (acc << len[s]) | codes[s];
    nbits += len[s];
    while (nbits >= 8) { nbits -= 8; *w++ = (uint8_t)(acc >> nbits); }
  }
  if (nbits) *w++ = (uint8_t)(acc << (8 - nbits));
  return (size_t)(w - dst);
}

static inline size_t huff_decompress(const uint8_t* src, size_t clen, uint8_t* dst, size_t ralen) {
  if (clen < 258) return 0;
  size_t tbl = ((size_t)src[0] << 8) | src[1];
  if (2 + tbl + 1 > clen || tbl != 256) return 0;
  const uint8_t* len = src + 2;
  const uint8_t* bits = src + 2 + tbl;
  size_t bitslen = clen - 2 - tbl;
  uint32_t bl_count[16] = {0};
  for (uint32_t s = 0; s < 256; s++) bl_count[len[s]]++;
  bl_count[0] = 0;
  uint32_t code_acc = 0, next_code[16]; next_code[0] = 0;
  for (int b = 1; b <= 15; b++) { code_acc = (code_acc + bl_count[b - 1]) << 1; next_code[b] = code_acc; }
  uint32_t first_code[16];
  uint32_t codes[256];
  for (int b = 1; b <= 15; b++) { first_code[b] = next_code[b]; }
  for (int b = 1; b <= 15; b++)
    for (uint32_t s = 0; s < 256; s++) if (len[s] == (uint32_t)b) codes[s] = next_code[b]++;
  static uint8_t lutlen[32768]; // code length per 15-bit peek (32KB, L1-resident on server)
  static uint8_t symlut[16][256]; // symbols by length in canonical code order
  memset(lutlen, 0, sizeof(lutlen));
  memset(symlut, 0, sizeof(symlut));
  for (int b = 1; b <= 15; b++) {
    uint32_t k = 0;
    for (uint32_t s = 0; s < 256; s++) {
      if (len[s] != (uint32_t)b) continue;
      symlut[b][k++] = (uint8_t)s;
      uint32_t c = codes[s] << (15 - b);
      for (uint32_t f = 0; f < (1u << (15 - b)); f++) lutlen[c | f] = (uint8_t)b;
    }
  }
  uint64_t acc = 0; int nbits = 0; size_t ip = 0, op = 0;
  while (op < ralen) {
    while (nbits < 15 && ip < bitslen) { acc = (acc << 8) | bits[ip++]; nbits += 8; }
    uint32_t peek = (nbits >= 15) ? (uint32_t)((acc >> (nbits - 15)) & 0x7FFF) : (uint32_t)((acc << (15 - nbits)) & 0x7FFF);
    uint8_t L = lutlen[peek];
    if (L == 0 || L > (uint32_t)nbits) return 0;
    uint32_t code = peek >> (15 - L);
    dst[op++] = symlut[L][code - first_code[L]];
    nbits -= L;
  }
  return op;
}
