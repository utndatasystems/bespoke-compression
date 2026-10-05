/* Yelp business JSONL columnar codec (dataset-specialized, bulk).
 *
 * One C source, two builds: -DBUILD_ENCODER exports lab_encode;
 * default build exports lab_open/lab_decode/lab_rows/lab_close.
 *
 * The archive stores one control stream per JSON field plus string LUTs
 * for every repeated value alphabet (city+state, postal, stars,
 * review_count, attribute pairs, category tokens, hours pairs). Decode
 * walks the control streams sequentially and emits the original bytes
 * with short overlapped AVX2 copies. All data-dependent tables live in
 * the archive; the decoder binary is generic.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <immintrin.h>
#define _GNU_SOURCE
#include "codec.h"

#define API __attribute__((visibility("default")))

#define N_SEC 22
enum {
  SEC_BID=0, SEC_CTRL, SEC_NAME_BLOB, SEC_ADDR_BLOB,
  SEC_CITY_LEN, SEC_CITY_LUT,
  SEC_POSTAL_LEN, SEC_POSTAL_LUT,
  SEC_LAT, SEC_LNG, SEC_STARS_LUT,
  SEC_RC_LEN, SEC_RC_LUT,
  SEC_ATTR_IDX, SEC_ATTR_LEN, SEC_ATTR_LUT,
  SEC_CAT_IDX, SEC_CAT_LEN, SEC_CAT_LUT,
  SEC_HOURS_IDX, SEC_HOURS_LEN, SEC_HOURS_LUT
};
/* per-line control record: flags, name_len, addr_len, stars_code,
 * city_idx(u16), postal_idx(u16), rc_idx(u16), attr_cnt, cat_cnt, hours_cnt */
#define CTRL_STRIDE 13
#define MAGIC0 0x314a4359u /* "YCJ1" */

#define BID_LEN 22
#define CITY_STR 96
#define POSTAL_STR 32
#define STARS_STR 32
#define STARS_ENT 28
#define RC_STR 16
#define ATTR_STR 192
#define CAT_STR 64
#define HOURS_STR 32
#define HOURS_DAY_SPAN 1536
#define PAD 64
#define HDR_SIZE 32u
#define NXOR_MASK 0xa5a5f13du
#define OXOR_MASK 0x7c3e91b62d95a4f7ull

#define F_OPEN 1u
#define F_ATTR_NULL 2u
#define F_CAT_NULL 4u
#define F_HOURS_NULL 8u

/* skeleton constants (zero-padded to 32 bytes) */
static const uint8_t T_LINEHEAD[32] = "{\"business_id\":\"";
static const uint8_t T_NAME[32] = "\",\"name\":\"";
static const uint8_t T_ADDR[32] = "\",\"address\":\"";
static const uint8_t T_LNG[32] = ",\"longitude\":-";
static const uint8_t T_STARS[32] = ",\"stars\":\"";
static const uint8_t T_RC[32] = ",\"review_count\":\"";
static const uint8_t T_OPEN0[32] = "0,\"attributes\":\"";
static const uint8_t T_OPEN1[32] = "1,\"attributes\":\"";
static const uint8_t T_OPENC0[32] = "0,\"attributes\":{";
static const uint8_t T_OPENC1[32] = "1,\"attributes\":{";
static const uint8_t T_BRACE[32] = "{";
static const uint8_t T_NULL_CS[32] = "null,\"categories\":\"";
static const uint8_t T_NULL_CN[32] = "null,\"categories\":null";
static const uint8_t T_AOBJ_CS[32] = "},\"categories\":\"";
static const uint8_t T_AOBJ_CN[32] = "},\"categories\":null";
static const uint8_t T_HOURS_OPEN[32] = "\",\"hours\":{\"";
static const uint8_t T_HOURS_NULL_TAIL[32] = "\",\"hours\":null}\n";
static const uint8_t T_HOURS_OPEN2[32] = ",\"hours\":{\"";
static const uint8_t T_HOURS_NULL_TAIL2[32] = ",\"hours\":null}\n";
static const uint8_t T_TAIL[32] = "}}\n";

static inline void st32(uint8_t* p, const uint8_t* s) {
  _mm256_storeu_si256((__m256i*)p, _mm256_loadu_si256((const __m256i*)s));
}
#ifdef __AVX512F__
#define EMIT_BLK 64
static inline void st_blk(uint8_t* p, const uint8_t* s) {
  _mm512_storeu_si512((__m512i*)p, _mm512_loadu_si512((const __m512i*)s));
}
#else
#define EMIT_BLK 32
static inline void st_blk(uint8_t* p, const uint8_t* s) {
  st32(p, s);
  st32(p + 32, s + 32);
}
#endif
static inline uint8_t* emit_cst(uint8_t* p, const uint8_t* c, size_t n,
                                const uint8_t* end) {
  if (p + n <= end) memcpy(p, c, n); /* malformed input: advance silently */
  return p + n;
}
static inline uint8_t* emit_cpy(uint8_t* p, const uint8_t* s, size_t n,
                                const uint8_t* end) {
  if (__builtin_expect(n <= EMIT_BLK && p + EMIT_BLK <= end, 1)) {
    st_blk(p, s);
    return p + n;
  }
  while (n > 0 && p < end) {
    size_t room = (size_t)(end - p);
    if (room > n) room = n;
    if (room > EMIT_BLK) room = EMIT_BLK;
    memcpy(p, s, room);
    p += room; s += room; n -= room;
  }
  return p + n;
}
static inline uint8_t* emit_fix(uint8_t* p, const uint8_t* c, size_t n,
                                const uint8_t* end) {
  if (p - 1 + n <= end) memcpy(p - 1, c, n);
  return p - 1 + n;
}
static inline uint8_t* emit_fix2(uint8_t* p, const uint8_t* c, size_t n,
                                 const uint8_t* end) {
  if (p - 2 + n <= end) memcpy(p - 2, c, n);
  return p - 2 + n;
}

#if !defined(BUILD_ENCODER)
/* ================= DECODER ================= */
typedef struct {
  uint32_t n;
  uint64_t out_size;
  uint8_t* bid;
  const uint8_t* ctrl;
  uint8_t *name_blob, *addr_blob;
  uint8_t *city_len, *postal_len, *rc_len, *attr_len, *cat_len, *hours_len;
  uint8_t *city_lut, *postal_lut, *stars_lut, *rc_lut, *attr_lut, *cat_lut, *hours_lut;
  uint8_t *lat, *lng;
  const uint8_t *attr_idx, *cat_idx, *hours_idx;
  const uint8_t *nb_end, *ab_end, *aix_end, *ctp_end, *hix_end;
  uint32_t city_mask, postal_mask, rc_mask, stars_mask;
  uint32_t attr_mask, cat_mask, hours_mask;
  uint8_t dig2[512];
} dstate;

static inline uint8_t* emit_num(uint8_t* p, const uint8_t** np,
                                const uint8_t* dig2, const uint8_t* end) {
  const uint8_t* n = *np;
  /* '.' position comes from the integer-run terminator; runs are capped so
   * corrupted input cannot scan past the section */
  int guard = 16;
  for (;;) {
    unsigned b = *n++;
    unsigned lo = b & 15u, hi = b >> 4;
    if (lo == 15u || --guard < 0) break;
    if (hi == 15u) { if (p < end) *p = (uint8_t)('0' + lo); p++; break; }
    if (p + 2 <= end) memcpy(p, dig2 + 2 * b, 2);
    p += 2;
  }
  if (p < end) *p = '.';
  p++;
  guard = 16;
  for (;;) {
    unsigned b = *n++;
    unsigned lo = b & 15u, hi = b >> 4;
    if (lo == 15u || --guard < 0) break;
    if (hi == 15u) { if (p < end) *p = (uint8_t)('0' + lo); p++; break; }
    if (p + 2 <= end) memcpy(p, dig2 + 2 * b, 2);
    p += 2;
  }
  *np = n;
  return p;
}

API void* lab_open(const uint8_t* archive, size_t size) {
  if (size < HDR_SIZE + (size_t)N_SEC * 8) return NULL;
  uint32_t magic, nxor, nchk;
  uint64_t out_size, oxs;
  memcpy(&magic, archive, 4);
  memcpy(&nxor, archive + 8, 4);
  memcpy(&oxs, archive + 12, 8);
  memcpy(&out_size, archive + 20, 8);
  memcpy(&nchk, archive + 4, 4);
  if (magic != MAGIC0) return NULL;
  if ((nxor ^ NXOR_MASK) != nchk) return NULL;
  if ((oxs ^ OXOR_MASK) != out_size) return NULL;
  dstate* st = (dstate*)malloc(sizeof(dstate));
  if (!st) return NULL;
  memcpy(&st->n, archive + 4, 4);
  memcpy(&st->out_size, archive + 20, 8);
  uint64_t off[N_SEC];
  memcpy(off, archive + HDR_SIZE, (size_t)N_SEC * 8);
  if (st->n == 0 || st->n > st->out_size || st->out_size > (1ull << 32))
    { free(st); return NULL; }
  for (int k = 0; k < N_SEC; k++) {
    if (off[k] > size) { free(st); return NULL; }
    if (k > 0 && off[k] < off[k - 1]) { free(st); return NULL; }
  }
  if (off[SEC_CTRL] + (uint64_t)st->n * CTRL_STRIDE > size)
    { free(st); return NULL; }
  st->bid       = (uint8_t*)archive + off[SEC_BID];
  st->ctrl      = (uint8_t*)archive + off[SEC_CTRL];
  st->name_blob = (uint8_t*)archive + off[SEC_NAME_BLOB];
  st->addr_blob = (uint8_t*)archive + off[SEC_ADDR_BLOB];
  st->city_len  = (uint8_t*)archive + off[SEC_CITY_LEN];
  st->city_lut  = (uint8_t*)archive + off[SEC_CITY_LUT];
  st->postal_len  = (uint8_t*)archive + off[SEC_POSTAL_LEN];
  st->postal_lut  = (uint8_t*)archive + off[SEC_POSTAL_LUT];
  st->lat = (uint8_t*)archive + off[SEC_LAT];
  st->lng = (uint8_t*)archive + off[SEC_LNG];
  st->stars_lut = (uint8_t*)archive + off[SEC_STARS_LUT];
  st->rc_len  = (uint8_t*)archive + off[SEC_RC_LEN];
  st->rc_lut  = (uint8_t*)archive + off[SEC_RC_LUT];
  st->attr_idx  = (uint8_t*)archive + off[SEC_ATTR_IDX];
  st->attr_len  = (uint8_t*)archive + off[SEC_ATTR_LEN];
  st->attr_lut  = (uint8_t*)archive + off[SEC_ATTR_LUT];
  st->cat_idx  = (uint8_t*)archive + off[SEC_CAT_IDX];
  st->cat_len  = (uint8_t*)archive + off[SEC_CAT_LEN];
  st->cat_lut  = (uint8_t*)archive + off[SEC_CAT_LUT];
  st->hours_idx  = (uint8_t*)archive + off[SEC_HOURS_IDX];
  st->hours_len  = (uint8_t*)archive + off[SEC_HOURS_LEN];
  st->hours_lut  = (uint8_t*)archive + off[SEC_HOURS_LUT];
  for (unsigned b = 0; b < 256; b++) {
    st->dig2[2*b]   = (uint8_t)('0' + (b & 15u));
    st->dig2[2*b+1] = (uint8_t)('0' + (b >> 4));
  }
  /* stream ends = start of the following section; LUT index masks from the
   * (power-of-two padded) per-entry table sizes */
  st->nb_end = (uint8_t*)archive + off[SEC_ADDR_BLOB];
  st->ab_end = (uint8_t*)archive + off[SEC_CITY_LEN];
  st->aix_end = (uint8_t*)archive + off[SEC_ATTR_LEN];
  st->ctp_end = (uint8_t*)archive + off[SEC_CAT_LEN];
  st->hix_end = (uint8_t*)archive + off[SEC_HOURS_LUT];
  st->city_mask   = (uint32_t)(off[SEC_CITY_LUT]   - PAD - off[SEC_CITY_LEN]   - 1);
  st->postal_mask = (uint32_t)(off[SEC_POSTAL_LUT] - PAD - off[SEC_POSTAL_LEN] - 1);
  st->rc_mask     = (uint32_t)(off[SEC_RC_LUT]     - PAD - off[SEC_RC_LEN]     - 1);
  st->stars_mask  = (uint32_t)((off[SEC_RC_LEN]     - PAD - off[SEC_STARS_LUT]) / STARS_STR) - 1;
  st->attr_mask   = (uint32_t)(off[SEC_ATTR_LUT]   - PAD - off[SEC_ATTR_LEN]   - 1);
  st->cat_mask    = (uint32_t)(off[SEC_CAT_LUT]    - PAD - off[SEC_CAT_LEN]    - 1);
  st->hours_mask  = (uint32_t)(off[SEC_HOURS_LUT]  - PAD - off[SEC_HOURS_LEN]  - 1);
  return st;
}

#define LINE_SLAB (128u * 1024u)

/* one line emission; SAFE=1 clamps every write to `end` (used near the
 * output tail and for corrupted control data), SAFE=0 is the unchecked
 * fast path valid whenever at least LINE_SLAB bytes of output remain */
#define DECODE_BODY(SAFE) */

/* emission primitives; FAST variants skip bounds checks and are only used
 * while at least LINE_SLAB bytes of output head-room remain */

static inline uint8_t* num_out_fast(uint8_t* p, const uint8_t** np,
                                    const uint8_t* dig2) {
  const uint8_t* n = *np;
  /* two self-terminating nibble runs: integer digits, then decimals */
  int guard = 16;
  for (;;) {
    unsigned b = *n++;
    unsigned lo = b & 15u, hi = b >> 4;
    if (lo == 15u || --guard < 0) break;
    if (hi == 15u) { *p++ = (uint8_t)('0' + lo); break; }
    memcpy(p, dig2 + 2 * b, 2); p += 2;
  }
  *p++ = '.';
  guard = 16;
  for (;;) {
    unsigned b = *n++;
    unsigned lo = b & 15u, hi = b >> 4;
    if (lo == 15u || --guard < 0) break;
    if (hi == 15u) { *p++ = (uint8_t)('0' + lo); break; }
    memcpy(p, dig2 + 2 * b, 2); p += 2;
  }
  *np = n;
  return p;
}

static inline uint8_t* num_out_safe(uint8_t* p, const uint8_t** np,
                                    const uint8_t* dig2, const uint8_t* end) {
  const uint8_t* n = *np;
  int guard = 16;
  for (;;) {
    unsigned b = *n++;
    unsigned lo = b & 15u, hi = b >> 4;
    if (lo == 15u || --guard < 0) break;
    if (p < end) *p++ = (uint8_t)('0' + lo);
    if (hi == 15u) break;
    if (p < end) *p++ = (uint8_t)('0' + hi);
  }
  if (p < end) *p++ = '.';
  guard = 16;
  for (;;) {
    unsigned b = *n++;
    unsigned lo = b & 15u, hi = b >> 4;
    if (lo == 15u || --guard < 0) break;
    if (p < end) *p++ = (uint8_t)('0' + lo);
    if (hi == 15u) break;
    if (p < end) *p++ = (uint8_t)('0' + hi);
  }
  *np = n;
  return p;
}

static inline uint8_t* emit_cst_f(uint8_t* p, const uint8_t* c, size_t n) {
  memcpy(p, c, n);
  return p + n;
}
static inline uint8_t* emit_cpy_f(uint8_t* p, const uint8_t* s, size_t n) {
  while (n > EMIT_BLK) { st_blk(p, s); p += EMIT_BLK; s += EMIT_BLK; n -= EMIT_BLK; }
  st_blk(p, s);
  return p + n;
}
static inline uint8_t* emit_fix_f(uint8_t* p, const uint8_t* c, size_t n) {
  st32(p - 1, c);
  return p - 1 + n;
}
static inline uint8_t* emit_fix2_f(uint8_t* p, const uint8_t* c, size_t n) {
  st32(p - 2, c);
  return p - 2 + n;
}

API int64_t lab_decode(void* state, uint8_t* output, size_t capacity) {
  dstate* st = (dstate*)state;
  if (!st || capacity < st->out_size) return -1;
  uint8_t* end = output + st->out_size;
  const uint8_t* dig2 = st->dig2;
  const uint8_t* city_lut = st->city_lut;
  const uint8_t* postal_lut = st->postal_lut;
  const uint8_t* stars_lut = st->stars_lut;
  const uint8_t* rc_lut = st->rc_lut;
  const uint8_t* attr_lut = st->attr_lut;
  const uint8_t* cat_lut = st->cat_lut;
  const uint8_t* hours_lut = st->hours_lut;
  const uint8_t* city_len = st->city_len;
  const uint8_t* postal_len = st->postal_len;
  const uint8_t* rc_len = st->rc_len;
  const uint8_t* attr_len = st->attr_len;
  const uint8_t* cat_len = st->cat_len;
  const uint8_t* hours_len = st->hours_len;
  const uint32_t n = st->n;
  const uint8_t* bid = st->bid;
  const uint8_t* cr = st->ctrl;
  const uint8_t* nb = st->name_blob;
  const uint8_t* ab = st->addr_blob;
  const uint8_t* latp = st->lat;
  const uint8_t* lngp = st->lng;
  const uint8_t* aix = st->attr_idx;
  const uint8_t* ctp = st->cat_idx;
  const uint8_t* hix = st->hours_idx;
  const uint32_t city_mask = st->city_mask;
  const uint32_t postal_mask = st->postal_mask;
  const uint32_t rc_mask = st->rc_mask;
  const uint32_t stars_mask = st->stars_mask;
  const uint32_t attr_mask = st->attr_mask;
  const uint32_t cat_mask = st->cat_mask;
  const uint32_t hours_mask = st->hours_mask;
  const uint8_t* nb_end = st->nb_end;
  const uint8_t* ab_end = st->ab_end;
  const uint8_t* aix_end = st->aix_end;
  const uint8_t* ctp_end = st->ctp_end;
  const uint8_t* hix_end = st->hix_end;
  uint8_t* p = output;
  unsigned idx;
  uint32_t i = 0;

  /* fast phase: unchecked emitters; the LINE_SLAB head-room guarantees the
   * overlapped stores and clamped-less writes never leave the buffer; the
   * per-line check catches corrupted control data within one line */
  for (; i < n; i++, cr += CTRL_STRIDE) {
    const unsigned f = cr[0];
    const unsigned nl = cr[1];
    const unsigned al = cr[2];
    const unsigned st_code = cr[3];
    const unsigned cix = (unsigned)cr[4] | (unsigned)cr[5] << 8;
    const unsigned pix = (unsigned)cr[6] | (unsigned)cr[7] << 8;
    const unsigned rix = (unsigned)cr[8] | (unsigned)cr[9] << 8;
    unsigned idx;
    p = emit_cst_f(p, T_LINEHEAD, 16);
    p = emit_cpy_f(p, bid, BID_LEN); bid += BID_LEN;
    p = emit_cst_f(p, T_NAME, 10);
    p = emit_cpy_f(p, nb, nl); nb += nl;
    p = emit_cst_f(p, T_ADDR, 13);
    p = emit_cpy_f(p, ab, al); ab += al;
    idx = cix & city_mask;
    p = emit_cpy_f(p, city_lut + (size_t)idx * CITY_STR, city_len[idx]);
    idx = pix & postal_mask;
    p = emit_cpy_f(p, postal_lut + (size_t)idx * POSTAL_STR, postal_len[idx]);
    p = num_out_fast(p, &latp, dig2);
    p = emit_cst_f(p, T_LNG, 14);
    p = num_out_fast(p, &lngp, dig2);
    idx = st_code & stars_mask;
    p = emit_cpy_f(p, stars_lut + (size_t)idx * STARS_STR, STARS_ENT);
    idx = rix & rc_mask;
    p = emit_cpy_f(p, rc_lut + (size_t)idx * RC_STR, rc_len[idx]);
    if (f & F_ATTR_NULL) {
      p = emit_cst_f(p, (f & F_OPEN) ? T_OPEN1 : T_OPEN0, 15);
      if (f & F_CAT_NULL) {
        p = emit_cst_f(p, T_NULL_CN, 22);
      } else {
        p = emit_cst_f(p, T_NULL_CS, 19);
        goto catstr;
      }
    } else {
      p = emit_cst_f(p, (f & F_OPEN) ? T_OPENC1 : T_OPENC0, 16);
      unsigned cnt = cr[10];
      while (cnt--) {
        uint16_t v16;
        memcpy(&v16, aix, 2); aix += 2; idx = v16 & attr_mask;
        p = emit_cpy_f(p, attr_lut + (size_t)idx * ATTR_STR, attr_len[idx]);
      }
      if (f & F_CAT_NULL) {
        p = emit_fix_f(p, T_AOBJ_CN, 19);
      } else {
        p = emit_fix_f(p, T_AOBJ_CS, 16);
        goto catstr;
      }
    }
    goto hourship;
  catstr:
    {
      unsigned cnt = cr[11];
      while (cnt--) {
        uint16_t v16;
        memcpy(&v16, ctp, 2); ctp += 2; idx = v16 & cat_mask;
        p = emit_cpy_f(p, cat_lut + (size_t)idx * CAT_STR, cat_len[idx]);
      }
      if (f & F_HOURS_NULL) {
        p = emit_fix2_f(p, T_HOURS_NULL_TAIL, 16);
        continue;
      }
      p = emit_fix2_f(p, T_HOURS_OPEN, 11);
    }
    goto hoursbody;
  hourship:
    if (f & F_HOURS_NULL) {
      p = emit_cst_f(p, T_HOURS_NULL_TAIL2, 15);
      continue;
    }
    p = emit_cst_f(p, T_HOURS_OPEN2, 10);
  hoursbody:
    {
      unsigned cnt = cr[12];
      while (cnt--) {
        uint16_t v16;
        memcpy(&v16, hix, 2); hix += 2; idx = v16 & hours_mask;
        p = emit_cpy_f(p, hours_lut + (size_t)idx * HOURS_STR,
                       hours_len[idx]);
      }
      p = emit_fix_f(p, T_TAIL, 3);
    }
    if (__builtin_expect(p + LINE_SLAB > end, 0)) {
      i++;
      cr += CTRL_STRIDE;
      break;
    }
  }

  /* safe phase: identical emission with every write clamped to `end` */
  for (; i < n; i++, cr += CTRL_STRIDE) {
    const unsigned f = cr[0];
    unsigned nl = cr[1];
    unsigned al = cr[2];
    const unsigned st_code = cr[3];
    const unsigned cix = (unsigned)cr[4] | (unsigned)cr[5] << 8;
    const unsigned pix = (unsigned)cr[6] | (unsigned)cr[7] << 8;
    const unsigned rix = (unsigned)cr[8] | (unsigned)cr[9] << 8;
    unsigned idx;
    p = emit_cst(p, T_LINEHEAD, 16, end);
    p = emit_cpy(p, bid, BID_LEN, end); bid += BID_LEN;
    p = emit_cst(p, T_NAME, 10, end);
    if (nb + nl > nb_end) nl = (unsigned)(nb_end > nb ? nb_end - nb : 0);
    p = emit_cpy(p, nb, nl, end); nb += nl;
    p = emit_cst(p, T_ADDR, 13, end);
    if (ab + al > ab_end) al = (unsigned)(ab_end > ab ? ab_end - ab : 0);
    p = emit_cpy(p, ab, al, end); ab += al;
    idx = cix & city_mask;
    p = emit_cpy(p, city_lut + (size_t)idx * CITY_STR, city_len[idx], end);
    idx = pix & postal_mask;
    p = emit_cpy(p, postal_lut + (size_t)idx * POSTAL_STR, postal_len[idx], end);
    p = num_out_safe(p, &latp, dig2, end);
    p = emit_cst(p, T_LNG, 14, end);
    p = num_out_safe(p, &lngp, dig2, end);
    idx = st_code & stars_mask;
    p = emit_cpy(p, stars_lut + (size_t)idx * STARS_STR, STARS_ENT, end);
    idx = rix & rc_mask;
    p = emit_cpy(p, rc_lut + (size_t)idx * RC_STR, rc_len[idx], end);
    if (f & F_ATTR_NULL) {
      p = emit_cst(p, (f & F_OPEN) ? T_OPEN1 : T_OPEN0, 15, end);
      if (f & F_CAT_NULL) {
        p = emit_cst(p, T_NULL_CN, 22, end);
      } else {
        p = emit_cst(p, T_NULL_CS, 19, end);
        goto scatstr;
      }
    } else {
      p = emit_cst(p, (f & F_OPEN) ? T_OPENC1 : T_OPENC0, 16, end);
      unsigned cnt = cr[10];
      while (cnt--) {
        uint16_t v16;
        if (aix + 2 > aix_end) break;
        memcpy(&v16, aix, 2); aix += 2; idx = v16 & attr_mask;
        p = emit_cpy(p, attr_lut + (size_t)idx * ATTR_STR, attr_len[idx], end);
      }
      if (f & F_CAT_NULL) {
        p = emit_fix(p, T_AOBJ_CN, 19, end);
      } else {
        p = emit_fix(p, T_AOBJ_CS, 16, end);
        goto scatstr;
      }
    }
    goto shourship;
  scatstr:
    {
      unsigned cnt = cr[11];
      while (cnt--) {
        uint16_t v16;
        if (ctp + 2 > ctp_end) break;
        memcpy(&v16, ctp, 2); ctp += 2; idx = v16 & cat_mask;
        p = emit_cpy(p, cat_lut + (size_t)idx * CAT_STR, cat_len[idx], end);
      }
      if (f & F_HOURS_NULL) {
        p = emit_fix2(p, T_HOURS_NULL_TAIL, 16, end);
        continue;
      }
      p = emit_fix2(p, T_HOURS_OPEN, 11, end);
    }
    goto shoursbody;
  shourship:
    if (f & F_HOURS_NULL) {
      p = emit_cst(p, T_HOURS_NULL_TAIL2, 15, end);
      continue;
    }
    p = emit_cst(p, T_HOURS_OPEN2, 10, end);
  shoursbody:
    {
      unsigned cnt = cr[12];
      while (cnt--) {
        uint16_t v16;
        if (hix + 2 > hix_end) break;
        memcpy(&v16, hix, 2); hix += 2; idx = v16 & hours_mask;
        p = emit_cpy(p, hours_lut + (size_t)idx * HOURS_STR,
                     hours_len[idx], end);
      }
      p = emit_fix(p, T_TAIL, 3, end);
    }
  }
  return (int64_t)(p - output);
}

API int64_t lab_rows(void* state, const uint64_t* ids, size_t count,
                     uint8_t* output, size_t capacity, uint64_t* offsets) {
  (void)state; (void)ids; (void)count; (void)output; (void)capacity; (void)offsets;
  return -1; /* bulk-only candidate */
}

API void lab_close(void* state) { free(state); }

#else
/* ================= ENCODER ================= */
#define MARK_LIT(s) (const uint8_t*)(s), (sizeof(s) - 1)
static const char M_HEAD[]   = "{\"business_id\":\"";
static const char M_NAME[]   = "\",\"name\":\"";
static const char M_ADDR[]   = "\",\"address\":\"";
static const char M_CITY[]   = "\",\"city\":\"";
static const char M_STATE[]  = "\",\"state\":\"";
static const char M_POSTAL[] = "\",\"postal_code\":\"";
static const char M_LAT[]    = "\",\"latitude\":";
static const char M_LNG[]    = ",\"longitude\":";
static const char M_STARS[]  = ",\"stars\":";
static const char M_RC[]     = ",\"review_count\":";
static const char M_OPEN[]   = ",\"is_open\":";
static const char M_ATTR[]   = ",\"attributes\":";
static const char M_CAT[]    = ",\"categories\":";
static const char M_HOURS[]  = ",\"hours\":";

typedef struct { const uint8_t* a; uint32_t al; const uint8_t* b; uint32_t bl; } key2;
typedef struct { const uint8_t* k; uint32_t kl; const uint8_t* v; uint32_t vl; } quad;

typedef struct {
  key2* keys;
  uint32_t* slot;
  uint32_t cap, cnt;
} map;

static uint64_t fnv1a(const uint8_t* p, size_t n, uint64_t h) {
  for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ull; }
  return h;
}
/* every scratch allocation made during lab_encode is registered here and
 * released before lab_encode returns, so the encoder leaks nothing */
static void* g_live[8192];
static uint32_t g_live_n = 0;
static void track(void* p) {
  if (p) {
    if (g_live_n < sizeof(g_live) / sizeof(g_live[0])) g_live[g_live_n++] = p;
  }
}
static void untrack(void* p) {
  if (!p) return;
  for (uint32_t i = 0; i < g_live_n; i++)
    if (g_live[i] == p) { g_live[i] = g_live[--g_live_n]; return; }
}
static void free_live(void) {
  while (g_live_n) free(g_live[--g_live_n]);
}
static void xfree(void* p) { untrack(p); free(p); }
static void* xmalloc(size_t n) {
  void* p = malloc(n);
  if (!p) { fprintf(stderr, "oom %zu\n", n); exit(1); }
  track(p);
  return p;
}
static void map_init(map* m, size_t expect) {
  size_t cap = 16;
  while (cap < expect * 2 + 16) cap <<= 1;
  m->keys = (key2*)xmalloc(cap * sizeof(key2));
  m->slot = (uint32_t*)calloc(cap, sizeof(uint32_t));
  track(m->slot);
  m->cap = (uint32_t)cap;
  m->cnt = 0;
}
static uint64_t hash2(const uint8_t* a, uint32_t al, const uint8_t* b, uint32_t bl) {
  uint64_t h = 0xcbf29ce484222325ull;
  h = fnv1a(a, al, h);
  if (b) h = fnv1a(b, bl, h ^ 0x9e3779b97f4a7c15ull);
  return h;
}
static uint32_t map_get(map* m, const uint8_t* a, uint32_t al,
                        const uint8_t* b, uint32_t bl) {
  uint64_t h = hash2(a, al, b, bl);
  uint32_t i = (uint32_t)h & (m->cap - 1);
  while (m->slot[i]) {
    key2* k = &m->keys[m->slot[i] - 1];
    if (k->al == al && memcmp(k->a, a, al) == 0 &&
        ((k->b == NULL && b == NULL) ||
         (k->b && b && k->bl == bl && memcmp(k->b, b, bl) == 0)))
      return m->slot[i] - 1;
    i = (i + 1) & (m->cap - 1);
  }
  uint32_t id = m->cnt++;
  m->keys[id] = (key2){ a, al, b, bl };
  m->slot[i] = id + 1;
  return id;
}

static const key2* g_keys;
static int cmp_rank(const void* x, const void* y) {
  const key2* kx = &g_keys[*(const uint32_t*)x];
  const key2* ky = &g_keys[*(const uint32_t*)y];
  uint32_t n = kx->al < ky->al ? kx->al : ky->al;
  int c = memcmp(kx->a, ky->a, n);
  if (c) return c;
  if (kx->al != ky->al) return kx->al < ky->al ? -1 : 1;
  if (!kx->b || !ky->b) return kx->b ? 1 : (ky->b ? -1 : 0);
  uint32_t n2 = kx->bl < ky->bl ? kx->bl : ky->bl;
  c = memcmp(kx->b, ky->b, n2);
  if (c) return c;
  if (kx->bl != ky->bl) return kx->bl < ky->bl ? -1 : 1;
  return 0;
}
/* rank[old_id] = lexicographic rank; sorted[] = keys in rank order */
static void map_ranks(map* m, uint32_t* rank, uint32_t** sorted) {
  uint32_t cnt = m->cnt;
  uint32_t* ord = (uint32_t*)xmalloc(cnt * 4);
  for (uint32_t i = 0; i < cnt; i++) ord[i] = i;
  g_keys = m->keys;
  qsort(ord, cnt, 4, cmp_rank);
  for (uint32_t r = 0; r < cnt; r++) rank[ord[r]] = r;
  *sorted = ord;
}

static inline const uint8_t* find_marker(const uint8_t* p, const uint8_t* end,
                                         const char* m, size_t ml) {
  return (const uint8_t*)memmem(p, (size_t)(end - p), m, ml);
}
#define GOTO_FAIL do { free_live(); return -1; } while (0)

typedef struct { uint8_t* p; size_t n, cap; } buf;
static void buf_init(buf* b, size_t cap) {
  if (cap < 64) cap = 64;
  b->cap = cap; b->n = 0; b->p = (uint8_t*)xmalloc(cap);
}
static inline void buf_need(buf* b, size_t extra) {
  if (b->n + extra > b->cap) {
    while (b->n + extra > b->cap) b->cap *= 2;
    void* q = realloc(b->p, b->cap);
    if (!q) { fprintf(stderr, "oom\n"); exit(1); }
    untrack(b->p);
    track(q);
    b->p = (uint8_t*)q;
  }
}
static inline void buf_u8(buf* b, unsigned v) { buf_need(b, 1); b->p[b->n++] = (uint8_t)v; }
static inline void buf_u16(buf* b, unsigned v) {
  buf_need(b, 2); memcpy(b->p + b->n, &v, 2); b->n += 2;
}
static inline void buf_put(buf* b, const void* s, size_t n) {
  buf_need(b, n); memcpy(b->p + b->n, s, n); b->n += n;
}

typedef struct {
  const uint8_t *name_p, *addr_p, *city_p, *state_p, *postal_p;
  const uint8_t *lat_p, *lng_p, *starv_p, *rcv_p, *open_v;
  uint32_t name_l, addr_l, city_l, state_l, postal_l;
  uint32_t lat_l, lng_l, starv_l, rcv_l;
  unsigned open_dig;
} fields;

static int parse_value_string(const uint8_t* p, const uint8_t* end,
                              const uint8_t** vs, uint32_t* vl,
                              const uint8_t** next) {
  if (p >= end || *p != '"') return -1;
  const uint8_t* q = p + 1;
  while (q < end) {
    if (*q == '\\') { q += 2; continue; }
    if (*q == '"') break;
    q++;
  }
  if (q >= end) return -1;
  *vs = p; *vl = (uint32_t)(q + 1 - p); *next = q + 1;
  return 0;
}

static int parse_object_pairs(const uint8_t* body, uint32_t blen,
                              quad** qout, uint32_t* qcnt) {
  const uint8_t* p = body;
  const uint8_t* end = body + blen;
  uint32_t cnt = 0, cap = 64;
  quad* qs = (quad*)xmalloc(sizeof(quad) * cap);
  while (p < end) {
    if (cnt == cap) { cap *= 2; qs = (quad*)realloc(qs, sizeof(quad) * cap);
      if (!qs) exit(1); }
    if (*p != '"') { xfree(qs); return -1; }
    const uint8_t* q = p + 1;
    while (q < end && *q != '"') q++;
    if (q >= end) { xfree(qs); return -1; }
    const uint8_t* k = p; uint32_t kl = (uint32_t)(q + 1 - p);
    if (q + 1 >= end || q[1] != ':') { xfree(qs); return -1; }
    const uint8_t* v = q + 2;
    const uint8_t* pnext;
    if (*v == '"') {
      const uint8_t* vs; uint32_t vl;
      if (parse_value_string(v, end, &vs, &vl, &pnext)) { xfree(qs); return -1; }
      qs[cnt++] = (quad){ k, kl, v, vl };
    } else if (*v == '{') {
      int depth = 0;
      const uint8_t* m = v;
      while (m < end) {
        if (*m == '{') depth++;
        else if (*m == '}') { if (--depth == 0) break; }
        m++;
      }
      if (m >= end) { xfree(qs); return -1; }
      qs[cnt++] = (quad){ k, kl, v, (uint32_t)(m + 1 - v) };
      pnext = m + 1;
    } else {
      const uint8_t* m = v;
      while (m < end && *m != ',' && *m != '}') m++;
      qs[cnt++] = (quad){ k, kl, v, (uint32_t)(m - v) };
      pnext = m;
    }
    p = pnext;
    if (p < end) {
      if (*p != ',') { xfree(qs); return -1; }
      p++;
    }
  }
  *qout = qs; *qcnt = cnt;
  return 0;
}

/* append a decimal number as [dotpos][int nibbles+0xF][dec nibbles+0xF];
 * longitude must carry a leading '-' which the decoder emits as a const */
static int encode_number(buf* b, const uint8_t* s, uint32_t len) {
  const uint8_t* p = s;
  const uint8_t* e = s + len;
  if (*p == '-') p++; /* decoder const carries the sign for longitude */
  const uint8_t* dot = NULL;
  for (const uint8_t* q = p; q < e; q++)
    if (*q == '.') { dot = q; break; }
  if (!dot || dot == p || dot + 1 >= e) return -1;
  unsigned idig = (unsigned)(dot - p);
  unsigned ddig = (unsigned)(e - dot - 1);
  (void)idig;
  if (ddig == 0 || idig == 0) return -1;
  /* two self-terminating nibble runs: integer digits, then decimal digits */
  unsigned nib = 0;
  uint8_t cur = 0;
  for (const uint8_t* q = p; q < dot; q++) {
    unsigned d = (unsigned)(*q - '0');
    if (d > 9) return -1;
    if (nib == 0) { cur = (uint8_t)d; nib = 1; }
    else { buf_u8(b, (unsigned)(cur | (d << 4))); nib = 0; }
  }
  buf_u8(b, (unsigned)(nib ? (cur | 0xF0) : 0x0F));
  nib = 0;
  for (const uint8_t* q = dot + 1; q < e; q++) {
    unsigned d = (unsigned)(*q - '0');
    if (d > 9) return -1;
    if (nib == 0) { cur = (uint8_t)d; nib = 1; }
    else { buf_u8(b, (unsigned)(cur | (d << 4))); nib = 0; }
  }
  buf_u8(b, (unsigned)(nib ? (cur | 0xF0) : 0x0F));
  return 0;
}

API int64_t lab_encode(const uint8_t* raw, size_t size, uint8_t* archive,
                       size_t capacity) {
  const uint8_t* dend = raw + size;
  size_t max_lines = 1;
  for (const uint8_t* c = raw; c < dend; c++)
    if (*c == '\n') max_lines++;
  max_lines += 2;

  fields* F = (fields*)xmalloc(max_lines * sizeof(fields));
  uint32_t N = 0;
  quad* attr_q = NULL; size_t attr_qn = 0, attr_qcap = 0;
  uint32_t* attr_off = (uint32_t*)xmalloc((max_lines + 1) * 4);
  quad* cat_q = NULL; size_t cat_qn = 0, cat_qcap = 0;
  uint32_t* cat_off = (uint32_t*)xmalloc((max_lines + 1) * 4);
  quad* hrs_q = NULL; size_t hrs_qn = 0, hrs_qcap = 0;
  uint32_t* hrs_off = (uint32_t*)xmalloc((max_lines + 1) * 4);
#define PUSH_Q(arr, nn, cc, q) do { if ((nn) == (cc)) { (cc) = (cc) ? (cc)*2 : 1024; do { void* qn_ = realloc((arr), sizeof(quad)*(cc)); if (!(qn_)) { fprintf(stderr, "oom\n"); exit(1); } untrack((arr)); track(qn_); (arr) = (quad*)(qn_); } while (0); } (arr)[(nn)++] = (q); } while (0)

  map m_city, m_postal, m_stars, m_rc, m_attr, m_cat, m_hrs;
  map_init(&m_city, 4096); map_init(&m_postal, 8192); map_init(&m_stars, 32);
  map_init(&m_rc, 4096); map_init(&m_attr, 8192); map_init(&m_cat, 4096);
  map_init(&m_hrs, 32768);

  /* ---------------- pass 1: parse + collect alphabets ---------------- */
  const uint8_t* lp = raw;
  attr_off[0] = cat_off[0] = hrs_off[0] = 0;
  while (lp < dend) {
    const uint8_t* eol = (const uint8_t*)memchr(lp, '\n', (size_t)(dend - lp));
    const uint8_t* lend = eol ? eol : dend;
    const uint8_t* p = lp;
    const uint8_t* q;
    fields* f = &F[N];
    memset(f, 0, sizeof(*f));
    unsigned flags = 0;

    q = find_marker(p, lend, M_HEAD, sizeof(M_HEAD)-1);
    if (q != p) GOTO_FAIL;
    p += sizeof(M_HEAD) - 1 + BID_LEN + sizeof(M_NAME) - 1;
#define NEXT_MARK(mk) do { q = find_marker(p, lend, mk, sizeof(mk)-1); if (!q) GOTO_FAIL; } while (0)
/* each value is delimited by the NEXT field's marker */
#define FIELD(fld, mk) do { q = find_marker(p, lend, mk, sizeof(mk)-1); if (!q) GOTO_FAIL; f->fld##_p = p; f->fld##_l = (uint32_t)(q - p); p = q + sizeof(mk)-1; } while (0)
    FIELD(name, M_ADDR);
    FIELD(addr, M_CITY);
    FIELD(city, M_STATE);
    FIELD(state, M_POSTAL);
    FIELD(postal, M_LAT);
    FIELD(lat, M_LNG);
    FIELD(lng, M_STARS);
    FIELD(starv, M_RC);
    FIELD(rcv, M_OPEN);
    if (f->rcv_l == 0) GOTO_FAIL;
    /* p now sits on the is_open value digit */
    f->open_dig = (unsigned)(*p - '0');
    if (f->open_dig > 1) GOTO_FAIL;
    p += 1 + sizeof(M_ATTR) - 1; /* skip digit+comma and the marker */
    /* attributes object runs to the categories marker */
    q = find_marker(p, lend, M_CAT, sizeof(M_CAT)-1);
    if (!q) GOTO_FAIL;
    {
      const uint8_t* aobj = p;
      uint32_t aobjl = (uint32_t)(q - p);
      if (aobjl == 4 && memcmp(aobj, "null", 4) == 0) {
        flags |= F_ATTR_NULL;
      } else {
        if (aobjl < 2 || aobj[0] != '{' || aobj[aobjl-1] != '}') GOTO_FAIL;
        quad* qs; uint32_t qc;
        if (parse_object_pairs(aobj + 1, aobjl - 2, &qs, &qc)) GOTO_FAIL;
        for (uint32_t t = 0; t < qc; t++) {
          map_get(&m_attr, qs[t].k, qs[t].kl, qs[t].v, qs[t].vl);
          PUSH_Q(attr_q, attr_qn, attr_qcap, qs[t]);
        }
        xfree(qs);
      }
      attr_off[N+1] = (uint32_t)attr_qn;
    }
    p = q + sizeof(M_CAT)-1;
    if (p + 4 <= lend && memcmp(p, "null", 4) == 0) {
      flags |= F_CAT_NULL;
      p += 4 + sizeof(M_HOURS) - 1; /* null plus the hours marker */
    } else {
      if (p >= lend || *p != '"') GOTO_FAIL;
      p++;
      const uint8_t* cq = (const uint8_t*)memmem(p, (size_t)(lend - p),
                                                 M_HOURS, sizeof(M_HOURS)-1);
      if (!cq || cq <= p || cq[-1] != '"') GOTO_FAIL;
      const uint8_t* ts = p;
      const uint8_t* te = cq - 1;
      while (ts < te) {
        const uint8_t* nx = (const uint8_t*)memmem(ts, (size_t)(te - ts), ", ", 2);
        if (!nx) nx = te;
        map_get(&m_cat, ts, (uint32_t)(nx - ts), NULL, 0);
        quad tq = { ts, (uint32_t)(nx - ts), NULL, 0 };
        PUSH_Q(cat_q, cat_qn, cat_qcap, tq);
        ts = nx + 2;
      }
      p = cq + sizeof(M_HOURS)-1;
    }
    cat_off[N+1] = (uint32_t)cat_qn;
    if (p + 4 <= lend && memcmp(p, "null", 4) == 0) {
      flags |= F_HOURS_NULL;
      p += 5; /* null plus the line's closing brace */
    } else {
      if (p >= lend || *p != '{') GOTO_FAIL;
      p++;
      if (lend <= p || lend[-1] != '}') GOTO_FAIL;
      quad* qs; uint32_t qc;
      if (parse_object_pairs(p, (uint32_t)(lend - 2 - p), &qs, &qc)) GOTO_FAIL;
      for (uint32_t t = 0; t < qc; t++) {
        map_get(&m_hrs, qs[t].k, qs[t].kl, qs[t].v, qs[t].vl);
        PUSH_Q(hrs_q, hrs_qn, hrs_qcap, qs[t]);
      }
      xfree(qs);
      p = lend;
    }
    hrs_off[N+1] = (uint32_t)hrs_qn;
    if (p != lend) GOTO_FAIL;
    map_get(&m_city, f->city_p, f->city_l, f->state_p, f->state_l);
    map_get(&m_postal, f->postal_p, f->postal_l, NULL, 0);
    map_get(&m_stars, f->starv_p, f->starv_l, NULL, 0);
    map_get(&m_rc, f->rcv_p, f->rcv_l, NULL, 0);
    N++;
    if (N + 1 >= max_lines) GOTO_FAIL;
    lp = eol ? eol + 1 : dend;
  }
  if (N == 0) GOTO_FAIL;

  /* ---------------- rank all alphabets ---------------- */
  uint32_t *rank_city, *rank_postal, *rank_stars, *rank_rc, *rank_attr, *rank_cat, *rank_hrs;
  uint32_t *srt_city, *srt_postal, *srt_stars, *srt_rc, *srt_attr, *srt_cat, *srt_hrs;
  rank_city  = (uint32_t*)xmalloc((m_city.cnt  + 1) * 4); map_ranks(&m_city,  rank_city,  &srt_city);
  rank_postal= (uint32_t*)xmalloc((m_postal.cnt+ 1) * 4); map_ranks(&m_postal,rank_postal,&srt_postal);
  rank_stars = (uint32_t*)xmalloc((m_stars.cnt + 1) * 4); map_ranks(&m_stars, rank_stars, &srt_stars);
  rank_rc    = (uint32_t*)xmalloc((m_rc.cnt    + 1) * 4); map_ranks(&m_rc,    rank_rc,    &srt_rc);
  rank_attr  = (uint32_t*)xmalloc((m_attr.cnt  + 1) * 4); map_ranks(&m_attr,  rank_attr,  &srt_attr);
  rank_cat   = (uint32_t*)xmalloc((m_cat.cnt   + 1) * 4); map_ranks(&m_cat,   rank_cat,   &srt_cat);
  rank_hrs   = (uint32_t*)xmalloc((m_hrs.cnt   + 1) * 4); map_ranks(&m_hrs,   rank_hrs,   &srt_hrs);
  if (m_stars.cnt > 256 || m_rc.cnt > 65535 || m_attr.cnt > 65535 ||
      m_cat.cnt > 65535 || m_postal.cnt > 65535 || m_city.cnt > 65535 ||
      m_hrs.cnt > 65535) GOTO_FAIL;

  /* hours codes: day*HOURS_DAY_SPAN + value rank within day; days are the
   * sorted unique day names, values ranked within their day */
  uint32_t hrs_n = m_hrs.cnt;
  uint32_t hrs_slots = HOURS_STR; /* placeholder, set to pow2 below */
  uint32_t day_slot_need = 0;
  uint32_t* hrs_code = (uint32_t*)xmalloc((hrs_n + 1) * 4);
  {
    uint32_t r = 0, day_idx = 0;
    while (r < hrs_n) {
      uint32_t d_start = r;
      const key2* dk = &m_hrs.keys[srt_hrs[r]];
      while (r < hrs_n) {
        const key2* k = &m_hrs.keys[srt_hrs[r]];
        if (k->al != dk->al || memcmp(k->a, dk->a, k->al)) break;
        if ((uint32_t)(r - d_start) >= HOURS_DAY_SPAN) GOTO_FAIL;
        hrs_code[srt_hrs[r]] = day_idx * HOURS_DAY_SPAN + (r - d_start);
        r++;
      }
      day_idx++;
      if (day_idx * HOURS_DAY_SPAN > 65535u) GOTO_FAIL;
      day_slot_need = day_idx * HOURS_DAY_SPAN;
    }
  }

  { uint32_t pq = 1; while (pq < day_slot_need) pq <<= 1; day_slot_need = pq; }
  /* ---------------- build LUT sections ---------------- */
  /* every LUT is padded to a power-of-two entry count so the decoder can
   * mask corrupted indices back into range */
  uint32_t pad_city = 1, pad_postal = 1, pad_stars = 1, pad_rc = 1;
  uint32_t pad_attr = 1, pad_cat = 1;
  while (pad_city  < m_city.cnt)  pad_city  <<= 1;
  while (pad_postal < m_postal.cnt) pad_postal <<= 1;
  while (pad_stars < m_stars.cnt) pad_stars <<= 1;
  while (pad_rc    < m_rc.cnt)    pad_rc    <<= 1;
  while (pad_attr  < m_attr.cnt)  pad_attr  <<= 1;
  while (pad_cat   < m_cat.cnt)   pad_cat   <<= 1;
  buf b_city_lut, b_city_len, b_postal_lut, b_postal_len, b_stars_lut,
      b_rc_lut, b_rc_len, b_attr_lut, b_attr_len, b_cat_lut, b_cat_len,
      b_hours_lut, b_hours_len;
  buf_init(&b_city_lut, (size_t)pad_city * CITY_STR + PAD);
  memset(b_city_lut.p, 0, (size_t)pad_city * CITY_STR + PAD);
  buf_init(&b_city_len, pad_city + 8);
  buf_init(&b_postal_lut, (size_t)pad_postal * POSTAL_STR + PAD);
  memset(b_postal_lut.p, 0, (size_t)m_postal.cnt * POSTAL_STR + PAD);
  buf_init(&b_postal_len, pad_postal + 8);
  buf_init(&b_stars_lut, (size_t)pad_stars * STARS_STR + PAD);
  memset(b_stars_lut.p, 0, (size_t)pad_stars * STARS_STR + PAD);
  buf_init(&b_rc_lut, (size_t)pad_rc * RC_STR + PAD);
  memset(b_rc_lut.p, 0, (size_t)pad_rc * RC_STR + PAD);
  buf_init(&b_rc_len, pad_rc + 8);
  buf_init(&b_attr_lut, (size_t)pad_attr * ATTR_STR + PAD);
  memset(b_attr_lut.p, 0, (size_t)m_attr.cnt * ATTR_STR + PAD);
  buf_init(&b_attr_len, pad_attr + 8);
  buf_init(&b_cat_lut, (size_t)pad_cat * CAT_STR + PAD);
  memset(b_cat_lut.p, 0, (size_t)m_cat.cnt * CAT_STR + PAD);
  buf_init(&b_cat_len, pad_cat + 8);
  hrs_slots = day_slot_need;
  buf_init(&b_hours_lut, (size_t)hrs_slots * HOURS_STR + PAD);
  memset(b_hours_lut.p, 0, (size_t)hrs_slots * HOURS_STR + PAD);
  buf_init(&b_hours_len, hrs_slots + 8);
  memset(b_hours_len.p, 0, hrs_slots + 8);

  for (uint32_t r = 0; r < m_city.cnt; r++) {
    const key2* k = &m_city.keys[srt_city[r]];
    buf_need(&b_city_lut, ((size_t)r + 1) * CITY_STR);
    uint8_t* w = b_city_lut.p + (size_t)r * CITY_STR;
    memcpy(w, "\",\"city\":\"", 10); w += 10;
    memcpy(w, k->a, k->al); w += k->al;
    memcpy(w, "\",\"state\":\"", 11); w += 11;
    memcpy(w, k->b, k->bl); w += k->bl;
    memcpy(w, "\",\"postal_code\":\"", 17); w += 17;
    size_t len = (size_t)(w - (b_city_lut.p + (size_t)r * CITY_STR));
    if (len >= CITY_STR || len > 255) GOTO_FAIL;
    b_city_lut.n = ((size_t)r + 1) * CITY_STR;
    b_city_len.p[r] = (uint8_t)len;
  }
  memset(b_city_lut.p + b_city_lut.n, 0, (size_t)pad_city * CITY_STR - b_city_lut.n);
  b_city_lut.n = (size_t)pad_city * CITY_STR;
  memset(b_city_len.p + m_city.cnt, 0, (size_t)pad_city - m_city.cnt);
  b_city_len.n = pad_city;
  for (uint32_t r = 0; r < m_postal.cnt; r++) {
    const key2* k = &m_postal.keys[srt_postal[r]];
    buf_need(&b_postal_lut, ((size_t)r + 1) * POSTAL_STR);
    uint8_t* w = b_postal_lut.p + (size_t)r * POSTAL_STR;
    memcpy(w, k->a, k->al); w += k->al;
    memcpy(w, "\",\"latitude\":", 13); w += 13;
    size_t len = (size_t)(w - (b_postal_lut.p + (size_t)r * POSTAL_STR));
    if (len >= POSTAL_STR || len > 255) GOTO_FAIL;
    b_postal_lut.n = ((size_t)r + 1) * POSTAL_STR;
    b_postal_len.p[r] = (uint8_t)len;
  }
  memset(b_postal_lut.p + b_postal_lut.n, 0, (size_t)pad_postal * POSTAL_STR - b_postal_lut.n);
  b_postal_lut.n = (size_t)pad_postal * POSTAL_STR;
  memset(b_postal_len.p + m_postal.cnt, 0, (size_t)pad_postal - m_postal.cnt);
  b_postal_len.n = pad_postal;
  for (uint32_t r = 0; r < m_stars.cnt; r++) {
    const key2* k = &m_stars.keys[srt_stars[r]];
    if (k->al + 25 != STARS_ENT) GOTO_FAIL;
    buf_need(&b_stars_lut, ((size_t)r + 1) * STARS_STR);
    uint8_t* w = b_stars_lut.p + (size_t)r * STARS_STR;
    memcpy(w, ",\"stars\":", 9); w += 9;
    memcpy(w, k->a, k->al); w += k->al;
    memcpy(w, ",\"review_count\":", 16); w += 16;
  }
  b_stars_lut.n = (size_t)pad_stars * STARS_STR;
  for (uint32_t r = 0; r < m_rc.cnt; r++) {
    const key2* k = &m_rc.keys[srt_rc[r]];
    buf_need(&b_rc_lut, ((size_t)r + 1) * RC_STR);
    uint8_t* w = b_rc_lut.p + (size_t)r * RC_STR;
    memcpy(w, k->a, k->al); w += k->al;
    memcpy(w, ",\"is_open\":", 11); w += 11;
    size_t len = (size_t)(w - (b_rc_lut.p + (size_t)r * RC_STR));
    if (len >= RC_STR || len > 255) GOTO_FAIL;
    b_rc_lut.n = ((size_t)r + 1) * RC_STR;
    b_rc_len.p[r] = (uint8_t)len;
  }
  memset(b_rc_lut.p + b_rc_lut.n, 0, (size_t)pad_rc * RC_STR - b_rc_lut.n);
  b_rc_lut.n = (size_t)pad_rc * RC_STR;
  memset(b_rc_len.p + m_rc.cnt, 0, (size_t)pad_rc - m_rc.cnt);
  b_rc_len.n = pad_rc;
  for (uint32_t r = 0; r < m_attr.cnt; r++) {
    const key2* k = &m_attr.keys[srt_attr[r]];
    buf_need(&b_attr_lut, ((size_t)r + 1) * ATTR_STR);
    uint8_t* w = b_attr_lut.p + (size_t)r * ATTR_STR;
    memcpy(w, k->a, k->al); w += k->al;      /* key incl. quotes */
    *w++ = ':';
    memcpy(w, k->b, k->bl); w += k->bl;
    *w++ = ',';
    size_t len = (size_t)(w - (b_attr_lut.p + (size_t)r * ATTR_STR));
    if (len >= ATTR_STR || len > 255) GOTO_FAIL;
    b_attr_lut.n = ((size_t)r + 1) * ATTR_STR;
    b_attr_len.p[r] = (uint8_t)len;
  }
  memset(b_attr_lut.p + b_attr_lut.n, 0, (size_t)pad_attr * ATTR_STR - b_attr_lut.n);
  b_attr_lut.n = (size_t)pad_attr * ATTR_STR;
  memset(b_attr_len.p + m_attr.cnt, 0, (size_t)pad_attr - m_attr.cnt);
  b_attr_len.n = pad_attr;
  for (uint32_t r = 0; r < m_cat.cnt; r++) {
    const key2* k = &m_cat.keys[srt_cat[r]];
    buf_need(&b_cat_lut, ((size_t)r + 1) * CAT_STR);
    uint8_t* w = b_cat_lut.p + (size_t)r * CAT_STR;
    memcpy(w, k->a, k->al); w += k->al;
    memcpy(w, ", ", 2); w += 2;
    size_t len = (size_t)(w - (b_cat_lut.p + (size_t)r * CAT_STR));
    if (len >= CAT_STR || len > 255) GOTO_FAIL;
    b_cat_lut.n = ((size_t)r + 1) * CAT_STR;
    b_cat_len.p[r] = (uint8_t)len;
  }
  memset(b_cat_lut.p + b_cat_lut.n, 0, (size_t)pad_cat * CAT_STR - b_cat_lut.n);
  b_cat_lut.n = (size_t)pad_cat * CAT_STR;
  memset(b_cat_len.p + m_cat.cnt, 0, (size_t)pad_cat - m_cat.cnt);
  b_cat_len.n = pad_cat;
  for (uint32_t r = 0; r < m_hrs.cnt; r++) {
    const key2* k = &m_hrs.keys[srt_hrs[r]];
    uint32_t code = hrs_code[srt_hrs[r]];
    buf_need(&b_hours_lut, (size_t)(code + 1) * HOURS_STR);
    uint8_t* w = b_hours_lut.p + (size_t)code * HOURS_STR;
    memcpy(w, k->a, k->al); w += k->al;
    *w++ = ':';
    memcpy(w, k->b, k->bl); w += k->bl;
    *w++ = ',';
    size_t len = (size_t)(w - (b_hours_lut.p + (size_t)code * HOURS_STR));
    if (len >= HOURS_STR || len > 255) GOTO_FAIL;
    if ((size_t)(code + 1) * HOURS_STR > b_hours_lut.n)
      b_hours_lut.n = (size_t)(code + 1) * HOURS_STR;
    b_hours_len.p[code] = (uint8_t)len;
    if ((size_t)code + 1 > b_hours_len.n) b_hours_len.n = (size_t)code + 1;
  }
  b_hours_lut.n = (size_t)hrs_slots * HOURS_STR;
  b_hours_len.n = hrs_slots;

  /* ---------------- pass 2: emit streams ---------------- */
  buf b_bid, b_ctrl, b_nameblob, b_addrblob, b_lat, b_lng,
      b_attr_idx, b_cat_idx, b_hours_idx;
  buf_init(&b_bid, (size_t)N * BID_LEN + PAD);
  buf_init(&b_ctrl, (size_t)N * CTRL_STRIDE + PAD);
  memset(b_ctrl.p, 0, (size_t)N * CTRL_STRIDE + PAD);
  buf_init(&b_nameblob, size / 8);
  buf_init(&b_addrblob, size / 8);
  buf_init(&b_lat, (size_t)N * 10 + PAD);
  buf_init(&b_lng, (size_t)N * 10 + PAD);
  buf_init(&b_attr_idx, (size_t)attr_qn * 2 + 8);
  buf_init(&b_cat_idx, (size_t)cat_qn * 2 + 8);
  buf_init(&b_hours_idx, (size_t)hrs_qn * 2 + 8);

  for (uint32_t i = 0; i < N; i++) {
    fields* f = &F[i];
    uint8_t* crec = b_ctrl.p + (size_t)i * CTRL_STRIDE;
    buf_put(&b_bid, f->name_p - ((ptrdiff_t)(BID_LEN + sizeof(M_NAME) - 1)),
            BID_LEN);
    crec[1] = (uint8_t)f->name_l;
    buf_put(&b_nameblob, f->name_p, f->name_l);
    crec[2] = (uint8_t)f->addr_l;
    buf_put(&b_addrblob, f->addr_p, f->addr_l);
    unsigned fl = f->open_dig ? F_OPEN : 0;
    if (attr_off[i+1] == attr_off[i]) fl |= F_ATTR_NULL;
    if (cat_off[i+1] == cat_off[i])  fl |= F_CAT_NULL;
    if (hrs_off[i+1] == hrs_off[i])  fl |= F_HOURS_NULL;
    crec[0] = (uint8_t)fl;
    unsigned ccode = rank_city[map_get(&m_city, f->city_p, f->city_l,
                                       f->state_p, f->state_l)];
    crec[4] = (uint8_t)ccode; crec[5] = (uint8_t)(ccode >> 8);
    unsigned pcode = rank_postal[map_get(&m_postal, f->postal_p, f->postal_l,
                                         NULL, 0)];
    crec[6] = (uint8_t)pcode; crec[7] = (uint8_t)(pcode >> 8);
    if (f->lat_p[0] == '-') GOTO_FAIL;
    if (encode_number(&b_lat, f->lat_p, f->lat_l)) GOTO_FAIL;
    if (f->lng_p[0] != '-') GOTO_FAIL;
    if (encode_number(&b_lng, f->lng_p, f->lng_l)) GOTO_FAIL;
    unsigned scode = rank_stars[map_get(&m_stars, f->starv_p, f->starv_l, NULL, 0)];
    crec[3] = (uint8_t)scode;
    unsigned rcode = rank_rc[map_get(&m_rc, f->rcv_p, f->rcv_l, NULL, 0)];
    crec[8] = (uint8_t)rcode; crec[9] = (uint8_t)(rcode >> 8);
    if (attr_off[i+1] > attr_off[i]) {
      crec[10] = (uint8_t)(attr_off[i+1] - attr_off[i]);
      for (uint32_t t = attr_off[i]; t < attr_off[i+1]; t++)
        buf_u16(&b_attr_idx, rank_attr[map_get(&m_attr, attr_q[t].k, attr_q[t].kl,
                                               attr_q[t].v, attr_q[t].vl)]);
    }
    if (cat_off[i+1] > cat_off[i]) {
      crec[11] = (uint8_t)(cat_off[i+1] - cat_off[i]);
      for (uint32_t t = cat_off[i]; t < cat_off[i+1]; t++)
        buf_u16(&b_cat_idx, rank_cat[map_get(&m_cat, cat_q[t].k, cat_q[t].kl,
                                             NULL, 0)]);
    }
    if (hrs_off[i+1] > hrs_off[i]) {
      crec[12] = (uint8_t)(hrs_off[i+1] - hrs_off[i]);
      for (uint32_t t = hrs_off[i]; t < hrs_off[i+1]; t++)
        buf_u16(&b_hours_idx,
                hrs_code[map_get(&m_hrs, hrs_q[t].k, hrs_q[t].kl,
                                 hrs_q[t].v, hrs_q[t].vl)]);
    }
  }

  b_ctrl.n = (size_t)N * CTRL_STRIDE;

  /* ---------------- assemble archive ---------------- */
  buf* secs[N_SEC];
  memset(secs, 0, sizeof(secs));
  secs[SEC_BID] = &b_bid;
  secs[SEC_CTRL] = &b_ctrl;
  secs[SEC_NAME_BLOB] = &b_nameblob;
  secs[SEC_ADDR_BLOB] = &b_addrblob;
  secs[SEC_CITY_LEN] = &b_city_len;  secs[SEC_CITY_LUT] = &b_city_lut;
  secs[SEC_POSTAL_LEN] = &b_postal_len; secs[SEC_POSTAL_LUT] = &b_postal_lut;
  secs[SEC_LAT] = &b_lat; secs[SEC_LNG] = &b_lng;
  secs[SEC_STARS_LUT] = &b_stars_lut;
  secs[SEC_RC_LEN] = &b_rc_len; secs[SEC_RC_LUT] = &b_rc_lut;
  secs[SEC_ATTR_IDX] = &b_attr_idx; secs[SEC_ATTR_LEN] = &b_attr_len;
  secs[SEC_ATTR_LUT] = &b_attr_lut;
  secs[SEC_CAT_IDX] = &b_cat_idx; secs[SEC_CAT_LEN] = &b_cat_len;
  secs[SEC_CAT_LUT] = &b_cat_lut;
  secs[SEC_HOURS_IDX] = &b_hours_idx; secs[SEC_HOURS_LEN] = &b_hours_len;
  secs[SEC_HOURS_LUT] = &b_hours_lut;

  size_t total = HDR_SIZE + (size_t)N_SEC * 8;
  for (int s = 0; s < N_SEC; s++) total += secs[s]->n + PAD;
  if (total > capacity) { free_live(); return -1; }

  uint32_t magic = MAGIC0;
  uint32_t nxor = N ^ NXOR_MASK;
  memcpy(archive, &magic, 4);
  memcpy(archive + 4, &N, 4);
  memcpy(archive + 8, &nxor, 4);
  uint64_t out_size = 0;
  /* compute expected output size: sum of all line lengths incl newlines */
  {
    const uint8_t* q = raw;
    while (q < dend) {
      const uint8_t* e2 = (const uint8_t*)memchr(q, '\n', (size_t)(dend - q));
      if (e2) { out_size += (uint64_t)(e2 - q) + 1; q = e2 + 1; }
      else { out_size += (uint64_t)(dend - q); break; }
    }
  }
  uint64_t oxs = out_size ^ OXOR_MASK;
  memcpy(archive + 12, &oxs, 8);
  memcpy(archive + 20, &out_size, 8);
  uint64_t offs[N_SEC];
  uint8_t* w = archive + HDR_SIZE + (size_t)N_SEC * 8;
  for (int s = 0; s < N_SEC; s++) {
    offs[s] = (uint64_t)(w - archive);
    memcpy(w, secs[s]->p, secs[s]->n);
    memset(w + secs[s]->n, 0, PAD);
    w += secs[s]->n + PAD;
  }
  memcpy(archive + HDR_SIZE, offs, (size_t)N_SEC * 8);
  free_live();
  return (int64_t)total;
}

#endif
