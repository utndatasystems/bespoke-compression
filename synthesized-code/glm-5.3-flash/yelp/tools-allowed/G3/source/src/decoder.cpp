#include "common.h"
#include "interface/codec.h"
#include <x86intrin.h>
#include <malloc.h>

static const char ALPHA[65] = "-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz";
static const char SEG0[16] = "{\"business_id\":";
static const char SEG1[9] = ",\"name\":";
static const char SEG2[12] = ",\"address\":";
static const char SEG3[9] = ",\"city\":";
static const char SEG4[10] = ",\"state\":";
static const char SEG5[16] = ",\"postal_code\":";
static const char SEG6[13] = ",\"latitude\":";
static const char SEG7[14] = ",\"longitude\":";
static const char SEG8[10] = ",\"stars\":";
static const char SEG9[17] = ",\"review_count\":";
static const char SEG10[12] = ",\"is_open\":";
static const char SEG11[15] = ",\"attributes\":";
static const char SEG12[15] = ",\"categories\":";
static const char SEG13[10] = ",\"hours\":";
static const char SEG14[3] = "}\n";

static const char* DAYS[7] = {"Monday","Tuesday","Wednesday","Thursday","Friday","Saturday","Sunday"};
static const int DAYLEN[7] = {6,7,9,8,6,8,6};

struct Pool { std::vector<uint8_t> data; std::vector<uint32_t> off; std::vector<uint8_t> len; int bits = 0; std::vector<uint8_t> ids; std::vector<uint8_t> run; };
struct DCol { std::vector<uint8_t> data; std::vector<uint32_t> off; std::vector<uint8_t> len; int bits = 0; std::vector<uint8_t> ids; std::vector<uint8_t> run; };

struct State {
  uint64_t orig = 0;
  uint32_t nrec = 0;
  std::string seg[15];
  std::vector<uint8_t> bid;
  DCol loc, num3, ll;
  std::vector<uint8_t> aFlat;
  const uint8_t* fe = nullptr;
  std::vector<uint32_t> fOff;
  std::vector<uint8_t> aflags;
  uint32_t nAU = 0;
  bool bad = false;
  std::vector<uint8_t> acbData;
  std::vector<uint32_t> acbOff;
  int aBits = 0; std::vector<uint8_t> aIds, aRun;
  HuffTable tokH;
  std::vector<uint32_t> cOff; std::vector<uint8_t> cBlob;
  int cBits = 0; std::vector<uint8_t> cIds, cRun;
  std::vector<uint8_t> catPool; std::vector<uint32_t> catOff; std::vector<uint32_t> catLen;
  HuffTable hvH;
  std::vector<uint32_t> hOff; std::vector<uint8_t> hBlob;
  int hBits = 0; std::vector<uint8_t> hIds, hRun;
  std::vector<uint8_t> hPool; std::vector<uint32_t> hOffP; std::vector<uint32_t> hLenP;
  const uint8_t* dayPreP[7]; const uint8_t* dayPre0P[7];
  uint8_t dayPreL[7], dayPre0L[7];
  std::vector<uint8_t> dayPre, dayPre0;
};

typedef const uint8_t* CUR;
struct CK { CUR p; CUR e; bool ok; };
static inline uint32_t rd32(CK& c) {
  if (!c.ok || c.e - c.p < 4) { c.ok = false; return 0; }
  uint32_t v; memcpy(&v, c.p, 4); c.p += 4; return v;
}
static inline uint64_t rd64(CK& c) {
  if (!c.ok || c.e - c.p < 8) { c.ok = false; return 0; }
  uint64_t v; memcpy(&v, c.p, 8); c.p += 8; return v;
}
static inline uint64_t rduv(CK& c) {
  uint64_t x = 0; int s = 0;
  while (true) {
    if (!c.ok || c.p >= c.e || s > 56) { c.ok = false; return 0; }
    uint8_t b = *c.p++;
    x |= (uint64_t)(b & 127) << s;
    if (!(b & 128)) break;
    s += 7;
  }
  return x;
}
static inline bool cskip(CK& c, uint64_t n) {
  if (!c.ok || (uint64_t)(c.e - c.p) < n) { c.ok = false; return false; }
  c.p += (size_t)n;
  return true;
}
static inline uint32_t rd32(CUR& p) { uint32_t v; memcpy(&v, p, 4); p += 4; return v; }
static inline uint64_t rd64(CUR& p) { uint64_t v; memcpy(&v, p, 8); p += 8; return v; }

static inline uint32_t huff_dec(BitR& br, const HuffTable& t) {
  if (t.maxlen == 0 || t.map.empty()) return 0;
  uint32_t e = t.map[br.peek(t.maxlen)];
  br.n -= (int)(e >> 16);
  return e & 0xFFFFu;
}

static void rebuild_offs(CK& c, std::vector<uint32_t>& offs, uint32_t n) {
  if (n > (1u << 28)) { c.ok = false; return; }
  if (n == 0) return;
  offs.resize(n);
  offs[0] = (uint32_t)rduv(c);
  for (uint32_t i = 1; i < n; i++) offs[i] = offs[i-1] + (uint32_t)rduv(c);
  uint64_t nck = rduv(c);
  if (!c.ok || nck > (1u << 24) || (uint64_t)(c.e - c.p) < nck) { c.ok = false; return; }
  for (uint64_t cK = 0; cK < nck; cK++) rduv(c);
}

static inline uint8_t* emit(uint8_t* d, const void* s, size_t n) {
  const uint8_t* q = (const uint8_t*)s;
  if (n <= 8) { uint64_t a = 0; memcpy(&a, q, n); memcpy(d, &a, n); return d + n; }
  if (n <= 16) { uint64_t a, b; memcpy(&a, q, 8); memcpy(&b, q + n - 8, 8); memcpy(d, &a, 8); memcpy(d + n - 8, &b, 8); return d + n; }
  if (n <= 24) { uint64_t a, b, c; memcpy(&a, q, 8); memcpy(&b, q + n - 16, 8); memcpy(&c, q + n - 8, 8); memcpy(d, &a, 8); memcpy(d + n - 16, &b, 8); memcpy(d + n - 8, &c, 8); return d + n; }
  memcpy(d, q, n);
  return d + n;
}

static bool g_avx512 = false;

__attribute__((target("avx512f")))
static void copynt512(uint8_t* d, const uint8_t* s, size_t n) {
  size_t head = (size_t)((64 - ((uintptr_t)d & 63)) & 63);
  if (head > n) head = n;
  for (size_t i = 0; i < head; i++) d[i] = s[i];
  d += head; s += head; n -= head;
  while (n >= 64) {
    __m512i a = _mm512_loadu_si512((const void*)s);
    _mm512_stream_si512((__m512i*)d, a);
    d += 64; s += 64; n -= 64;
  }
  for (size_t i = 0; i < n; i++) d[i] = s[i];
}

static inline uint8_t* copyv(uint8_t* d, const uint8_t* s, size_t n) {
  if (n >= 96) {
    if (g_avx512) { copynt512(d, s, n); return d + n; }
    memcpy(d, s, n); return d + n;
  }
  if (n <= 16) {
    uint64_t a, b;
    if (n > 8) { memcpy(&a, s, 8); memcpy(&b, s + n - 8, 8); memcpy(d, &a, 8); memcpy(d + n - 8, &b, 8); }
    else { memcpy(&a, s, n); memcpy(d, &a, n); }
    return d + n;
  }
  memcpy(d, s, n);
  return d + n;
}

static const char DIGIT_PAIRS[201] =
    "00010203040506070809101112131415161718192021222324"
    "25262728293031323334353637383940414243444546474849"
    "50515253545556575859606162636465666768697071727374"
    "75767778798081828384858687888990919293949596979899";

static void render_scaled(int64_t v, int nd, uint8_t* out, int* outlen) {
  uint64_t a = v < 0 ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
  char buf[24];
  int pos = 24;
  while (a >= 100) {
    uint64_t q = a / 100;
    uint32_t r = (uint32_t)(a - q * 100);
    pos -= 2;
    buf[pos] = DIGIT_PAIRS[r * 2];
    buf[pos + 1] = DIGIT_PAIRS[r * 2 + 1];
    a = q;
  }
  if (a >= 10) { pos -= 2; buf[pos] = DIGIT_PAIRS[a * 2]; buf[pos + 1] = DIGIT_PAIRS[a * 2 + 1]; }
  else buf[--pos] = (char)('0' + a);
  int ndig = 24 - pos;
  int need = nd + 1;
  char tmp[28];
  int t = 0;
  for (int i = ndig; i < need; i++) tmp[t++] = '0';
  int total = t + ndig;
  memcpy(tmp + t, buf + pos, (size_t)ndig);
  uint8_t* w = out;
  if (v < 0) *w++ = '-';
  memcpy(w, tmp, (size_t)(total - nd)); w += total - nd;
  *w++ = '.';
  memcpy(w, tmp + total - nd, (size_t)nd); w += nd;
  *outlen = (int)(w - out);
}

void* lab_open(const uint8_t* archive, size_t size) {
  mallopt(M_MMAP_THRESHOLD, 96 * 1024 * 1024); // cache big pool allocations across trials
  g_avx512 = __builtin_cpu_supports("avx512f");
  if (size < 64) return nullptr;
  if (size < 64 + 8ull * NSEC) return nullptr;
  CUR p = archive;
  if (rd32(p) != 0x31504c59) return nullptr;
  if (rd32(p) != 1) return nullptr;
  State* S = new State();
  S->orig = rd64(p);
  S->nrec = rd32(p);
  uint32_t nsec = rd32(p);
  if (nsec != NSEC) { delete S; return nullptr; }
  uint32_t nrec = S->nrec;
  uint64_t offs[NSEC];
  for (int i = 0; i < NSEC; i++) offs[i] = rd64(p);
  const uint8_t* sec[NSEC];
  for (int i = 0; i < NSEC; i++) {
    if (offs[i] > size) { delete S; return nullptr; }
    if (i && offs[i] < offs[i - 1]) { delete S; return nullptr; }
    sec[i] = archive + offs[i];
  }
  const uint8_t* secEnd[NSEC];
  for (int i = 0; i < NSEC - 1; i++) secEnd[i] = sec[i + 1];
  secEnd[NSEC - 1] = archive + size;

  {
    CK q { sec[SEC_SKEL], secEnd[SEC_SKEL], true };
    for (int k = 0; k < 15; k++) {
      uint64_t l = rduv(q);
      if (!q.ok || (uint64_t)(q.e - q.p) < l) { delete S; return nullptr; }
      S->seg[k].assign((const char*)q.p, (size_t)l); q.p += l;
    }
  }
  {
    static const char* EXP[15] = {SEG0,SEG1,SEG2,SEG3,SEG4,SEG5,SEG6,SEG7,SEG8,SEG9,SEG10,SEG11,SEG12,SEG13,SEG14};
    static const int EXPL[15] = {15,8,11,8,9,15,12,13,9,16,11,14,14,9,2};
    for (int k = 0; k < 15; k++) {
      if (S->seg[k].size() != (size_t)EXPL[k] || memcmp(S->seg[k].data(), EXP[k], (size_t)EXPL[k]) != 0)
        return nullptr;
    }
  }
  S->bid.assign(sec[SEC_BID], secEnd[SEC_BID]);
  for (int c = 0; c < 3; c++) {
    DCol& D = *(c == 0 ? &S->loc : c == 1 ? &S->num3 : &S->ll);
    int sp = SEC_LOC + c;
    CK q { sec[sp], secEnd[sp], true };

    if (!zstd_decompress_buf(q.p, q.e, D.data))       { delete S; return nullptr; }
    std::vector<uint8_t> iraw;
    if (!zstd_decompress_buf(q.p, q.e, iraw))       { delete S; return nullptr; }
    CK r { iraw.data(), iraw.data() + iraw.size(), true };
    uint64_t n = rduv(r);
    if (!r.ok || n == 0 || n > (1u << 28))       { delete S; return nullptr; }
    D.off.resize((size_t)n + 1);
    D.off[0] = (uint32_t)rduv(r);
    uint32_t cur2 = D.off[0];
    for (uint64_t i = 1; i <= n; i++) { cur2 += (uint32_t)rduv(r); D.off[i] = cur2; }
    if (!r.ok || (uint64_t)(r.e - r.p) < n)       { delete S; return nullptr; }
    if (!r.ok || (uint64_t)(r.e - r.p) < n)      { delete S; return nullptr; }
    D.len.assign(r.p, r.p + n); r.p += n;
    for (uint64_t i = 0; i < n; i++) if (D.off[i] > D.off[i + 1])      { delete S; return nullptr; }
    if (D.data.size() != D.off[n])       { delete S; return nullptr; }
    uint64_t nck = rduv(r);
    if (!r.ok || nck > (1u << 24) || (uint64_t)(r.e - r.p) < nck)       { delete S; return nullptr; }
    for (uint64_t cK = 0; cK < nck; cK++) rduv(r);
    if (!r.ok)       { delete S; return nullptr; }
    uint64_t bitsv = rduv(q);
    if (!q.ok || bitsv == 0 || bitsv > 24)       { delete S; return nullptr; }
    D.bits = (int)bitsv;
    uint64_t rbl = rduv(q);
    if (!q.ok || rbl > (1u << 28) || (uint64_t)(q.e - q.p) < rbl)       { delete S; return nullptr; }
    D.run.assign(q.p, q.p + rbl); q.p += rbl;
    uint64_t il = rduv(q);
    if (!q.ok || il > (1u << 28) || (uint64_t)(q.e - q.p) < il)       { delete S; return nullptr; }
    D.ids.assign(q.p, q.p + il); q.p += il;
  }
  // attributes: flat arrays + combos
  {
    CK q { sec[SEC_ATTR], secEnd[SEC_ATTR], true };
    uint64_t nU = rduv(q);
    uint64_t rl = rduv(q);
    if (!q.ok || rl > (1u << 28) || (uint64_t)(q.e - q.p) < rl)       { delete S; return nullptr; }
    S->aRun.assign(q.p, q.p + rl); q.p += rl;
    uint64_t aBits = rduv(q);
    if (!q.ok || aBits == 0 || aBits > 24)       { delete S; return nullptr; }
    S->aBits = (int)aBits;
    uint64_t il = rduv(q);
    if (!q.ok || il > (1u << 28) || (uint64_t)(q.e - q.p) < il)       { delete S; return nullptr; }
    S->aIds.assign(q.p, q.p + il); q.p += il;
    uint64_t flz = rduv(q);
    if (!q.ok || (uint64_t)(q.e - q.p) < flz)       { delete S; return nullptr; }
    std::vector<uint8_t> fr;
    if (!zstd_decompress_buf(q.p, q.e, fr))       { delete S; return nullptr; }
    CK v { fr.data(), fr.data() + fr.size(), true };
    uint64_t nU2 = rd32(v);
    uint64_t nPairs = rd32(v);
    if (!v.ok || nU2 > (1u << 28) || nPairs > (1u << 28))       { delete S; return nullptr; }
    if (fr.size() < 8 + nU2 + 4 * (nU2 + 1) + 2 * nPairs)       { delete S; return nullptr; }
    const uint8_t* flags = v.p; v.p += nU2;
    S->fOff.resize((size_t)nU2 + 1);
    memcpy(S->fOff.data(), v.p, 4 * (size_t)(nU2 + 1)); v.p += 4 * (size_t)(nU2 + 1);
    if (S->fOff[0] != 0)       { delete S; return nullptr; }
    for (uint64_t u = 0; u < nU2; u++) if (S->fOff[u] > S->fOff[u + 1] || S->fOff[u + 1] > nPairs)       { delete S; return nullptr; }
    if (S->fOff[nU2] != nPairs)       { delete S; return nullptr; }
    S->nAU = (uint32_t)nU2;
    S->aFlat.swap(fr);
    S->fe = S->aFlat.data() + 8 + nU2 + 4 * (size_t)(nU2 + 1);
    S->aflags.assign(flags, flags + nU2);
    uint64_t cmz = rduv(q);
    if (!q.ok || (uint64_t)(q.e - q.p) < cmz)       { delete S; return nullptr; }
    std::vector<uint8_t> cm;
    if (!zstd_decompress_buf(q.p, q.e, cm))       { delete S; return nullptr; }
    CK cmv { cm.data(), cm.data() + cm.size(), true };
    uint64_t nCombos = rduv(cmv);
    if (!cmv.ok || nCombos > (1u << 28))       { delete S; return nullptr; }
    S->acbOff.resize((size_t)nCombos + 1);
    for (uint64_t i2b = 0; i2b <= nCombos; i2b++) S->acbOff[i2b] = rd32(cmv);
    if (!cmv.ok)       { delete S; return nullptr; }
    for (uint64_t i2b = 0; i2b < nCombos; i2b++) if (S->acbOff[i2b] > S->acbOff[i2b + 1] || S->acbOff[i2b + 1] > (uint64_t)cm.size())       { delete S; return nullptr; }
    S->acbData.assign(cmv.p, cmv.e);
  }
  // categories
  {
    CK q { sec[SEC_CAT], secEnd[SEC_CAT], true };
    uint64_t nTok = rduv(q);
    if (!q.ok || nTok > (1u << 24))       { delete S; return nullptr; }
    std::vector<uint32_t> toff((size_t)nTok + 1);
    for (uint64_t i = 0; i <= nTok; i++) toff[i] = rd32(q);
    if (!q.ok || (uint64_t)(q.e - q.p) < nTok)       { delete S; return nullptr; }
    q.p += nTok; // token string lengths (redundant with toff deltas)
    for (uint64_t i = 0; i < nTok; i++) if (toff[i] > toff[i + 1])      { delete S; return nullptr; }
    if ((uint64_t)(q.e - q.p) < toff[nTok])       { delete S; return nullptr; }
    std::vector<uint8_t> tdat(q.p, q.p + toff[nTok]); q.p += toff[nTok];
    if ((uint64_t)(q.e - q.p) < nTok)       { delete S; return nullptr; }
    std::vector<uint8_t> thl(q.p, q.p + nTok); q.p += nTok;
    if (nTok == 0)       { delete S; return nullptr; }
    huff_build_table(thl, S->tokH);
    if (S->tokH.maxlen == 0)       { delete S; return nullptr; }
    uint64_t nCat = rduv(q);
    if (!q.ok || nCat > (1u << 28))       { delete S; return nullptr; }
    uint64_t cBits = rduv(q);
    if (!q.ok || cBits == 0 || cBits > 24)       { delete S; return nullptr; }
    S->cBits = (int)cBits;
    uint64_t rl = rduv(q);
    if (!q.ok || rl > (1u << 28) || (uint64_t)(q.e - q.p) < rl)       { delete S; return nullptr; }
    S->cRun.assign(q.p, q.p + rl); q.p += rl;
    uint64_t il = rduv(q);
    if (!q.ok || il > (1u << 28) || (uint64_t)(q.e - q.p) < il)       { delete S; return nullptr; }
    S->cIds.assign(q.p, q.p + il); q.p += il;
    uint64_t idxl = rduv(q);
    if (!q.ok || (uint64_t)(q.e - q.p) < idxl)       { delete S; return nullptr; }
    CK idxp { q.p, q.p + idxl, true };
    q.p += idxl;
    rebuild_offs(idxp, S->cOff, (uint32_t)nCat);
    if (!idxp.ok)       { delete S; return nullptr; }
    for (uint32_t u = 0; u + 1 < nCat; u++) if (S->cOff[u] > S->cOff[u + 1])      { delete S; return nullptr; }
    uint64_t bl = rduv(q);
    if (!q.ok || (uint64_t)(q.e - q.p) < bl)       { delete S; return nullptr; }
    S->cBlob.assign(q.p, q.p + bl); q.p += bl;
    for (uint32_t u = 0; u < nCat; u++) if (S->cOff[u] > bl)      { delete S; return nullptr; }
    S->catOff.resize(nCat + 1); S->catLen.resize(nCat);
    // exact size first
    size_t tot = 0;
    for (uint32_t u = 0; u < nCat; u++) {
      const uint8_t* b = S->cBlob.data() + S->cOff[u];
      uint8_t nt = *b;
      if (nt == 0xFF) { tot += 4; continue; }
      tot += 2 + (size_t)(nt) - 1 + 2 * ((size_t)nt - 1);
      for (uint32_t t = 0; t < nt; t++) { /* lengths unknown pre-decode; add via vocab lens after decode */ }
    }
    // simpler: render into a scratch then copy exact sizes; reserve upper bound
    S->catPool.resize(0); S->catPool.reserve(8 << 20);
    std::vector<uint8_t>& CP = S->catPool;
    for (uint32_t u = 0; u < nCat; u++) {
      S->catOff[u] = (uint32_t)CP.size();
      const uint8_t* b = S->cBlob.data() + S->cOff[u];
      uint8_t nt = *b++;
      if (nt == 0xFF) { CP.insert(CP.end(), {'n','u','l','l'}); }
      else {
        BitR br; br.init(b, S->cBlob.data() + S->cBlob.size());
        CP.push_back('"');
        for (uint32_t t = 0; t < nt; t++) {
          if (t) { CP.push_back(','); CP.push_back(' '); }
          uint32_t tid = huff_dec(br, S->tokH);
          CP.insert(CP.end(), tdat.begin() + toff[tid], tdat.begin() + toff[tid + 1]);
        }
        CP.push_back('"');
      }
      S->catLen[u] = (uint32_t)CP.size() - S->catOff[u];
      if (CP.size() > (1ull << 27)) { delete S; return nullptr; }
    }
    S->catOff[nCat] = (uint32_t)CP.size();
  }
  // hours
  {
    CK q { sec[SEC_HOURS], secEnd[SEC_HOURS], true };
    uint64_t nU = rduv(q);
    if (!q.ok || nU > (1u << 28))      { delete S; return nullptr; }
    uint64_t hBits = rduv(q);
    if (!q.ok || hBits == 0 || hBits > 24)      { delete S; return nullptr; }
    S->hBits = (int)hBits;
    uint64_t nV = rduv(q);
    if (!q.ok || nV > (1u << 28))      { delete S; return nullptr; }
    uint64_t rl = rduv(q);
    if (!q.ok || rl > (1u << 28) || (uint64_t)(q.e - q.p) < rl)      { delete S; return nullptr; }
    S->hRun.assign(q.p, q.p + rl); q.p += rl;
    uint64_t il = rduv(q);
    if (!q.ok || il > (1u << 28) || (uint64_t)(q.e - q.p) < il)      { delete S; return nullptr; }
    S->hIds.assign(q.p, q.p + il); q.p += il;
    if ((uint64_t)(q.e - q.p) < nV)      { delete S; return nullptr; }
    std::vector<uint8_t> vhl(q.p, q.p + nV); q.p += nV;
    if (nV == 0)      { delete S; return nullptr; }
    huff_build_table(vhl, S->hvH);
    if (S->hvH.maxlen == 0)      { delete S; return nullptr; }
    uint64_t idxl = rduv(q);
    if (!q.ok || (uint64_t)(q.e - q.p) < idxl)      { delete S; return nullptr; }
    CK idxp { q.p, q.p + idxl, true };
    q.p += idxl;
    rebuild_offs(idxp, S->hOff, (uint32_t)nU);
    if (!idxp.ok)      { delete S; return nullptr; }
    for (uint32_t u = 0; u + 1 < nU; u++) if (S->hOff[u] > S->hOff[u + 1])      { delete S; return nullptr; }
    uint64_t bl = rduv(q);
    if (!q.ok || (uint64_t)(q.e - q.p) < bl)      { delete S; return nullptr; }
    for (uint32_t u = 0; u < nU; u++) if (S->hOff[u] > bl)      { delete S; return nullptr; }
    S->hBlob.assign(q.p, q.p + bl); q.p += bl;
    std::vector<uint32_t> voffs((size_t)nV + 1);
    for (uint64_t i = 0; i <= nV; i++) voffs[i] = rd32(q);
    if (!q.ok || (uint64_t)(q.e - q.p) < nV)      { delete S; return nullptr; }
    for (uint64_t i = 0; i < nV; i++) if (voffs[i] > voffs[i + 1])      { delete S; return nullptr; }
    std::vector<uint8_t> vlens(q.p, q.p + nV); q.p += nV;
    std::vector<uint8_t> vdat(q.p, secEnd[SEC_HOURS]);
    if (voffs[nV] > vdat.size())      { delete S; return nullptr; }
    {
      char tmp0[7][24]; char tmp1[7][24];
      for (int d = 0; d < 7; d++) {
        int l = 2 + DAYLEN[d] + 2;
        tmp0[d][0] = '{'; tmp0[d][1] = '"';
        memcpy(tmp0[d] + 2, DAYS[d], DAYLEN[d]);
        tmp0[d][2 + DAYLEN[d]] = '"'; tmp0[d][3 + DAYLEN[d]] = ':';
        tmp1[d][0] = ','; tmp1[d][1] = '"';
        memcpy(tmp1[d] + 2, DAYS[d], DAYLEN[d]);
        tmp1[d][2 + DAYLEN[d]] = '"'; tmp1[d][3 + DAYLEN[d]] = ':';
        S->dayPre0L[d] = (uint8_t)l; S->dayPreL[d] = (uint8_t)l;
      }
      S->dayPre0.clear(); S->dayPre.clear();
      for (int d = 0; d < 7; d++) S->dayPre0.insert(S->dayPre0.end(), tmp0[d], tmp0[d] + S->dayPre0L[d]);
      for (int d = 0; d < 7; d++) S->dayPre.insert(S->dayPre.end(), tmp1[d], tmp1[d] + S->dayPreL[d]);
      uint32_t dp = 0;
      for (int d = 0; d < 7; d++) { S->dayPre0P[d] = S->dayPre0.data() + dp; dp += S->dayPre0L[d]; }
      dp = 0;
      for (int d = 0; d < 7; d++) { S->dayPreP[d] = S->dayPre.data() + dp; dp += S->dayPreL[d]; }
    }
    S->hOffP.resize(nU + 1); S->hLenP.resize(nU);
    S->hPool.resize(7 << 20);
    uint8_t* w = S->hPool.data();
    for (uint32_t u = 0; u < nU; u++) {
      if ((size_t)(S->hPool.size() - (size_t)(w - S->hPool.data())) < (1u << 20)) {
        size_t off = (size_t)(w - S->hPool.data());
        S->hPool.resize(S->hPool.size() * 2);
        w = S->hPool.data() + off;
      }
      S->hOffP[u] = (uint32_t)(w - S->hPool.data());
      if ((size_t)(w - S->hPool.data()) + 2048 > S->hPool.size()) return nullptr;
      const uint8_t* b = S->hBlob.data() + S->hOff[u];
      uint8_t m = *b++;
      if (m == 0xFF) {
        memcpy(w, "null", 4); w += 4;
      } else {
        BitR br; br.init(b, S->hBlob.data() + S->hBlob.size());
        bool first = true;
        for (int d = 0; d < 7; d++) {
          if (!(m & (1u << d))) continue;
          bool isfirst = first;
          first = false;
          const uint8_t* pre = isfirst ? S->dayPre0P[d] : S->dayPreP[d];
          uint32_t pl2 = isfirst ? S->dayPre0L[d] : S->dayPreL[d];
          memcpy(w, pre, pl2); w += pl2;
          uint32_t vid = huff_dec(br, S->hvH);
          uint32_t vs = voffs[vid], vl = voffs[vid + 1] - vs;
          memcpy(w, vdat.data() + vs, vl); w += vl;
        }
        *w++ = '}';
      }
      S->hLenP[u] = (uint32_t)(w - S->hPool.data()) - S->hOffP[u];
      if ((size_t)(w - S->hPool.data()) > (1ull << 27)) { delete S; return nullptr; }
    }
    S->hOffP[nU] = (uint32_t)(w - S->hPool.data());
    S->hPool.resize((size_t)(w - S->hPool.data()));
  }
  return (void*)S;
}

int64_t lab_decode(void* state, uint8_t* output, size_t capacity) {
  State* S = (State*)state;
  if (!S || capacity < S->orig) return -1;
  const uint32_t nrec = S->nrec;
  uint8_t* o = output;

  BitR bidB, locB, numB, llB, aB, arB, cB, crB, hB, hrB;
  BitR locR, numR, llR;
  bidB.init(S->bid.data(), S->bid.data() + S->bid.size());
  locB.init(S->loc.ids.data(), S->loc.ids.data() + S->loc.ids.size());
  numB.init(S->num3.ids.data(), S->num3.ids.data() + S->num3.ids.size());
  llB.init(S->ll.ids.data(), S->ll.ids.data() + S->ll.ids.size());
  locR.init(S->loc.run.data(), S->loc.run.data() + S->loc.run.size());
  numR.init(S->num3.run.data(), S->num3.run.data() + S->num3.run.size());
  llR.init(S->ll.run.data(), S->ll.run.data() + S->ll.run.size());
  aB.init(S->aIds.data(), S->aIds.data() + S->aIds.size());
  arB.init(S->aRun.data(), S->aRun.data() + S->aRun.size());
  cB.init(S->cIds.data(), S->cIds.data() + S->cIds.size());
  crB.init(S->cRun.data(), S->cRun.data() + S->cRun.size());
  hB.init(S->hIds.data(), S->hIds.data() + S->hIds.size());
  hrB.init(S->hRun.data(), S->hRun.data() + S->hRun.size());

  const uint8_t* const locData = S->loc.data.data();
  const uint8_t* const numData = S->num3.data.data();
  const uint8_t* const llData = S->ll.data.data();
  const uint8_t* const cpD = S->catPool.data();
  const uint8_t* const hpD = S->hPool.data();

  struct RecIds {
    uint32_t loc, num3, ll, auid, cuid, huid;
    bool arun, crun, hrun;
    const uint8_t *locP, *numP, *llP, *apP, *cpP, *hpP;
    uint32_t locL, numL, llL, apL, cpL, hpL;
    bool apNull;
  };
  RecIds cur{}, nxt{};
  uint32_t prevLoc = 0, prevNum = 0, prevLl = 0;
  const uint8_t* prevA = nullptr; uint32_t prevALen = 0;
  const uint8_t* prevC = nullptr; uint32_t prevCLen = 0;
  const uint8_t* prevH = nullptr; uint32_t prevHLen = 0;

  auto read_ids = [&](RecIds& R, const RecIds& prv) {
    R.loc = locR.get(1) ? prv.loc : (uint32_t)locB.get(S->loc.bits);
    R.num3 = numR.get(1) ? prv.num3 : (uint32_t)numB.get(S->num3.bits);
    R.ll = llR.get(1) ? prv.ll : (uint32_t)llB.get(S->ll.bits);
    R.arun = arB.get(1) != 0;
    if (!R.arun) R.auid = (uint32_t)aB.get(S->aBits);
    R.crun = crB.get(1) != 0;
    if (!R.crun) R.cuid = (uint32_t)cB.get(S->cBits);
    R.hrun = hrB.get(1) != 0;
    if (!R.hrun) R.huid = (uint32_t)hB.get(S->hBits);
    if (R.loc >= (uint32_t)S->loc.off.size() - 1u || R.num3 >= (uint32_t)S->num3.off.size() - 1u ||
        R.ll >= (uint32_t)S->ll.off.size() - 1u || R.auid >= (uint32_t)S->fOff.size() - 1u ||
        R.cuid >= (uint32_t)S->catLen.size() || R.huid >= (uint32_t)S->hLenP.size()) {
      S->bad = true;
      return;
    }
    R.locP = locData + S->loc.off[R.loc]; R.locL = S->loc.len[R.loc];
    R.numP = numData + S->num3.off[R.num3]; R.numL = S->num3.len[R.num3];
    R.llP = llData + S->ll.off[R.ll]; R.llL = S->ll.len[R.ll];
    R.apP = S->fe + 2 * (size_t)S->fOff[R.auid]; R.apL = S->fOff[R.auid + 1] - S->fOff[R.auid];
    R.apNull = (S->aflags[R.auid] & 1) != 0;
    R.cpP = cpD + S->catOff[R.cuid]; R.cpL = S->catLen[R.cuid];
    R.hpP = hpD + S->hOffP[R.huid]; R.hpL = S->hLenP[R.huid];
    _mm_prefetch((const char*)R.locP, _MM_HINT_T0);
    _mm_prefetch((const char*)R.locP + 64, _MM_HINT_T0);
    _mm_prefetch((const char*)R.numP, _MM_HINT_T0);
    _mm_prefetch((const char*)R.llP, _MM_HINT_T0);
    _mm_prefetch((const char*)R.apP, _MM_HINT_T0);
    _mm_prefetch((const char*)R.apP + 64, _MM_HINT_T0);
    _mm_prefetch((const char*)R.cpP, _MM_HINT_T0);
    _mm_prefetch((const char*)R.hpP, _MM_HINT_T0);
    _mm_prefetch((const char*)R.hpP + 64, _MM_HINT_T0);
  };

  if (nrec) read_ids(cur, cur);
  for (uint32_t i = 0; i < nrec; i++) {
    if (S->bad) return -2;
    if ((size_t)(o - output) + 107 > capacity) return -7;
    if (i + 1 < nrec) {
          read_ids(nxt, cur);
    }
    const RecIds& R = cur;
    o = emit(o, SEG0, 15);
    *o++ = '"';
    for (int j = 0; j < 11; j++) {
      uint32_t pair = (uint32_t)bidB.get(12);
      *o++ = ALPHA[pair >> 6];
      *o++ = ALPHA[pair & 63];
    }
    *o++ = '"';
    o = emit(o, SEG1, 8);
    if ((size_t)(o - output) + R.locL + 60 > capacity) return -8;
    memcpy(o, R.locP, R.locL); o += R.locL;
    o = emit(o, SEG6, 12);
    if ((size_t)(o - output) + R.llL + 48 > capacity) return -8;
    memcpy(o, R.llP, R.llL); o += R.llL;
    o = emit(o, SEG8, 9);
    if ((size_t)(o - output) + R.numL + 39 > capacity) return -8;
    memcpy(o, R.numP, R.numL); o += R.numL;
    o = emit(o, SEG11, 14);
    if (R.arun && prevA) {
      if ((size_t)(o - output) + prevALen + 25 > capacity) return -8;
      memcpy(o, prevA, prevALen); o += prevALen;
    } else {
      prevA = o;
      if (R.apNull) {
        if ((size_t)(o - output) + 4 + 25 > capacity) return -8;
        memcpy(o, "null", 4); o += 4;
      } else if (R.apL == 0) {
        if ((size_t)(o - output) + 2 + 26 > capacity) return -8;
        *o++ = '{'; *o++ = '}';
      } else {
        const uint16_t* cp = (const uint16_t*)R.apP;
        const uint16_t* cpe = cp + R.apL;
        bool first = true;
        while (cp != cpe) {
          uint32_t c = *cp++;
          if (c >= (uint32_t)S->acbOff.size() - 1u) return -3;
          uint32_t off = S->acbOff[c], len = S->acbOff[c + 1] - off;
          // pair writes '{'(first only) + body(len-1 or len); later '}': tail 25 covers SEG12..SEG14
          if ((size_t)(o - output) + (size_t)len + 26 > capacity) return -8;
          if (first) { *o++ = '{'; ++off; --len; first = false; }
          o = copyv(o, S->acbData.data() + off, len);
        }
        if (first) *o++ = '{';
        *o++ = '}';
      }
      prevALen = (uint32_t)(o - prevA);
    }
    o = emit(o, SEG12, 14);
    if (R.crun && prevC) {
      if ((size_t)(o - output) + prevCLen + 11 > capacity) return -8;
      memcpy(o, prevC, prevCLen); o += prevCLen;
    } else {
      if ((size_t)(o - output) + R.cpL + 11 > capacity) return -8;
      prevC = o;
      o = copyv(o, R.cpP, R.cpL);
      prevCLen = R.cpL;
    }
    o = emit(o, SEG13, 9);
    if (R.hrun && prevH) {
      if ((size_t)(o - output) + prevHLen + 2 > capacity) return -8;
      memcpy(o, prevH, prevHLen); o += prevHLen;
    } else {
      if ((size_t)(o - output) + R.hpL + 2 > capacity) return -8;
      prevH = o;
      o = copyv(o, R.hpP, R.hpL);
      prevHLen = R.hpL;
    }
    o = emit(o, SEG14, 2);
    RecIds& swapcur = nxt;
    RecIds& swapnxt = cur;
    (void)swapcur; (void)swapnxt;
    cur = nxt;
  }
  return (int64_t)(o - output);
}

void lab_close(void* state) {
  delete (State*)state;
}
