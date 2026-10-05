#pragma once
#include <immintrin.h>
#include <stdint.h>
#include <string.h>

// Read 16 bytes and write 32 (only the first 22 are meaningful).
// Source is the conventional big-endian packed URL-safe base64 bit stream.
static inline void yelp_id_decode(const uint8_t* src, char* dst) {
  const __m256i order = _mm256_setr_epi8(5,4,3,2,1,0,16,16,11,10,9,8,7,6,16,16,16,16,15,14,13,12,16,16,16,16,16,16,16,16,16,16);
  const __m256i shifts = _mm256_setr_epi8(42,36,30,24,18,12,6,0,42,36,30,24,18,12,6,0,42,36,30,24,18,12,6,0,42,36,30,24,18,12,6,0);
  const __m256i a = _mm256_loadu_si256((const __m256i*)"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef");
  const __m256i b = _mm256_loadu_si256((const __m256i*)"ghijklmnopqrstuvwxyz0123456789-_");
  __m256i v = _mm256_zextsi128_si256(_mm_loadu_si128((const __m128i*)src));
  v = _mm256_permutexvar_epi8(order, v);
  v = _mm256_multishift_epi64_epi8(shifts, v);
  v = _mm256_permutex2var_epi8(a, v, b);
  _mm256_storeu_si256((__m256i*)dst, v);
}

// Read 8 bytes, low nibble first, and write 16 characters.
// Codes 0..9 => digits, 10 => '.', 11 => '-'.
static inline void yelp_bcd_decode(const uint8_t* src, char* dst) {
  const __m128i table = _mm_loadu_si128((const __m128i*)"0123456789.-????");
  const __m128i v = _mm_loadl_epi64((const __m128i*)src);
  const __m128i n = _mm_and_si128(_mm_unpacklo_epi8(v, _mm_srli_epi16(v,4)),_mm_set1_epi8(15));
  _mm_storeu_si128((__m128i*)dst,_mm_shuffle_epi8(table,n));
}

static inline void yelp_id_decode512(const uint8_t* src, char* dst) {
  const __m256i order = _mm256_setr_epi8(5,4,3,2,1,0,16,16,11,10,9,8,7,6,16,16,16,16,15,14,13,12,16,16,16,16,16,16,16,16,16,16);
  const __m256i shifts = _mm256_setr_epi8(42,36,30,24,18,12,6,0,42,36,30,24,18,12,6,0,42,36,30,24,18,12,6,0,42,36,30,24,18,12,6,0);
  const __m512i alphabet = _mm512_loadu_si512((const void*)"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_");
  __m256i v = _mm256_zextsi128_si256(_mm_loadu_si128((const __m128i*)src));
  v = _mm256_permutexvar_epi8(order, v);
  v = _mm256_multishift_epi64_epi8(shifts, v);
  _mm256_storeu_si256((__m256i*)dst,_mm512_castsi512_si256(_mm512_permutexvar_epi8(_mm512_castsi256_si512(v),alphabet)));
}
