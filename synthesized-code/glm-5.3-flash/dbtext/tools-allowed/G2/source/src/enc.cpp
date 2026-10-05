#include "common.h"
#include "bpe.h"
#include "huff.h"
#include <cstdlib>
#include <unordered_map>

using namespace std;

typedef vector<uint8_t> Buf;
static inline void put8(Buf& b, uint32_t v) { b.push_back((uint8_t)v); }
static inline void put16(Buf& b, uint32_t v) { b.push_back((uint8_t)v); b.push_back((uint8_t)(v >> 8)); }
static inline void put32(Buf& b, uint64_t v) { for (int i = 0; i < 4; i++) b.push_back((uint8_t)(v >> (8 * i))); }
static inline void put64(Buf& b, uint64_t v) { for (int i = 0; i < 8; i++) b.push_back((uint8_t)(v >> (8 * i))); }
static inline void putuv(Buf& b, uint64_t v) { while (v >= 128) { b.push_back((uint8_t)(128u | (v & 127))); v >>= 7; } b.push_back((uint8_t)v); }
static inline void putuvb(Buf& b, uint32_t v) { while (v >= 128) { b.push_back((uint8_t)(128u | (v & 127))); v >>= 7; } b.push_back((uint8_t)v); }
static inline int nbits_needed(uint64_t x) { int n = 0; while (x) { n++; x >>= 1; } return n; }
// bump codes until no symbol has an all-ones code (reserved for MATCH)
static void reserve_allones(std::vector<uint8_t>& lens) {
  for (int guard = 0; guard < 64; guard++) {
    std::vector<uint32_t> codes;
    canonical_codes(lens, codes);
    bool bad = false;
    for (uint32_t s = 0; s < lens.size(); s++) {
      if (lens[s] && codes[s] == (1u << lens[s]) - 1) {
        lens[s]++;
        if (lens[s] > 15) { /* give up: leave bumped (cap enforced by caller) */ }
        bad = true;
      }
    }
    if (!bad) return;
  }
}

static inline uint32_t rev_bits(uint32_t c, int L) {
  uint32_t r = 0;
  for (int i = 0; i < L; i++) { r = (r << 1) | (c & 1); c >>= 1; }
  return r;
}

static bool is_digit(uint8_t c) { return c >= '0' && c <= '9'; }
static int hexval(uint8_t c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// ---------------- structure detection (same as v1) ----------------

struct CnameInfo { std::string prefix; int digits; std::vector<uint32_t> vals; };
static bool try_cname(const vector<string>& rows, CnameInfo& ci) {
  size_t n = rows.size();
  if (n < 100) return false;
  size_t p = 0;
  while (p < rows[0].size() && !is_digit((uint8_t)rows[0][p])) p++;
  if (p == 0 || p == rows[0].size()) return false;
  int d = (int)rows[0].size() - (int)p;
  if (d < 1 || d > 9) return false;
  ci.prefix = rows[0].substr(0, p); ci.digits = d;
  ci.vals.resize(n);
  for (size_t i = 0; i < n; i++) {
    const string& s = rows[i];
    if (s.size() != p + (size_t)d) return false;
    if (s.compare(0, p, ci.prefix) != 0) return false;
    uint64_t v = 0;
    for (int j = 0; j < d; j++) {
      uint8_t c = (uint8_t)s[p + j];
      if (!is_digit(c)) return false;
      v = v * 10 + (c - '0');
    }
    ci.vals[i] = (uint32_t)v;
  }
  return true;
}

static bool try_genome(const vector<string>& rows, int& len) {
  if (rows.size() < 100) return false;
  len = (int)rows[0].size();
  if (len < 4 || len > 32) return false;
  for (auto& s : rows) {
    if ((int)s.size() != len) return false;
    for (uint8_t c : s) if (c != 'a' && c != 'c' && c != 'g' && c != 't') return false;
  }
  return true;
}

static bool try_hexcol(const vector<string>& rows, int& minl, int& maxl) {
  if (rows.size() < 100) return false;
  minl = 100; maxl = 0;
  for (auto& s : rows) {
    int L = (int)s.size();
    if (L < 1 || L > 8) return false;
    if (L < minl) minl = L;
    if (L > maxl) maxl = L;
    for (uint8_t c : s) if (hexval(c) < 0) return false;
  }
  return true;
}

struct UuidInfo {
  uint16_t time_mid, time_hiv;
  vector<uint32_t> tl, clk;
  vector<uint64_t> node;
  uint32_t tlmin; int wtl;
};
static bool try_uuid(const vector<string>& rows, UuidInfo& ui) {
  if (rows.size() < 100) return false;
  for (auto& s : rows) {
    if (s.size() != 36) return false;
    if (s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-') return false;
    for (int i = 9; i < 13; i++) if (hexval((uint8_t)s[i]) < 0) return false;
    for (int i = 14; i < 18; i++) if (hexval((uint8_t)s[i]) < 0) return false;
    for (int i = 19; i < 23; i++) if (hexval((uint8_t)s[i]) < 0) return false;
    for (int i = 24; i < 36; i++) if (hexval((uint8_t)s[i]) < 0) return false;
  }
  uint16_t tm = 0, th = 0;
  for (int i = 9; i < 13; i++) tm = (tm << 4) | hexval((uint8_t)rows[0][i]);
  for (int i = 14; i < 18; i++) th = (th << 4) | hexval((uint8_t)rows[0][i]);
  for (size_t r = 1; r < rows.size(); r++) {
    uint16_t tm2 = 0, th2 = 0;
    for (int i = 9; i < 13; i++) tm2 = (tm2 << 4) | hexval((uint8_t)rows[r][i]);
    for (int i = 14; i < 18; i++) th2 = (th2 << 4) | hexval((uint8_t)rows[r][i]);
    if (tm2 != tm || th2 != th) return false;
  }
  ui.time_mid = tm; ui.time_hiv = th;
  ui.tl.resize(rows.size()); ui.clk.resize(rows.size()); ui.node.resize(rows.size());
  uint32_t tlmin = 0xffffffffu, tlmax = 0;
  for (size_t i = 0; i < rows.size(); i++) {
    const string& s = rows[i];
    uint32_t tl = 0; for (int j = 0; j < 8; j++) tl = (tl << 4) | hexval((uint8_t)s[j]);
    uint32_t ck = 0; for (int j = 19; j < 23; j++) ck = (ck << 4) | hexval((uint8_t)s[j]);
    uint64_t nd = 0; for (int j = 24; j < 36; j++) nd = (nd << 4) | hexval((uint8_t)s[j]);
    if ((ck >> 14) != 2) return false;
    if (nd >> 48) return false;
    ui.tl[i] = tl; ui.clk[i] = ck & 0x3fffu; ui.node[i] = nd;
    if (tl < tlmin) tlmin = tl;
    if (tl > tlmax) tlmax = tl;
  }
  ui.tlmin = tlmin;
  ui.wtl = nbits_needed((uint64_t)(tlmax - tlmin)); if (!ui.wtl) ui.wtl = 1;
  if (ui.wtl > 22) return false;
  return true;
}

struct LocInfo {
  int lat_int;
  int lon_a, lon_b;
  vector<uint32_t> dlat, dlon;
  vector<uint64_t> vlat, vlon;
  vector<uint8_t> lonb;
  int dlat_min, dlon_min, dlat_max, dlon_max;
  uint64_t lat_min, lon_min; int wlat, wlon;
  vector<uint32_t> special_idx;
};

static bool parse_loc(const string& s, int& lat_int, uint64_t& latf, int& latd,
                      int& lon_int, uint64_t& lonf, int& lond) {
  size_t i = 0;
  if (i >= s.size() || s[i] != '(') return false; i++;
  size_t st = i; while (i < s.size() && is_digit((uint8_t)s[i])) i++;
  if (i == st) return false;
  lat_int = atoi(s.substr(st, i - st).c_str());
  if (i >= s.size() || s[i] != '.') return false; i++;
  st = i; while (i < s.size() && is_digit((uint8_t)s[i])) i++;
  if (i == st || i - st > 15) return false;
  latd = (int)(i - st); latf = 0;
  for (size_t j = st; j < i; j++) latf = latf * 10 + (s[j] - '0');
  if (i + 1 >= s.size() || s[i] != ',' || s[i + 1] != ' ') return false; i += 2;
  if (i >= s.size() || s[i] != '-') return false; i++;
  st = i; while (i < s.size() && is_digit((uint8_t)s[i])) i++;
  if (i == st) return false;
  lon_int = -atoi(s.substr(st, i - st).c_str());
  if (i >= s.size() || s[i] != '.') return false; i++;
  st = i; while (i < s.size() && is_digit((uint8_t)s[i])) i++;
  if (i == st || i - st > 15) return false;
  lond = (int)(i - st); lonf = 0;
  for (size_t j = st; j < i; j++) lonf = lonf * 10 + (s[j] - '0');
  if (i + 1 != s.size() || s[i] != ')') return false;
  return true;
}

static bool try_loc(const vector<string>& rows, LocInfo& li) {
  size_t n = rows.size();
  if (n < 100) return false;
  li.dlat.resize(n); li.dlon.resize(n); li.vlat.resize(n); li.vlon.resize(n); li.lonb.resize(n);
  li.lat_int = -9999; li.lon_a = 9999; li.lon_b = 9999;
  li.dlat_min = 99; li.dlon_min = 99; li.dlat_max = 0; li.dlon_max = 0;
  li.special_idx.clear();
  int lat_int = 0, lon_int = 0; uint64_t latf = 0, lonf = 0; int latd = 0, lond = 0;
  for (size_t i = 0; i < n; i++) {
    if (!parse_loc(rows[i], lat_int, latf, latd, lon_int, lonf, lond)) {
      li.special_idx.push_back((uint32_t)i);
      continue;
    }
    if (li.lat_int == -9999) { li.lat_int = lat_int; li.lon_a = lon_int; li.lon_b = lon_int; }
    if (lat_int != li.lat_int) return false;
    if (lon_int == li.lon_a || lon_int == li.lon_b) {}
    else if (li.lon_a == li.lon_b) li.lon_b = lon_int;
    else return false;
    li.dlat[i] = latd; li.dlon[i] = lond;
    if (latd < li.dlat_min) li.dlat_min = latd;
    if (lond < li.dlon_min) li.dlon_min = lond;
    if (latd > li.dlat_max) li.dlat_max = latd;
    if (lond > li.dlon_max) li.dlon_max = lond;
  }
  if (li.special_idx.size() * 2 > n) return false;
  int Dl = li.dlat_max, Do = li.dlon_max;
  uint64_t latmin = ~0ull, lonmin = ~0ull, latmax = 0, lonmax = 0;
  size_t si = 0;
  for (size_t i = 0; i < n; i++) {
    if (si < li.special_idx.size() && li.special_idx[si] == i) { si++; continue; }
    int li2, lo2, ld1, ld2; uint64_t f1, f2;
    parse_loc(rows[i], li2, f1, ld1, lo2, f2, ld2);
    uint64_t sl = 1, so = 1;
    for (int j = ld1; j < Dl; j++) sl *= 10;
    for (int j = ld2; j < Do; j++) so *= 10;
    uint64_t vl = f1 * sl, vo = f2 * so;
    li.vlat[i] = vl; li.vlon[i] = vo;
    li.lonb[i] = (lo2 == li.lon_b && li.lon_a != li.lon_b) ? 1 : 0;
    if (vl < latmin) latmin = vl; if (vl > latmax) latmax = vl;
    if (vo < lonmin) lonmin = vo; if (vo > lonmax) lonmax = vo;
  }
  li.lat_min = latmin; li.lon_min = lonmin;
  li.wlat = nbits_needed(latmax - latmin);
  li.wlon = nbits_needed(lonmax - lonmin);
  if (li.wlat > 60 || li.wlon > 60) return false;
  if (li.dlat_max - li.dlat_min > 15 || li.dlon_max - li.dlon_min > 15) return false;
  return true;
}

// ---------------- token codec with contexts ----------------

struct CtxCandidate {
  int mode;       // 0 none, 1 positional, 2 prev-bucket
  int nctx;
};

// context id per token position
struct CtxMap {
  const uint32_t* rankb; // per-token bucket+1 (0 = not in top list); mode 3 only
  int ctx_of(int mode, int nctx, int pos, uint32_t prev) const {
    if (mode == 0) return 0;
    if (mode == 1) return pos < nctx - 1 ? pos : nctx - 1;
    if (mode == 2) return pos == 0 ? 0 : 1 + (int)(prev % (uint32_t)(nctx - 1));
    // mode 3: prev top-K
    if (pos == 0) return 0;
    uint32_t b = rankb[prev];
    return b ? (int)b : nctx - 1;
  }
};

struct TokConfig { int cap; int mode; int nctx; };

struct MatchSeg { uint32_t tpos, src, spos, cnt; };

static size_t materialize_token(const vector<string>& rows, const Bpe& bpe,
                                const vector<uint32_t>& order,
                                const vector<vector<MatchSeg>>& matches,
                                const TokConfig& tc, uint8_t final_nl, Buf& out) {
  uint32_t vocab = (uint32_t)bpe.toks.size();
  uint32_t nrows = (uint32_t)rows.size();
  size_t orig = 0;
  for (auto& r : rows) orig += r.size();

  vector<uint32_t> rankb(vocab, 0);
  if (tc.mode == 3) {
    int K = tc.nctx - 2;
    for (int i = 0; i < K && i < (int)vocab; i++) rankb[order[i]] = 1 + i;
  }
  CtxMap cm{rankb.data()};
  int nctx = tc.nctx;

  vector<vector<uint64_t>> cf(nctx);
  for (int c = 0; c < nctx; c++) cf[c].assign(vocab, 0);
  for (uint32_t i = 0; i < nrows; i++) {
    uint32_t prev = 0;
    int pos = 0;
    for (uint32_t t : bpe.row_seq[i]) {
      cf[cm.ctx_of(tc.mode, nctx, pos, prev)][t]++;
      prev = t;
      pos++;
    }
  }
  vector<vector<uint8_t>> lens(nctx);
  int k = 9;
  for (int c = 0; c < nctx; c++) {
    huff_lengths(cf[c], tc.cap, lens[c]);
    for (uint32_t s2 = 0; s2 < vocab; s2++) if (lens[c][s2] && lens[c][s2] < 4) lens[c][s2] = 4;
    for (uint32_t s2 = 0; s2 < vocab; s2++) if (lens[c][s2] > k) k = lens[c][s2];
  }
  bool has_match = false; // row matching disabled (net-negative)
  vector<vector<uint32_t>> codes(nctx);
  for (int c = 0; c < nctx; c++) {
    if (has_match) reserve_allones(lens[c]);
    canonical_codes(lens[c], codes[c]);
    if (has_match) for (uint32_t s2 = 0; s2 < vocab; s2++) if (lens[c][s2] > k) k = lens[c][s2];
  }

  Buf arena;
  vector<uint32_t> toff(vocab + 1, 0);
  vector<uint8_t> tlens(vocab, 0);
  for (uint32_t i = 0; i < vocab; i++) {
    tlens[i] = (uint8_t)bpe.toks[i].size();
    toff[i + 1] = toff[i] + (uint32_t)bpe.toks[i].size();
    arena.insert(arena.end(), bpe.toks[i].begin(), bpe.toks[i].end());
  }

  uint32_t sbits = nbits_needed(nrows > 1 ? nrows - 1 : 0); if (sbits < 1) sbits = 1;
  uint32_t maxtok = 1;
  for (uint32_t i = 0; i < nrows; i++) if (bpe.row_seq[i].size() > maxtok) maxtok = (uint32_t)bpe.row_seq[i].size();
  uint32_t obits = nbits_needed(maxtok); if (obits < 1) obits = 1;
  vector<uint64_t> starts(nrows + 1, 0);
  Buf stream;
  stream.reserve(orig / 2 + 64);
  BitW bw(stream);
  uint64_t matchmark = (1ull << k) - 1;
  for (uint32_t i = 0; i < nrows; i++) {
    starts[i] = bw.bitlen();
    uint32_t prev = 0;
    int pos = 0;
    uint32_t t = 0;
    uint32_t mi = 0;
    const auto& rs = bpe.row_seq[i];
    while (t < rs.size()) {
      if (mi < matches[i].size() && matches[i][mi].tpos == t) {
        if (getenv("DUMPMATCH") && i == 83) fprintf(stderr, "ENC-MATCH i=%u k=%u sbits=%u obits=%u src=%u spos=%u cnt=%u\n", i, k, sbits, obits, matches[i][mi].src, matches[i][mi].spos, matches[i][mi].cnt);
        const MatchSeg& m = matches[i][mi];
        bw.put(matchmark, k);
        bw.put(m.src, sbits);
        bw.put(m.spos, obits);
        bw.put(m.cnt, 8);
        mi++;
        t += m.cnt;
        prev = 0;
        pos = 0;
        continue;
      }
      uint32_t tok = rs[t];
      int c = cm.ctx_of(tc.mode, nctx, pos, prev);
      bw.put(rev_bits(codes[c][tok], lens[c][tok]), lens[c][tok]);
      prev = tok;
      pos++;
      t++;
    }
    while (bw.bitlen() & 3) bw.put(0, 1);
  }
  if (getenv("DUMPMATCH")) {
    uint32_t target = (uint32_t)atoi(getenv("DUMPMATCH"));
    for (auto& m : matches[target]) {
      string cat;
      for (uint32_t z = 0; z < m.cnt; z++) cat += bpe.toks[bpe.row_seq[m.src][m.spos + z]];
      fprintf(stderr, "MATCH row=%u tpos=%u src=%u spos=%u cnt=%u bytes=[\n", target, m.tpos, m.src, m.spos, m.cnt);
      fprintf(stderr, "%s\n", cat.c_str());
    }
  }
  starts[nrows] = bw.bitlen();
  uint64_t nbits = starts[nrows];
  bw.flush();
  stream.resize(stream.size() + 16, 0);

  bool failed_flag = false;
  int wide = 0, nib = 1;
  vector<uint32_t> d32(nrows);
  for (uint32_t i = 0; i < nrows; i++) {
    uint64_t span = (starts[i + 1] - starts[i] + 3) >> 2;
    d32[i] = (uint32_t)span;
    if (span > 254) wide = 1;
    if (span > 14) nib = 0;
  }
  if (failed_flag) return 0;
  // self-check: simulate decoder walk over every row and compare bytes
  {
    vector<uint8_t> sd = stream;
    auto rd = [&](uint64_t bitpos, int nb2) -> uint64_t {
      uint64_t w; memcpy(&w, sd.data() + (bitpos >> 3), 8);
      return (w >> (bitpos & 7)) & ((nb2 >= 64 ? ~0ULL : ((1ULL << nb2) - 1)));
    };
    vector<vector<uint32_t>> tabs(nctx);
    for (int c = 0; c < nctx; c++) {
      tabs[c].assign(1ull << k, 0);
      for (uint32_t s2 = 0; s2 < vocab; s2++) {
        if (!lens[c][s2]) continue;
        uint32_t rv = rev_bits(codes[c][s2], lens[c][s2]);
        for (uint64_t v = rv; v < (1ull << k); v += (1ull << lens[c][s2])) tabs[c][v] = s2 | ((uint32_t)lens[c][s2] << 24);
      }
    }
    uint64_t matchmark = (1ull << k) - 1;
    uint32_t sb2 = nbits_needed(nrows > 1 ? nrows - 1 : 0); if (sb2 < 1) sb2 = 1;
    uint32_t ob2 = nbits_needed(maxtok); if (ob2 < 1) ob2 = 1;
uint64_t p2 = 0;
    for (uint32_t i = 0; i < nrows; i++) {
uint64_t send = p2 + 4ull * d32[i];
      string cat;
      uint32_t prev = 0;
      int pos = 0;
      uint64_t spos2 = 0;
      while (p2 < send) {
        if (has_match) {
          uint64_t idx2 = rd(p2, k);
          if (idx2 == matchmark) {
            uint64_t src = rd(p2 + k, sb2);
            uint64_t sp = rd(p2 + k + sb2, ob2);
            uint64_t cn = rd(p2 + k + sb2 + ob2, 8);
            if (src < nrows) {
              for (uint32_t z = 0; z < cn && sp + z < bpe.row_seq[src].size(); z++) cat += bpe.toks[bpe.row_seq[src][sp + z]];
            }
            p2 += k + (uint64_t)sb2 + ob2 + 8;
            prev = 0; pos = 0;
            continue;
          }
        }
        int c = 0;
        if (tc.mode == 1) c = pos < nctx - 1 ? pos : nctx - 1;
        else if (tc.mode == 2) c = (pos == 0) ? 0 : 1 + (int)(prev % (uint32_t)(nctx - 1));
        else if (tc.mode == 3) {
          if (pos == 0) c = 0;
          else { uint32_t b3 = rankb[prev]; c = b3 ? (int)b3 : nctx - 1; }
        }
        uint32_t e = tabs[c][rd(p2, k)];
        uint32_t L = e >> 24, s3 = e & 0xffffffu;
        if (!L || p2 + L > send) break;
        if (getenv("DUMPMATCH") && i == 83 && cat.size() >= 70) {
          fprintf(stderr, "ENC-SC tok p2=%llu idx=%u L=%u sym=%u len=%u\n", (unsigned long long)p2, (unsigned)(rd(p2, k)), L, s3, bpe.toks[s3].size());
        }
        p2 += L;
        cat += bpe.toks[s3];
        prev = s3;
        pos++;
      }
      p2 = send;
      if (cat != rows[i]) {
        fprintf(stderr, "SELF-CHECK FAIL cfg cap=%d mode=%d nctx=%d row %u got=%zu want=%zu\n", tc.cap, tc.mode, tc.nctx, i, cat.size(), rows[i].size());
        if (getenv("DUMPDIFF")) {
          size_t zz = 0;
          while (zz < cat.size() && zz < rows[i].size() && cat[zz] == rows[i][zz]) zz++;
          fprintf(stderr, "DIFF at %zu got=", zz);
          for (size_t y = zz; y < zz + 12 && y < cat.size(); y++) fprintf(stderr, "%02x", (unsigned char)cat[y]);
          fprintf(stderr, " want=");
          for (size_t y = zz; y < zz + 12 && y < rows[i].size(); y++) fprintf(stderr, "%02x", (unsigned char)rows[i][y]);
          fprintf(stderr, "\n");
        }
        break;
      }
    }
  }
  uint32_t shift = 5;
  uint32_t nsamp = (nrows + (1u << shift) - 1) >> shift;
  vector<uint32_t> samples(nsamp);
  for (uint32_t j = 0; j < nsamp; j++) samples[j] = (uint32_t)starts[j << shift];

  put8(out, C_TOKEN);
  putuv(out, nrows); putuv(out, orig); put8(out, final_nl);
  put8(out, (uint8_t)k);
  put8(out, (uint8_t)((wide ? 1 : 0) | (nib ? 8 : 0) | (tc.mode << 1) | (has_match ? 16 : 0)));
  put8(out, (uint8_t)nctx);
  putuv(out, vocab);
  put32(out, arena.size()); out.insert(out.end(), arena.begin(), arena.end());
  out.insert(out.end(), tlens.begin(), tlens.end());
  out.insert(out.end(), lens[nctx - 1].begin(), lens[nctx - 1].end());
  for (int c = 0; c + 1 < nctx; c++) {
    Buf packed;
    uint32_t run = 0;
    for (uint32_t s2 = 0; s2 < vocab; s2++) {
      if (cf[c][s2]) {
        putuvb(packed, run);
        packed.push_back((uint8_t)(lens[c][s2] - 1));
        run = 0;
      } else run++;
    }
    put32(out, (uint32_t)packed.size());
    out.insert(out.end(), packed.begin(), packed.end());
  }
  if (has_match) {
    put8(out, (uint8_t)sbits);
    put8(out, (uint8_t)obits);
  }
  if (tc.mode == 3) {
    int K = nctx - 2;
    putuv(out, (uint32_t)K);
    for (int i = 0; i < K && i < (int)vocab; i++) putuv(out, order[i]);
  }
  put32(out, (uint32_t)nbits);
  out.insert(out.end(), stream.begin(), stream.end());
  put8(out, shift);
  put32(out, nsamp);
  for (uint32_t j = 0; j < nsamp; j++) put32(out, samples[j]);
  if (wide) {
    for (uint32_t i = 0; i < nrows; i++) put16(out, d32[i]);
  } else if (nib) {
    for (uint32_t i = 0; i + 1 < nrows; i += 2)
      put8(out, d32[i] | (d32[i + 1] << 4));
    if (nrows & 1) put8(out, d32[nrows - 1]);
  } else {
    for (uint32_t i = 0; i < nrows; i++) put8(out, d32[i]);
  }
  return out.size();
}

static inline uint32_t nrows_dummy_get(const vector<string>& rows) { return (uint32_t)rows.size(); }

static void encode_token(const vector<string>& rows, Buf& out, uint8_t final_nl, uint32_t max_vocab) {
  Bpe bpe;
  bpe.train(rows, max_vocab, 255);
  uint32_t vocab = (uint32_t)bpe.toks.size();
  vector<uint64_t> freq(vocab, 0);
  for (auto& r : bpe.row_seq) for (uint32_t t : r) freq[t]++;
  vector<uint32_t> order(vocab);
  for (uint32_t i = 0; i < vocab; i++) order[i] = i;
  sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b2) { return freq[a] > freq[b2]; });
  // remap token ids by frequency rank: hot tokens get low ids (arena/table locality)
  {
    vector<uint32_t> remap(vocab);
    for (uint32_t i = 0; i < vocab; i++) remap[order[i]] = i;
    vector<std::string> toks2(vocab);
    for (uint32_t i = 0; i < vocab; i++) toks2[i] = bpe.toks[order[i]];
    bpe.toks.swap(toks2);
    for (auto& r : bpe.row_seq) for (uint32_t& t : r) t = remap[t];
    vector<uint64_t> freq2(vocab);
    for (uint32_t i = 0; i < vocab; i++) freq2[i] = freq[order[i]];
    freq.swap(freq2);
  }

  // cross-row token-sequence matching (depth-1: sources are match-free)
  vector<vector<MatchSeg>> matches(nrows_dummy_get(rows));
  vector<char> matched(rows.size(), 0);
  vector<uint32_t> tlen(vocab);
  for (uint32_t i = 0; i < vocab; i++) tlen[i] = (uint32_t)bpe.toks[i].size();
  vector<uint32_t> est_bits(vocab, 0);
  {
    vector<uint8_t> el;
    huff_lengths(freq, 15, el);
    for (uint32_t i = 0; i < vocab; i++) est_bits[i] = el[i] ? el[i] : 15;
  }
  {
    std::unordered_map<uint64_t, std::vector<std::pair<uint32_t, uint32_t>>> cand;
    cand.reserve(bpe.row_seq.size() * 4);
    for (uint32_t i = 0; i < rows.size(); i++) {
      const auto& rs = bpe.row_seq[i];
      if (rs.size() >= 2) {
        uint32_t t = 0;
        while (t + 1 < rs.size()) {
          uint64_t key = ((uint64_t)rs[t] << 32) | rs[t + 1];
          auto it = cand.find(key);
          int bestb = -1;
          uint32_t bestr = 0, bestp = 0, bestc = 0;
          if (it != cand.end()) {
            for (auto& pr : it->second) {
              uint32_t r = pr.first, tp = pr.second;
              const auto& src = bpe.row_seq[r];
              uint32_t cnt = 0;
              while (t + cnt < rs.size() && tp + cnt < src.size() && rs[t + cnt] == src[tp + cnt] && cnt < 255) cnt++;
              uint64_t bytes = 0;
              uint64_t litbits = 0;
              for (uint32_t z = 0; z < cnt; z++) {
                bytes += tlen[rs[t + z]];
                litbits += est_bits[rs[t + z]];
              }
              if ((int)litbits > bestb) { bestb = (int)litbits; bestr = r; bestp = tp; bestc = cnt; }
            }
          }
          int mcost = 15 + 24 + 12 + 8 + 4; // k + src + off + cnt + slack (conservative)
          if (bestb > mcost) {
            matches[i].push_back({t, bestr, bestp, bestc});
            matched[i] = 1;
            t += bestc;
          } else t++;
        }
      }
      // register pairs if this row stays match-free (depth-1 sources)
      if (!matched[i] && rs.size() >= 2) {
        for (uint32_t t = 0; t + 1 < rs.size(); t++) {
          uint64_t key = ((uint64_t)rs[t] << 32) | rs[t + 1];
          auto& v = cand[key];
          if (v.size() < 12) v.push_back({i, t});
        }
      }
    }
  }

  {
    size_t tot = 0;
    for (auto& m : matches) tot += m.size();
    fprintf(stderr, "MATCHES total=%zu rows=%u\n", tot, (unsigned)rows.size());
  }
  for (auto& m : matches) m.clear();
  Buf best, cur;
  size_t best_sz = (size_t)~0ll;
  TokConfig best_tc{15, 0, 1};
  for (int cap : {12, 15}) {
    if ((1 << cap) < (int)vocab) continue;
    for (int mode_nctx_i = 0; mode_nctx_i < 10; mode_nctx_i++) {
      static const TokConfig cfgs[10] = {
        {0, 0, 1}, {0, 1, 3}, {0, 1, 4}, {0, 1, 6},
        {0, 2, 5}, {0, 2, 9}, {0, 3, 6}, {0, 3, 10}, {0, 3, 18}, {0, 3, 34},
      };
      TokConfig tc = cfgs[mode_nctx_i];
      tc.cap = cap;
      cur.clear();
      size_t sz = materialize_token(rows, bpe, order, matches, tc, final_nl, cur);
      bool better = sz < best_sz ||
                    (sz <= best_sz + best_sz / 150 + 48 && tc.cap < best_tc.cap);
      if (better) { best_sz = sz; best_tc = tc; best.swap(cur); }
    }
  }
  fprintf(stderr, "CFG mv=%u k=%d mode=%d nctx=%d\n", max_vocab, best_tc.cap, best_tc.mode, best_tc.nctx);
  out.insert(out.end(), best.begin(), best.end());
}

extern "C" int64_t lab_encode(const uint8_t* raw, size_t size, uint8_t* archive, size_t capacity) {
  vector<string> rows;
  bool final_nl;
  split_rows(raw, size, rows, final_nl);
  uint8_t fnl = final_nl ? 1 : 0;
  Buf out;
  out.reserve(size / 2 + 4096);

  CnameInfo ci;
  int glen = 0, hmin = 0, hmax = 0;
  UuidInfo ui;
  LocInfo li;

  if (try_cname(rows, ci)) {
    uint32_t maxv = 0;
    for (uint32_t v : ci.vals) if (v > maxv) maxv = v;
    int w = nbits_needed(maxv); if (!w) w = 1;
    put8(out, C_CNAME);
    putuv(out, rows.size()); putuv(out, size); put8(out, fnl);
    put8(out, (uint8_t)w);
    put8(out, (uint8_t)ci.prefix.size());
    put8(out, (uint8_t)ci.digits);
    out.insert(out.end(), ci.prefix.begin(), ci.prefix.end());
    BitW bw(out);
    for (uint32_t v : ci.vals) bw.put(v, w);
    bw.flush();
    out.resize(out.size() + 16, 0);
  } else if (try_genome(rows, glen)) {
    put8(out, C_GENOME);
    putuv(out, rows.size()); putuv(out, size); put8(out, fnl);
    put8(out, (uint8_t)glen);
    BitW bw(out);
    for (auto& s : rows) {
      uint64_t v = 0;
      for (uint8_t c : s) v = (v << 2) | (c == 'c' ? 1 : c == 'g' ? 2 : c == 't' ? 3 : 0);
      bw.put(v, glen * 2);
    }
    bw.flush();
    out.resize(out.size() + 16, 0);
  } else if (try_hexcol(rows, hmin, hmax)) {
    put8(out, C_HEX);
    putuv(out, rows.size()); putuv(out, size); put8(out, fnl);
    put8(out, (uint8_t)hmax); put8(out, (uint8_t)hmin);
    BitW bw(out);
    for (auto& s : rows) {
      uint64_t v = 0;
      for (uint8_t c : s) v = (v << 4) | hexval(c);
      bw.put((uint64_t)((int)s.size() - hmin), 3);
      bw.put(v, hmax * 4);
    }
    bw.flush();
    out.resize(out.size() + 16, 0);
  } else if (try_uuid(rows, ui)) {
    put8(out, C_UUID);
    putuv(out, rows.size()); putuv(out, size); put8(out, fnl);
    put16(out, ui.time_mid); put16(out, ui.time_hiv);
    put8(out, (uint8_t)ui.wtl); put32(out, ui.tlmin);
    BitW bw(out);
    for (uint32_t v : ui.tl) bw.put(v - ui.tlmin, ui.wtl);
    for (uint32_t v : ui.clk) bw.put(v, 14);
    for (uint64_t v : ui.node) bw.put(v, 48);
    bw.flush();
    out.resize(out.size() + 16, 0);
  } else if (try_loc(rows, li)) {
    put8(out, C_LOC);
    putuv(out, rows.size()); putuv(out, size); put8(out, fnl);
    put16(out, (uint16_t)li.lat_int); put16(out, (uint16_t)li.lon_a); put16(out, (uint16_t)(int16_t)li.lon_b);
    put8(out, (uint8_t)li.dlat_min); put8(out, (uint8_t)(li.dlat_max - li.dlat_min));
    put8(out, (uint8_t)li.dlon_min); put8(out, (uint8_t)(li.dlon_max - li.dlon_min));
    put8(out, (uint8_t)li.wlat); put8(out, (uint8_t)li.wlon);
    put64(out, li.lat_min); put64(out, li.lon_min);
    put32(out, li.special_idx.size());
    for (uint32_t idx : li.special_idx) {
      put32(out, idx);
      put8(out, (uint8_t)rows[idx].size());
      out.insert(out.end(), rows[idx].begin(), rows[idx].end());
    }
    int dl_bits = nbits_needed(li.dlat_max - li.dlat_min); if (!dl_bits) dl_bits = 1;
    int do_bits = nbits_needed(li.dlon_max - li.dlon_min); if (!do_bits) do_bits = 1;
    BitW bw(out);
    size_t si = 0;
    for (size_t i = 0; i < rows.size(); i++) {
      if (si < li.special_idx.size() && li.special_idx[si] == i) { si++; bw.put(0, 1 + dl_bits + li.wlat + do_bits + li.wlon); continue; }
      bw.put(li.lonb[i], 1);
      bw.put(li.dlat[i] - li.dlat_min, dl_bits);
      bw.put(li.vlat[i] - li.lat_min, li.wlat);
      bw.put(li.dlon[i] - li.dlon_min, do_bits);
      bw.put(li.vlon[i] - li.lon_min, li.wlon);
    }
    bw.flush();
    out.resize(out.size() + 16, 0);
  } else {
    size_t total = 0;
    for (auto& r : rows) total += r.size();
    // vocabulary candidates by column size
    vector<uint32_t> vc;
    if (total < 200000) vc = {768, 2048, 6144};
    else if (total < 800000) vc = {1536, 4096, 12288};
    else vc = {4096, 8192, 16384};
    // encode with each candidate, keep smallest
    Buf best;
    size_t best_sz = (size_t)~0ll;
    for (uint32_t mv : vc) {
      Buf trial;
      encode_token(rows, trial, fnl, mv);
      if (trial.size() < best_sz) { best_sz = trial.size(); best.swap(trial); }
    }
    out.insert(out.end(), best.begin(), best.end());
  }

  if (out.size() > capacity) return -1;
  memcpy(archive, out.data(), out.size());
  return (int64_t)out.size();
}
