// Decoder: reconstructs the Yelp business JSONL byte-for-byte from the columnar archive.
// Robustness: checksum + structural validation at open; padded arena and padded piece
// tables so that even garbage-driven state cannot read or write out of bounds; per-row
// stream sanity checks fail gracefully with -1.
#include "codec_common.hpp"
#include "xxh64.hpp"
#include <vector>
#include <cstdlib>
#include <cstring>
#include <zstd.h>
#include <brotli/decode.h>

using namespace std;

namespace {

inline uint16_t ld16(const uint8_t* p) { uint16_t v; memcpy(&v, p, 2); return v; }

// copy n bytes; small copies may round up to 16/32 bytes when slack allows
inline uint8_t* cpyk(uint8_t* d, const void* sv_, size_t n, const uint8_t* cap) {
  const uint8_t* s = (const uint8_t*)sv_;
  size_t slack = (size_t)(cap - d);
  if (slack >= n + 31) {
    if (n <= 16) memcpy(d, s, 16);
    else if (n <= 32) memcpy(d, s, 32);
    else memcpy(d, s, n);
    return d + n;
  }
  if (slack >= n) memcpy(d, s, n);
  return d + n;
}

struct State {
  uint32_t nrows = 0;
  uint64_t out_size = 0;
  uint16_t nkeys = 0;
  uint8_t* arena = nullptr;
  vector<uint8_t*> bp;
  vector<uint32_t> bsz;
  // piece tables padded to the full 16-bit id space (len 0 = unused)
  vector<uint32_t> ld_off, pv_off, cv_off, sv_off;
  vector<uint8_t> ld_len, pv_len, cv_len, sv_len;
  vector<uint16_t> enc12;
  // stream ends for per-row sanity checks
  const uint8_t *nm_e, *ad_e, *lidx_e, *lt_e, *ln_e, *acnt_e, *akey_e;
  const uint8_t* aval_e[256];
  const uint8_t *ccnt_e, *cids_e, *hmk_e, *hsl_e, *rcp_e, *stp_e, *opp_e;
  ZSTD_DCtx* dctx = nullptr;
  ~State() { free(arena); if (dctx) ZSTD_freeDCtx(dctx); }
};

const char A64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

} // namespace

extern "C" LAB_EXPORT void* lab_open(const uint8_t* archive, size_t size) {
  if (size < sizeof(ArchiveHeader) || archive == nullptr) return nullptr;
  ArchiveHeader h;
  memcpy(&h, archive, sizeof(h));
  if (memcmp(h.magic, "YBIZ1\0", 6) != 0) return nullptr;
  if (h.reserved != 0) return nullptr;
  if (h.nrows == 0 || h.nrows > (1u << 30)) return nullptr;
  if (h.out_size == 0 || h.out_size > (1ULL << 40)) return nullptr;
  if (h.nblobs < 20) return nullptr;
  const uint8_t* tbl = archive + sizeof(h);
  const size_t hdr = sizeof(h) + (size_t)h.nblobs * sizeof(BlobHeader);
  if (size < hdr) return nullptr;
  size_t csum = 0;
  for (uint16_t i = 0; i < h.nblobs; i++) {
    BlobHeader bh; memcpy(&bh, tbl + (size_t)i * sizeof(bh), sizeof(bh));
    if (bh.codec > 2) return nullptr;
    csum += bh.csize;
    if (csum > size) return nullptr;
  }
  if (size < hdr + csum) return nullptr;
  {
    uint64_t ck = xxh64_hash(archive + sizeof(h), size - sizeof(h), 0);
    if (ck != h.checksum) return nullptr;
  }

  State* S = new State();
  S->nrows = h.nrows;
  S->out_size = h.out_size;
  const size_t PAD = 1024;                 // per-blob slack for bounded overshoot
  size_t total = 64;
  for (uint16_t i = 0; i < h.nblobs; i++) {
    BlobHeader bh; memcpy(&bh, tbl + (size_t)i * sizeof(bh), sizeof(bh));
    total += bh.dsize + PAD;
  }
  S->arena = (uint8_t*)malloc(total);
  if (!S->arena) { delete S; return nullptr; }
  S->dctx = ZSTD_createDCtx();
  S->bp.resize(h.nblobs); S->bsz.resize(h.nblobs);
  const uint8_t* src = tbl + (size_t)h.nblobs * sizeof(BlobHeader);
  uint8_t* dst = S->arena;
  for (uint16_t i = 0; i < h.nblobs; i++) {
    BlobHeader bh; memcpy(&bh, tbl + (size_t)i * sizeof(bh), sizeof(bh));
    S->bp[i] = dst; S->bsz[i] = bh.dsize;
    if (bh.codec == BC_RAW) {
      if (bh.csize) memcpy(dst, src, bh.csize);
    } else if (bh.codec == BC_ZSTD) {
      size_t r = ZSTD_decompressDCtx(S->dctx, dst, bh.dsize, src, bh.csize);
      if (ZSTD_isError(r) || r != bh.dsize) { delete S; return nullptr; }
    } else {
      size_t dn = bh.dsize;
      BrotliDecoderResult r = BrotliDecoderDecompress(bh.csize, src, &dn, dst);
      if (r != BROTLI_DECODER_RESULT_SUCCESS || dn != bh.dsize) { delete S; return nullptr; }
    }
    src += bh.csize;
    dst += bh.dsize + PAD;
  }
  const size_t nb = h.nblobs;
  S->nkeys = ld16(S->bp[1]);
  if (S->nkeys == 0 || S->nkeys > 200) { delete S; return nullptr; }
  if ((size_t)10 + S->nkeys + 10 != nb) { delete S; return nullptr; }
  // structural size checks (independent of checksum)
  if (S->bsz[0] != (size_t)16 * h.nrows) { delete S; return nullptr; }
  if (S->bsz[1] != (size_t)2 + 5 * (size_t)S->nkeys) { delete S; return nullptr; }
  if (S->bsz[5] != (size_t)2 * h.nrows) { delete S; return nullptr; }
  if (S->bsz[8] != h.nrows) { delete S; return nullptr; }
  if (S->bsz[nb-9] != h.nrows || S->bsz[nb-6] != h.nrows) { delete S; return nullptr; }
  if (S->bsz[nb-2] != h.nrows || S->bsz[nb-1] != h.nrows) { delete S; return nullptr; }
  if ((S->bsz[nb-8] | S->bsz[nb-5] | S->bsz[5]) & 1) { delete S; return nullptr; }
  // piece tables
  auto scan_entries = [&](const uint8_t* p, size_t n,
                          vector<uint32_t>& off, vector<uint8_t>& len) -> bool {
    const uint8_t* start = p;
    const uint8_t* e = p + n;
    while (p < e) {
      uint8_t l = *p++;
      if (p + l > e) return false;
      off.push_back((uint32_t)(p - start));
      len.push_back(l);
      p += l;
    }
    return p == e;
  };
  if (!scan_entries(S->bp[4], S->bsz[4], S->ld_off, S->ld_len)) { delete S; return nullptr; }
  if (!scan_entries(S->bp[nb-10], S->bsz[nb-10], S->pv_off, S->pv_len)) { delete S; return nullptr; }
  if (!scan_entries(S->bp[nb-7], S->bsz[nb-7], S->cv_off, S->cv_len)) { delete S; return nullptr; }
  if (!scan_entries(S->bp[nb-4], S->bsz[nb-4], S->sv_off, S->sv_len)) { delete S; return nullptr; }
  if (S->ld_len.empty() || S->pv_len.empty() || S->cv_len.empty() || S->sv_len.empty()) { delete S; return nullptr; }
  // pad tables to the full 16-bit space: unused ids map to zero-length entries
  auto pad_table = [](vector<uint32_t>& off, vector<uint8_t>& len) {
    off.resize(65536, 0);
    len.resize(65536, 0);
  };
  pad_table(S->ld_off, S->ld_len);
  pad_table(S->pv_off, S->pv_len);
  pad_table(S->cv_off, S->cv_len);
  pad_table(S->sv_off, S->sv_len);
  // base64 LUT
  S->enc12.resize(4096);
  for (int v = 0; v < 4096; v++)
    S->enc12[v] = (uint16_t)((uint8_t)A64[v >> 6] | ((uint16_t)(uint8_t)A64[v & 63] << 8));
  // stream ends
  S->nm_e = S->bp[2] + S->bsz[2];
  S->ad_e = S->bp[3] + S->bsz[3];
  S->lidx_e = S->bp[5] + S->bsz[5];
  S->lt_e = S->bp[6] + S->bsz[6];
  S->ln_e = S->bp[7] + S->bsz[7];
  S->acnt_e = S->bp[8] + S->bsz[8];
  S->akey_e = S->bp[9] + S->bsz[9];
  for (size_t k = 0; k < S->nkeys; k++) S->aval_e[k] = S->bp[10 + k] + S->bsz[10 + k];
  S->ccnt_e = S->bp[nb-9] + S->bsz[nb-9];
  S->cids_e = S->bp[nb-8] + S->bsz[nb-8];
  S->hmk_e = S->bp[nb-6] + S->bsz[nb-6];
  S->hsl_e = S->bp[nb-5] + S->bsz[nb-5];
  S->rcp_e = S->bp[nb-3] + S->bsz[nb-3];
  S->stp_e = S->bp[nb-2] + S->bsz[nb-2];
  S->opp_e = S->bp[nb-1] + S->bsz[nb-1];
  return S;
}

extern "C" LAB_EXPORT int64_t lab_decode(void* st, uint8_t* output, size_t capacity) {
  State* S = (State*)st;
  if (!S || !output) return -1;
  if (capacity < S->out_size) return -1;
  const size_t NK = S->nkeys;
  const size_t nb = S->bp.size();
  const uint8_t* ids = S->bp[0];
  const uint8_t* meta = S->bp[1];
  const uint8_t* widths = meta + 2;
  const uint8_t* kstart_a = widths + NK;
  const uint8_t* nm = S->bp[2];
  const uint8_t* ad = S->bp[3];
  const uint8_t* ld_base = S->bp[4];
  const uint16_t* lidx16 = (const uint16_t*)S->bp[5];
  const uint8_t* lt = S->bp[6];
  const uint8_t* ln = S->bp[7];
  const uint8_t* acnt = S->bp[8];
  const uint8_t* akey = S->bp[9];
  const uint8_t* aval[256];
  for (size_t k = 0; k < NK; k++) aval[k] = S->bp[10 + k];
  const uint8_t* pv_base = S->bp[nb-10];
  const uint8_t* ccnt = S->bp[nb-9];
  const uint16_t* cids16 = (const uint16_t*)S->bp[nb-8];
  const uint8_t* cv_base = S->bp[nb-7];
  const uint8_t* hmk = S->bp[nb-6];
  const uint16_t* hsl16 = (const uint16_t*)S->bp[nb-5];
  const uint8_t* sv_base = S->bp[nb-4];
  const uint8_t* rcp = S->bp[nb-3];
  const uint8_t* stp = S->bp[nb-2];
  const uint8_t* opp = S->bp[nb-1];
  const uint16_t* enc12 = S->enc12.data();

  uint8_t* o = output;
  const uint8_t* cap = output + capacity;

  for (uint32_t row = 0; row < S->nrows; row++) {
    if ((size_t)(cap - o) < S->out_size - (size_t)(o - output)) return -1;
    memcpy(o, T_PREFIX, 16);
    uint8_t* q = o + 16;
    const uint8_t* b = ids + (size_t)row * 16;
    uint32_t w[5];
    for (int g = 0; g < 5; g++) {
      const uint8_t* t = b + g * 3;
      uint32_t v1 = ((uint32_t)t[0] << 4) | (t[1] >> 4);
      uint32_t v2 = (((uint32_t)t[1] & 15) << 8) | t[2];
      w[g] = (uint32_t)enc12[v1] | ((uint32_t)enc12[v2] << 16);
    }
    memcpy(q, w, 20);
    {
      uint8_t t2[3];
      t2[0] = (uint8_t)A64[b[15] >> 2];
      t2[1] = (uint8_t)A64[(b[15] & 3) << 4];
      t2[2] = '"';
      memcpy(q + 20, t2, 3);
    }
    o += 39;
    o = cpyk(o, T_NAMESEP, 9, cap);
    { uint32_t L = *nm++;
      o = cpyk(o, nm, L, cap); nm += L; }
    o = cpyk(o, T_NAME, 13, cap);
    // address (fused: + ",\"city\":\"")
    { uint32_t L = *ad++;
      o = cpyk(o, ad, L, cap); ad += L; }
    o = cpyk(o, T_ADDR, 10, cap);
    // city/state/zip (+ ",\"latitude\":")
    { uint16_t id = ld16((const uint8_t*)lidx16++);
      o = cpyk(o, ld_base + S->ld_off[id], S->ld_len[id], cap); }
    // latitude (fused: + ",\"longitude\":")
    { uint32_t L = *lt++;
      o = cpyk(o, lt, L, cap); lt += L; }
    o = cpyk(o, T_LAT, 13, cap);
    // longitude (fused: + ",\"stars\":")
    { uint32_t L = *ln++;
      o = cpyk(o, ln, L, cap); ln += L; }
    o = cpyk(o, T_LNG, 9, cap);
    // stars (+ ",\"review_count\":")
    o = cpyk(o, STAR_PIECES[*stp++], 19, cap);
    // review_count
    {
      uint32_t v = 0; int sh = 0; uint8_t bb;
      do { bb = *rcp++; v |= (uint32_t)(bb & 127) << sh; sh += 7; } while (bb & 128);
      v %= 1000000u;
      char tmp[6]; int m = 0;
      do { tmp[m++] = (char)('0' + v % 10); v /= 10; } while (v);
      while (m) *o++ = tmp[--m];
    }
    o = cpyk(o, T_OPEN, 11, cap);
    // is_open + ",\"attributes\":"
    *o++ = (char)('0' + (*opp++ & 1));
    o = cpyk(o, T_ATTR, 14, cap);
    // attributes
    {
      uint32_t n = *acnt++;
      if (n == 0) {
        o = cpyk(o, T_NULL_ATTR, 18, cap);
      } else {
        *o++ = '{';
        for (uint32_t j = 0; j < n; j++) {
          uint32_t k = *akey++;
          uint16_t vid;
          if (widths[k] == 1) vid = *aval[k]++;
          else { vid = ld16(aval[k]); aval[k] += 2; }
          uint16_t pid = ld16(kstart_a + 2 * (size_t)k) + vid;
          o = cpyk(o, pv_base + S->pv_off[pid], S->pv_len[pid], cap);
        }
        o -= 1;
        o = cpyk(o, T_ATTR_END, 15, cap);
      }
    }
    // categories
    {
      uint32_t n = *ccnt++;
      if (n == 0) {
        o = cpyk(o, T_NULL_CAT, 13, cap);
      } else {
        *o++ = '"';
        for (uint32_t j = 0; j < n; j++) {
          uint16_t id = ld16((const uint8_t*)cids16++);
          o = cpyk(o, cv_base + S->cv_off[id], S->cv_len[id], cap);
        }
        o -= 2;
        o = cpyk(o, T_CATQ, 10, cap);
      }
    }
    // hours
    {
      uint8_t mask = *hmk++;
      if (mask == 0) {
        o = cpyk(o, T_NULL_HRS, 6, cap);
      } else {
        *o++ = '{';
        while (mask) {
          uint16_t id = ld16((const uint8_t*)hsl16++);
          o = cpyk(o, sv_base + S->sv_off[id], S->sv_len[id], cap);
          mask &= mask - 1;
        }
        o -= 1;
        o = cpyk(o, T_HRS_END, 3, cap);
      }
    }
    // per-row stream sanity
    if (nm > S->nm_e || ad > S->ad_e || (const uint8_t*)lidx16 > S->lidx_e ||
        lt > S->lt_e || ln > S->ln_e || acnt > S->acnt_e || akey > S->akey_e ||
        ccnt > S->ccnt_e || (const uint8_t*)cids16 > S->cids_e ||
        hmk > S->hmk_e || (const uint8_t*)hsl16 > S->hsl_e ||
        rcp > S->rcp_e || stp > S->stp_e || opp > S->opp_e) return -1;
    for (size_t k = 0; k < NK; k++) if (aval[k] > S->aval_e[k]) return -1;
  }
  if ((size_t)(o - output) != S->out_size) return -1;
  return (int64_t)(o - output);
}

extern "C" LAB_EXPORT int64_t lab_rows(void*, const uint64_t*, size_t, uint8_t*, size_t, uint64_t*) {
  return -1;
}

extern "C" LAB_EXPORT void lab_close(void* st) { delete (State*)st; }
