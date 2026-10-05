// Columnar encoder for the Yelp business JSONL corpus (dataset-specialized).
// Pass 1 builds vocabularies, pass 2 emits streams.
#include "codec_common.hpp"
#include "xxh64.hpp"
#include <vector>
#include <map>
#include <string>
#include <cstdlib>
#include <zstd.h>
#include <cstdio>

using namespace std;

struct SV { const uint8_t* p; uint32_t n; };

struct Out {
  vector<uint8_t> b;
  void u8(uint8_t v) { b.push_back(v); }
  void u16(uint16_t v) { b.push_back(uint8_t(v & 255)); b.push_back(uint8_t(v >> 8)); }
  void u32(uint32_t v) { for (int i = 0; i < 4; i++) b.push_back(uint8_t((v >> (8 * i)) & 255)); }
  void u64(uint64_t v) { for (int i = 0; i < 8; i++) b.push_back(uint8_t((v >> (8 * i)) & 255)); }
  void raw(const void* p, size_t n) { const uint8_t* q = (const uint8_t*)p; b.insert(b.end(), q, q + n); }
  void sv(const SV& s) { raw(s.p, s.n); }
};

static void put_varint(Out& o, uint32_t v) {
  while (v >= 128) { o.u8(uint8_t(v | 128)); v >>= 7; }
  o.u8(uint8_t(v));
}

static inline size_t scan_str(const uint8_t* p, size_t i, size_t end) {
  while (i < end) {
    uint8_t c = p[i];
    if (c == '\\') { i += 2; continue; }
    if (c == '"') return i + 1;
    i++;
  }
  return (size_t)-1;
}
static inline size_t scan_obj(const uint8_t* p, size_t i, size_t end) {
  int depth = 0;
  while (i < end) {
    uint8_t c = p[i];
    if (c == '"') { i = scan_str(p, i + 1, end); if (i == (size_t)-1) return (size_t)-1; continue; }
    if (c == '{') depth++;
    else if (c == '}') { if (--depth == 0) return i + 1; }
    i++;
  }
  return (size_t)-1;
}
static inline bool lit(const uint8_t* p, const char* s, size_t n) { return memcmp(p, s, n) == 0; }

struct Ctx {
  Out ids;
  Out name_col, addr_col, lat_col, lng_col;
  Out attr_cnt, attr_key;
  vector<Out> attr_val;
  vector<uint8_t> attr_width;
  Out cat_cnt;
  Out h_mask;
  Out rc_col, stars_col, open_col;
  vector<uint16_t> loc_idx_v, cat_ids_v, h_slots_v;
  map<string, int> loc_vocab, cat_vocab, slot_vocab;
  vector<string> loc_keys;
  vector<string> akeys;
  map<string, int> akey_id;
  vector<map<string, int>> aval_id;
  // per-row temps
  vector<pair<uint16_t,uint16_t>> row_pairs;
  vector<uint16_t> row_cats;
  vector<uint16_t> row_slots;
};

static const char SEP_NAME[] = ",\"name\":\"";  // 9
static const char SEP_ADDR[] = ",\"address\":\"";  // 12
static const char SEP_CITY[] = ",\"city\":\"";  // 9
static const char SEP_STATE[] = ",\"state\":\"";  // 10
static const char SEP_ZIP[] = ",\"postal_code\":\"";  // 16
static const char SEP_LAT[] = ",\"latitude\":";  // 12
static const char SEP_LNG[] = ",\"longitude\":";  // 13
static const char SEP_STARS[] = ",\"stars\":";  // 9
static const char SEP_RC[] = ",\"review_count\":";  // 16
static const char SEP_OPEN[] = ",\"is_open\":";  // 11
static const char SEP_ATTR[] = ",\"attributes\":";  // 14
static const char SEP_CAT[] = ",\"categories\":";  // 14
static const char SEP_HRS[] = ",\"hours\":";  // 9

static int put_id16(Ctx& C, const uint8_t* id) {
  static int8_t tab[256];
  static bool init = [] {
    const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    memset(tab, -1, sizeof(tab));
    for (int k = 0; k < 64; k++) tab[(uint8_t)A[k]] = (int8_t)k;
    return true;
  }();
  (void)init;
  uint8_t buf[16]; int bc = 0; uint32_t acc = 0; int abits = 0;
  for (int k = 0; k < 22; k++) {
    int v = tab[id[k]]; if (v < 0) return -1;
    acc = (acc << 6) | (uint32_t)v; abits += 6;
    if (abits >= 8) { abits -= 8; buf[bc++] = uint8_t(acc >> abits); }
  }
  if (bc != 16) return -1;
  C.ids.raw(buf, 16);
  return 0;
}

static int encode_line(const uint8_t* p, size_t n, Ctx& C, bool emit) {
  size_t i = 0;
  if (n < 39 || !lit(p, T_PREFIX, 16)) return -101;
  if (emit && put_id16(C, p + 16)) return -1;
  i = 39;
  auto str_after = [&](const char* sep, size_t sn, Out* col, const char* tail, size_t tn) -> int {
    if (i + sn > n || !lit(p + i, sep, sn)) return -1;
    i += sn;
    size_t e = scan_str(p, i, n); if (e == (size_t)-1) return -1;
    uint32_t len = uint32_t(e - 1 - i);
    if (len > 255) return -1;
    if (emit && col) { col->u8(uint8_t(len)); col->raw(p + i, len); if (tail) col->raw(tail, tn); }
    i = e;
    return 0;
  };
  if (str_after(SEP_NAME, 9, emit ? &C.name_col : nullptr, nullptr, 0)) return -102;
  if (str_after(SEP_ADDR, 12, emit ? &C.addr_col : nullptr, nullptr, 0)) return -103;
  if (i + 9 > n || !lit(p + i, SEP_CITY, 9)) return -104; i += 9;
  size_t ecity = scan_str(p, i, n); if (ecity == (size_t)-1) return -1;
  SV city{p + i, uint32_t(ecity - 1 - i)}; i = ecity;
  if (i + 10 > n || !lit(p + i, SEP_STATE, 10)) return -105; i += 10;
  size_t estate = scan_str(p, i, n); if (estate == (size_t)-1) return -1;
  SV state{p + i, uint32_t(estate - 1 - i)}; i = estate;
  if (i + 16 > n || !lit(p + i, SEP_ZIP, 16)) return -106; i += 16;
  size_t ezip = scan_str(p, i, n); if (ezip == (size_t)-1) return -1;
  SV zip{p + i, uint32_t(ezip - 1 - i)}; i = ezip;
  {
    string k; k.reserve(city.n + state.n + zip.n + 2);
    k.append((const char*)city.p, city.n); k.push_back('\1');
    k.append((const char*)state.p, state.n); k.push_back('\1');
    k.append((const char*)zip.p, zip.n);
    auto it = C.loc_vocab.emplace(move(k), (int)C.loc_vocab.size());
    if (it.second) C.loc_keys.push_back(it.first->first);
    if (emit) C.loc_idx_v.push_back((uint16_t)it.first->second);
  }
  auto num_after = [&](const char* sep, size_t sn, Out* col, const char* tail, size_t tn) -> int {
    if (i + sn > n || !lit(p + i, sep, sn)) return -1;
    i += sn;
    size_t j = i;
    while (j < n && ((p[j] >= '0' && p[j] <= '9') || p[j] == '.' || p[j] == '-')) j++;
    if (j == i || j - i > 255) return -1;
    if (emit && col) { col->u8(uint8_t(j - i)); col->raw(p + i, j - i); if (tail) col->raw(tail, tn); }
    i = j;
    return 0;
  };
  if (num_after(SEP_LAT, 12, emit ? &C.lat_col : nullptr, nullptr, 0)) return -107;
  if (num_after(SEP_LNG, 13, emit ? &C.lng_col : nullptr, nullptr, 0)) return -108;
  if (i + 9 > n || !lit(p + i, SEP_STARS, 9)) return -109; i += 9;
  {
    if (p[i] < '1' || p[i] > '5' || p[i+1] != '.' || (p[i+2] != '0' && p[i+2] != '5')) return -1;
    if (emit) C.stars_col.u8(uint8_t((p[i] - '1') * 2 + (p[i+2] == '5')));
    i += 3;
  }
  if (i + 16 > n || !lit(p + i, SEP_RC, 16)) return -110; i += 16;
  {
    size_t j = i; while (j < n && p[j] >= '0' && p[j] <= '9') j++;
    if (j == i || j - i > 6) return -1;
    if (emit) { uint32_t v = 0; for (size_t k2 = i; k2 < j; k2++) v = v * 10 + (p[k2] - '0'); put_varint(C.rc_col, v); }
    i = j;
  }
  if (i + 11 > n || !lit(p + i, SEP_OPEN, 11)) return -111; i += 11;
  if (p[i] != '0' && p[i] != '1') return -1;
  if (emit) C.open_col.u8(uint8_t(p[i] - '0'));
  i += 1;
  if (i + 14 > n || !lit(p + i, SEP_ATTR, 14)) return -112; i += 14;
  C.row_pairs.clear();
  if (i + 4 <= n && lit(p + i, "null", 4)) { i += 4; }
  else {
    if (i >= n || p[i] != '{') return -1;
    size_t eobj = scan_obj(p, i, n); if (eobj == (size_t)-1) return -1;
    size_t j = i + 1;
    while (j < eobj - 1) {
      if (p[j] != '"') return -1;
      size_t ek = scan_str(p, j + 1, eobj); if (ek == (size_t)-1) return -1;
      string key((const char*)p + j + 1, ek - 2 - j);
      if (ek >= eobj || p[ek] != ':') return -1;
      size_t v0 = ek + 1, v1 = v0;
      int depth = 0;
      while (v1 < eobj - 1) {
        uint8_t c = p[v1];
        if (c == '"') { v1 = scan_str(p, v1 + 1, eobj); if (v1 == (size_t)-1) return -1; continue; }
        if (c == '{' || c == '[') depth++;
        else if (c == '}' || c == ']') depth--;
        else if (c == ',' && depth == 0) break;
        v1++;
      }
      string val((const char*)p + v0, v1 - v0);
      auto kit = C.akey_id.emplace(key, (int)C.akeys.size());
      if (kit.second) { C.akeys.push_back(key); C.aval_id.emplace_back(); }
      int kid = kit.first->second;
      auto vit = C.aval_id[kid].emplace(move(val), (int)C.aval_id[kid].size());
      C.row_pairs.push_back({(uint16_t)kid, (uint16_t)vit.first->second});
      j = (v1 < eobj - 1) ? v1 + 1 : v1;
    }
    i = eobj;
  }
  if (emit) {
    C.attr_cnt.u8(uint8_t(C.row_pairs.size()));
    for (auto& pr : C.row_pairs) C.attr_key.u8(uint8_t(pr.first));
    for (auto& pr : C.row_pairs) {
      Out& o = C.attr_val[pr.first];
      if (C.attr_width[pr.first] == 1) o.u8(uint8_t(pr.second)); else o.u16(pr.second);
    }
  }
  if (i + 14 > n || !lit(p + i, SEP_CAT, 14)) return -113; i += 14;
  C.row_cats.clear();
  if (i + 4 <= n && lit(p + i, "null", 4)) { i += 4; }
  else {
    if (i >= n || p[i] != '"') return -1;
    size_t e0 = scan_str(p, i + 1, n); if (e0 == (size_t)-1) return -1;
    size_t j = i + 1, tok = j;
    while (j + 2 <= e0) {
      if (p[j] == ',' && p[j+1] == ' ') {
        string t((const char*)p + tok, j - tok);
        auto it = C.cat_vocab.emplace(move(t), (int)C.cat_vocab.size());
        C.row_cats.push_back((uint16_t)it.first->second);
        j += 2; tok = j;
      } else j++;
    }
    string t((const char*)p + tok, e0 - 1 - tok);
    auto it = C.cat_vocab.emplace(move(t), (int)C.cat_vocab.size());
    C.row_cats.push_back((uint16_t)it.first->second);
    i = e0;
  }
  if (emit) {
    C.cat_cnt.u8(uint8_t(C.row_cats.size()));
    for (uint16_t v : C.row_cats) C.cat_ids_v.push_back(v);
  }
  if (i + 9 > n || !lit(p + i, SEP_HRS, 9)) return -114; i += 9;
  uint8_t mask = 0;
  C.row_slots.clear();
  if (i + 4 <= n && lit(p + i, "null", 4)) { i += 4; }
  else {
    if (i >= n || p[i] != '{') return -1;
    size_t eobj = scan_obj(p, i, n); if (eobj == (size_t)-1) return -1;
    size_t j = i + 1;
    int prev_day = -1;
    while (j < eobj - 1) {
      if (p[j] != '"') return -1;
      size_t ek = scan_str(p, j + 1, eobj); if (ek == (size_t)-1) return -1;
      string key((const char*)p + j + 1, ek - 2 - j);
      int d = 0; while (d < 7 && key != DAYS[d]) d++; if (d == 7) return -1;
      if (d <= prev_day) return -1;
      prev_day = d;
      if (ek >= eobj || p[ek] != ':' || p[ek+1] != '"') return -1;
      size_t ev = scan_str(p, ek + 2, eobj); if (ev == (size_t)-1) return -1;
      string time((const char*)p + ek + 2, ev - 3 - ek);
      string sk = key; sk.push_back('\1'); sk.append(time);
      auto it = C.slot_vocab.emplace(move(sk), (int)C.slot_vocab.size());
      C.row_slots.push_back((uint16_t)it.first->second);
      mask |= uint8_t(1u << d);
      j = (ev < eobj - 1) ? ev + 1 : ev;
    }
    i = eobj;
  }
  if (emit) {
    C.h_mask.u8(mask);
    for (uint16_t v : C.row_slots) C.h_slots_v.push_back(v);
  }
  if (i + 1 > n || p[i] != '}') return -115;
  return 0;
}

struct Blob { int codec; Out comp; uint32_t dsize; };

static void zstd_blob(vector<Blob>& out, const Out& src, int level, ZSTD_CCtx* cc) {
  Blob bl; bl.codec = BC_ZSTD; bl.dsize = (uint32_t)src.b.size();
  size_t bound = ZSTD_compressBound(src.b.size() + 16);
  bl.comp.b.resize(bound);
  ZSTD_CCtx_setParameter(cc, ZSTD_c_compressionLevel, level);
  ZSTD_CCtx_setParameter(cc, ZSTD_c_checksumFlag, 0);
  ZSTD_CCtx_setParameter(cc, ZSTD_c_contentSizeFlag, 0);
  size_t r = ZSTD_compress2(cc, bl.comp.b.data(), bound,
                            src.b.data() ? src.b.data() : (const uint8_t*)"", src.b.size());
  if (ZSTD_isError(r)) abort();
  bl.comp.b.resize(r);
  out.push_back(move(bl));
}
static void raw_blob(vector<Blob>& out, const Out& src) {
  Blob bl; bl.codec = BC_RAW; bl.dsize = (uint32_t)src.b.size();
  bl.comp = src;
  out.push_back(move(bl));
}

extern "C" LAB_EXPORT int64_t lab_encode(const uint8_t* raw, size_t size,
                                          uint8_t* archive, size_t capacity) {
  Ctx C;
  size_t nrows = 0;
  for (size_t i = 0; i < size; ) {
    const uint8_t* nl = (const uint8_t*)memchr(raw + i, '\n', size - i);
    if (!nl) return -1;
    size_t len = (size_t)(nl - (raw + i));
       { int rc = encode_line(raw + i, len, C, false); if (rc) { fprintf(stderr, "pass1 fail line %zu rc %d\n", nrows, rc); return -2; } }
    nrows++;
    i += len + 1;
  }
  if (nrows == 0 || nrows > 0xffffffu) return -3;
  size_t nkeys = C.akeys.size();
  if (nkeys > 200) return -3;
  C.attr_width.resize(nkeys);
  for (size_t k = 0; k < nkeys; k++) {
    if (C.aval_id[k].size() > 65535) return -3;
    C.attr_width[k] = (C.aval_id[k].size() > 255) ? 2 : 1;
  }
  if (C.loc_vocab.size() > 65535 || C.cat_vocab.size() > 65535 || C.slot_vocab.size() > 65535) return -3;
  C.attr_val.resize(nkeys);
  for (size_t i = 0; i < size; ) {
    const uint8_t* nl = (const uint8_t*)memchr(raw + i, '\n', size - i);
    size_t len = (size_t)(nl - (raw + i));
    if (encode_line(raw + i, len, C, true)) { fprintf(stderr, "parse fail pass2 line %zu\n", (size_t)(i ? 1 : 1)); return -2; }
    i += len + 1;
  }

  Out pv; vector<uint16_t> kstart(nkeys), kcount(nkeys);
  { uint32_t pid = 0;
    for (size_t k = 0; k < nkeys; k++) {
      kstart[k] = (uint16_t)pid; uint32_t cnt = 0;
      for (auto& kv : C.aval_id[k]) {
        pid = kstart[k] + (uint32_t)kv.second; (void)pid;
        cnt++;
      }
      kcount[k] = (uint16_t)cnt;
      pid = kstart[k] + cnt;
    }
    uint32_t ptotal = 0;
    for (auto v : kcount) ptotal += v;
    vector<string> pe(ptotal);
    for (size_t k = 0; k < nkeys; k++) {
      const string QT(1, char(34));
      for (auto& kv : C.aval_id[k]) {
        string e2 = QT + C.akeys[k] + QT + ":" + kv.first + ",";
        if (e2.size() > 255) return -5;
        pe[kstart[k] + kv.second] = move(e2);
      }
    }
    for (auto& s : pe) {
      if (s.empty()) return -5;
      pv.u8(uint8_t(s.size())); pv.raw(s.data(), s.size());
    }
  }
  Out m; m.u16((uint16_t)nkeys);
  for (auto w : C.attr_width) m.u8(w);
  for (auto v : kstart) m.u16(v);
  for (auto v : kcount) m.u16(v);
  vector<Blob> blobs;
  ZSTD_CCtx* cc = ZSTD_createCCtx();
  // blob 0: ids (raw), blob 1: meta (raw)
  raw_blob(blobs, C.ids);
  raw_blob(blobs, m);
  zstd_blob(blobs, C.name_col, 22, cc);
  zstd_blob(blobs, C.addr_col, 22, cc);
  { Out ld;
    for (auto& k : C.loc_keys) {
      size_t p1 = k.find('\1'), p2 = k.find('\1', p1 + 1);
      string city = k.substr(0, p1), state = k.substr(p1 + 1, p2 - p1 - 1), zip = k.substr(p2 + 1);
      const string QT(1, char(34));
      string e = city + QT + SEP_STATE + state + QT + SEP_ZIP + zip + QT + SEP_LAT;
      if (e.size() > 255) return -4;
      ld.u8(uint8_t(e.size())); ld.raw(e.data(), e.size());
    }
    zstd_blob(blobs, ld, 22, cc);
  }
  { Out li; for (uint16_t v : C.loc_idx_v) li.u16(v); zstd_blob(blobs, li, 22, cc); }
  zstd_blob(blobs, C.lat_col, 22, cc);
  zstd_blob(blobs, C.lng_col, 22, cc);
  zstd_blob(blobs, C.attr_cnt, 22, cc);
  zstd_blob(blobs, C.attr_key, 22, cc);
  for (size_t k = 0; k < nkeys; k++) zstd_blob(blobs, C.attr_val[k], 22, cc);
  zstd_blob(blobs, pv, 22, cc);
  zstd_blob(blobs, C.cat_cnt, 22, cc);
  { Out ci; for (uint16_t v : C.cat_ids_v) ci.u16(v); zstd_blob(blobs, ci, 22, cc); }
  { Out cv; vector<string> ce(C.cat_vocab.size());
    for (auto& kv : C.cat_vocab) ce[kv.second] = kv.first;
    for (auto& s : ce) {
      string e2 = s + ", ";
      if (e2.size() > 255) return -6;
      cv.u8(uint8_t(e2.size())); cv.raw(e2.data(), e2.size());
    }
    zstd_blob(blobs, cv, 22, cc); }
  zstd_blob(blobs, C.h_mask, 22, cc);
  { Out hs; for (uint16_t v : C.h_slots_v) hs.u16(v); zstd_blob(blobs, hs, 22, cc); }
  { Out sv; vector<string> se(C.slot_vocab.size());
    for (auto& kv : C.slot_vocab) {
      size_t q = kv.first.find('\1');
      se[kv.second] = "\"" + kv.first.substr(0, q) + "\":\"" + kv.first.substr(q + 1) + "\",";
    }
    for (auto& s : se) {
      if (s.size() > 255) return -7;
      sv.u8(uint8_t(s.size())); sv.raw(s.data(), s.size());
    }
    zstd_blob(blobs, sv, 22, cc); }
  zstd_blob(blobs, C.rc_col, 22, cc);
  zstd_blob(blobs, C.stars_col, 22, cc);
  zstd_blob(blobs, C.open_col, 22, cc);
  ZSTD_freeCCtx(cc);

  Out arc;
  arc.raw("YBIZ1\0", 6);
  arc.u32((uint32_t)nrows);
  arc.u16((uint16_t)blobs.size());
  arc.u16(0);
  arc.u64((uint64_t)size);
  arc.u64(0);
  for (auto& b : blobs) { arc.u8(uint8_t(b.codec)); arc.u32((uint32_t)b.comp.b.size()); arc.u32(b.dsize); }
  for (auto& b : blobs) if (!b.comp.b.empty()) arc.raw(b.comp.b.data(), b.comp.b.size());
  uint64_t ck = xxh64_hash(arc.b.data() + 30, arc.b.size() - 30, 0);
  memcpy(arc.b.data() + 22, &ck, 8);
  if (arc.b.size() > capacity) return -8;
  memcpy(archive, arc.b.data(), arc.b.size());
  return (int64_t)arc.b.size();
}
