#include "codec.h"
#include "format.h"
#include <zstd.h>
#include <lz4.h>
#include "huff.h"
extern "C" {
#include "fsst/fsst.h"
}
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>

using namespace std;

struct ZBuf { uint8_t* p = nullptr; size_t n = 0; };

static bool zdecomp(const uint8_t* src, size_t sn, ZBuf& out) {
  uint64_t n = ZSTD_getFrameContentSize(src, sn);
  if (n == ZSTD_CONTENTSIZE_ERROR || n == ZSTD_CONTENTSIZE_UNKNOWN || n > (1ull << 31)) return false;
  out.p = (uint8_t*)malloc((size_t)n + 64);
  if (!out.p) return false;
  static ZSTD_DCtx* g_dctx = nullptr;
  if (!g_dctx) g_dctx = ZSTD_createDCtx();
  size_t r = ZSTD_decompressDCtx(g_dctx, out.p, (size_t)n + 64, src, sn);
  if (r != (size_t)n) { free(out.p); out.p = nullptr; return false; }
  out.n = (size_t)n;
  return true;
}

struct Col {
  uint8_t scheme = 0;
  uint8_t flags = 0;
  uint32_t n = 0;
  // structured
  const uint8_t* bits = nullptr;
  const uint8_t* vals = nullptr;
  const uint8_t* uud = nullptr;
  uint32_t null_row = 0;
  const uint8_t* counts = nullptr;
  const uint8_t* lon74 = nullptr;
  const uint8_t* slots = nullptr;
  // rowdict
  uint32_t nd = 0;
  int w = 0;
  vector<uint32_t> doff;
  const uint8_t* dbase = nullptr;
  // flz
  int n_hot = 0, n_cold = 0;
  vector<uint32_t> hoff;
  const uint8_t* hbase = nullptr;
  vector<uint32_t> coff;
  const uint8_t* cbase = nullptr;
  vector<uint32_t> codeoff;
  const uint8_t* code = nullptr;
  fsst_decoder_t fdec;
  uint32_t total_out = 0;
  bool idx_ready = false;
  ZBuf lens_z;
  vector<uint32_t> fsst_esc;
  uint8_t lens_codec = 0;
  uint32_t lens_ralen = 0;
  uint32_t code_total = 0;
  // owned buffers
  vector<ZBuf> owned;
  uint8_t* bits_own = nullptr;
  ~Col() {
    for (auto& z : owned) free(z.p);
    free(bits_own);
  }
};

// LUTs (built at open, RAM only)
struct Luts {
  char lut2[200];      // 2-digit decimal
  char g3[192];        // 64 x 3 bases
  char h2[512];        // 256 x 2 uppercase hex chars
  char h2l[512];       // 256 x 2 lowercase hex chars
  char lut3[3000];     // 1000 x 3 decimal digits
};
static Luts g_luts;
static bool g_luts_init = false;

static void init_luts() {
  if (g_luts_init) return;
  g_luts_init = true;
  for (int d = 0; d < 100; d++) {
    g_luts.lut2[d * 2] = (char)('0' + d / 10);
    g_luts.lut2[d * 2 + 1] = (char)('0' + d % 10);
  }
  static const char bas[4] = { 'a', 'c', 'g', 't' };
  for (int v = 0; v < 64; v++) {
    g_luts.g3[v * 3] = bas[(v >> 0) & 3];
    g_luts.g3[v * 3 + 1] = bas[(v >> 2) & 3];
    g_luts.g3[v * 3 + 2] = bas[(v >> 4) & 3];
  }
  static const char hex[] = "0123456789ABCDEF";
  static const char hexl[] = "0123456789abcdef";
  for (int b = 0; b < 256; b++) {
    g_luts.h2[b * 2] = hex[(b >> 4) & 15];
    g_luts.h2[b * 2 + 1] = hex[b & 15];
    g_luts.h2l[b * 2] = hexl[(b >> 4) & 15];
    g_luts.h2l[b * 2 + 1] = hexl[b & 15];
  }
  for (int v = 0; v < 1000; v++) {
    g_luts.lut3[v * 3] = (char)('0' + (v / 100) % 10);
    g_luts.lut3[v * 3 + 1] = (char)('0' + (v / 10) % 10);
    g_luts.lut3[v * 3 + 2] = (char)('0' + v % 10);
  }
}

static bool parse_flz_payload(const uint8_t*& p, Col* c) {
  const uint8_t*& q = p;
  (void)q;
      uint32_t zl; memcpy(&zl, p, 4); p += 4;
      ZBuf zd;
      if (!zdecomp(p, zl, zd)) return false;
      c->owned.push_back(zd);
      p += zl;
      {
        const uint8_t* q = zd.p;
        c->n_hot = (int)get_u32(q);
        if (c->n_hot < 1 || c->n_hot > FLZ_HOT_MAX) return false;
        q += 4; // count field
        c->hoff.resize(c->n_hot + 1);
        for (int k = 0; k <= c->n_hot; k++) c->hoff[k] = get_u32(q);
        c->hbase = q;
        if (c->hoff[c->n_hot] > zd.n || (size_t)c->hoff[c->n_hot] > zd.n) return false;
        q += c->hoff[c->n_hot]; // skip hot payload
        if (q + 8 > zd.p + zd.n) return false;
        c->n_cold = (int)get_u32(q);
        if (c->n_cold < 0 || c->n_cold > FLZ_COLD_TOTAL) return false;
        q += 4; // count field
        if (q + 4 * (size_t)(c->n_cold + 1) > zd.p + zd.n) return false;
        c->coff.resize(c->n_cold + 1);
        for (int k = 0; k <= c->n_cold; k++) c->coff[k] = get_u32(q);
        c->cbase = q;
        if ((size_t)c->coff[c->n_cold] != zd.n - (size_t)(q - zd.p)) return false;
      }
      uint32_t zl2; memcpy(&zl2, p, 4); p += 4;
      ZBuf zl1;
      if (!zdecomp(p, zl2, zl1)) return false;
      c->owned.push_back(zl1);
      p += zl2;
      uint32_t nesc = get_u32(p);
      vector<uint32_t> esc(nesc);
      for (uint32_t k = 0; k < nesc; k++) esc[k] = get_u32(p);
      uint32_t zl3; memcpy(&zl3, p, 4); p += 4;
      ZBuf zc;
      if (!zdecomp(p, zl3, zc)) return false;
      c->owned.push_back(zc);
      c->code = zc.p;
      // build code offsets
            c->codeoff.resize(c->n + 1);
      {
        size_t ei = 0, pos = 0;
        const uint8_t* L1 = zl1.p;
        for (uint32_t i = 0; i < c->n; i++) {
          c->codeoff[i] = (uint32_t)pos;
          uint32_t L = L1[i];
          if (L == 255) L = esc[ei++];
          pos += L;
        }
        c->codeoff[c->n] = (uint32_t)pos;
      }
                    return true;
}

void* lab_open(const uint8_t* archive, size_t size) {
  if (size < 18) return nullptr;
  const uint8_t* p = archive;
  uint32_t magic; memcpy(&magic, p, 4); p += 4;
  if (magic != ARC_MAGIC) return nullptr;
  Col* c = new Col();
  c->scheme = *p++; c->flags = *p++; p += 2;
  uint32_t plen; memcpy(&plen, p, 4); p += 4;
  if ((size_t)(p - archive) + plen > size) { delete c; return nullptr; }
  init_luts();
  switch (c->scheme) {
    case SC_CNAME: {
      c->n = get_u32(p);
      size_t nb = ((size_t)c->n * 18 + 7) / 8;
      c->bits_own = (uint8_t*)malloc(nb + 8);
      memcpy(c->bits_own, p, nb); memset(c->bits_own + nb, 0, 8);
      c->bits = c->bits_own;
      break;
    }
    case SC_GENOME: {
      c->n = get_u32(p);
      size_t nb = ((size_t)c->n * 18 + 7) / 8;
      c->bits_own = (uint8_t*)malloc(nb + 8);
      memcpy(c->bits_own, p, nb); memset(c->bits_own + nb, 0, 8);
      c->bits = c->bits_own;
      break;
    }
    case SC_HEX: {
      c->n = get_u32(p);
      c->vals = p;
      break;
    }
    case SC_UUID: {
      c->n = get_u32(p);
      c->uud = p;
      break;
    }
    case SC_LOC: {
      c->n = get_u32(p);
      c->null_row = get_u32(p);
      c->counts = p; p += c->n;
      c->lon74 = p; p += (c->n + 7) / 8;
      {
        // copy slots into a padded buffer: group extraction reads up to 3 bytes past a slot
        size_t sn = (size_t)c->n * 13;
        uint8_t* sb = (uint8_t*)malloc(sn + 8);
        memcpy(sb, p, sn); memset(sb + sn, 0, 8);
        c->owned.push_back({sb, sn});
        c->slots = sb;
      }
      break;
    }
    case SC_ROWDICT: {
      c->n = get_u32(p);
      c->nd = get_u32(p);
      c->w = *p++;
      p += 4; // count field
      c->doff.resize(c->nd + 1);
      for (uint32_t k = 0; k <= c->nd; k++) c->doff[k] = get_u32(p);
      uint32_t dserlen = get_u32(p);
      {
        uint64_t version = 0;
        uint32_t code2 = 0, pos2 = 17;
        uint8_t lenH[8];
        memcpy(&version, p, 8);
        if ((version >> 32) == 20190218ull) {
          c->fdec.zeroTerminated = p[8] & 1;
          memcpy(lenH, p + 9, 8);
          c->fdec.len[0] = 1;
          c->fdec.symbol[0] = 0;
          code2 = c->fdec.zeroTerminated;
          if (c->fdec.zeroTerminated) lenH[0]--;
          for (uint32_t l = 1; l <= 8; l++) {
            for (uint32_t i2 = 0; i2 < lenH[(l & 7)]; i2++, code2++) {
              c->fdec.len[code2] = (uint8_t)((l & 7) + 1);
              c->fdec.symbol[code2] = 0;
              for (uint32_t j = 0; j < c->fdec.len[code2]; j++)
                ((uint8_t*)&c->fdec.symbol[code2])[j] = p[pos2++];
            }
          }
        }
      }
      p += dserlen;
      size_t draw = c->doff[c->nd];
      ZBuf z;
      if (dserlen > 0) {
        uint32_t zl = get_u32(p);
        uint8_t* tmp = (uint8_t*)malloc(draw + 64);
        z.p = (uint8_t*)malloc(draw + 64);
        if (!z.p || !tmp) { free(tmp); free(z.p); delete c; return nullptr; }
        size_t got1 = ZSTD_decompress(tmp, draw + 64, p, zl);
        if (got1 == 0 || got1 > draw) { free(tmp); free(z.p); delete c; return nullptr; }
        p += zl;
        size_t got2 = fsst_decompress(&c->fdec, got1, tmp, draw + 64, z.p);
        free(tmp);
        if (got2 != draw) { free(z.p); delete c; return nullptr; }
        z.n = draw;
      } else {
        uint32_t zl; memcpy(&zl, p, 4); p += 4;
        if (!zdecomp(p, zl, z)) { delete c; return nullptr; }
        p += zl;
      }
      c->owned.push_back(z);
      c->dbase = z.p;
      size_t nb = ((size_t)c->n * c->w + 7) / 8;
      c->bits_own = (uint8_t*)malloc(nb + 8);
      memcpy(c->bits_own, p, nb); memset(c->bits_own + nb, 0, 8);
      c->bits = c->bits_own;
      break;
    }
    case SC_FLZ: {
      c->n = get_u32(p);
      if (!parse_flz_payload(p, c)) { delete c; return nullptr; }
      break;
    }
    case SC_FSST: {
      c->n = get_u32(p);
      uint32_t serlen = get_u32(p);
      {
        uint64_t version = 0;
        uint32_t code2 = 0, pos2 = 17;
        uint8_t lenH[8];
        memcpy(&version, p, 8);
        if ((version >> 32) == 20190218ull) {
          c->fdec.zeroTerminated = p[8] & 1;
          memcpy(lenH, p + 9, 8);
          c->fdec.len[0] = 1;
          c->fdec.symbol[0] = 0;
          code2 = c->fdec.zeroTerminated;
          if (c->fdec.zeroTerminated) lenH[0]--;
          for (uint32_t l = 1; l <= 8; l++) {
            for (uint32_t i2 = 0; i2 < lenH[(l & 7)]; i2++, code2++) {
              c->fdec.len[code2] = (uint8_t)((l & 7) + 1);
              c->fdec.symbol[code2] = 0;
              for (uint32_t j = 0; j < c->fdec.len[code2]; j++)
                ((uint8_t*)&c->fdec.symbol[code2])[j] = p[pos2++];
            }
          }
        }
      }
      p += serlen;
      c->total_out = get_u32(p);
      uint8_t codec = *p++;
      uint32_t ralen = get_u32(p);
      uint32_t zc = get_u32(p);
      ZBuf zcode;
      static uint8_t* g_arena = nullptr; static size_t g_arena_cap = 0, g_arena_off = 0;
      if (!g_arena) { g_arena_cap = 96u << 20; g_arena = (uint8_t*)malloc(g_arena_cap); g_arena_off = 0; }
      if (g_arena_off + (size_t)ralen + 64 > g_arena_cap) g_arena_off = 0; // wrap: prior states are closed by then
      zcode.p = g_arena + g_arena_off;
      g_arena_off += (size_t)ralen + 64;
      if (!zcode.p) { delete c; return nullptr; }
      static ZSTD_DCtx* g_cdctx = nullptr;
      if (!g_cdctx) g_cdctx = ZSTD_createDCtx();
      size_t got = codec == 2 ? huff_decompress(p, zc, zcode.p, ralen)
                  : codec == 1 ? (size_t)LZ4_decompress_safe((const char*)p, (char*)zcode.p, (int)zc, (int)ralen)
                               : ZSTD_decompressDCtx(g_cdctx, zcode.p, (size_t)ralen + 64, p, zc);
      if (got != ralen) { delete c; return nullptr; }
      zcode.n = ralen;
      c->code_total = ralen;
      c->code = zcode.p; // arena-owned: not freed by ~Col
      p += zc;
      uint32_t zl2 = get_u32(p);
      c->lens_z.p = (uint8_t*)malloc((size_t)zl2 + 8);
      if (!c->lens_z.p) { delete c; return nullptr; }
      memcpy(c->lens_z.p, p, zl2);
      c->lens_z.n = zl2;
      c->owned.push_back(c->lens_z);
      c->lens_codec = codec;
      c->lens_ralen = c->n;
      p += zl2;
      uint32_t nesc = get_u32(p);
      c->fsst_esc.resize(nesc);
      for (uint32_t k = 0; k < nesc; k++) c->fsst_esc[k] = get_u32(p);
      c->idx_ready = false;
      break;
    }
    case SC_EMAIL: {
      c->n = get_u32(p);
      c->nd = get_u32(p);
      c->w = *p++;
      p += 4; // count field
      c->doff.resize(c->nd + 1);
      for (uint32_t k = 0; k <= c->nd; k++) c->doff[k] = get_u32(p);
      uint32_t zd = 0; memcpy(&zd, p, 4); p += 4;
      ZBuf zb;
      if (!zdecomp(p, zd, zb)) { delete c; return nullptr; }
      c->owned.push_back(zb);
      c->dbase = zb.p;
      p += zd;
      size_t nb2 = ((size_t)c->n * c->w + 7) / 8;
      c->bits_own = (uint8_t*)malloc(nb2 + 8);
      memcpy(c->bits_own, p, nb2); memset(c->bits_own + nb2, 0, 8);
      c->bits = c->bits_own;
      p += nb2;
      if (!parse_flz_payload(p, c)) { delete c; return nullptr; }
      break;
    }
    default:
      delete c; return nullptr;
  }
  return c;
}

// ---- per-row content writers (no LF; caller appends per flag) ----
static inline uint32_t rd_bits18(const uint8_t* bits, uint32_t i) {
  size_t bp = (size_t)i * 18;
  const uint8_t* q = bits + (bp >> 3);
  uint32_t v; memcpy(&v, q, 4);
  return (v >> (bp & 7)) & 0x3FFFFu;
}

static inline uint32_t row_len_struct(const Col* c, uint32_t i) {
  uint32_t base;
  switch (c->scheme) {
    case SC_CNAME: base = 18; break;
    case SC_GENOME: base = 9; break;
    case SC_UUID: base = 36; break;
    case SC_LOC: {
      if ((uint32_t)i == c->null_row) { base = 4; break; }
      uint8_t cb = c->counts[i];
      base = 11 + (cb & 15) + (cb >> 4);
      break;
    }
    case SC_HEX: {
      uint32_t v; memcpy(&v, c->vals + (size_t)i * 4, 4);
      if (v < 0x10000) base = 4;
      else { int bl = 32 - __builtin_clz(v); base = (uint32_t)((bl + 3) / 4); }
      break;
    }
    default: base = 0;
  }
  return base + (((i + 1 < c->n) || (c->flags & 1)) ? 1 : 0);
}

static inline int64_t flz_row_bytes(const Col* c, uint32_t i, uint8_t* o, size_t cap) {
  const uint8_t* p = c->code + c->codeoff[i];
  const uint8_t* e = c->code + c->codeoff[i + 1];
  uint8_t* q = o;
      while (p < e) {
        uint8_t cc = *p++;
        if (cc < (uint8_t)c->n_hot) {
          uint32_t L = c->hoff[cc + 1] - c->hoff[cc];
          if ((size_t)(q - o) + L > cap) return -1;
          memcpy(q, c->hbase + c->hoff[cc], L); q += L;
        } else if (cc < 255) {
          uint32_t cid;
          if (cc < 254) cid = ((uint32_t)(cc - 192) << 8) | *p++;
          else { cid = (uint32_t)FLZ_COLD2 + (((uint32_t)*p++) << 8); cid |= *p++; }
          if ((uint32_t)c->n_cold && cid >= (uint32_t)c->n_cold) return -1;
          uint32_t L = c->coff[cid + 1] - c->coff[cid];
          if ((size_t)(q - o) + L > cap) return -1;
          memcpy(q, c->cbase + c->coff[cid], L); q += L;
        } else {
          uint32_t L = (uint32_t)(*p++) + 1;
          if ((size_t)(q - o) + L > cap) return -1;
          memcpy(q, p, L); q += L; p += L;
        }
      }
      
  return (int64_t)(q - o);
}

static inline int64_t dec_row(const Col* c, uint32_t i, uint8_t* out, size_t cap) {
  uint8_t* o = out;
  switch (c->scheme) {
    case SC_CNAME: {
      if (cap < 18) return -1;
      uint32_t v = rd_bits18(c->bits, i);
      memcpy(o, "Customer#000", 12);
      uint32_t a = v / 10000, b = (v / 100) % 100, d = v % 100;
      memcpy(o + 12, g_luts.lut2 + a * 2, 2);
      memcpy(o + 14, g_luts.lut2 + b * 2, 2);
      memcpy(o + 16, g_luts.lut2 + d * 2, 2);
      return 18;
    }
    case SC_GENOME: {
      if (cap < 9) return -1;
      uint32_t v = rd_bits18(c->bits, i);
      const char* g3 = g_luts.g3;
      memcpy(o, g3 + (v & 63) * 3, 3);
      memcpy(o + 3, g3 + ((v >> 6) & 63) * 3, 3);
      memcpy(o + 6, g3 + ((v >> 12) & 63) * 3, 3);
      return 9;
    }
    case SC_HEX: {
      uint32_t v; memcpy(&v, c->vals + (size_t)i * 4, 4);
      uint32_t len;
      if (v < 0x10000) len = 4;
      else { int bl = 32 - __builtin_clz(v); len = (uint32_t)((bl + 3) / 4); }
      if (cap < len) return -1;
      const char* hx = "0123456789ABCDEF";
      for (uint32_t k = 0; k < len; k++)
        o[len - 1 - k] = hx[(v >> (4 * k)) & 15];
      return (int64_t)len;
    }
    case SC_UUID: {
      if (cap < 36) return -1;
      const uint8_t* u = c->uud + (size_t)i * 12;
      const char* h2 = g_luts.h2l;
      uint32_t tl; memcpy(&tl, u, 4);
      uint16_t cs; memcpy(&cs, u + 4, 2);
      uint8_t* q = o;
      for (int k = 0; k < 4; k++) { uint8_t b = (uint8_t)(tl >> (24 - 8 * k)); q[0] = h2[b * 2]; q[1] = h2[b * 2 + 1]; q += 2; }
      *q++ = '-'; *q++ = '2'; *q++ = 'd'; *q++ = 'a'; *q++ = '5'; *q++ = '-';
      *q++ = '1'; *q++ = '1'; *q++ = 'e'; *q++ = '8'; *q++ = '-';
      q[0] = h2[u[5] * 2]; q[1] = h2[u[5] * 2 + 1];
      q[2] = h2[u[4] * 2]; q[3] = h2[u[4] * 2 + 1];
      q += 4;
      *q++ = '-';
      for (int k = 6; k < 12; k++) { uint8_t b = u[k]; q[0] = h2[b * 2]; q[1] = h2[b * 2 + 1]; q += 2; }
      return 36;
    }
    case SC_LOC: {
      if ((uint32_t)i == c->null_row) {
        if (cap < 4) return -1;
        memcpy(o, "NULL", 4);
        return 4;
      }
      uint8_t cb = c->counts[i];
      uint32_t d1 = cb & 15, d2 = cb >> 4;
      uint32_t need = 11 + d1 + d2;
      if (cap < need) return -1;
      bool is74 = (c->lon74[i >> 3] >> (i & 7)) & 1;
      const uint8_t* sl = c->slots + (size_t)i * 13;
      const char* l3 = g_luts.lut3;
      uint8_t* q = o;
      *q++ = '('; *q++ = '4'; *q++ = '0'; *q++ = '.';
      uint32_t g1 = (d1 + 2) / 3, g2 = (d2 + 2) / 3;
      for (uint32_t gk = 0; gk < g1; gk++) {
        size_t bp = gk * 10;
        uint32_t v; memcpy(&v, sl + (bp >> 3), 4);
        v = (v >> (bp & 7)) & 0x3FFu;
        uint32_t digits = (gk + 1) * 3 <= d1 ? 3 : (d1 - gk * 3);
        memcpy(q, l3 + v * 3, digits);
        q += digits;
      }
      *q++ = ','; *q++ = ' ';
      *q++ = '-'; *q++ = '7'; *q++ = is74 ? '4' : '3'; *q++ = '.';
      for (uint32_t gk = 0; gk < g2; gk++) {
        size_t bp = (g1 + gk) * 10;
        uint32_t v; memcpy(&v, sl + (bp >> 3), 4);
        v = (v >> (bp & 7)) & 0x3FFu;
        uint32_t digits = (gk + 1) * 3 <= d2 ? 3 : (d2 - gk * 3);
        memcpy(q, l3 + v * 3, digits);
        q += digits;
      }
      *q++ = ')';
      return (int64_t)(q - o);
    }
    case SC_ROWDICT: {
      uint32_t id;
      size_t bp = (size_t)i * c->w;
      const uint8_t* q = c->bits + (bp >> 3);
      uint32_t v = 0; memcpy(&v, q, 4);
      id = (v >> (bp & 7)) & ((1u << c->w) - 1);
      size_t L = c->doff[id + 1] - c->doff[id];
      if (cap < L) return -1;
      memcpy(o, c->dbase + c->doff[id], L);
      return (int64_t)L;
    }
    case SC_FLZ:
      return flz_row_bytes(c, i, o, cap);
    case SC_EMAIL: {
      int64_t L = flz_row_bytes(c, i, o, cap);
      if (L < 0) return -1;
      size_t used = (size_t)L;
      if (cap < used + 1) return -1;
      o[used] = '@';
      used++;
      uint32_t id;
      size_t bp = (size_t)i * c->w;
      const uint8_t* qb = c->bits + (bp >> 3);
      uint32_t v = 0; memcpy(&v, qb, 4);
      id = (v >> (bp & 7)) & ((1u << c->w) - 1);
      size_t dl = c->doff[id + 1] - c->doff[id];
      if (cap < used + dl) return -1;
      memcpy(o + used, c->dbase + c->doff[id], dl);
      used += dl;
      return (int64_t)used;
    }
    case SC_FSST: {
      size_t cl = c->codeoff[i + 1] - c->codeoff[i];
      size_t got = fsst_decompress(&c->fdec, cl, c->code + c->codeoff[i], cap, o);
      if (got > cap) return -1;
      return (int64_t)got;
    }
  }
}

static inline int64_t decode_full_row(const Col* c, uint32_t i, uint8_t* out, size_t cap) {
  if (c->scheme == SC_ROWDICT || c->scheme == SC_FLZ || c->scheme == SC_FSST) return dec_row(c, i, out, cap);
  int64_t L = dec_row(c, i, out, cap);
  if (L < 0) return -1;
  if ((i + 1 < c->n) || (c->flags & 1)) {
    if (cap < (size_t)L + 1) return -1;
    out[L] = '\n';
    return L + 1;
  }
  return L;
}

int64_t lab_decode(void* state, uint8_t* output, size_t capacity) {
  Col* c = (Col*)state;
  if (!c) return -1;
  if (c->scheme == SC_FSST) {
    size_t total = c->code_total;
    size_t got = fsst_decompress(&c->fdec, total, c->code, capacity, output);
    if (got != c->total_out || got > capacity) return -1;
    return (int64_t)got;
  }
  uint64_t pos = 0;
  for (uint32_t i = 0; i < c->n; i++) {
    int64_t L = decode_full_row(c, i, output + pos, capacity > pos ? capacity - pos : 0);
    if (L < 0) return -1;
    pos += (uint64_t)L;
  }
  return (int64_t)pos;
}

static void ensure_idx(Col* c) {
  if (c->idx_ready) return;
  ZBuf zl1;
  zl1.p = (uint8_t*)malloc((size_t)c->lens_ralen + 64);
  size_t got = ZSTD_decompress(zl1.p, (size_t)c->lens_ralen + 64, c->lens_z.p, c->lens_z.n);
  if (got != c->lens_ralen) { free(zl1.p); return; }
  c->codeoff.resize(c->n + 1);
  {
    size_t ei = 0, pos = 0;
    const uint8_t* L1 = zl1.p;
    for (uint32_t i = 0; i < c->n; i++) {
      c->codeoff[i] = (uint32_t)pos;
      uint32_t L = L1[i];
      if (L == 255) L = c->fsst_esc[ei++];
      pos += L;
    }
    c->codeoff[c->n] = (uint32_t)pos;
  }
  free(zl1.p);
  c->idx_ready = true;
}

int64_t lab_rows(void* state, const uint64_t* ids, size_t count,
                 uint8_t* output, size_t capacity, uint64_t* offsets) {
  Col* c = (Col*)state;
  if (!c) return -1;
  if (c->scheme == SC_FSST && !c->idx_ready) ensure_idx(c);
  uint64_t pos = 0;
  offsets[0] = 0; // interface: count+1 offsets starting at zero, even for empty selections
  for (size_t k = 0; k < count; k++) {
    uint64_t id = ids[k];
    if (id >= c->n) return -1;
    int64_t L = decode_full_row(c, (uint32_t)id, output + pos, capacity > pos ? capacity - pos : 0);
    if (L < 0) return -1;
    pos += (uint64_t)L;
    offsets[k + 1] = pos;
  }
  return (int64_t)pos;
}

void lab_close(void* state) {
  Col* c = (Col*)state;
  delete c;
}
