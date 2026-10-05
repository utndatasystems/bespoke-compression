#include "common.h"
#include "huff.h"
#include <cstdlib>
#include <new>
#include <vector>

struct SpecEnt { uint32_t idx; uint32_t off; uint32_t len; };
#define THREAD_LOCAL __thread

struct State {
  uint8_t codec; uint8_t final_nl; uint32_t nrows; uint64_t orig;
  uint8_t w, pfx_len, ddigits; char pfx[16]; const uint8_t* bits; uint64_t rowbits;
  uint8_t glen;
  uint8_t hmax, hmin;
  uint16_t tm, th; uint8_t wtl; uint32_t tlmin;
  const uint8_t *s_tl, *s_clk, *s_node;
  int16_t lat_int; int16_t lon_a, lon_b;
  uint8_t dlat_min, dlat_rng, dlon_min, dlon_rng, wlat, wlon;
  uint64_t lat_min, lon_min;
  std::vector<SpecEnt> spec;
  const uint8_t* specbase;
  const uint8_t* locbits; int dl_bits, do_bits; int Dl, Do; int locslot;
  uint8_t k; uint8_t wide; uint8_t nib; uint8_t ctxmode; uint8_t nctx; uint32_t vocab;
  const uint8_t* arena; const uint8_t* tlens; const uint8_t* hlens;
  uint64_t nbits; const uint8_t* stream;
  uint8_t shift; uint32_t nsamp; const uint8_t* samples_b;
  const uint8_t* d8; const uint8_t* d16_b;
  uint32_t* tab; uint32_t* ctab[40]; uint32_t* toff; uint64_t maskk;
  std::vector<uint16_t> cmap;
  uint8_t has_match, sbits, obits; uint8_t* scratch;
  uint64_t p10mag[16];
  State() : spec(), tab(nullptr), toff(nullptr), has_match(0), sbits(0), obits(0), scratch(nullptr) { for (int i = 0; i < 40; i++) ctab[i] = nullptr; }
  ~State() { delete[] tab; delete[] toff; delete[] scratch; for (int i = 0; i < 40; i++) delete[] ctab[i]; }
};

static inline uint64_t rd64(const uint8_t* p) { uint64_t v; __builtin_memcpy(&v, p, 8); return v; }
static inline uint32_t rd32u(const uint8_t* p) { uint32_t v; __builtin_memcpy(&v, p, 4); return v; }
static inline uint16_t rd16u(const uint8_t* p) { uint16_t v; __builtin_memcpy(&v, p, 2); return v; }
static inline uint64_t peekbits(const uint8_t* base, uint64_t pos, uint64_t mask) {
  return (rd64(base + (pos >> 3)) >> (pos & 7)) & mask;
}
static inline int nbits_of(uint64_t x) { int n = 0; while (x) { n++; x >>= 1; } return n; }
static void build_p10mag(uint64_t* mag) {
  static const uint64_t P10L[16] = {1,10,100,1000,10000,100000,1000000,10000000,
    100000000ull,1000000000ull,10000000000ull,100000000000ull,1000000000000ull,
    10000000000000ull,100000000000000ull,1000000000000000ull};
  for (int kk = 1; kk < 16; kk++) {
    unsigned __int128 m = 1; m <<= 64;
    unsigned __int128 dd = P10L[kk];
    mag[kk] = (uint64_t)((m + dd - 1) / dd);
  }
}
static inline uint64_t div_p10(const State* st, uint64_t v, int kk) {
  if (!kk) return v;
  return (uint64_t)(((__uint128_t)v * st->p10mag[kk]) >> 64);
}

static void fill_table(uint32_t* tab, uint32_t k, const std::vector<uint8_t>& lens,
                       const std::vector<uint32_t>& codes) {
  uint64_t maskk = (1ull << k) - 1;
  memset(tab, 0, (1ull << k) * 4);
  for (uint32_t s2 = 0; s2 < lens.size(); s2++) {
    uint32_t L = lens[s2];
    if (!L || L > k) continue;
    uint32_t rv = 0;
    for (int b = 0; b < (int)L; b++) rv = (rv << 1) | ((codes[s2] >> b) & 1);
    uint32_t entry = s2 | (L << 24);
    for (uint64_t v = rv; v <= maskk; v += (1ull << L)) tab[v] = entry;
  }
}

extern "C" void* lab_open(const uint8_t* a, size_t n) {
  if (!a || n < 8) return nullptr;
  State* st = new (std::nothrow) State();
  if (!st) return nullptr;
  build_p10mag(st->p10mag);
  const uint8_t* p = a;
  const uint8_t* end = a + n;
  st->codec = *p++;
  uint64_t nrows, orig;
  p = get_uv(p, end, nrows);
  p = get_uv(p, end, orig);
  if (p >= end || nrows == 0 || nrows > 4000000ull) { delete st; return nullptr; }
  st->nrows = (uint32_t)nrows; st->orig = orig;
  st->final_nl = *p++;
  switch (st->codec) {
    case C_CNAME: {
      if (end - p < 3) { delete st; return nullptr; }
      st->w = *p++; st->pfx_len = *p++; st->ddigits = *p++;
      if (st->pfx_len > 15 || st->w > 32 || st->w < 1 || st->ddigits > 9 || end - p < st->pfx_len) { delete st; return nullptr; }
      memcpy(st->pfx, p, st->pfx_len); p += st->pfx_len;
      st->rowbits = (uint64_t)st->w;
      uint64_t need = (st->rowbits * nrows + 7) / 8 + 16;
      if ((uint64_t)(end - p) < need) { delete st; return nullptr; }
      st->bits = p;
      break;
    }
    case C_GENOME: {
      if (end - p < 1) { delete st; return nullptr; }
      st->glen = *p++;
      if (st->glen < 1 || st->glen > 32) { delete st; return nullptr; }
      uint64_t need = ((uint64_t)st->glen * 2 * nrows + 7) / 8 + 16;
      if ((uint64_t)(end - p) < need) { delete st; return nullptr; }
      st->bits = p;
      break;
    }
    case C_HEX: {
      if (end - p < 2) { delete st; return nullptr; }
      st->hmax = *p++; st->hmin = *p++;
      if (st->hmax > 8 || st->hmin < 1 || st->hmin > st->hmax || st->hmax - st->hmin > 7) { delete st; return nullptr; }
      st->rowbits = 4ull * st->hmax + 3;
      uint64_t need = (st->rowbits * nrows + 7) / 8 + 16;
      if ((uint64_t)(end - p) < need) { delete st; return nullptr; }
      st->bits = p;
      break;
    }
    case C_UUID: {
      if (end - p < 9) { delete st; return nullptr; }
      st->tm = (uint16_t)(p[0] | (p[1] << 8));
      st->th = (uint16_t)(p[2] | (p[3] << 8));
      st->wtl = p[4];
      st->tlmin = (uint32_t)p[5] | ((uint32_t)p[6] << 8) | ((uint32_t)p[7] << 16) | ((uint32_t)p[8] << 24);
      p += 9;
      if (st->wtl > 32 || st->wtl < 1) { delete st; return nullptr; }
      uint64_t tlb = ((uint64_t)st->wtl * nrows + 7) / 8;
      uint64_t ckb = (14ull * nrows + 7) / 8;
      uint64_t ndb = (48ull * nrows + 7) / 8;
      if ((uint64_t)(end - p) < tlb + ckb + ndb + 16) { delete st; return nullptr; }
      st->s_tl = p; st->s_clk = p + tlb; st->s_node = p + tlb + ckb;
      break;
    }
    case C_LOC: {
      if (end - p < 6 + 6 + 16 + 4) { delete st; return nullptr; }
      st->lat_int = (int16_t)(p[0] | (p[1] << 8));
      st->lon_a = (int16_t)(p[2] | (p[3] << 8));
      st->lon_b = (int16_t)(p[4] | (p[5] << 8));
      p += 6;
      st->dlat_min = *p++; st->dlat_rng = *p++; st->dlon_min = *p++; st->dlon_rng = *p++;
      st->wlat = *p++; st->wlon = *p++;
      memcpy(&st->lat_min, p, 8); p += 8;
      memcpy(&st->lon_min, p, 8); p += 8;
      uint32_t nspec = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
      p += 4;
      if (nspec > nrows || st->wlat > 60 || st->wlon > 60 || st->dlat_rng > 15 || st->dlon_rng > 15) { delete st; return nullptr; }
      st->dl_bits = nbits_of(st->dlat_rng); if (!st->dl_bits) st->dl_bits = 1;
      st->do_bits = nbits_of(st->dlon_rng); if (!st->do_bits) st->do_bits = 1;
      st->Dl = st->dlat_min + st->dlat_rng;
      st->Do = st->dlon_min + st->dlon_rng;
      st->locslot = 1 + st->dl_bits + st->wlat + st->do_bits + st->wlon;
      st->specbase = p;
      st->spec.resize(nspec);
      for (uint32_t i = 0; i < nspec; i++) {
        if (end - p < 5) { delete st; return nullptr; }
        st->spec[i].idx = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        st->spec[i].off = (uint32_t)(p - st->specbase) + 5;
        st->spec[i].len = p[4];
        p += 5 + p[4];
        if (p > end) { delete st; return nullptr; }
        if (i && st->spec[i].idx <= st->spec[i - 1].idx) { delete st; return nullptr; }
      }
      uint64_t need = ((uint64_t)st->locslot * nrows + 7) / 8 + 16;
      if ((uint64_t)(end - p) < need) { delete st; return nullptr; }
      st->locbits = p;
      break;
    }
    case C_TOKEN: {
      if (end - p < 4) { delete st; return nullptr; }
      st->k = *p++;
      uint8_t flags = *p++;
      st->wide = flags & 1;
      st->ctxmode = (flags >> 1) & 3;
      st->nib = (flags >> 3) & 1;
      st->has_match = (flags >> 4) & 1;
      st->nctx = *p++;
      uint64_t vocab;
      p = get_uv(p, end, vocab);
      if (st->k < 8 || st->k > 16 || vocab == 0 || vocab > 262144 || st->wide > 1 || st->nib > 1) { delete st; return nullptr; }
      if (st->ctxmode == 0 && st->nctx != 1) { delete st; return nullptr; }
      if (st->ctxmode != 0 && (st->nctx < 2 || st->nctx > 40)) { delete st; return nullptr; }
      st->vocab = (uint32_t)vocab;
      if (end - p < 4) { delete st; return nullptr; }
      uint32_t alen = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
      p += 4;
      if ((uint64_t)(end - p) < alen + 2ull * st->vocab + 4) { delete st; return nullptr; }
      st->arena = p; p += alen;
      st->tlens = p; p += st->vocab;
      st->hlens = p; p += st->vocab;
      {
        uint64_t sum = 0;
        for (uint32_t i = 0; i < st->vocab; i++) sum += st->tlens[i];
        if (sum != alen) { delete st; return nullptr; }
      }
      {
        std::vector<uint8_t> lens(st->hlens, st->hlens + st->vocab);
        std::vector<uint32_t> codes;
        canonical_codes(lens, codes);
        st->tab = new (std::nothrow) uint32_t[1ull << st->k];
        if (!st->tab) { delete st; return nullptr; }
        fill_table(st->tab, st->k, lens, codes);
        int ntab = st->nctx - 1;
        for (int c = 0; c < ntab; c++) {
          if (end - p < 4) { delete st; return nullptr; }
          uint32_t pklen = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
          p += 4;
          if ((uint64_t)(end - p) < pklen) { delete st; return nullptr; }
          std::vector<uint8_t> clens(st->vocab, 0);
          const uint8_t* q = p;
          const uint8_t* qend = p + pklen;
          uint32_t id = 0;
          while (q < qend) {
            uint64_t run = 0; int sh = 0;
            while (q < qend) { uint8_t b = *q++; run |= (uint64_t)(b & 127) << sh; if (!(b & 128)) break; sh += 7; }
            if (q >= qend || id + run > st->vocab) { delete st; return nullptr; }
            id += (uint32_t)run;
            if (id >= st->vocab) { delete st; return nullptr; }
            clens[id] = (uint8_t)(*q + 1);
            q++;
            id++;
          }
          std::vector<uint32_t> ccodes;
          canonical_codes(clens, ccodes);
          st->ctab[c] = new (std::nothrow) uint32_t[1ull << st->k];
          if (!st->ctab[c]) { delete st; return nullptr; }
          fill_table(st->ctab[c], st->k, clens, ccodes);
          p = qend;
        }
      }
      if (st->has_match) {
        if (end - p < 2) { delete st; return nullptr; }
        st->sbits = *p++;
        st->obits = *p++;
        if (st->sbits > 24 || st->obits > 20 || st->sbits < 1 || st->obits < 1) { delete st; return nullptr; }
        st->scratch = new (std::nothrow) uint8_t[65600];
        if (!st->scratch) { delete st; return nullptr; }
      }
      if (st->ctxmode == 3) {
        uint64_t K;
        p = get_uv(p, end, K);
        if (K > 4096 || (uint64_t)(end - p) < K) { delete st; return nullptr; }
        st->cmap.assign(st->vocab, 0);
        for (uint64_t i = 0; i < K; i++) {
          uint64_t id;
          p = get_uv(p, end, id);
          if (id >= st->vocab) { delete st; return nullptr; }
          st->cmap[id] = (uint16_t)(1 + i);
        }
      }
      if (end - p < 4) { delete st; return nullptr; }
      { uint32_t nb; memcpy(&nb, p, 4); st->nbits = nb; } p += 4;
      uint64_t sb = (st->nbits + 7) / 8;
      if ((uint64_t)(end - p) < sb + 16) { delete st; return nullptr; }
      st->stream = p; p += sb + 16;
      st->shift = *p++;
      if (st->shift < 2 || st->shift > 16) { delete st; return nullptr; }
      if (end - p < 4) { delete st; return nullptr; }
      { uint32_t ns; memcpy(&ns, p, 4); st->nsamp = ns; } p += 4;
      if (st->nsamp != ((nrows + (1ull << st->shift) - 1) >> st->shift)) { delete st; return nullptr; }
      uint64_t dbytes = st->wide ? 2ull * nrows : (st->nib ? (nrows + 1) / 2 : nrows);
      if ((uint64_t)(end - p) < 4ull * st->nsamp + dbytes) { delete st; return nullptr; }
      st->samples_b = p; p += 4ull * st->nsamp;
      if (st->wide) st->d16_b = p; else st->d8 = p;
      st->maskk = (1ull << st->k) - 1;
      st->toff = new (std::nothrow) uint32_t[st->vocab + 1];
      if (!st->toff) { delete st; return nullptr; }
      st->toff[0] = 0;
      for (uint32_t i = 0; i < st->vocab; i++) st->toff[i + 1] = st->toff[i] + st->tlens[i];
      break;
    }
    default:
      delete st;
      return nullptr;
  }
  return st;
}

extern "C" void lab_close(void* s) { delete (State*)s; }

static inline uint64_t swar8(const uint64_t v) {
  uint64_t a = (v & 0x00FF00FF00FF00FFull) + ((v >> 8) & 0x00FF00FF00FF00FFull);
  uint64_t b = (a & 0x0000FFFF0000FFFFull) + ((a >> 16) & 0x0000FFFF0000FFFFull);
  return (b & 0xFFFFFFFFull) + (b >> 32);
}
static inline uint64_t sum_bytes_swar(const uint8_t* p, uint32_t n2) {
  uint64_t s = 0;
  while (n2 >= 8) { s += swar8(rd64(p)); p += 8; n2 -= 8; }
  if (n2) {
    uint64_t v = 0;
    memcpy(&v, p, n2);
    s += swar8(v);
  }
  return s;
}

static inline void tok_row_span(const State* st, uint32_t i, uint64_t& start, uint64_t& end) {
  uint32_t seg = i >> st->shift;
  uint64_t pos = rd32u(st->samples_b + 4ull * seg);
  uint32_t j0 = seg << st->shift;
  if (st->wide) {
    for (uint32_t j = j0; j < i; j++) pos += 4ull * rd16u(st->d16_b + 2ull * j);
    end = pos + 4ull * rd16u(st->d16_b + 2ull * i);
  } else if (st->nib) {
    const uint8_t* q = st->d8 + (j0 >> 1);
    uint32_t j = j0;
    if (j & 1) { pos += 4ull * (*q++ >> 4); j++; }
    for (; j + 2 <= i; j += 2, q++) pos += 4ull * ((*q & 15) + (*q >> 4));
    uint32_t di;
    if (j == i) {
      uint8_t b = st->d8[i >> 1];
      di = (i & 1) ? (uint32_t)(b >> 4) : (uint32_t)(b & 15);
    } else {
      di = *q >> 4;
    }
    end = pos + 4ull * di;
  } else {
    pos += 4ull * sum_bytes_swar(st->d8 + j0, i - j0);
    end = pos + 4ull * st->d8[i];
  }
  start = pos;
}

static inline void tok_row_span(const State* st, uint32_t i, uint64_t& start, uint64_t& end);

static size_t decode_token_window(const State* st, uint32_t row, uint32_t skip, uint32_t cnt, uint8_t* buf) {
  uint64_t start, end;
  tok_row_span(st, row, start, end);
  const uint8_t* stream = st->stream;
  uint64_t mask = st->maskk;
  const uint32_t* tab = st->tab;
  uint32_t* const* ctab = st->ctab;
  const uint8_t* arena = st->arena;
  const uint32_t* toff = st->toff;
  uint8_t ctxmode = st->ctxmode;
  uint32_t nctxm1 = st->nctx - 1;
  uint64_t pos = start;
  uint32_t prev = 0;
  int posinrow = 0;
  uint32_t skipped = 0, emitted = 0;
  size_t outb = 0;
  while (pos < end) {
    const uint32_t* T = tab;
    if (ctxmode) {
      int c;
      if (ctxmode == 1) c = posinrow < (int)nctxm1 ? posinrow : (int)nctxm1;
      else if (ctxmode == 2) c = (posinrow == 0) ? 0 : 1 + (int)(prev % nctxm1);
      else {
        if (posinrow == 0) c = 0;
        else { uint32_t b2 = st->cmap[prev]; c = b2 ? (int)b2 : (int)nctxm1; }
      }
      if (c < (int)nctxm1) T = ctab[c];
    }
    uint64_t w = rd64(stream + (pos >> 3));
    uint32_t idx = (w >> (pos & 7)) & mask;
    if (idx == mask && st->has_match) {
      pos += st->k + (uint64_t)st->sbits + st->obits + 8;
      prev = 0; posinrow = 0;
      continue;
    }
    uint32_t e = T[idx];
    uint32_t L = e >> 24;
    uint32_t s2 = e & 0xffffffu;
    if (!L || pos + L > end) break;
    pos += L;
    if (skipped < skip) { skipped++; }
    else {
      uint32_t len = toff[s2 + 1] - toff[s2];
      if (outb + len > (size_t)cnt * 256 + 16) break;
      memcpy(buf + outb, arena + toff[s2], len);
      outb += len;
      emitted++;
      if (emitted >= cnt) break;
    }
    prev = s2;
    posinrow++;
  }
  return outb;
}

static inline size_t tok_row(const State* st, uint64_t start, uint64_t end, uint8_t* out, uint8_t* op_end) {
  const uint8_t* stream = st->stream;
  uint64_t mask = st->maskk;
  const uint32_t* tab = st->tab;
  uint32_t* const* ctab = st->ctab;
  const uint8_t* arena = st->arena;
  const uint32_t* toff = st->toff;
  uint8_t ctxmode = st->ctxmode;
  uint32_t nctxm1 = st->nctx - 1;
  uint8_t* op = out;
  uint64_t pos = start;
  uint32_t prev = 0;
  int posinrow = 0;
  while (pos < end) {
    const uint32_t* T = tab;
    if (ctxmode) {
      int c;
      if (ctxmode == 1) c = posinrow < (int)nctxm1 ? posinrow : (int)nctxm1;
      else if (ctxmode == 2) c = (posinrow == 0) ? 0 : 1 + (int)(prev % nctxm1);
      else {
        if (posinrow == 0) c = 0;
        else { uint32_t b2 = st->cmap[prev]; c = b2 ? (int)b2 : (int)nctxm1; }
      }
      if (c < (int)nctxm1) T = ctab[c];
    }
    uint64_t w = rd64(stream + (pos >> 3));
    uint32_t idx = (w >> (pos & 7)) & mask;
    if (idx == mask && st->has_match) {
      uint64_t src = peekbits(stream, pos + st->k, (1ull << st->sbits) - 1);
      uint64_t spos = peekbits(stream, pos + st->k + st->sbits, (1ull << st->obits) - 1);
      uint64_t cnt = peekbits(stream, pos + st->k + st->sbits + st->obits, 255);
      if (src < st->nrows && cnt > 0) {
        size_t wb = decode_token_window(st, (uint32_t)src, (uint32_t)spos, (uint32_t)cnt, st->scratch);
        memcpy(op, st->scratch, wb);
        op += wb;
      }
      pos += st->k + (uint64_t)st->sbits + st->obits + 8;
      prev = 0;
      posinrow = 0;
      continue;
    }
    uint32_t e = T[idx];
    uint32_t L = e >> 24;
    uint32_t s2 = e & 0xffffffu;
    if (!L || pos + L > end) break;
    pos += L;
    const uint8_t* src = arena + toff[s2];
    uint32_t len = toff[s2 + 1] - toff[s2];
    if (op + len > op_end) return (size_t)-1;
    __builtin_memcpy(op, src, len);
    op += len;
    prev = s2;
    posinrow++;
  }
  return (size_t)(op - out);
}

static const char g2c[4] = { 'a', 'c', 'g', 't' };
static const char* hexd = "0123456789ABCDEF";
static const char* hexl = "0123456789abcdef";

static inline uint8_t* u64_dec(uint8_t* o, uint64_t v) {
  char tmp[20]; int n2 = 0;
  do { tmp[n2++] = (char)('0' + (v % 10)); v /= 10; } while (v);
  while (n2) *o++ = (uint8_t)tmp[--n2];
  return o;
}

static inline size_t row_bytes(const State* st, uint32_t i, uint8_t* out, uint8_t* oend) {
  switch (st->codec) {
    case C_CNAME: {
      if (out + st->pfx_len + st->ddigits > oend) return (size_t)-1;
      uint64_t v = peekbits(st->bits, (uint64_t)i * st->w, (1ull << st->w) - 1);
      uint8_t* o = out;
      memcpy(o, st->pfx, st->pfx_len); o += st->pfx_len;
      char tmp[10]; int cnt = 0;
      do { tmp[cnt++] = (char)('0' + v % 10); v /= 10; } while (v);
      for (int z = st->ddigits - cnt; z > 0; z--) *o++ = '0';
      while (cnt) *o++ = (uint8_t)tmp[--cnt];
      return (size_t)(o - out);
    }
    case C_GENOME: {
      if (out + st->glen > oend) return (size_t)-1;
      uint64_t v = peekbits(st->bits, (uint64_t)i * 2 * st->glen, (1ull << (2 * st->glen)) - 1);
      for (int j = st->glen - 1; j >= 0; j--) out[st->glen - 1 - j] = g2c[(v >> (2 * j)) & 3];
      return st->glen;
    }
    case C_HEX: {
      if (out + st->hmax > oend) return (size_t)-1;
      uint64_t pos = (uint64_t)i * st->rowbits;
      uint64_t bits = peekbits(st->bits, pos, (1ull << 35) - 1);
      uint32_t L = st->hmin + (uint32_t)(bits & 7);
      uint64_t val = bits >> 3;
      for (int j = L - 1; j >= 0; j--) out[L - 1 - j] = hexd[(val >> (4 * j)) & 15];
      return L;
    }
    case C_UUID: {
      if (out + 36 > oend) return (size_t)-1;
      uint64_t tl = peekbits(st->s_tl, (uint64_t)i * st->wtl, (1ull << st->wtl) - 1) + st->tlmin;
      uint64_t ck = peekbits(st->s_clk, (uint64_t)i * 14, (1ull << 14) - 1) | 0x8000ull;
      uint64_t nd = peekbits(st->s_node, (uint64_t)i * 48, (1ull << 48) - 1);
      uint8_t* q = out;
      for (int j = 7; j >= 0; j--) *q++ = hexl[(tl >> (4 * j)) & 15];
      *q++ = '-';
      for (int j = 3; j >= 0; j--) *q++ = hexl[(st->tm >> (4 * j)) & 15];
      *q++ = '-';
      for (int j = 3; j >= 0; j--) *q++ = hexl[(st->th >> (4 * j)) & 15];
      *q++ = '-';
      for (int j = 3; j >= 0; j--) *q++ = hexl[(ck >> (4 * j)) & 15];
      *q++ = '-';
      for (int j = 11; j >= 0; j--) *q++ = hexl[(nd >> (4 * j)) & 15];
      return 36;
    }
    case C_LOC: {
      if (!st->spec.empty()) {
        size_t lo = 0, hi = st->spec.size();
        while (lo < hi) { size_t mid = (lo + hi) / 2; if (st->spec[mid].idx < i) lo = mid + 1; else hi = mid; }
        if (lo < st->spec.size() && st->spec[lo].idx == i) {
          if (out + st->spec[lo].len > oend) return (size_t)-1;
          memcpy(out, st->specbase + st->spec[lo].off, st->spec[lo].len);
          return st->spec[lo].len;
        }
      }
      uint64_t off = (uint64_t)i * st->locslot;
      int use_b = (int)peekbits(st->locbits, off, 1); off += 1;
      int32_t lon_int = use_b ? st->lon_b : st->lon_a;
      uint32_t dl = (uint32_t)peekbits(st->locbits, off, (1ull << st->dl_bits) - 1); off += st->dl_bits;
      uint64_t vl = peekbits(st->locbits, off, (1ull << st->wlat) - 1) + st->lat_min; off += st->wlat;
      uint32_t dno = (uint32_t)peekbits(st->locbits, off, (1ull << st->do_bits) - 1); off += st->do_bits;
      uint64_t vo = peekbits(st->locbits, off, (1ull << st->wlon) - 1) + st->lon_min;
      uint32_t ddi = st->dlat_min + dl, ddo = st->dlon_min + dno;
      uint64_t lati = (uint64_t)(st->lat_int < 0 ? -(int64_t)st->lat_int : st->lat_int);
      uint64_t loni = (uint64_t)(-(int64_t)lon_int);
      size_t ilat = lati < 10 ? 1 : lati < 100 ? 2 : 3;
      size_t ilon = loni < 10 ? 1 : loni < 100 ? 2 : 3;
      size_t need = 1 + (size_t)(st->lat_int < 0) + ilat + 1 + ddi + 2 + 1 + ilon + 1 + ddo + 1;
      if (out + need > oend) return (size_t)-1;
      uint8_t* o = out;
      *o++ = '(';
      if (st->lat_int < 0) *o++ = '-';
      o = u64_dec(o, (uint64_t)(st->lat_int < 0 ? -(int64_t)st->lat_int : st->lat_int));
      *o++ = '.';
      {
        uint32_t dd = st->dlat_min + dl;
        uint64_t f = st->Dl >= dd ? div_p10(st, vl, st->Dl - dd) : 0;
        uint8_t tmp[16]; int cnt = 0;
        for (uint32_t z = 0; z < dd; z++) { tmp[cnt++] = (uint8_t)('0' + f % 10); f /= 10; }
        while (cnt) *o++ = tmp[--cnt];
      }
      *o++ = ','; *o++ = ' ';
      *o++ = '-';
      o = u64_dec(o, (uint64_t)(-(int64_t)lon_int));
      *o++ = '.';
      {
        uint32_t dd = st->dlon_min + dno;
        uint64_t f = st->Do >= dd ? div_p10(st, vo, st->Do - dd) : 0;
        uint8_t tmp[16]; int cnt = 0;
        for (uint32_t z = 0; z < dd; z++) { tmp[cnt++] = (uint8_t)('0' + f % 10); f /= 10; }
        while (cnt) *o++ = tmp[--cnt];
      }
      *o++ = ')';
      return (size_t)(o - out);
    }
    case C_TOKEN: {
      uint64_t start, end;
      tok_row_span(st, i, start, end);
      return tok_row(st, start, end, out, oend);
    }
  }
  return 0;
}

extern "C" int64_t lab_decode(void* s, uint8_t* out, size_t capacity) {
  State* st = (State*)s;
  if (!st) return -1;
  if (capacity < st->orig) return -1;
  uint32_t n = st->nrows;
  uint8_t* o = out;
  uint8_t* oend = out + capacity;
  switch (st->codec) {
    case C_CNAME: {
      uint64_t mask = (1ull << st->w) - 1;
      for (uint32_t i = 0; i < n; i++) {
        if (o + st->pfx_len + st->ddigits + 1 > oend) return -1;
        uint64_t v = peekbits(st->bits, (uint64_t)i * st->w, mask);
        memcpy(o, st->pfx, st->pfx_len); o += st->pfx_len;
        char tmp[10]; int cnt = 0;
        do { tmp[cnt++] = (char)('0' + v % 10); v /= 10; } while (v);
        for (int z = st->ddigits - cnt; z > 0; z--) *o++ = '0';
        while (cnt) *o++ = (uint8_t)tmp[--cnt];
        *o++ = '\n';
      }
      break;
    }
    case C_GENOME: {
      uint64_t mask = (1ull << (2 * st->glen)) - 1;
      for (uint32_t i = 0; i < n; i++) {
        if (o + st->glen + 1 > oend) return -1;
        uint64_t v = peekbits(st->bits, (uint64_t)i * 2 * st->glen, mask);
        uint8_t* q = o;
        for (int j = st->glen - 1; j >= 0; j--) *q++ = g2c[(v >> (2 * j)) & 3];
        *q++ = '\n';
        o = q;
      }
      break;
    }
    case C_HEX: {
      for (uint32_t i = 0; i < n; i++) {
        size_t w = row_bytes(st, i, o, oend);
        if (w == (size_t)-1) return -1;
        o += w;
        *o++ = '\n';
      }
      break;
    }
    case C_UUID: {
      uint64_t mtl = (1ull << st->wtl) - 1, mck = (1ull << 14) - 1, mnd = (1ull << 48) - 1;
      for (uint32_t i = 0; i < n; i++) {
        if (o + 37 > oend) return -1;
        uint64_t tl = peekbits(st->s_tl, (uint64_t)i * st->wtl, mtl) + st->tlmin;
        uint64_t ck = peekbits(st->s_clk, (uint64_t)i * 14, mck) | 0x8000ull;
        uint64_t nd = peekbits(st->s_node, (uint64_t)i * 48, mnd);
        uint8_t* q = o;
        for (int j = 7; j >= 0; j--) *q++ = hexl[(tl >> (4 * j)) & 15];
        *q++ = '-';
        for (int j = 3; j >= 0; j--) *q++ = hexl[(st->tm >> (4 * j)) & 15];
        *q++ = '-';
        for (int j = 3; j >= 0; j--) *q++ = hexl[(st->th >> (4 * j)) & 15];
        *q++ = '-';
        for (int j = 3; j >= 0; j--) *q++ = hexl[(ck >> (4 * j)) & 15];
        *q++ = '-';
        for (int j = 11; j >= 0; j--) *q++ = hexl[(nd >> (4 * j)) & 15];
        *q++ = '\n';
        o = q;
      }
      break;
    }
    case C_LOC: {
      size_t si = 0;
      for (uint32_t i = 0; i < n; i++) {
        if (si < st->spec.size() && st->spec[si].idx == i) {
          if (o + st->spec[si].len + 1 > oend) return -1;
          memcpy(o, st->specbase + st->spec[si].off, st->spec[si].len);
          o += st->spec[si].len;
          si++;
        } else {
          size_t w = row_bytes(st, i, o, oend);
          if (w == (size_t)-1) return -1;
          o += w;
        }
        if (o + 1 > oend) return -1;
        *o++ = '\n';
      }
      break;
    }
    case C_TOKEN: {
      uint64_t pos = 0;
      for (uint32_t i = 0; i < n; i++) {
        uint64_t span;
        if (st->wide) span = 4ull * rd16u(st->d16_b + 2ull * i);
        else if (st->nib) span = 4ull * ((i & 1) ? (st->d8[i >> 1] >> 4) : (st->d8[i >> 1] & 15));
        else span = 4ull * st->d8[i];
        size_t w = tok_row(st, pos, pos + span, o, oend);
        if (w == (size_t)-1) return -1;
        o += w;
        pos += span;
        if (o + 1 > oend) return -1;
        if (i + 1 < n || st->final_nl) *o++ = '\n';
      }
      break;
    }
  }
  return (int64_t)(o - out);
}

extern "C" int64_t lab_rows(void* s, const uint64_t* ids, size_t count,
                            uint8_t* output, size_t capacity, uint64_t* offsets) {
  State* st = (State*)s;
  if (!st) return -1;
  uint8_t* o = output;
  offsets[0] = 0;
  // interleaved two-row decode for token columns: independent decode chains overlap latency
  if (st->codec == C_TOKEN && count >= 2 && !st->wide) {
    static __thread uint8_t buf0[65536 + 16];
    static __thread uint8_t buf1[65536 + 16];
    size_t qq = 0;
    for (; qq + 1 < count; qq += 2) {
      uint32_t i0 = (uint32_t)ids[qq], i1 = (uint32_t)ids[qq + 1];
      if (i0 >= st->nrows || i1 >= st->nrows) return -1;
      uint64_t s0, e0, s1, e1;
      tok_row_span(st, i0, s0, e0);
      tok_row_span(st, i1, s1, e1);
      size_t w0 = tok_row(st, s0, e0, buf0, buf0 + 65536);
      size_t w1 = tok_row(st, s1, e1, buf1, buf1 + 65536);
      if (w0 == (size_t)-1 || w1 == (size_t)-1) return -1;
      int nl0 = !(i0 + 1 == st->nrows && !st->final_nl);
      int nl1 = !(i1 + 1 == st->nrows && !st->final_nl);
      size_t need = w0 + w1 + (size_t)nl0 + (size_t)nl1;
      if ((size_t)(o - output) + need > capacity) return -1;
      memcpy(o, buf0, w0);
      if (nl0) { o[w0] = '\n'; }
      memcpy(o + w0 + nl0, buf1, w1);
      if (nl1) { o[w0 + nl0 + w1] = '\n'; }
      offsets[qq + 1] = (uint64_t)(o - output) + w0 + nl0;
      offsets[qq + 2] = (uint64_t)(o - output) + need;
      o += need;
    }
    for (; qq < count; qq++) {
      uint32_t i = (uint32_t)ids[qq];
      if (i >= st->nrows) return -1;
      size_t w = row_bytes(st, i, o, output + capacity);
      if (w == (size_t)-1) return -1;
      int nl = !(i + 1 == st->nrows && !st->final_nl);
      if ((size_t)(o - output) + w + (size_t)nl > capacity) return -1;
      if (nl) { o[w] = '\n'; w++; }
      o += w;
      offsets[qq + 1] = (uint64_t)(o - output);
    }
    return (int64_t)(o - output);
  }
  for (size_t qq = 0; qq < count; qq++) {
    uint32_t i = (uint32_t)ids[qq];
    if (i >= st->nrows) return -1;
    size_t w = row_bytes(st, i, o, output + capacity);
    if (w == (size_t)-1) return -1;
    int nl = !(i + 1 == st->nrows && !st->final_nl);
    if ((size_t)(o - output) + w + (size_t)nl > capacity) return -1;
    if (nl) { o[w] = '\n'; w++; }
    o += w;
    offsets[qq + 1] = (uint64_t)(o - output);
  }
  return (int64_t)(o - output);
}
