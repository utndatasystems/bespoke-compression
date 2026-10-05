#include "codec.h"
#include "format.h"
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>
#include <lz4.h>
#include "huff.h"
extern "C" {
#include "fsst/fsst.h"
}
#include <vector>
#include <string>
#include <algorithm>
#include <unordered_set>
#include <cstdio>

using namespace std;

static vector<pair<const uint8_t*, size_t>> split_rows(const uint8_t* raw, size_t size) {
  vector<pair<const uint8_t*, size_t>> rows;
  size_t start = 0;
  for (size_t i = 0; i < size; i++) {
    if (raw[i] == '\n') { rows.emplace_back(raw + start, i - start + 1); start = i + 1; }
  }
  if (start < size) rows.emplace_back(raw + start, size - start);
  return rows;
}

struct Buf {
  vector<uint8_t> b;
  void u32(uint32_t v) { for (int i = 0; i < 4; i++) b.push_back((uint8_t)(v >> (8 * i))); }
  void u8(uint8_t v) { b.push_back(v); }
  void bytes(const void* p, size_t n) { const uint8_t* q = (const uint8_t*)p; b.insert(b.end(), q, q + n); }
  void zblob(const void* p, size_t n) {
    size_t bound = ZSTD_compressBound(n);
    size_t off = b.size();
    b.resize(off + 4 + bound);
    size_t z = ZSTD_compress(b.data() + off + 4, bound, p, n, 19);
    b.resize(off + 4 + z);
    memcpy(b.data() + off, &z, 4);
  }
  void zblob6(const void* p, size_t n) {
    size_t bound = ZSTD_compressBound(n);
    size_t off = b.size();
    b.resize(off + 4 + bound);
    size_t z = ZSTD_compress(b.data() + off + 4, bound, p, n, 6);
    b.resize(off + 4 + z);
    memcpy(b.data() + off, &z, 4);
  }
};

static bool enc_cname(const vector<pair<const uint8_t*, size_t>>& rows, Buf& out, uint8_t flags) {
  uint32_t n = (uint32_t)rows.size();
  vector<uint32_t> vals(n);
  for (uint32_t i = 0; i < n; i++) {
    const uint8_t* r = rows[i].first; size_t L = rows[i].second;
    size_t cl = L - ((flags & 1) || i + 1 < n ? 1 : 0);
    if (cl != 18 || memcmp(r, "Customer#", 9)) return false;
    uint32_t v = 0;
    for (int k = 9; k < 18; k++) { uint8_t c = r[k]; if (c < '0' || c > '9') return false; v = v * 10 + (uint32_t)(c - '0'); }
    if (v >= (1u << 18)) return false;
    vals[i] = v;
  }
  size_t nb = ((size_t)n * 18 + 7) / 8;
  vector<uint8_t> bits(nb, 0);
  size_t bitpos = 0;
  for (uint32_t v : vals) {
    for (int i = 0; i < 18; i++) { if (v >> i & 1) bits[bitpos >> 3] |= (uint8_t)(1u << (bitpos & 7)); bitpos++; }
  }
  out.u32(n); out.bytes(bits.data(), nb);
  return true;
}

static bool enc_genome(const vector<pair<const uint8_t*, size_t>>& rows, Buf& out, uint8_t flags) {
  uint32_t n = (uint32_t)rows.size();
  static int code[256]; static bool init = false;
  if (!init) { memset(code, -1, sizeof(code)); code['a'] = 0; code['c'] = 1; code['g'] = 2; code['t'] = 3;
    code['A'] = 0; code['C'] = 1; code['G'] = 2; code['T'] = 3; init = true; }
  for (uint32_t i = 0; i < n; i++) {
    const uint8_t* r = rows[i].first; size_t L = rows[i].second;
    size_t cl = L - (((flags & 1) || i + 1 < n) ? 1 : 0);
    if (cl != 9) return false;
    for (int k = 0; k < 9; k++) { int c = code[r[k]]; if (c < 0) return false; }
  }
  size_t nb = ((size_t)n * 18 + 7) / 8;
  vector<uint8_t> bits(nb, 0);
  size_t bitpos = 0;
  for (uint32_t i = 0; i < n; i++) {
    const uint8_t* r = rows[i].first;
    uint32_t v = 0;
    for (int k = 0; k < 9; k++) v |= (uint32_t)code[r[k]] << (2 * k);
    for (int b = 0; b < 18; b++) { if (v >> b & 1) bits[bitpos >> 3] |= (uint8_t)(1u << (bitpos & 7)); bitpos++; }
  }
  out.u32(n); out.bytes(bits.data(), nb);
  return true;
}

static int hexval(uint8_t c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

static bool enc_hex(const vector<pair<const uint8_t*, size_t>>& rows, Buf& out, uint8_t flags) {
  uint32_t n = (uint32_t)rows.size();
  out.u32(n);
  for (uint32_t i = 0; i < n; i++) {
    const uint8_t* r = rows[i].first; size_t L = rows[i].second;
    size_t cl = L - (((flags & 1) || i + 1 < n) ? 1 : 0);
    if (cl < 1 || cl > 8) return false;
    uint32_t v = 0;
    for (size_t k = 0; k < cl; k++) { int d = hexval(r[k]); if (d < 0) return false; v = (v << 4) | (uint32_t)d; }
    int bl = 0; { uint32_t t = v; while (t) { bl += 4; t >>= 4; } }
    size_t want = v < 0x10000 ? 4 : (size_t)((bl + 3) / 4);
    if (cl != want) return false;
    out.u32(v);
  }
  return true;
}

static bool enc_uuid(const vector<pair<const uint8_t*, size_t>>& rows, Buf& out, uint8_t flags) {
  uint32_t n = (uint32_t)rows.size();
  for (uint32_t i = 0; i < n; i++) {
    const uint8_t* r = rows[i].first; size_t L = rows[i].second;
    size_t cl = L - (((flags & 1) || i + 1 < n) ? 1 : 0);
    if (cl != 36) return false;
    if (memcmp(r + 8, "-2da5-11e8-", 11)) return false;
    if (r[23] != '-') return false;
    for (int k = 0; k < 36; k++) if (k != 8 && k != 13 && k != 18 && k != 23 && hexval(r[k]) < 0) return false;
  }
  out.u32(n);
  for (uint32_t i = 0; i < n; i++) {
    const uint8_t* r = rows[i].first;
    uint32_t tl = 0; for (int k = 0; k < 8; k++) tl = (tl << 4) | (uint32_t)hexval(r[k]);
    uint32_t cs = 0; for (int k = 19; k < 23; k++) cs = (cs << 4) | (uint32_t)hexval(r[k]);
    out.u32(tl); out.u8((uint8_t)cs); out.u8((uint8_t)(cs >> 8));
    for (int k = 24; k < 36; k += 2) out.u8((uint8_t)((hexval(r[k]) << 4) | hexval(r[k + 1])));
  }
  return true;
}

static bool enc_loc(const vector<pair<const uint8_t*, size_t>>& rows, Buf& out, uint8_t flags) {
  uint32_t n = (uint32_t)rows.size();
  int32_t null_row = -1;
  vector<uint8_t> counts(n, 0);
  vector<uint8_t> lon74((n + 7) / 8, 0);
  vector<uint8_t> slots((size_t)n * 13, 0);
  for (uint32_t i = 0; i < n; i++) {
    const uint8_t* r = rows[i].first; size_t L = rows[i].second;
    size_t cl = L - (((flags & 1) || i + 1 < n) ? 1 : 0);
    if (cl == 4 && memcmp(r, "NULL", 4) == 0) {
      if (null_row >= 0) return false;
      null_row = (int32_t)i; continue;
    }
    // (40.Ddd, -73.Ddd)\n
    if (cl < 12 || cl > 40) return false;
    if (r[0] != '(' || memcmp(r + 1, "40.", 3)) return false;
    size_t p = 4, d1 = 0;
    while (p < cl && r[p] >= '0' && r[p] <= '9') { p++; d1++; }
    if (d1 < 1 || d1 > 15 || p + 6 > cl) return false;
    if (r[p] != ',' || r[p + 1] != ' ' || r[p + 2] != '-' || r[p + 3] != '7') return false;
    uint8_t x = r[p + 4];
    if (x != '3' && x != '4') return false;
    p += 5;
    if (r[p++] != '.') return false;
    size_t d2 = 0, d2start = p;
    while (p < cl && r[p] >= '0' && r[p] <= '9') { p++; d2++; }
    if (p + 1 != cl || r[p] != ')' || d2 < 1 || d2 > 15) return false;
    counts[i] = (uint8_t)(d1 | (d2 << 4));
    if (x == '4') lon74[i >> 3] |= (uint8_t)(1u << (i & 7));
    uint8_t* sl = slots.data() + (size_t)i * 13;
    size_t bitpos = 0;
    auto putd = [&](const uint8_t* q, size_t cnt) {
      for (size_t k = 0; k < cnt; k += 3) {
        uint32_t g = 0; int m = 0;
        for (size_t j = k; j < cnt && m < 3; j++, m++) g = g * 10 + (uint32_t)(q[j] - '0');
        for (int t = m; t < 3; t++) g *= 10;
        for (int b = 0; b < 10; b++) { if (g >> b & 1) sl[bitpos >> 3] |= (uint8_t)(1u << (bitpos & 7)); bitpos++; }
      }
    };
    putd(r + 4, d1); putd(r + d2start, d2);
  }
  if (null_row < 0) return false;
  out.u32(n); out.u32((uint32_t)null_row);
  out.bytes(counts.data(), n);
  out.bytes(lon74.data(), lon74.size());
  out.bytes(slots.data(), slots.size());
  return true;
}

static bool enc_rowdict(const vector<pair<const uint8_t*, size_t>>& rows, Buf& out, uint8_t flags) {
  (void)flags;
  uint32_t n = (uint32_t)rows.size();
  vector<uint32_t> ids(n);
  vector<size_t> doff;
  vector<const uint8_t*> dptr; vector<size_t> dlen;
  {
    // open-addressing map on row content
    struct Ent { uint32_t h; int32_t id; };
    size_t tb = 1; while (tb < rows.size() * 2) tb <<= 1;
    vector<Ent> table(tb, {0, -1});
    size_t tot = 0;
    for (uint32_t i = 0; i < n; i++) {
      const uint8_t* r = rows[i].first; size_t L = rows[i].second;
      uint64_t hh = 1469598103934665603ull;
      for (size_t k = 0; k < L; k++) { hh ^= r[k]; hh *= 1099511628211ull; }
      uint32_t h32 = (uint32_t)(hh ^ (hh >> 32));
      if (!h32) h32 = 1;
      size_t m = h32 & (tb - 1);
      while (table[m].id >= 0 && !(table[m].h == h32 && dlen[table[m].id] == L && memcmp(dptr[table[m].id], r, L) == 0)) m = (m + 1) & (tb - 1);
      if (table[m].id < 0) { table[m] = {h32, (int32_t)dptr.size()}; dptr.push_back(r); dlen.push_back(L); tot += L; }
      ids[i] = (uint32_t)table[m].id;
    }
    uint32_t nd = (uint32_t)dptr.size();
    int w = 1; while ((1u << w) < nd) w++;
    vector<uint8_t> bits(((size_t)n * w + 7) / 8, 0);
    size_t bitpos = 0;
    for (uint32_t id : ids) for (int b = 0; b < w; b++) { if (id >> b & 1) bits[bitpos >> 3] |= (uint8_t)(1u << (bitpos & 7)); bitpos++; }
    out.u32(n); out.u32(nd); out.u8((uint8_t)w);
    vector<uint8_t> dblob; dblob.reserve(tot);
    for (size_t k = 0; k < nd; k++) dblob.insert(dblob.end(), dptr[k], dptr[k] + dlen[k]);
    out.u32(nd + 1);
    size_t off = 0;
    for (size_t k = 0; k < nd; k++) { out.u32((uint32_t)off); off += dlen[k]; }
    out.u32((uint32_t)off);
    out.u32(0);
    out.zblob6(dblob.data(), dblob.size());
    out.bytes(bits.data(), bits.size());
  }
  return true;
}

// ---------------- FLZ: fragment dictionary LZ ----------------
static const int FLZ_MINMATCH = 4;

struct MatchIdx {
  static const int HBITS = 21;
  vector<int32_t> head;
  vector<int32_t> nxt;
  const vector<string>* fs = nullptr;
  static inline uint64_t h5(const uint8_t* p) {
    uint64_t v = 0; memcpy(&v, p, 5);
    v *= 0x9E3779B97F4A7C15ull;
    return (v >> 41);
  }
  void build(const vector<string>& frags) {
    fs = &frags;
    head.assign(1 << HBITS, -1);
    nxt.assign(frags.size(), -1);
    vector<uint32_t> order(frags.size());
    for (uint32_t i = 0; i < frags.size(); i++) order[i] = i;
    sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
      if (frags[a].size() != frags[b].size()) return frags[a].size() > frags[b].size();
      return frags[a] < frags[b];
    });
    vector<uint8_t> cnt(1 << HBITS, 0);
    for (uint32_t id : order) {
      const string& f = frags[id];
      if (f.size() < 5) continue;
      uint64_t h = h5((const uint8_t*)f.data()) & ((1u << HBITS) - 1);
      if (cnt[h] >= 8) continue;
      nxt[id] = head[h]; head[h] = (int32_t)id; cnt[h]++;
    }
  }
  inline int find(const uint8_t* p, size_t avail, uint32_t& out_id) const {
    out_id = 0;
    if (avail < 5) return 0;
    uint64_t h = h5(p) & ((1u << HBITS) - 1);
    int best = 0;
    for (int32_t cur = head[h]; cur >= 0; cur = nxt[cur]) {
      const string& f = (*fs)[cur];
      if ((int)f.size() <= best) break; // chains sorted len desc
      if (f.size() <= avail && memcmp(p, f.data(), f.size()) == 0) { best = (int)f.size(); out_id = (uint32_t)cur; }
    }
    return best;
  }
};

struct GramTab {
  static const int TBITS = 22;
  vector<uint64_t> key;
  vector<uint32_t> cnt;
  vector<uint32_t> pos;
  vector<uint8_t> used;
  GramTab() : key(1u << TBITS), cnt(1u << TBITS), pos(1u << TBITS), used(1u << TBITS, 0) {}
  void clear() { fill(used.begin(), used.end(), (uint8_t)0); }
  inline void add(uint64_t h, uint32_t p) {
    uint32_t m = (uint32_t)h & ((1u << TBITS) - 1);
    for (int probes = 0; probes < 4; probes++) {
      if (!used[m]) { used[m] = 1; key[m] = h; cnt[m] = 1; pos[m] = p; return; }
      if (key[m] == h) { if (cnt[m] < 0xFFFFFFFu) cnt[m]++; return; }
      m = (m + 1) & ((1u << TBITS) - 1);
    }
    // contested: probabilistic replace to keep counts approximate but bounded
    cnt[m]++;
    if ((cnt[m] & 2047) == 0) { key[m] = h; pos[m] = p; used[m] = 1; }
  }
};

struct Cand { uint64_t score; uint32_t pos; uint32_t len; };

static uint64_t g_score_mode = 0; // 0: cnt*L, 1: (cnt-1)*(L-2)
static size_t g_quota = 0;        // 0: global cap, else per-length quota
static size_t g_workcap = 24000;
static void mine_spans(const uint8_t* raw, const vector<pair<uint32_t, uint32_t>>& spans,
                       vector<Cand>& cands, const int* LENS, int nlen, size_t quota) {
  GramTab tab;
  size_t nsp = spans.size();
  size_t stride = 1 + nsp / 20000;
  uint64_t powB[80];
  powB[0] = 1; for (int i = 1; i < 80; i++) powB[i] = powB[i - 1] * 1000003ull;
  vector<Cand> tmp;
  for (int li = 0; li < nlen; li++) {
    int L = LENS[li];
    uint64_t Bm = powB[L];
    tab.clear();
    for (size_t ri = 0; ri < nsp; ri += stride) {
      const uint8_t* p = raw + spans[ri].first; size_t n = spans[ri].second;
      if (n < (size_t)L) continue;
      uint64_t h = 0;
      for (int k = 0; k < L; k++) h = h * 1000003ull + p[k];
      tab.add(h, (uint32_t)(p - raw));
      for (size_t i = L; i < n; i++) {
        h = h * 1000003ull + p[i] - p[i - L] * Bm;
        tab.add(h, (uint32_t)(p + i - L + 1 - raw));
      }
    }
    tmp.clear();
    for (size_t m = 0; m < (1u << GramTab::TBITS); m++) {
      if (tab.used[m] && tab.cnt[m] >= 2) {
        uint64_t sc = g_score_mode ? (uint64_t)(tab.cnt[m] - 1) * (uint64_t)(L - 2)
                                   : (uint64_t)tab.cnt[m] * (uint64_t)L;
        if (g_quota) tmp.push_back({sc, tab.pos[m], (uint32_t)L});
        else if (cands.size() < 400000) cands.push_back({sc, tab.pos[m], (uint32_t)L});
      }
    }
    if (g_quota && tmp.size() > quota) {
      partial_sort(tmp.begin(), tmp.begin() + quota, tmp.end(), [](const Cand& a, const Cand& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.len != b.len) return a.len > b.len;
        return a.pos < b.pos;
      });
      tmp.resize(quota);
    }
    if (g_quota) for (auto& c : tmp) cands.push_back(c);
  }
}

// encode pass; if usage != nullptr, gather stats too (code stream is still produced)
static void flz_encode_rows(const vector<pair<const uint8_t*, size_t>>& rows, const vector<string>& frags,
                            int n_hot, vector<uint8_t>& code, vector<uint32_t>& lens,
                            vector<uint32_t>* usage, vector<uint64_t>* bytes_cov,
                            const uint8_t* raw, vector<pair<uint32_t, uint32_t>>* lits) {
  MatchIdx idx;
  idx.build(frags);
  vector<string> hot;
  MatchIdx idx_hot;
  if (n_hot > 0) { hot.assign(frags.begin(), frags.begin() + n_hot); idx_hot.build(hot); }
  code.clear(); code.reserve(1 << 20);
  lens.resize(rows.size());
  if (usage) { usage->assign(frags.size(), 0); bytes_cov->assign(frags.size(), 0); }
  for (size_t ri = 0; ri < rows.size(); ri++) {
    const uint8_t* p = rows[ri].first;
    const uint8_t* end = p + rows[ri].second;
    size_t start = code.size();
    const uint8_t* lit = p;
    while (p < end) {
      size_t avail = end - p;
      uint32_t id = 0;
      int L = idx.find(p, avail, id);
      bool use_hot = false;
      if (L >= FLZ_MINMATCH && id >= (uint32_t)n_hot && n_hot > 0) {
        uint32_t hid = 0;
        int HL = idx_hot.find(p, avail, hid);
        if (HL >= FLZ_MINMATCH && HL * 2 >= L) { use_hot = true; id = hid; L = HL; }
      }
      if (L >= FLZ_MINMATCH) {
        const uint8_t* q = lit;
        while (q < p) {
          size_t n = (size_t)(p - q); if (n > 255) n = 255;
          code.push_back(255); code.push_back((uint8_t)(n - 1));
          code.insert(code.end(), q, q + n); q += n;
          if (lits) lits->emplace_back((uint32_t)(q - n - raw), (uint32_t)n);
        }
        if (id < (uint32_t)n_hot) { code.push_back((uint8_t)id); }
        else {
          uint32_t cid = id - (uint32_t)n_hot;
          if (cid < (uint32_t)FLZ_COLD2) {
            code.push_back((uint8_t)(192 + (cid >> 8)));
            code.push_back((uint8_t)(cid & 0xFF));
          } else {
            uint32_t e = cid - (uint32_t)FLZ_COLD2;
            code.push_back(254);
            code.push_back((uint8_t)(e >> 8));
            code.push_back((uint8_t)(e & 0xFF));
          }
        }
        if (usage) { (*usage)[id]++; (*bytes_cov)[id] += L; }
        p += L; lit = p;
      } else p++;
    }
    const uint8_t* q = lit;
    while (q < end) {
      size_t n = (size_t)(end - q); if (n > 255) n = 255;
      code.push_back(255); code.push_back((uint8_t)(n - 1));
      code.insert(code.end(), q, q + n); q += n;
      if (lits) lits->emplace_back((uint32_t)(q - n - raw), (uint32_t)n);
    }
    lens[ri] = (uint32_t)(code.size() - start);
  }
}

static int g_selmetric = 0; // 0: usage*len, 1: usage*(len-2)
static int g_coldcap = FLZ_COLD_MAX;
static void select_frags(const vector<string>& work, const vector<uint32_t>& usage,
                         const vector<uint64_t>& bcov, size_t limit,
                         vector<string>& frags, int& n_hot) {
  vector<uint32_t> order(limit);
  for (uint32_t i = 0; i < limit; i++) order[i] = i;
  sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
    uint64_t va = g_selmetric ? (uint64_t)usage[a] * (work[a].size() - 2) : bcov[a];
    uint64_t vb = g_selmetric ? (uint64_t)usage[b] * (work[b].size() - 2) : bcov[b];
    if (va != vb) return va > vb;
    if (usage[a] != usage[b]) return usage[a] > usage[b];
    return a < b;
  });
  n_hot = 0;
  int n_cold = 0;
  frags.clear();
  for (uint32_t id : order) {
    if (usage[id] == 0) continue;
    if ((int)frags.size() < FLZ_HOT_MAX) { frags.push_back(work[id]); n_hot++; }
    else if (n_cold < g_coldcap && n_cold < FLZ_COLD_TOTAL) { frags.push_back(work[id]); n_cold++; }
    else break;
  }
}

static bool flz_block(const uint8_t* raw, const vector<pair<const uint8_t*, size_t>>& rows, Buf& out) {
  // 1) mine candidates over full rows
  vector<pair<uint32_t, uint32_t>> row_spans;
  row_spans.reserve(rows.size());
  for (auto& r : rows) row_spans.emplace_back((uint32_t)(r.first - raw), (uint32_t)r.second);
  static const int LENS_MAIN[] = {8, 12, 16, 24, 32, 48, 64};
  static const int LENS_ORIG[] = {8, 12, 16, 24, 32, 48};
  const char* sm = getenv("MINE_SCORE"); g_score_mode = sm ? (uint64_t)atoll(sm) : 0;
  { const char* sv = getenv("SELMETRIC"); if (sv) g_selmetric = (int)atoi(sv); }
  { const char* cv = getenv("COLD_CAP"); if (cv) g_coldcap = (int)atoi(cv); }
  { const char* w = getenv("WORK_CAP"); if (w) g_workcap = (size_t)atoll(w); }
  const char* qm = getenv("MINE_QUOTA"); g_quota = qm ? (size_t)atoll(qm) : 0;
  int use64 = getenv("MINE_L64") ? 1 : 0;
  vector<Cand> cands;
  if (g_quota) mine_spans(raw, row_spans, cands, LENS_MAIN, use64 ? 7 : 6, g_quota);
  else mine_spans(raw, row_spans, cands, LENS_ORIG, 6, 0);
  if (cands.empty()) return false;
  sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
    if (a.score != b.score) return a.score > b.score;
    if (a.len != b.len) return a.len > b.len;
    return a.pos < b.pos;
  });
  vector<string> work;
  unordered_set<uint64_t> seen;
  for (const Cand& c : cands) {
    if (work.size() >= g_workcap) break;
    const uint8_t* s = raw + c.pos;
    uint64_t hh = 1469598103934665603ull;
    for (uint32_t k = 0; k < c.len; k++) { hh ^= s[k]; hh *= 1099511628211ull; }
    if (!seen.insert(hh).second) continue;
    work.emplace_back((const char*)s, c.len);
  }
  // 2) usage pass over work dict (all cold hypothetically)
  vector<uint8_t> tmpcode;
  vector<uint32_t> tmplens;
  vector<uint32_t> usage;
  vector<uint64_t> bcov;
  vector<pair<uint32_t, uint32_t>> lits;
  size_t base_end = work.size();
  flz_encode_rows(rows, work, 0, tmpcode, tmplens, &usage, &bcov, raw, &lits);
  vector<uint32_t> usage1 = usage;
  vector<uint64_t> bcov1 = bcov;
  // 2b) residual mining on unmatched literal spans, extend dict, re-run usage pass
  static int g_res = -1;
  if (g_res < 0) { const char* r = getenv("MINE_RES"); g_res = r ? (int)atoi(r) : 1; }
  size_t added = 0;
  if (g_res && lits.size() > 64) {
    static const int LENS_RES[] = {6, 8, 12, 16, 24};
    vector<Cand> cands2;
    mine_spans(raw, lits, cands2, LENS_RES, 5, 30000);
    sort(cands2.begin(), cands2.end(), [](const Cand& a, const Cand& b) {
      if (a.score != b.score) return a.score > b.score;
      if (a.len != b.len) return a.len > b.len;
      return a.pos < b.pos;
    });
    size_t added = 0;
    for (const Cand& c : cands2) {
      if (work.size() >= g_workcap + 8000) break;
      const uint8_t* s = raw + c.pos;
      uint64_t hh = 1469598103934665603ull;
      for (uint32_t k = 0; k < c.len; k++) { hh ^= s[k]; hh *= 1099511628211ull; }
      if (!seen.insert(hh).second) continue;
      work.emplace_back((const char*)s, c.len);
      added++;
    }
    if (added) flz_encode_rows(rows, work, 0, tmpcode, tmplens, &usage, &bcov, raw, nullptr);
  }
  // 3) two variants compete: original pool/cap vs big pool/cap (with residual)
  vector<string> fragsA; int nhA = 0;
  select_frags(work, usage1, bcov1, base_end, fragsA, nhA);
  vector<string> fragsB; int nhB = 0;
  const vector<string>* bestf = &fragsA;
  int bestnh = nhA;
  long long bestsz = -1;
  auto trye = [&](const vector<string>& fr, int nh, long long& sz_out) {
    if (fr.empty()) { sz_out = -1; return; }
    vector<uint8_t> tc; vector<uint32_t> tl2;
    flz_encode_rows(rows, fr, nh, tc, tl2, nullptr, nullptr, raw, nullptr);
    vector<uint8_t> l1b; vector<uint32_t> escb;
    l1b.reserve(tl2.size());
    for (uint32_t L : tl2) { if (L < 255) l1b.push_back((uint8_t)L); else { l1b.push_back(255); escb.push_back(L); } }
    Buf db;
    db.u32((uint32_t)nh);
    { vector<uint8_t> hb; db.u32((uint32_t)nh + 1); size_t off = 0;
      for (int i = 0; i < nh; i++) { db.u32((uint32_t)off); off += fr[i].size(); hb.insert(hb.end(), fr[i].begin(), fr[i].end()); }
      db.u32((uint32_t)off); db.bytes(hb.data(), hb.size()); }
    db.u32((uint32_t)(fr.size() - nh));
    { vector<uint8_t> cb; db.u32((uint32_t)(fr.size() - nh) + 1); size_t off = 0;
      for (int i = nh; i < (int)fr.size(); i++) { db.u32((uint32_t)off); off += fr[i].size(); cb.insert(cb.end(), fr[i].begin(), fr[i].end()); }
      db.u32((uint32_t)off); db.bytes(cb.data(), cb.size()); }
    size_t est = 0;
    { size_t bound = ZSTD_compressBound(db.b.size()); vector<uint8_t> z(bound); est += ZSTD_compress(z.data(), bound, db.b.data(), db.b.size(), 19); }
    { size_t bound = ZSTD_compressBound(l1b.size()); vector<uint8_t> z(bound); est += ZSTD_compress(z.data(), bound, l1b.data(), l1b.size(), 19); }
    { size_t bound = ZSTD_compressBound(tc.size()); vector<uint8_t> z(bound); est += ZSTD_compress(z.data(), bound, tc.data(), tc.size(), 19); }
    est += 24 + 4 * escb.size();
    sz_out = (long long)est;
    if (bestsz < 0 || sz_out < bestsz) { bestsz = sz_out; bestf = &fr; bestnh = nh; }
  };
  trye(fragsA, nhA, bestsz);
  if (added) { long long szC = -1; g_coldcap = 40000; select_frags(work, usage, bcov, work.size(), fragsB, nhB); g_coldcap = FLZ_COLD_MAX; if (!fragsB.empty()) trye(fragsB, nhB, szC); }
  const vector<string>& frags = *bestf;
  int n_hot = bestnh;
  if (frags.empty()) return false;
  // 4) final encode with the winning selection
  vector<uint8_t> code;
  vector<uint32_t> lens;
  flz_encode_rows(rows, frags, n_hot, code, lens, nullptr, nullptr, raw, nullptr);
  // 5) serialize
  // lengths: u8 with 255 escape -> u32 side list
  vector<uint8_t> l1;
  vector<uint32_t> esc;
  l1.reserve(lens.size());
  for (uint32_t L : lens) { if (L < 255) l1.push_back((uint8_t)L); else { l1.push_back(255); esc.push_back(L); } }
  // dict blob
  Buf d;
  d.u32((uint32_t)n_hot);
  {
    vector<uint8_t> hb;
    d.u32(n_hot + 1);
    size_t off = 0;
    for (int i = 0; i < n_hot; i++) { d.u32((uint32_t)off); off += frags[i].size(); hb.insert(hb.end(), frags[i].begin(), frags[i].end()); }
    d.u32((uint32_t)off);
    d.bytes(hb.data(), hb.size());
  }
  d.u32((uint32_t)(frags.size() - n_hot));
  {
    vector<uint8_t> cb;
    d.u32((uint32_t)(frags.size() - n_hot) + 1);
    size_t off = 0;
    for (int i = n_hot; i < (int)frags.size(); i++) { d.u32((uint32_t)off); off += frags[i].size(); cb.insert(cb.end(), frags[i].begin(), frags[i].end()); }
    d.u32((uint32_t)off);
    d.bytes(cb.data(), cb.size());
  }
  out.zblob(d.b.data(), d.b.size());
  out.zblob(l1.data(), l1.size());
  out.u32((uint32_t)esc.size());
  for (uint32_t ee : esc) out.u32(ee);
  out.zblob(code.data(), code.size());
  return true;
}

static bool enc_flz(const uint8_t* raw, const vector<pair<const uint8_t*, size_t>>& rows, Buf& out, uint8_t flags) {
  (void)flags;
  out.u32((uint32_t)rows.size());
  return flz_block(raw, rows, out);
}

// ---------------- FSST per-row symbol coding ----------------
static bool enc_fsst(const vector<pair<const uint8_t*, size_t>>& rows, Buf& out, uint8_t flags) {
  (void)flags;
  uint32_t n = (uint32_t)rows.size();
  vector<unsigned long> len(n);
  vector<const unsigned char*> ptr(n);
  size_t tot = 0;
  for (uint32_t i = 0; i < n; i++) { len[i] = rows[i].second; ptr[i] = rows[i].first; tot += len[i]; }
  vector<unsigned char> codes(2 * tot + 64 * (size_t)n + 64);
  vector<unsigned long> olen(n);
  vector<unsigned char*> optr(n);
  fsst_encoder_t* enc = fsst_create(n, len.data(), ptr.data(), 0);
  if (!enc) return false;
  fsst_compress(enc, n, len.data(), ptr.data(), tot, codes.data(), olen.data(), optr.data());
  size_t totout = 0;
  for (uint32_t i = 0; i < n; i++) totout += olen[i];
  unsigned char ser[FSST_MAXHEADER];
  unsigned int serlen = fsst_export(enc, ser);
  // lens u8 + escape
  vector<uint8_t> l1;
  vector<uint32_t> esc;
  l1.reserve(n);
  for (uint32_t i = 0; i < n; i++) { unsigned long L = olen[i]; if (L < 255) l1.push_back((uint8_t)L); else { l1.push_back(255); esc.push_back((uint32_t)L); } }
  out.u32(n);
  out.u32((uint32_t)serlen);
  out.bytes(ser, serlen);
  out.u32((uint32_t)tot); // total output size for truncation detection
  // blob codec choice: zstd (0) or lz4 (1) per archive, applied to codes+lens
  size_t zb = ZSTD_compressBound(totout);
  vector<uint8_t> zbuf(zb);
  size_t zlen = ZSTD_compress(zbuf.data(), zb, codes.data(), totout, 12);
  if (zlen > totout / 2) { // poorly compressible stream: raw literals decode much faster
    ZSTD_CCtx* cc = ZSTD_createCCtx();
    ZSTD_CCtx_setParameter(cc, ZSTD_c_compressionLevel, 12);
    ZSTD_CCtx_setParameter(cc, ZSTD_c_literalCompressionMode, ZSTD_lcm_uncompressed);
    size_t zr = ZSTD_compress2(cc, zbuf.data(), zb, codes.data(), totout);
    ZSTD_freeCCtx(cc);
    if (zr <= zlen + zlen / 4 && zr) zlen = zr;
  }
  size_t hb = huff_bound(totout);
  vector<uint8_t> hbuf(hb);
  size_t hlen = huff_compress(codes.data(), totout, hbuf.data(), hb);
  uint8_t codec = 0; (void)hlen;
  if (getenv("DBG_HUFF")) fprintf(stderr, "HUFF zlen=%zu hlen=%zu delta=%+.2f pct\n", zlen, hlen, zlen ? 100.0 * ((double)hlen - (double)zlen) / (double)zlen : 0.0);
  size_t clen = codec ? hlen : zlen;
  const uint8_t* csrc = codec ? hbuf.data() : zbuf.data();
  size_t zb2 = ZSTD_compressBound(l1.size());
  vector<uint8_t> zbuf2(zb2);
  size_t zlen2 = ZSTD_compress(zbuf2.data(), zb2, l1.data(), l1.size(), 19);
  size_t clen2 = zlen2;
  const uint8_t* lsrc = zbuf2.data();
  out.u8(codec);
  out.u32((uint32_t)totout);
  out.u32((uint32_t)clen);
  out.bytes(csrc, clen);
  out.u32((uint32_t)clen2);
  out.bytes(lsrc, clen2);
  out.u32((uint32_t)esc.size());
  for (uint32_t ee : esc) out.u32(ee);
  return true;
}

// ---------------- email: local@domain split ----------------
static bool enc_email(const uint8_t* raw, const vector<pair<const uint8_t*, size_t>>& rows, Buf& out, uint8_t flags) {
  (void)flags;
  uint32_t n = (uint32_t)rows.size();
  vector<pair<const uint8_t*, size_t>> locals(n);
  vector<uint32_t> dids(n);
  vector<const uint8_t*> dptr; vector<size_t> dlen;
  {
    struct Ent { uint32_t h; int32_t id; };
    size_t tb = 1; while (tb < n * 2) tb <<= 1;
    vector<Ent> table(tb, {0, -1});
    for (uint32_t i = 0; i < n; i++) {
      const uint8_t* r = rows[i].first; size_t L = rows[i].second;
      size_t cl = L - (((flags & 1) || i + 1 < n) ? 1 : 0);
      long at = -1;
      for (size_t k = cl; k > 0; k--) if (r[k - 1] == '@') { at = (long)(k - 1); break; }
      if (at < 0 || at == 0 || (size_t)at == cl - 1) return false;
      locals[i] = {r, (size_t)at};
      const uint8_t* dp = r + at + 1; size_t dl = cl - (size_t)at - 1;
      uint64_t hh = 1469598103934665603ull;
      for (size_t k = 0; k < dl; k++) { hh ^= dp[k]; hh *= 1099511628211ull; }
      uint32_t h32 = (uint32_t)(hh ^ (hh >> 32)); if (!h32) h32 = 1;
      size_t m = h32 & (tb - 1);
      while (table[m].id >= 0 && !(table[m].h == h32 && dlen[table[m].id] == dl && memcmp(dptr[table[m].id], dp, dl) == 0)) m = (m + 1) & (tb - 1);
      if (table[m].id < 0) { table[m] = {h32, (int32_t)dptr.size()}; dptr.push_back(dp); dlen.push_back(dl); }
      dids[i] = (uint32_t)table[m].id;
    }
  }
  uint32_t nd = (uint32_t)dptr.size();
  int w = 1; while ((1u << w) < nd) w++;
  out.u32(n); out.u32(nd); out.u8((uint8_t)w);
  vector<uint8_t> dblob;
  size_t off = 0;
  out.u32(nd + 1);
  for (size_t k = 0; k < nd; k++) { out.u32((uint32_t)off); off += dlen[k]; dblob.insert(dblob.end(), dptr[k], dptr[k] + dlen[k]); }
  out.u32((uint32_t)off);
  out.zblob6(dblob.data(), dblob.size());
  {
    vector<uint8_t> bits(((size_t)n * w + 7) / 8, 0);
    size_t bitpos = 0;
    for (uint32_t id : dids) for (int b = 0; b < w; b++) { if (id >> b & 1) bits[bitpos >> 3] |= (uint8_t)(1u << (bitpos & 7)); bitpos++; }
    out.bytes(bits.data(), bits.size());
  }
  return flz_block(raw, locals, out);
}

// ---------------- dispatch ----------------
static size_t try_scheme(bool (*fn)(const vector<pair<const uint8_t*, size_t>>&, Buf&, uint8_t),
                         const vector<pair<const uint8_t*, size_t>>& rows, uint8_t flags, vector<uint8_t>& out) {
  Buf b;
  if (!fn(rows, b, flags)) return (size_t)-1;
  out.swap(b.b);
  return out.size();
}

int64_t lab_encode(const uint8_t* raw, size_t size, uint8_t* archive, size_t capacity) {
  auto rows = split_rows(raw, size);
  uint8_t flags = (size && raw[size - 1] == '\n') ? 1 : 0;
  vector<pair<const uint8_t*, size_t>> noflag_rows;
  if (!flags && size) {
    // all rows have LF except possibly last; flags==0 means last row lacks LF
  }
  uint8_t scheme = 0;
  vector<uint8_t> best;
  size_t best_size = (size_t)-1;

  struct Cand2 { uint8_t scheme; size_t size; vector<uint8_t> data; };
  vector<Cand2> cands;
  auto add = [&](uint8_t sc, bool (*fn)(const vector<pair<const uint8_t*, size_t>>&, Buf&, uint8_t)) {
    vector<uint8_t> data;
    size_t sz = try_scheme(fn, rows, flags, data);
    if (sz != (size_t)-1) cands.push_back({sc, sz, move(data)});
  };
  add(SC_CNAME, enc_cname);
  add(SC_GENOME, enc_genome);
  add(SC_HEX, enc_hex);
  add(SC_UUID, enc_uuid);
  add(SC_LOC, enc_loc);
  add(SC_ROWDICT, enc_rowdict);
  {
    // FLZ needs raw pointer for mining positions
    Buf b;
    if (enc_flz(raw, rows, b, flags)) cands.push_back({SC_FLZ, b.b.size(), move(b.b)});
  }
  {
    Buf b;
    if (enc_email(raw, rows, b, flags)) cands.push_back({SC_EMAIL, b.b.size(), move(b.b)});
  }
  {
    Buf b;
    if (enc_fsst(rows, b, flags)) cands.push_back({SC_FSST, b.b.size(), move(b.b)});
  }
  const char* pb = getenv("SIZE_PREF");
  double pref = pb ? atof(pb) : 1.05; // structured schemes may cost up to this factor for speed
  double avglen = (double)size / (double)rows.size();
  for (auto& c : cands) {
    if (getenv("DBG_ENC")) fprintf(stderr, "CAND scheme=%d size=%zu\n", (int)c.scheme, c.size);
    double adj = c.size;
    if (c.scheme <= SC_LOC) adj = c.size / pref; // prefer structured codecs at small size cost
    else if (c.scheme == SC_ROWDICT && avglen >= 70.0) adj = c.size / 1.25; // memcpy-class rows for long-row columns
    else if (c.scheme == SC_FSST) adj = c.size / 1.03; // FSST decodes ~2x faster than FLZ
    if (adj < best_size) { best_size = adj; best = move(c.data); scheme = c.scheme; }
  }
  if (!best_size || best_size == (size_t)-1) return -1;
  // emit container
  vector<uint8_t> hdr;
  hdr.reserve(16);
  uint8_t* hp;
  hdr.resize(12);
  hp = hdr.data();
  uint32_t magic = ARC_MAGIC;
  memcpy(hp, &magic, 4); hp += 4;
  *hp++ = scheme; *hp++ = flags; *hp++ = 0; *hp++ = 0;
  uint32_t plen = (uint32_t)best.size();
  memcpy(hp, &plen, 4);
  size_t total = 12 + best.size();
  if (total > capacity) return -1;
  memcpy(archive, hdr.data(), 12);
  memcpy(archive + 12, best.data(), best.size());
  return (int64_t)total;
}
