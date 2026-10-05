#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <immintrin.h>
#include "codec.h"
#include "fmt.h"

typedef struct {
  const uint8_t* a; size_t n;
  uint32_t engine, rows, flags; uint64_t orig;
  uint16_t pair2[100];
  uint16_t lutH[256];
  /* TOK */
  uint64_t *t1e; const uint8_t *dict,*tok,*deltas; const uint32_t *dir;
  uint64_t *e3; uint8_t *len2; uint32_t *off2;
  uint32_t nblocks,ntok,logb,n1,nes2,nes3,deltab,maxlens;
  uint32_t *rowtok; uint32_t rowtok_built;
  uint8_t *scratch; size_t scratchsz;
  /* CNAME */
  uint32_t vbits,plen,ndig; const uint8_t* pre; const uint8_t* vals;
  /* GENOME */
  uint32_t gL,gsb,g_lut[256]; const uint8_t* gvals;
  /* HEX */
  const uint8_t *hvals,*hlens;
  /* UUID */
  uint32_t tm,tlmin,tb,c6,c7; const uint8_t* uvals;
  /* LOC */
  uint32_t null_row; const uint8_t *latint,*lnga,*lngb,*nullstr,*llens,*ldigs,*ldir;
  uint32_t latl,lngal,lngbl,nulll;
} St;

static const uint8_t HEXU[16]={'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};
static const uint8_t HEXL[16]={'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};

static void build_lutH(St* s, const uint8_t* tab){
  for (int i=0;i<256;i++) s->lutH[i] = (uint16_t)(tab[i>>4] | ((uint16_t)tab[i&15]<<8));
}
static void build_pair2(St* s){
  for (int i=0;i<100;i++) s->pair2[i] = (uint16_t)('0'+i/10 | ((uint16_t)('0'+i%10)<<8));
}

/* ---------------- TOK ---------------- */
#define LB_ESC2 224
#define LB_ESC2_IDS 6144
#define LB_ESC3 248

static inline const uint8_t* tok_delta(const uint8_t* dp, size_t* row_bytes){
  uint32_t dv = *dp++;
  if (dv == 255){ dv = (uint32_t)dp[0] | ((uint32_t)dp[1]<<8); dp += 2; }
  *row_bytes = dv;
  return dp;
}

static inline uint8_t* tok_row_scratch(St* s, const uint8_t* tp, uint32_t delta, uint8_t* o);
static int64_t tok_decode_all(St* s, uint8_t* out, size_t cap){
  const uint8_t* tp = s->tok;
  const uint8_t* dp = s->deltas;
  uint8_t* o = out; uint8_t* oend = out + cap;
  const uint32_t rows = s->rows;
  const int last_nl = (s->flags & 1) ? 1 : 0;
  for (uint32_t r=0; r<rows; r++) {
    size_t delta;
    dp = tok_delta(dp, &delta);
    if ((size_t)(o - out) + delta * (size_t)s->maxlens + 64 <= cap){
      o = tok_row_scratch(s, tp, (uint32_t)delta, o);
    } else {
      uint8_t* end = tok_row_scratch(s, tp, (uint32_t)delta, s->scratch); /* near the end: exact path */
      size_t rb = (size_t)(end - s->scratch);
      if ((size_t)(o - out) + rb > cap) return -1;
      memcpy(o, s->scratch, rb); o += rb;
    }
    tp += delta;
    if (r + 1 < rows || last_nl){
      if (o >= out + cap) return -1;
      *o++ = '\n';
    }
  }
  return (int64_t)(o - out);
}

static void tok_build_rowtok(St* s){
  s->rowtok = (uint32_t*)malloc(((size_t)s->rows + 1) * 4);
  const uint8_t* dp = s->deltas;
  size_t acc = 0; size_t maxd = 0;
  for (uint32_t r=0; r<s->rows; r++){
    s->rowtok[r] = (uint32_t)acc;
    uint32_t dv = dp[0]; dp += 1;
    if (dv == 255){ dv = (uint32_t)dp[0] | ((uint32_t)dp[1]<<8); dp += 2; }
    acc += dv;
    if (dv > maxd) maxd = dv;
  }
  s->rowtok[s->rows] = (uint32_t)acc;
  if (!s->scratch){
    s->scratchsz = (size_t)maxd * (size_t)s->maxlens + 128;
    s->scratch = (uint8_t*)malloc(s->scratchsz);
    memset(s->scratch, 0, s->scratchsz);
  }
  s->rowtok_built = 1;
}

static inline uint8_t* tok_row_scratch(St* s, const uint8_t* tp, uint32_t delta, uint8_t* o){
  const uint8_t* end = tp + delta;
  const uint32_t n1 = s->n1;
  uint64_t* t1e = s->t1e;
  uint8_t* len2 = s->len2; uint32_t* off2 = s->off2;
  uint64_t* e3 = s->e3;
  const uint8_t* dict = s->dict;
  while (tp < end){
    uint32_t c = tp[0];
    uint32_t len; const uint8_t* src; uint32_t cl;
    if (c < n1){ uint64_t e = t1e[c]; len = (uint32_t)(e>>56); src = dict + (uint32_t)e; cl = 1; }
    else if (c < LB_ESC3){ uint32_t id = ((c-LB_ESC2)<<8)|tp[1]; len = len2[id]; src = dict + off2[id]; cl = 2; }
    else { uint64_t e = e3[((c-LB_ESC3)<<16)|((uint32_t)tp[1]<<8)|tp[2]]; len = (uint32_t)(e>>56); src = dict + (uint32_t)e; cl = 3; }
    tp += cl;
    if (len <= 32){
      __m256i x = _mm256_loadu_si256((const __m256i*)src);
      _mm256_storeu_si256((__m256i*)o, x);
      o += len;
    } else {
      do {
        __m256i x = _mm256_loadu_si256((const __m256i*)src);
        _mm256_storeu_si256((__m256i*)o, x);
        o += 32; src += 32; len -= 32;
      } while (len > 32);
      __m256i x = _mm256_loadu_si256((const __m256i*)src);
      _mm256_storeu_si256((__m256i*)o, x);
      o += len;
    }
  }
  return o;
}

static int64_t tok_rows(St* s, const uint64_t* ids, size_t count, uint8_t* out, size_t cap, uint64_t* offs){
  uint8_t* o = out; offs[0] = 0;
  if (!s->rowtok_built) tok_build_rowtok(s);
  for (size_t k=0; k<count; k++){
    uint32_t id = (uint32_t)ids[k];
    uint32_t t0 = s->rowtok[id], t1 = s->rowtok[id+1];
    size_t ubound = (size_t)(t1 - t0) * s->maxlens + 32;
    if ((size_t)(o - out) + ubound <= cap){
      o = tok_row_scratch(s, s->tok + t0, t1 - t0, o);
    } else {
      uint8_t* end = tok_row_scratch(s, s->tok + t0, t1 - t0, s->scratch);
      size_t rb = (size_t)(end - s->scratch);
      if ((size_t)(o - out) + rb > cap) return -1;
      memcpy(o, s->scratch, rb); o += rb;
    }
    if (id + 1 < s->rows || (s->flags & 1)){
      if ((size_t)(o - out) >= cap) return -1;
      *o++ = 10;
    }
    offs[k+1] = (uint64_t)(o - out);
  }
  return (int64_t)(o - out);
}




/* ---------------- CNAME ---------------- */
static inline uint8_t* cname_row(St* s, uint32_t r, uint8_t* o){
  uint64_t boff = (uint64_t)r * s->vbits;
  const uint8_t* p = s->vals + (boff>>3);
  uint32_t v = (uint32_t)((rd64(p) >> (boff&7)) & ((1u<<s->vbits)-1));
  memcpy(o, s->pre, s->plen); o += s->plen;
  if (s->ndig == 9){
    uint32_t hi = v/1000, lo = v%1000;
    uint64_t acc = 0x3030303030303030ull;
    acc |= (uint64_t)('0' + hi/100) << 24;
    acc |= (uint64_t)s->pair2[hi%100] << 32;
    acc |= (uint64_t)('0' + lo/100) << 48;
    acc |= (uint64_t)s->pair2[lo%100] << 56;
    wr64(o, acc); o[8] = (uint8_t)(s->pair2[lo%100] >> 8);
    o += 9;
  } else {
    uint32_t nd = s->ndig, vv = v;
    for (uint32_t i=0;i<nd;i++){ o[nd-1-i] = (uint8_t)('0' + vv%10); vv/=10; }
    o += nd;
  }
  return o;
}

/* ---------------- GENOME ---------------- */
static inline uint8_t* genome_row(St* s, uint32_t r, uint8_t* o){
  uint64_t boff = (uint64_t)r * s->gL * s->gsb;
  const uint8_t* p = s->gvals + (boff>>3);
  uint64_t x = rd64(p) >> (boff & 7);
  uint32_t L = s->gL;
  if (s->gsb == 2){
    uint32_t nb = L>>2;
    for (uint32_t j=0;j<nb;j++) wr32(o+4*j, s->g_lut[(uint32_t)(x>>(8*j)) & 0xFF]);
    o += 4*nb;
    for (uint32_t i=4*nb;i<L;i++) *o++ = (uint8_t)s->g_lut[(x>>(2*i))&3];
  } else {
    for (uint32_t i=0;i<L;i++) *o++ = (uint8_t)s->g_lut[(x>>(i*s->gsb))&((1u<<s->gsb)-1)];
  }
  return o;
}

/* ---------------- HEX ---------------- */
static inline uint8_t* hex_row(St* s, uint32_t r, uint8_t* o, uint8_t* oend){
  uint32_t v = rd32(s->hvals + 4ull*r);
  const uint8_t* lp = s->hlens + (((uint64_t)r*3)>>3);
  uint32_t L = (uint32_t)((rd64(lp) >> (((uint64_t)r*3)&7)) & 7) + 1;
  uint32_t t = __builtin_bswap32(v);
  uint64_t acc = (uint64_t)s->lutH[t>>24] << 48 | (uint64_t)s->lutH[(t>>16)&0xFF] << 32
               | (uint64_t)s->lutH[(t>>8)&0xFF] << 16 | (uint64_t)s->lutH[t&0xFF];
  memcpy(o, (const char*)&acc + (8-L), L);
  return o + L;
}

/* ---------------- UUID ---------------- */
static inline uint8_t* uuid_row(St* s, uint32_t r, uint8_t* o){
  uint64_t boff = (uint64_t)r * (s->tb + 62);
  const uint8_t* p = s->uvals + (boff>>3);
  uint32_t sh = (uint32_t)(boff & 7);
  uint64_t lo = rd64(p) >> sh;
  uint32_t hi = (uint32_t)((rd64(p+8) >> sh) & 0xFFFFFFFF);
  uint64_t tbm = (1ull<<s->tb)-1;
  uint32_t tl = s->tlmin + (uint32_t)(lo & tbm);
  uint32_t seq = (uint32_t)((lo >> s->tb) & 0x3FFF);
  uint64_t node = ((lo >> (s->tb+14)) & 0xFFFFFF) | ((uint64_t)(hi & 0xFFFFFF) << 24);
  uint8_t b[16];
  b[0]=(uint8_t)(tl>>24); b[1]=(uint8_t)(tl>>16); b[2]=(uint8_t)(tl>>8); b[3]=(uint8_t)tl;
  b[4]=(uint8_t)(s->tm>>8); b[5]=(uint8_t)s->tm;
  b[6]=(uint8_t)s->c6; b[7]=(uint8_t)s->c7;
  b[8]=(uint8_t)(0x80 | (seq>>8)); b[9]=(uint8_t)seq;
  b[10]=(uint8_t)(node>>40); b[11]=(uint8_t)(node>>32); b[12]=(uint8_t)(node>>24);
  b[13]=(uint8_t)(node>>16); b[14]=(uint8_t)(node>>8); b[15]=(uint8_t)node;
  uint8_t hx[32];
  for (int i=0;i<4;i++){
    uint64_t acc = (uint64_t)s->lutH[b[4*i]] | (uint64_t)s->lutH[b[4*i+1]]<<16
                 | (uint64_t)s->lutH[b[4*i+2]]<<32 | (uint64_t)s->lutH[b[4*i+3]]<<48;
    memcpy(hx+8*i, &acc, 8);
  }
  memcpy(o, hx, 8); o[8]='-';
  memcpy(o+9, hx+8, 4); o[13]='-';
  memcpy(o+14, hx+12, 4); o[18]='-';
  memcpy(o+19, hx+16, 4); o[23]='-';
  memcpy(o+24, hx+20, 12);
  return o + 36;
}

/* ---------------- LOC ---------------- */
typedef struct { uint64_t bit; const uint8_t* p; } NR;
static inline void nr_init(NR* r, const uint8_t* p){ r->p=p; r->bit=0; }
static inline uint32_t nr_get(NR* r, uint32_t k){
  uint32_t v = (uint32_t)((rd64(r->p + (r->bit>>3)) >> (r->bit&7)) & ((1u<<k)-1));
  r->bit += k;
  return v;
}
static inline uint8_t* loc_digits(St* s, NR* nd, uint32_t cnt, uint8_t* o){
  while (cnt >= 2){ uint32_t v = nr_get(nd,8); uint16_t pr = s->pair2[(v&15)*10 + (v>>4)]; memcpy(o,&pr,2); o+=2; cnt-=2; }
  if (cnt) *o++ = (uint8_t)('0' + nr_get(nd,4));
  return o;
}

static int64_t loc_decode_all(St* s, uint8_t* out, size_t cap){
  NR nl; nr_init(&nl, s->llens);
  NR nd; nr_init(&nd, s->ldigs);
  uint8_t* o = out;
  (void)cap;
  const uint32_t rows = s->rows;
  for (uint32_t r=0; r<rows; r++){
    if (r == s->null_row){
      nr_get(&nl,7);
      memcpy(o, s->nullstr, s->nulll); o += s->nulll;
      if (r+1<rows || (s->flags&1)) *o++='\n';
      continue;
    }
    uint32_t v = nr_get(&nl,7);
    uint32_t D1 = 8 + (v&7), D2 = 10 + ((v>>3)&7);
    *o++='('; memcpy(o, s->latint, s->latl); o += s->latl; *o++='.';
    o = loc_digits(s, &nd, D1, o);
    *o++=','; *o++=' '; *o++='-';
    if (v>>6){ memcpy(o, s->lngb, s->lngbl); o += s->lngbl; }
    else { memcpy(o, s->lnga, s->lngal); o += s->lngal; }
    *o++='.';
    o = loc_digits(s, &nd, D2, o);
    *o++=')';
    if (r+1<rows || (s->flags&1)) *o++='\n';
  }
  return (int64_t)(o - out);
}

static int64_t loc_rows(St* s, const uint64_t* ids, size_t count, uint8_t* out, size_t cap, uint64_t* offs){
  uint8_t* o = out; offs[0]=0;
  for (size_t k=0;k<count;k++){
    uint32_t r = (uint32_t)ids[k];
    if (r == s->null_row){
      uint32_t dn = (r + 1 < s->rows || (s->flags & 1)) ? 1u : 0u;
      if ((size_t)(o-out) + s->nulll + dn > cap) return -1;
      memcpy(o, s->nullstr, s->nulll); o += s->nulll;
      if (dn) *o++ = '\n';
      offs[k+1]=(uint64_t)(o-out);
      continue;
    }
    const uint8_t* lp = s->llens + (((uint64_t)r*7)>>3);
    uint32_t v = (uint32_t)((rd64(lp) >> (((uint64_t)r*7)&7)) & 0x7F);
    uint32_t D1 = 8 + (v&7), D2 = 10 + ((v>>3)&7);
    uint32_t delim = (r + 1 < s->rows || (s->flags & 1)) ? 1u : 0u;
    if ((size_t)(o-out) + 7u + s->latl + D1 + s->lngal + D2 + delim > cap) return -1;
    uint32_t b = r >> TOK_LOG_BLOCK;
    uint64_t dbit = rd32((const uint8_t*)s->dir + 8*(size_t)b); /* dir is 2 u32 per block; archive pointers are unaligned */
    uint32_t q0 = b << TOK_LOG_BLOCK;
    for (uint32_t q=q0; q<r; q++){
      const uint8_t* lq = s->llens + (((uint64_t)q*7)>>3);
      uint32_t vq = (uint32_t)((rd64(lq) >> (((uint64_t)q*7)&7)) & 0x7F);
      dbit += (uint64_t)(4*((8+(vq&7)) + (10+((vq>>3)&7))));
    }
    NR nd; nd.p = s->ldigs; nd.bit = dbit;
    *o++='('; memcpy(o, s->latint, s->latl); o += s->latl; *o++='.';
    o = loc_digits(s, &nd, D1, o);
    *o++=','; *o++=' '; *o++='-';
    if (v>>6){ memcpy(o, s->lngb, s->lngbl); o += s->lngbl; }
    else { memcpy(o, s->lnga, s->lngal); o += s->lngal; }
    *o++='.';
    o = loc_digits(s, &nd, D2, o);
    *o++=')';
    if (delim) *o++='\n';
    offs[k+1]=(uint64_t)(o-out);
  }
  (void)cap;
  return (int64_t)(o - out);
}

/* ---------------- open/close ---------------- */
void* lab_open(const uint8_t* archive, size_t size){
  if (size < LB_HDR) return NULL;
  if (rd32(archive) != LB_MAGIC) return NULL;
  St* s = (St*)calloc(1, sizeof(St));
  if (!s) return NULL;
  s->a = archive; s->n = size;
  s->engine = archive[4]; s->flags = archive[5];
  s->rows = rd32(archive+8); s->orig = rd64(archive+12);
  build_pair2(s);
  const uint8_t* p = archive + LB_HDR;
  switch (s->engine){
    case ENG_TOK: {
      s->logb = p[0]; s->n1 = p[1];
      uint32_t ne = rd32(p+4);
      s->ntok = rd32(p+8);
      uint32_t dictb = rd32(p+12);
      s->nblocks = rd32(p+16);
      s->deltab = rd32(p+24);
      if (s->nblocks != (s->rows + TOK_BLOCK - 1) / TOK_BLOCK) { free(s); return NULL; }
      if (48ULL + (uint64_t)ne + (uint64_t)dictb + 64ULL + (uint64_t)s->ntok + (uint64_t)s->deltab + 8ULL*s->nblocks > size) { free(s); return NULL; }
      const uint8_t* q = p + 28;
      const uint8_t* lens = q; q += ne;
      { uint32_t ml = 1; for (uint32_t i=0;i<ne;i++) if (lens[i] > ml) ml = lens[i]; s->maxlens = ml; } /* bounds the decoded row extent */
      s->dict = q; q += dictb + 64;
      s->tok = q; q += s->ntok;
      s->deltas = q; q += s->deltab;
      { size_t maxd = 0; const uint8_t* dp2 = s->deltas;
        for (uint32_t r2=0; r2<s->rows; r2++){ size_t rb; dp2 = tok_delta(dp2,&rb); if (rb>maxd) maxd=rb; }
        s->scratchsz = maxd * (size_t)s->maxlens + 128;
        s->scratch = (uint8_t*)malloc(s->scratchsz);
        if (!s->scratch){ free(s); return NULL; }
        memset(s->scratch, 0, s->scratchsz);
      }
      s->dir = (const uint32_t*)q; /* 2 u32 per block: tok_start, delta_off */
      uint64_t* eoff = (uint64_t*)malloc((ne+1)*8);
      eoff[0]=0; for (uint32_t i=0;i<ne;i++) eoff[i+1] = eoff[i] + lens[i];
      uint32_t n1 = s->n1;
      if (n1 > ne) n1 = ne;
      s->t1e = (uint64_t*)malloc((size_t)TOK_N1*8);
      for (uint32_t c=0;c<n1;c++) s->t1e[c] = (uint64_t)lens[c]<<56 | eoff[c];
      for (uint32_t c=n1;c<TOK_N1;c++) s->t1e[c] = 0;
      s->nes2 = ne - n1; if (s->nes2 > LB_ESC2_IDS) s->nes2 = LB_ESC2_IDS;
      s->nes3 = (ne > n1 + s->nes2) ? ne - n1 - s->nes2 : 0;
      s->len2 = (uint8_t*)calloc(LB_ESC2_IDS,1);
      s->off2 = (uint32_t*)calloc(LB_ESC2_IDS,4);
      for (uint32_t i=0;i<s->nes2;i++){ uint32_t r = n1+i; s->len2[i]=lens[r]; s->off2[i]=(uint32_t)eoff[r]; }
      s->e3 = (uint64_t*)calloc(s->nes3?s->nes3:1,8);
      for (uint32_t i=0;i<s->nes3;i++){ uint32_t r = n1+s->nes2+i; s->e3[i] = (uint64_t)lens[r]<<56 | eoff[r]; }
      free(eoff);
      break; }
    case ENG_CNAME: {
      s->plen = p[0]; s->ndig = p[1]; s->vbits = rd32(p+4);
      if (s->plen > 32 || s->ndig > 9 || s->vbits > 31 || 28ULL + s->plen + (((uint64_t)s->rows * s->vbits + 7) / 8) + 16 > size) { free(s); return NULL; }
      s->pre = p+8; s->vals = p+8+s->plen;
      break; }
    case ENG_GENOME: {
      s->gL = p[0]; s->gsb = p[1];
      if (s->gL > 32 || (s->gsb != 1 && s->gsb != 2) || 28ULL + (((uint64_t)s->rows * s->gL * s->gsb + 7) / 8) + 16 > size) { free(s); return NULL; }
      uint32_t sy = rd32(p+4);
      for (int i=0;i<256;i++){
        uint32_t v=0;
        for (int k=0;k<4;k++){
          uint8_t c = (uint8_t)(sy >> (8*((i>>(2*k))&3)));
          v |= (uint32_t)c << (8*k);
        }
        s->g_lut[i]=v;
      }
      s->gvals = p+8;
      break; }
    case ENG_HEXU32: {
      if (24ULL + 4ULL*s->rows + (((uint64_t)s->rows*3+7)/8) + 16 > size) { free(s); return NULL; }
      s->hvals = p+4;
      s->hlens = p+4+4ull*s->rows;
      build_lutH(s, HEXU);
      break; }
    case ENG_UUID: {
      s->c6 = p[0]; s->c7 = p[1];
      s->tm = rd32(p+4); s->tlmin = rd32(p+8); s->tb = rd32(p+12);
      if (s->tb == 0 || s->tb > 32 || 36ULL + (((uint64_t)s->rows * (s->tb + 62) + 7) / 8) + 16 > size) { free(s); return NULL; }
      s->uvals = p+16;
      build_lutH(s, HEXL);
      break; }
    case ENG_LOC: {
      s->null_row = rd32(p); p += 4;
      if ((size_t)(p - archive) >= size) { free(s); return NULL; }
      s->latl = *p++;
      s->latint = p; p += s->latl;
      if ((size_t)(p - archive) >= size) { free(s); return NULL; }
      s->lngal = *p++;
      s->lnga = p; p += s->lngal;
      if ((size_t)(p - archive) >= size) { free(s); return NULL; }
      s->lngbl = *p++;
      s->lngb = p; p += s->lngbl;
      if ((size_t)(p - archive) >= size) { free(s); return NULL; }
      s->nulll = *p++;
      s->nullstr = p; p += s->nulll;
      p += 1; /* pad */
      if ((size_t)(p - archive) + 1 > size) { free(s); return NULL; }
      s->dir = (const uint32_t*)p;
      p += 8*((s->rows+TOK_BLOCK-1)/TOK_BLOCK);
      if ((size_t)(p - archive) + (((uint64_t)s->rows*7+7)/8) + 32 > size) { free(s); return NULL; }
      s->llens = p;
      s->ldigs = p + (( (uint64_t)s->rows*7 +7)/8 + 32);
      { size_t nib = 0;
        for (uint32_t r2=0; r2<s->rows; r2++){
          if (r2 == s->null_row) continue;
          const uint8_t* lq = s->llens + (((uint64_t)r2*7)>>3);
          uint32_t vq = (uint32_t)((rd64(lq) >> (((uint64_t)r2*7)&7)) & 0x7F);
          nib += (8+(vq&7)) + (10+((vq>>3)&7));
        }
        if ((size_t)(s->ldigs - archive) + 32 + ((nib + 1) / 2) > size) { free(s); return NULL; }
      }
      break; }
    default: free(s); return NULL;
  }
  return s;
}

int64_t lab_decode(void* state, uint8_t* output, size_t capacity){
  St* s = (St*)state;
  if (!s) return -1;
  if (capacity < s->orig) return -1;
  switch (s->engine){
    case ENG_TOK: return tok_decode_all(s, output, capacity);
    case ENG_CNAME: {
      uint8_t* o = output;
      const uint32_t rows = s->rows;
      const int last_nl = (s->flags&1)?1:0;
      for (uint32_t r=0;r<rows;r++){ o = cname_row(s,r,o); if (r+1<rows||last_nl) *o++='\n'; }
      return (int64_t)(o-output); }
    case ENG_GENOME: {
      uint8_t* o = output;
      const uint32_t rows = s->rows;
      const int last_nl = (s->flags&1)?1:0;
      for (uint32_t r=0;r<rows;r++){ o = genome_row(s,r,o); if (r+1<rows||last_nl) *o++='\n'; }
      return (int64_t)(o-output); }
    case ENG_HEXU32: {
      uint8_t* o = output; uint8_t* oend = output + capacity;
      const uint32_t rows = s->rows;
      const int last_nl = (s->flags&1)?1:0;
      for (uint32_t r=0;r<rows;r++){ o = hex_row(s,r,o,oend); if (r+1<rows||last_nl) *o++='\n'; }
      return (int64_t)(o-output); }
    case ENG_UUID: {
      uint8_t* o = output;
      const uint32_t rows = s->rows;
      const int last_nl = (s->flags&1)?1:0;
      for (uint32_t r=0;r<rows;r++){ o = uuid_row(s,r,o); if (r+1<rows||last_nl) *o++='\n'; }
      return (int64_t)(o-output); }
    case ENG_LOC: return loc_decode_all(s, output, capacity);
    default: return -1;
  }
}

int64_t lab_rows(void* state, const uint64_t* ids, size_t count, uint8_t* output, size_t capacity, uint64_t* offsets){
  St* s = (St*)state;
  if (!s) return -1;
  offsets[0] = 0;
  if (!count) return 0;
  switch (s->engine){
    case ENG_TOK: return tok_rows(s, ids, count, output, capacity, offsets);
    case ENG_LOC: return loc_rows(s, ids, count, output, capacity, offsets);
    case ENG_CNAME: case ENG_GENOME: case ENG_HEXU32: case ENG_UUID: {
      uint8_t* o = output; uint8_t* oend = output + capacity;
      for (size_t k=0;k<count;k++){
        uint32_t r = (uint32_t)ids[k];
        size_t need;
        switch (s->engine){
          case ENG_CNAME: need = s->plen + s->ndig; break;
          case ENG_GENOME: need = s->gL; break;
          case ENG_UUID: need = 36; break;
          default: {
            const uint8_t* lp = s->hlens + (((uint64_t)r*3)>>3);
            need = (uint32_t)((rd64(lp) >> (((uint64_t)r*3)&7)) & 7) + 1;
          }
        }
        need += (r + 1 < s->rows || (s->flags & 1)) ? 1u : 0u;
        if ((size_t)(o-output) + need > capacity) return -1;
        switch (s->engine){
          case ENG_CNAME: o = cname_row(s,r,o); break;
          case ENG_GENOME: o = genome_row(s,r,o); break;
          case ENG_HEXU32: o = hex_row(s,r,o,oend); break;
          case ENG_UUID: o = uuid_row(s,r,o); break;
        }
        if (r + 1 < s->rows || (s->flags & 1)) *o++ = '\n';
        offsets[k+1] = (uint64_t)(o-output);
      }
      return (int64_t)(o-output); }
    default: return -1;
  }
}

void lab_close(void* state){
  St* s = (St*)state;
  if (!s) return;
  free(s->t1e); free(s->len2); free(s->off2); free(s->e3);
  free(s->rowtok); free(s->scratch);
  free(s);
}
