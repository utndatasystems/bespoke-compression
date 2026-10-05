// Shared emit logic: decoder (production) and encoder (roundtrip verify). Format v4.
#pragma once
#include <stdint.h>
#include <string.h>

#define YLP_MAGIC 0x34504c59u /* 'YLP4' */

static inline void put_u32(uint8_t** p, uint32_t v){ memcpy(*p, &v, 4); *p += 4; }
static inline uint32_t get_u32(const uint8_t** p){ uint32_t v; memcpy(&v, *p, 4); *p += 4; return v; }
static inline void put_u64(uint8_t** p, uint64_t v){ memcpy(*p, &v, 8); *p += 8; }
static inline uint64_t get_u64(const uint8_t** p){ uint64_t v; memcpy(&v, *p, 8); *p += 8; return v; }
static inline uint32_t rd_u32(const uint8_t* p){ uint32_t v; memcpy(&v, p, 4); return v; }
static inline uint16_t rd_u16(const uint8_t* p){ uint16_t v; memcpy(&v, p, 2); return v; }

static inline uint32_t fnv1a(const uint8_t* p, size_t n){
  uint32_t h = 2166136261u;
  for (size_t i=0;i<n;i++){ h ^= p[i]; h *= 16777619u; }
  return h;
}

#ifdef __cplusplus
extern "C" {
#endif
size_t ZSTD_compress(void* dst, size_t dstCap, const void* src, size_t srcSize, int level);
size_t ZSTD_decompress(void* dst, size_t dstCap, const void* src, size_t srcSize);
unsigned ZSTD_isError(size_t code);
const char* ZSTD_getErrorName(size_t code);
size_t ZSTD_compressBound(size_t srcSize);
typedef struct ZSTD_DCtx_s ZSTD_DCtx;
ZSTD_DCtx* ZSTD_createDCtx(void);
size_t ZSTD_decompressDCtx(ZSTD_DCtx*, void* dst, size_t dstCap, const void* src, size_t srcSize);
/* LZ4 (liblz4.so.1, pinned-exempt) */
int LZ4_compressBound(int inputSize);
int LZ4_compress_HC(const char* src, char* dst, int srcSize, int dstCapacity, int compressionLevel);
int LZ4_decompress_safe(const char* src, char* dst, int compressedSize, int dstCapacity);
#ifdef __cplusplus
}
#endif

typedef struct { const char* ptr; uint32_t len; } strref;
typedef struct { uint32_t off; uint32_t len; } sref;

static const char SKEL[] =
    "{\"business_id\":\""
    "\",\"name\":\""
    "\",\"address\":\""
    "\",\"city\":\""
    "\",\"state\":\""
    "\",\"postal_code\":\""
    "\",\"latitude\":"
    ",\"longitude\":"
    ",\"stars\":"
    ",\"review_count\":"
    ",\"is_open\":"
    ",\"attributes\":"
    ",\"categories\":"
    ",\"hours\":"
    "}"
    "\n";
static const uint32_t SKEL_OFF[16] = {0,16,26,39,49,60,77,90,103,112,128,139,153,167,176,177};

static const char B64C[64] =
    "-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz";
static const char HEXD[17] = "0123456789";
static const char DD[100][2] = {{'0','0'},{'0','1'},{'0','2'},{'0','3'},{'0','4'},{'0','5'},{'0','6'},{'0','7'},{'0','8'},{'0','9'},{'1','0'},{'1','1'},{'1','2'},{'1','3'},{'1','4'},{'1','5'},{'1','6'},{'1','7'},{'1','8'},{'1','9'},{'2','0'},{'2','1'},{'2','2'},{'2','3'},{'2','4'},{'2','5'},{'2','6'},{'2','7'},{'2','8'},{'2','9'},{'3','0'},{'3','1'},{'3','2'},{'3','3'},{'3','4'},{'3','5'},{'3','6'},{'3','7'},{'3','8'},{'3','9'},{'4','0'},{'4','1'},{'4','2'},{'4','3'},{'4','4'},{'4','5'},{'4','6'},{'4','7'},{'4','8'},{'4','9'},{'5','0'},{'5','1'},{'5','2'},{'5','3'},{'5','4'},{'5','5'},{'5','6'},{'5','7'},{'5','8'},{'5','9'},{'6','0'},{'6','1'},{'6','2'},{'6','3'},{'6','4'},{'6','5'},{'6','6'},{'6','7'},{'6','8'},{'6','9'},{'7','0'},{'7','1'},{'7','2'},{'7','3'},{'7','4'},{'7','5'},{'7','6'},{'7','7'},{'7','8'},{'7','9'},{'8','0'},{'8','1'},{'8','2'},{'8','3'},{'8','4'},{'8','5'},{'8','6'},{'8','7'},{'8','8'},{'8','9'},{'9','0'},{'9','1'},{'9','2'},{'9','3'},{'9','4'},{'9','5'},{'9','6'},{'9','7'},{'9','8'},{'9','9'}};

static inline void cpy64(char* o, const char* p){
  memcpy(o, p, 64);
}

static inline char* cpyv(char* o, const char* p, uint32_t n){
  const uint8_t* src = (const uint8_t*)p;
  uint32_t k = 0;
  for (; k + 16 <= n; k += 16){ memcpy(o + k, src + k, 16); }
  for (; k + 8 <= n; k += 8){ memcpy(o + k, src + k, 8); }
  if (n - k >= 4){ memcpy(o + k, src + k, 4); k += 4; }
  if (n - k >= 2){ memcpy(o + k, src + k, 2); k += 2; }
  if (n - k >= 1){ memcpy(o + k, src + k, 1); }
  return o + n;
}
/* bounded variant: never writes past end; truncates (corrupt input only) */
static inline char* cpyvb(char* o, const char* p, uint32_t n, const char* end){
  if ((size_t)(end - o) < (size_t)n) n = (uint32_t)(end - o);
  return cpyv(o, p, n);
}

/* emit the 22-char id of row i; 5 groups of 4 chars + 2 chars */
static inline char* emit_ids(char* o, const uint8_t* ids, uint32_t i){
  uint32_t bit = i * 132u;
  for (int g = 0; g < 5; g++){
    uint32_t B = bit + 24u*(uint32_t)g;
    uint64_t w; memcpy(&w, ids + (B >> 3), 8);
    uint64_t U = __builtin_bswap64(w);
    uint32_t V = (uint32_t)((U >> (40u - (B & 7u))) & 0xffffffu);
    o[0] = B64C[(V >> 18) & 63u]; o[1] = B64C[(V >> 12) & 63u];
    o[2] = B64C[(V >> 6) & 63u];  o[3] = B64C[V & 63u];
    o += 4;
  }
  { uint32_t B = bit + 120u;
    uint64_t w; memcpy(&w, ids + (B >> 3), 8);
    uint64_t U = __builtin_bswap64(w);
    uint32_t V = (uint32_t)((U >> (52u - (B & 7u))) & 0xfffu);
    o[0] = B64C[(V >> 6) & 63u]; o[1] = B64C[V & 63u]; o += 2; }
  return o;
}

static inline char* emit_digits(char* o, const uint8_t** pp, int n){
  const uint8_t* p = *pp;
  for (int k = 0; k + 1 < n; k += 2){
    uint8_t b = *p++;
    unsigned hi = b >> 4; if (hi > 9) hi = 9;
    unsigned lo = b & 15; if (lo > 9) lo = 9;
    memcpy(o, DD[hi * 10 + lo], 2); o += 2;
  }
  if (n & 1){ uint8_t b = *p; *o++ = HEXD[b >> 4]; p++; }
  *pp = p;
  return o;
}

typedef struct {
  const uint8_t* name_blob; const sref* name;
  const uint8_t* addr_blob; const sref* addr;
  const uint8_t* small;
  const sref *city, *state, *postal, *tok, *ap, *hp, *star;
  const uint8_t *ids;
  const uint8_t *name_idx, *addr_idx, *city_idx, *postal_idx, *state_idx, *star_idx, *iso_idx;
  const uint8_t *rc, *cat, *attr, *hour, *lat, *lon;
  uint32_t nrows; uint8_t final_nl;
  uint64_t orig;                   /* expected full output size */
  uint32_t n_name,n_addr,n_city,n_state,n_postal,n_tok,n_ap,n_hp,n_star;
  const uint8_t *rc0,*cat0,*attr0,*hour0,*lat0,*lon0;
} Dec2;

typedef struct { char* o; const uint8_t *rc,*cat,*attr,*hour,*lat,*lon; char* obase; uint64_t ocap; } EmSt;

#define CPYP(o, p, n) ((size_t)((o) - S.obase) + (size_t)(n) + 2u + (exact ? 0u : 64u) > S.ocap ? S.o : (exact ? cpyv((o),(p),(n)) : ((n) <= 64 ? (cpy64((o),(p)), (o)+(n)) : cpyv((o),(p),(n)))))
#define SEG(i) (SKEL + SKEL_OFF[i])
#define SEGL(i) ((size_t)(SKEL_OFF[(i)+1] - SKEL_OFF[i]))

static inline EmSt emit3(EmSt S, const Dec2* d, uint32_t i, int exact){
  char* o = S.o;
  if ((size_t)(o - S.obase) + 300 > S.ocap) return S;
  /* ---- phase 1: gather row metadata + prefetch scattered sources ---- */
  const uint32_t ni = rd_u32(d->name_idx + 4*i);
  if (ni >= d->n_name) return S;
  const sref* ns = &d->name[ni];
  __builtin_prefetch((const char*)d->name_blob + ns->off, 0, 1);
  const uint32_t ai = rd_u32(d->addr_idx + 4*i);
  if (ai >= d->n_addr) return S;
  const sref* as_ = &d->addr[ai];
  __builtin_prefetch((const char*)d->addr_blob + as_->off, 0, 1);
  { uint32_t j = i + 8; if (j >= d->nrows) j = i;
    __builtin_prefetch((const char*)d->name_blob + d->name[rd_u32(d->name_idx + 4*j)].off, 0, 1);
    __builtin_prefetch((const char*)d->addr_blob + d->addr[rd_u32(d->addr_idx + 4*j)].off, 0, 1); }
  uint32_t ci = rd_u16(d->city_idx + 2*i); if (ci >= d->n_city) return S;
  uint32_t si = d->state_idx[i]; if (si >= d->n_state) return S;
  uint32_t pi = rd_u16(d->postal_idx + 2*i); if (pi >= d->n_postal) return S;
  uint32_t ti = d->star_idx[i]; if (ti >= d->n_star) return S;
  const sref* cs_ = &d->city[ci];
  const sref* ss_ = &d->state[si];
  const sref* ps_ = &d->postal[pi];
  const sref* ts_ = &d->star[ti];
  __builtin_prefetch((const char*)d->small + cs_->off, 0, 1);
  __builtin_prefetch((const char*)d->small + ps_->off, 0, 1);
  const uint8_t* ap = S.attr;
  const int a_cnt = *ap++;
  uint32_t a_off[256], a_len[256];
  if (a_cnt > 64) return S;
  if (a_cnt) {
    for (int k = 0; k < a_cnt; k++) {
      uint32_t id16 = rd_u16(ap + 2*k); if (id16 >= d->n_ap) return S;
      const sref q = d->ap[id16];
      a_off[k] = q.off; a_len[k] = q.len;
      if ((size_t)(o - S.obase) + (size_t)q.len + 2u + (exact ? 0u : 64u) > S.ocap) return S;
      __builtin_prefetch((const char*)d->small + q.off, 0, 1);
    }
    ap += 2*a_cnt;
  }
  S.attr = ap;
  const uint8_t* cp = S.cat;
  const int c_cnt = *cp++;
  uint32_t c_off[256], c_len[256];
  if (c_cnt > 64) return S;
  if (c_cnt) {
    for (int k = 0; k < c_cnt; k++) {
      uint32_t id16 = rd_u16(cp + 2*k); if (id16 >= d->n_tok) return S;
      const sref q = d->tok[id16];
      c_off[k] = q.off; c_len[k] = q.len;
      if ((size_t)(o - S.obase) + (size_t)q.len + 2u + (exact ? 0u : 64u) > S.ocap) return S;
      __builtin_prefetch((const char*)d->small + q.off, 0, 1);
    }
    cp += 2*c_cnt;
  }
  S.cat = cp;
  const uint8_t* hp = S.hour;
  const int h_cnt = *hp++;
  uint32_t h_off[8], h_len[8];
  if (h_cnt > 8) return S;
  if (h_cnt) {
    for (int k = 0; k < h_cnt; k++) {
      uint32_t id16 = rd_u16(hp + 2*k); if (id16 >= d->n_hp) return S;
      const sref q = d->hp[id16];
      h_off[k] = q.off; h_len[k] = q.len;
      if ((size_t)(o - S.obase) + (size_t)q.len + 2u + (exact ? 0u : 64u) > S.ocap) return S;
      __builtin_prefetch((const char*)d->small + q.off, 0, 1);
    }
    hp += 2*h_cnt;
  }
  S.hour = hp;
  const uint32_t n_off = ns->off, n_len = ns->len;
  const uint32_t a2_off = as_->off, a2_len = as_->len;
  __builtin_prefetch((const char*)d->addr_blob + a2_off, 0, 1);
  const uint32_t c2_off = cs_->off, c2_len = cs_->len;
  const uint32_t s2_off = ss_->off, s2_len = ss_->len;
  const uint32_t p2_off = ps_->off, p2_len = ps_->len;
  const uint32_t t2_off = ts_->off, t2_len = ts_->len;
  if ((size_t)(o - S.obase) + (size_t)n_len + (size_t)a2_len + (size_t)c2_len
      + (size_t)s2_len + (size_t)p2_len + (size_t)t2_len + (exact ? 2u : 510u) + 512u > S.ocap) return S;
  const char* const smallc = (const char*)d->small;
  const char* const nb = (const char*)d->name_blob;
  const char* const ab = (const char*)d->addr_blob;
  /* ---- phase 2: emit ---- */
  memcpy(o, SEG(0), SEGL(0)); o += SEGL(0);
  o = emit_ids(o, d->ids, i);
  memcpy(o, SEG(1), SEGL(1)); o += SEGL(1);
  o = CPYP(o, nb + n_off, n_len);
  memcpy(o, SEG(2), SEGL(2)); o += SEGL(2);
  o = CPYP(o, ab + a2_off, a2_len);
  memcpy(o, SEG(3), SEGL(3)); o += SEGL(3);
  o = CPYP(o, smallc + c2_off, c2_len);
  memcpy(o, SEG(4), SEGL(4)); o += SEGL(4);
  o = CPYP(o, smallc + s2_off, s2_len);
  memcpy(o, SEG(5), SEGL(5)); o += SEGL(5);
  o = CPYP(o, smallc + p2_off, p2_len);
  memcpy(o, SEG(6), SEGL(6)); o += SEGL(6);
  { const uint8_t* p = S.lat; int nf = *p++;
    o = emit_digits(o, &p, 2); *o++ = '.'; o = emit_digits(o, &p, nf); S.lat = p; }
  memcpy(o, SEG(7), SEGL(7)); o += SEGL(7);
  { const uint8_t* p = S.lon; uint8_t b = *p++; int L = (b >> 4) + 2, nf = b & 15;
    *o++ = '-'; o = emit_digits(o, &p, L); *o++ = '.'; o = emit_digits(o, &p, nf); S.lon = p; }
  memcpy(o, SEG(8), SEGL(8)); o += SEGL(8);
  o = CPYP(o, smallc + t2_off, t2_len);
  memcpy(o, SEG(9), SEGL(9)); o += SEGL(9);
  { uint32_t v = 0; int sh = 0; const uint8_t* p = S.rc;
    uint8_t b; do { b = *p++; v |= (uint32_t)(b & 127) << sh; sh += 7; } while ((b & 128) && sh < 28);
    S.rc = p;
    char buf[16]; int k = 16;
    do { buf[--k] = (char)('0' + v % 10); v /= 10; } while (v);
    int n = 16 - k; if (n < 0) n = 0; memcpy(o, buf + k, (size_t)n); o += n; }
  memcpy(o, SEG(10), SEGL(10)); o += SEGL(10);
  *o++ = (char)('0' + (d->iso_idx[i] & 1));
  memcpy(o, SEG(11), SEGL(11)); o += SEGL(11);
  if (a_cnt == 0) { memcpy(o, "null", 4); o += 4; }
  else {
    *o++ = '{';
    for (int k = 0; k < a_cnt; k++) o = CPYP(o, smallc + a_off[k], a_len[k]);
    o[-1] = '}';
  }
  memcpy(o, SEG(12), SEGL(12)); o += SEGL(12);
  if (c_cnt == 0) { memcpy(o, "null", 4); o += 4; }
  else {
    *o++ = '"';
    for (int k = 0; k < c_cnt; k++) o = CPYP(o, smallc + c_off[k], c_len[k]);
    o -= 2; *o++ = '"';
  }
  memcpy(o, SEG(13), SEGL(13)); o += SEGL(13);
  if (h_cnt == 0) { memcpy(o, "null", 4); o += 4; }
  else {
    *o++ = '{';
    for (int k = 0; k < h_cnt; k++) o = CPYP(o, smallc + h_off[k], h_len[k]);
    o[-1] = '}';
  }
  *o++ = '}';
  if (i + 1 < d->nrows || d->final_nl) *o++ = '\n';
  S.o = o;
  return S;
}

static inline int build_sref(const uint8_t* blob, size_t blen, uint32_t n, sref* tab, uint32_t base){
  size_t off=0;
  for (uint32_t i=0;i<n;i++){
    tab[i].off = base + (uint32_t)off;
    if (off >= blen && i+1<n) return 0;
    if (i+1<n){
      const uint8_t* nl = (const uint8_t*)memchr(blob+off,0x0a,blen-off);
      if (!nl) return 0;
      size_t pos = (size_t)(nl-blob); tab[i].len=(uint32_t)(pos-off); off=pos+1;
    }
    else tab[i].len = (uint32_t)(blen-off);
  }
  return 1;
}

static inline size_t take_blob(const uint8_t** pp, size_t rem, uint32_t n){
  const uint8_t* q = *pp;
  size_t used = 0;
  for (uint32_t k=0; k+1<n; k++){
    const uint8_t* f = (const uint8_t*)memchr(q+used,0x0a,rem-used);
    if (!f) return (size_t)-1;
    used = (size_t)(f-q)+1;
  }
  const uint8_t* f = (const uint8_t*)memchr(q+used,0x0a,rem-used);
  size_t len;
  if (f){ len = (size_t)(f-q); *pp = q + len + 1; }
  else { len = rem; *pp = q + rem; }
  return len;
}
