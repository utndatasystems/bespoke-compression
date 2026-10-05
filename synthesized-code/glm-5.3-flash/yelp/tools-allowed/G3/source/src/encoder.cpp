#include "common.h"
#include "interface/codec.h"

// The 14 fields in file order. SEG strings are the exact byte runs between value
// contents; their concatenation (plus the quoted string values) reproduces a line.
static const char* KEYS[14] = {"business_id","name","address","city","state","postal_code",
  "latitude","longitude","stars","review_count","is_open","attributes","categories","hours"};

// value kinds: 0=quoted string, 1=number/literal, 2=object
static const int VKIND[14] = {0,0,0,0,0,0,1,1,1,1,1,2,0,2};

static std::vector<uint8_t> SEG[15];

static const uint8_t* find_key(const uint8_t* from, const uint8_t* end, const char* key, int klen) {
  // search for "key":
  size_t pat = 1 + klen + 2;
  uint8_t tmp[64];
  tmp[0] = '"'; memcpy(tmp+1, key, klen); tmp[1+klen]='"'; tmp[2+klen]=':';
  const uint8_t* res = (const uint8_t*)memmem(from, (size_t)(end-from), tmp, pat);
  return res ? res + pat : nullptr;
}

static inline int hexval(uint8_t c) {
  if (c>='0'&&c<='9') return c-'0';
  if (c>='a'&&c<='f') return c-'a'+10;
  if (c>='A'&&c<='F') return c-'A'+10;
  return -1;
}

// find end of a JSON string starting at open quote pos
static const uint8_t* scan_string(const uint8_t* p, const uint8_t* end) {
  p++; // opening quote
  while (p < end) {
    if (*p == 0x5C) { p += 2; continue; }
    if (*p == 0x22) return p + 1;
    p++;
  }
  return nullptr;
}

// find end of a JSON object/array starting at '{' or '['
static const uint8_t* scan_container(const uint8_t* p, const uint8_t* end) {
  int depth = 0;
  while (p < end) {
    uint8_t c = *p;
    if (c == 0x22) { p = scan_string(p, end); if (!p) return nullptr; continue; }
    if (c == '{' || c == '[') depth++;
    else if (c == '}' || c == ']') { depth--; if (!depth) return p + 1; }
    p++;
  }
  return nullptr;
}

// split an object body into (key,val) raw spans
static void split_obj(const uint8_t* v, size_t vn, std::vector<std::pair<std::vector<uint8_t>,std::vector<uint8_t>>>& pairs) {
  pairs.clear();
  if (vn < 2 || v[0] != '{') return;
  const uint8_t* p = v + 1;
  const uint8_t* end = v + vn - 1;
  while (p < end) {
    // key string
    const uint8_t* ks = scan_string(p, end);
    const uint8_t* colon = ks; // ':'
    const uint8_t* vs = colon + 1;
    std::vector<uint8_t> key(p, ks), val;
    if (*vs == '"') { const uint8_t* ve = scan_string(vs, end); val.assign(vs, ve); p = ve; }
    else if (*vs == '{' || *vs == '[') { const uint8_t* ve = scan_container(vs, end); val.assign(vs, ve); p = ve; }
    else { const uint8_t* ve = vs; while (ve < end && *ve != ',' ) ve++; val.assign(vs, ve); p = ve; }
    pairs.push_back({key, val});
    if (p < end) p++; // comma
  }
}

struct VHash {
  size_t operator()(const std::vector<uint8_t>& v) const {
    uint64_t h = 1469598103934665603ull;
    for (uint8_t c : v) { h ^= c; h *= 1099511628211ull; }
    return (size_t)h;
  }
};
struct Bld {
  std::unordered_map<std::vector<uint8_t>,uint32_t,VHash> map;
  std::vector<std::vector<uint8_t>> vals;
  uint32_t intern(const std::vector<uint8_t>& v) {
    auto it = map.find(v);
    if (it != map.end()) return it->second;
    uint32_t id = (uint32_t)vals.size();
    map.emplace(v, id);
    vals.push_back(v);
    return id;
  }
};

static inline int bits_for(uint32_t x) { int b = 0; while (x) { b++; x >>= 1; } return b ? b : 1; }

// decimal render: value scaled by 10^nd, exact
static void render_scaled(int64_t v, int nd, char* out, int* outlen) {
  uint64_t a = v < 0 ? (uint64_t)(-v) : (uint64_t)v;
  char buf[24];
  int pos = 24;
  while (a) { buf[--pos] = (char)('0' + (a % 10)); a /= 10; }
  int ndig = 24 - pos;
  int need = nd + 1;
  char tmp[28];
  int t = 0;
  for (int i = ndig; i < need; i++) tmp[t++] = '0'; // left pad
  int total = t + ndig;
  memcpy(tmp + t, buf + pos, ndig);
  char* w = out;
  if (v < 0) *w++ = '-';
  memcpy(w, tmp, total - nd); w += total - nd;
  *w++ = '.';
  memcpy(w, tmp + total - nd, nd); w += nd;
  *outlen = (int)(w - out);
}

static bool parse_scaled(const std::vector<uint8_t>& s, int64_t& val, int& nd) {
  const uint8_t* p = s.data();
  const uint8_t* end = p + s.size();
  bool neg = false;
  if (*p == '-') { neg = true; p++; }
  const uint8_t* dot = (const uint8_t*)memchr(p, '.', (size_t)(end - p));
  if (!dot) return false;
  nd = (int)(end - dot - 1);
  if (nd <= 0 || nd > 15) return false;
  uint64_t a = 0;
  for (const uint8_t* q = p; q < end; q++) { if (*q == '.') continue; if (*q < '0' || *q > '9') return false; a = a * 10 + (uint64_t)(*q - '0'); }
  val = neg ? -(int64_t)a : (int64_t)a;
  return true;
}

extern "C" int64_t lab_encode(const uint8_t* raw, size_t size, uint8_t* archive, size_t capacity) {
  fprintf(stderr, "lab_encode enter size=%zu cap=%zu\n", size, capacity);
  (void)capacity;
  // ---------- split lines ----------
  std::vector<const uint8_t*> lines;
  const uint8_t* p = raw;
  const uint8_t* end = raw + size;
  while (p < end) {
    const uint8_t* nl = (const uint8_t*)memchr(p, '\n', (size_t)(end - p));
    if (!nl) { fprintf(stderr, "no trailing newline\n"); return -1; }
    lines.push_back(p);
    p = nl + 1;
  }
  uint32_t nrec = (uint32_t)lines.size();

  // ---------- parse spans ----------
  std::vector<std::vector<uint8_t>> val[14];
  for (int k = 0; k < 14; k++) val[k].resize(nrec);
  std::vector<std::array<std::vector<uint8_t>,15>> segs(nrec);

  for (uint32_t i = 0; i < nrec; i++) {
    const uint8_t* ls = lines[i];
    const uint8_t* le = (i + 1 < nrec) ? lines[i+1] - 1 : end - 1;
    const uint8_t* pos = ls;
    for (int k = 0; k < 14; k++) {
      const uint8_t* vs = find_key(pos, le, KEYS[k], (int)strlen(KEYS[k]));
      if (!vs) { fprintf(stderr, "FAIL keyfind k=%d\n", k); return -1; }
      segs[i][k].assign(pos, vs);
      const uint8_t* ve;
      if (*vs == '"') { ve = scan_string(vs, le); if (!ve) { fprintf(stderr, "FAIL scanstr k=%d\n", k); return -1; } }
      else if (*vs == '{' || *vs == '[') { ve = scan_container(vs, le); if (!ve) { fprintf(stderr, "FAIL scancont k=%d\n", k); return -1; } }
      else { ve = vs; while (ve < le && *ve != ',' && *ve != '}') ve++; }
      val[k][i].assign(vs, ve);
      pos = ve;
    }
    segs[i][14].assign(pos, le + 1); // include trailing newline
  }
  // segments must be identical across records
  for (int k = 0; k < 15; k++) {
    SEG[k] = segs[0][k];
    for (uint32_t i = 1; i < nrec; i++) if (segs[i][k] != SEG[k]) { fprintf(stderr, "segment %d mismatch\n", k); return -1; }
  }

  // ---------- verify full rebuild + collect columns ----------
  Bld colB[14];
  std::vector<uint32_t> colId[14];
  for (int k = 0; k < 14; k++) colId[k].resize(nrec);
  std::vector<uint8_t> tmp(4096);
  for (uint32_t i = 0; i < nrec; i++) {
    size_t need = 0;
    for (int k = 0; k < 14; k++) need += SEG[k].size() + val[k][i].size();
    need += SEG[14].size();
    if (tmp.size() < need) tmp.resize(need * 2);
    uint8_t* w = tmp.data();
    for (int k = 0; k < 14; k++) {
      memcpy(w, SEG[k].data(), SEG[k].size()); w += SEG[k].size();
      memcpy(w, val[k][i].data(), val[k][i].size()); w += val[k][i].size();
    }
    memcpy(w, SEG[14].data(), SEG[14].size()); w += SEG[14].size();
    const uint8_t* ls = lines[i];
    const uint8_t* le = (i + 1 < nrec) ? lines[i+1] : end;
    if ((size_t)(w - tmp.data()) != (size_t)(le - ls) || memcmp(tmp.data(), ls, (size_t)(le - ls)) != 0) {
      fprintf(stderr, "rebuild mismatch at record %u\n", i); return -1;
    }
  }

  // ---------- business_id 6-bit check ----------
  static const char ALPHA[65] = "-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz";
  int8_t alphaIdx[256];
  for (int i = 0; i < 256; i++) alphaIdx[i] = -1;
  for (int i = 0; i < 64; i++) alphaIdx[(uint8_t)ALPHA[i]] = (int8_t)i;
  std::vector<uint8_t> bid;
  bid.reserve((size_t)nrec * 17);
  {
    BitW bw; std::vector<uint8_t> buf;
    bw.out = &buf;
    for (uint32_t i = 0; i < nrec; i++) {
      const std::vector<uint8_t>& v = val[0][i];
      if (v.size() != 24) { fprintf(stderr, "FAIL bid size %zu rec %u\n", v.size(), i); return -1; }
      for (int j = 1; j <= 22; j++) {
        int8_t ix = alphaIdx[v[j]];
        if (ix < 0) { fprintf(stderr, "FAIL bid charset rec %u\n", i); return -1; }
        bw.put((uint64_t)ix, 6);
      }
    }
    bw.align_byte();
    bid.swap(buf);
  }

  // ---------- lat/lon ----------
  int64_t latScale[2] = {0,0};
  std::vector<int64_t> sc[2]; std::vector<uint8_t> ndg[2];
  for (int c = 0; c < 2; c++) {
    int k = 6 + c;
    sc[c].resize(nrec); ndg[c].resize(nrec);
    for (uint32_t i = 0; i < nrec; i++) {
      int64_t v; int nd;
      if (!parse_scaled(val[k][i], v, nd)) return -1;
      sc[c][i] = v; ndg[c][i] = (uint8_t)nd;
      // roundtrip check
      char out[32]; int ol;
      render_scaled(v, nd, out, &ol);
      if ((int)val[k][i].size() != ol || memcmp(out, val[k][i].data(), (size_t)ol) != 0) {
        fprintf(stderr, "num roundtrip fail rec %u col %d\n", i, k); return -1;
      }
    }
  }
  // unique sorted + varint deltas + flat id streams
  std::vector<uint8_t> numStream[2]; std::vector<uint32_t> numId[2]; int numBits[2]; uint32_t numUniq[2];
  for (int c = 0; c < 2; c++) {
    std::vector<int64_t> u = sc[c];
    std::sort(u.begin(), u.end());
    u.erase(std::unique(u.begin(), u.end()), u.end());
    numUniq[c] = (uint32_t)u.size();
    numBits[c] = bits_for(numUniq[c] - 1);
    std::unordered_map<int64_t,uint32_t> idx; idx.reserve(u.size() * 2);
    for (uint32_t i = 0; i < u.size(); i++) idx[u[i]] = i;
    numId[c].resize(nrec);
    for (uint32_t i = 0; i < nrec; i++) numId[c][i] = idx[sc[c][i]];
    std::vector<uint8_t> vs;
    put_uv(vs, (uint64_t)u[0]);
    for (size_t i = 1; i < u.size(); i++) put_uv(vs, (uint64_t)(u[i] - u[i-1]));
    numStream[c].swap(vs);
  }
  std::vector<uint8_t> decBytes(nrec);
  for (uint32_t i = 0; i < nrec; i++) decBytes[i] = (uint8_t)(ndg[0][i] | (ndg[1][i] << 4));

  // ---------- simple pools (name, addr) with sorted unique + zstd ----------
  // ---------- merged pool columns: LOC(name,addr,city,state,postal), NUM3(stars,rc,is_open), LL(lat,lon) ----------
  auto build_pool_col = [&](const std::vector<std::vector<uint8_t>>& valsV,
                             std::vector<uint8_t>& secOut) {
    Bld b;
    uint32_t n = (uint32_t)valsV.size();
    std::vector<uint32_t> ids(n);
    for (uint32_t i = 0; i < n; i++) ids[i] = b.intern(valsV[i]);
    uint32_t nu = (uint32_t)b.vals.size();
    int bits = bits_for(nu - 1);
    std::vector<const std::vector<uint8_t>*> ptrs;
    for (auto& v : b.vals) ptrs.push_back(&v);
    std::sort(ptrs.begin(), ptrs.end(), [](const std::vector<uint8_t>* a, const std::vector<uint8_t>* bq) { return *a < *bq; });
    std::unordered_map<std::vector<uint8_t>,uint32_t,VHash> sortedId; sortedId.reserve(ptrs.size() * 2);
    std::vector<uint8_t> concat, lens;
    std::vector<uint32_t> offs; offs.push_back(0);
    for (auto* v : ptrs) {
      sortedId[*v] = (uint32_t)offs.size() - 1;
      concat.insert(concat.end(), v->begin(), v->end());
      offs.push_back((uint32_t)concat.size());
      lens.push_back((uint8_t)v->size());
    }
    std::vector<uint8_t> run, idb;
    { BitW bw; bw.out = &run; bw.put(0, 1); for (uint32_t i = 1; i < n; i++) bw.put(ids[i] == ids[i-1] ? 1 : 0, 1); bw.align_byte(); }
    { BitW bw; bw.out = &idb; for (uint32_t i = 0; i < n; i++) { if (i && ids[i] == ids[i-1]) continue; bw.put(sortedId[b.vals[ids[i]]], bits); } bw.align_byte(); }
    std::vector<uint8_t> idxRaw;
    put_uv(idxRaw, nu);
    put_uv(idxRaw, offs[0]);
    for (uint32_t i = 1; i <= nu; i++) put_uv(idxRaw, offs[i] - offs[i-1]);
    idxRaw.insert(idxRaw.end(), lens.begin(), lens.end());
    { uint32_t nck = (nu + 255) / 256; put_uv(idxRaw, nck); for (uint32_t cK = 0; cK < nck; cK++) put_uv(idxRaw, offs[cK * 256]); }
    std::vector<uint8_t> idxZ;
    zstd_compress_to(idxZ, idxRaw.data(), idxRaw.size(), 12);
    std::vector<uint8_t> poolZ;
    zstd_compress_to(poolZ, concat.data(), concat.size(), 12);
    std::vector<uint8_t> s;
    s.insert(s.end(), poolZ.begin(), poolZ.end());
    s.insert(s.end(), idxZ.begin(), idxZ.end());
    s.push_back((uint8_t)bits);
    put_uv(s, (uint32_t)run.size()); s.insert(s.end(), run.begin(), run.end());
    put_uv(s, (uint32_t)idb.size()); s.insert(s.end(), idb.begin(), idb.end());
    secOut.swap(s);
  };

  std::vector<std::vector<uint8_t>> locV(nrec), numV(nrec), llV(nrec);
  for (uint32_t i = 0; i < nrec; i++) {
    std::vector<uint8_t>& L = locV[i];
    L.insert(L.end(), val[1][i].begin(), val[1][i].end());
    L.insert(L.end(), SEG[2].begin(), SEG[2].end());
    L.insert(L.end(), val[2][i].begin(), val[2][i].end());
    L.insert(L.end(), SEG[3].begin(), SEG[3].end());
    L.insert(L.end(), val[3][i].begin(), val[3][i].end());
    L.insert(L.end(), SEG[4].begin(), SEG[4].end());
    L.insert(L.end(), val[4][i].begin(), val[4][i].end());
    L.insert(L.end(), SEG[5].begin(), SEG[5].end());
    L.insert(L.end(), val[5][i].begin(), val[5][i].end());
    std::vector<uint8_t>& N = numV[i];
    N.insert(N.end(), val[8][i].begin(), val[8][i].end());
    N.insert(N.end(), SEG[9].begin(), SEG[9].end());
    N.insert(N.end(), val[9][i].begin(), val[9][i].end());
    N.insert(N.end(), SEG[10].begin(), SEG[10].end());
    N.insert(N.end(), val[10][i].begin(), val[10][i].end());
    std::vector<uint8_t>& W = llV[i];
    W.insert(W.end(), val[6][i].begin(), val[6][i].end());
    W.insert(W.end(), SEG[7].begin(), SEG[7].end());
    W.insert(W.end(), val[7][i].begin(), val[7][i].end());
  }
  std::vector<uint8_t> locSec, numSec, llSec;
  build_pool_col(locV, locSec);
  build_pool_col(numV, numSec);
  build_pool_col(llV, llSec);

  // ---------- attributes ----------
  // parse objects
  std::vector<std::vector<std::pair<std::vector<uint8_t>,std::vector<uint8_t>>>> attrParsed(nrec);
  for (uint32_t i = 0; i < nrec; i++) {
    const std::vector<uint8_t>& v = val[11][i];
    if (v == std::vector<uint8_t>{'n','u','l','l'}) continue;
    split_obj(v.data(), v.size(), attrParsed[i]);
    // rebuild check
    std::vector<uint8_t> rb;
    rb.push_back('{');
    for (size_t j = 0; j < attrParsed[i].size(); j++) {
      if (j) rb.push_back(',');
      rb.insert(rb.end(), attrParsed[i][j].first.begin(), attrParsed[i][j].first.end());
      rb.push_back(':');
      rb.insert(rb.end(), attrParsed[i][j].second.begin(), attrParsed[i][j].second.end());
    }
    rb.push_back('}');
    if (rb != v) { fprintf(stderr, "attr rebuild fail rec %u\n", i); return -1; }
  }
  // unique key list
  Bld keyB;
  std::vector<uint32_t> keyId;
  for (uint32_t i = 0; i < nrec; i++) for (auto& pr : attrParsed[i]) keyId.push_back(keyB.intern(pr.first));
  uint32_t nKeys = (uint32_t)keyB.vals.size();
  // per-key value vocab (global entry ids)
  std::vector<Bld> valB(nKeys);
  std::vector<uint32_t> attrValEntry; attrValEntry.reserve(keyId.size());
  {
    size_t ki = 0;
    for (uint32_t i = 0; i < nrec; i++) for (auto& pr : attrParsed[i]) {
      uint32_t k = keyId[ki];
      attrValEntry.push_back(valB[k].intern(pr.second));
      ki++;
    }
  }
  uint32_t nVocab = 0;
  for (int k = 0; k < (int)nKeys; k++) nVocab += (uint32_t)valB[k].vals.size();
  // unique attribute objects over raw span
  Bld objB;
  std::vector<uint32_t> attrUid(nrec);
  for (uint32_t i = 0; i < nrec; i++) attrUid[i] = objB.intern(val[11][i]);
  uint32_t nAttrUniq = (uint32_t)objB.vals.size();
  // parsed pairs per unique obj (in first-occurrence order)
  std::vector<std::vector<uint32_t>> uKeys(nAttrUniq), uVals(nAttrUniq);
  for (uint32_t i = 0; i < nrec; i++) {
    if (attrParsed[i].empty()) continue;
    uint32_t u = attrUid[i];
    if (!uKeys[u].empty()) continue;
    size_t ki = 0; // recompute: walk parsed entries again
    // simpler: we stored pairs; get ids via maps
    for (auto& pr : attrParsed[i]) {
      std::vector<uint8_t> keyv = pr.first;
      uint32_t k = keyB.map[keyv];
      uKeys[u].push_back(k);
      uVals[u].push_back(valB[k].map[pr.second]);
    }
    (void)ki;
  }
  // per-record repeat bit (same unique obj as previous)
  std::vector<uint8_t> attrRun;
  { BitW bw; bw.out = &attrRun;
    bw.put(0, 1);
    for (uint32_t i = 1; i < nrec; i++) bw.put(attrUid[i] == attrUid[i-1] ? 1 : 0, 1);
    bw.align_byte(); }
  // combos: per (key,value) pair: pre[k] + value, ordered by combo id (keyVStart order)
  std::vector<uint32_t> keyVStart(nKeys + 1, 0);
  { uint32_t acc = 0; for (int k = 0; k < (int)nKeys; k++) { keyVStart[k] = acc; acc += (uint32_t)valB[k].vals.size(); } keyVStart[nKeys] = acc; }
  // flat per-unique-object arrays: flags, offsets, key ids, global entry ids
  std::vector<uint8_t> fFlags(nAttrUniq, 0);
  std::vector<uint32_t> fOff(nAttrUniq + 1, 0);
  std::vector<uint8_t> fkIds;
  std::vector<uint16_t> feIds;
  {
    static const std::vector<uint8_t> NULLV = {'n','u','l','l'};
    fkIds.reserve(keyId.size()); feIds.reserve(keyId.size());
    for (uint32_t u = 0; u < nAttrUniq; u++) {
      fOff[u] = (uint32_t)fkIds.size();
      if (objB.vals[u] == NULLV) { fFlags[u] = 1; continue; }
      const auto& ks = uKeys[u];
      for (size_t j = 0; j < ks.size(); j++) {
        uint32_t k = ks[j];
        fkIds.push_back((uint8_t)k);
        feIds.push_back((uint16_t)(keyVStart[k] + uVals[u][j]));
      }
    }
    fOff[nAttrUniq] = (uint32_t)fkIds.size();
  }
  std::vector<uint8_t> flatRaw;
  flatRaw.insert(flatRaw.end(), (uint8_t*)&nAttrUniq, (uint8_t*)&nAttrUniq + 4);
  uint32_t nPairs32 = (uint32_t)feIds.size();
  flatRaw.insert(flatRaw.end(), (uint8_t*)&nPairs32, (uint8_t*)&nPairs32 + 4);
  flatRaw.insert(flatRaw.end(), fFlags.begin(), fFlags.end());
  flatRaw.insert(flatRaw.end(), (uint8_t*)fOff.data(), (uint8_t*)fOff.data() + 4 * fOff.size());
  flatRaw.insert(flatRaw.end(), (uint8_t*)feIds.data(), (uint8_t*)feIds.data() + 2 * feIds.size());
  std::vector<uint8_t> flatZ;
  zstd_compress_to(flatZ, flatRaw.data(), flatRaw.size(), 3);
  (void)fkIds;
  // keyfix strings: pre (with leading comma) and pre0 (first, with '{')
  std::vector<uint8_t> pre, pre0;
  std::vector<uint8_t> preLen(nKeys), pre0Len(nKeys);
  std::vector<uint32_t> preOff(nKeys + 1, 0), pre0Off(nKeys + 1, 0);
  for (int k = 0; k < (int)nKeys; k++) {
    std::vector<uint8_t> s;
    s.push_back(','); s.insert(s.end(), keyB.vals[k].begin(), keyB.vals[k].end()); s.push_back(':');
    pre.insert(pre.end(), s.begin(), s.end()); preOff[k+1] = (uint32_t)pre.size(); preLen[k] = (uint8_t)s.size();
    s[0] = '{';
    pre0.insert(pre0.end(), s.begin(), s.end()); pre0Off[k+1] = (uint32_t)pre0.size(); pre0Len[k] = (uint8_t)s.size();
  }
  std::vector<uint8_t> attrSec;
  {
    std::vector<uint8_t>& s = attrSec;
    put_uv(s, nAttrUniq);
    put_uv(s, (uint32_t)attrRun.size()); s.insert(s.end(), attrRun.begin(), attrRun.end());
    // rec -> uid flat stream
    int abits = bits_for(nAttrUniq - 1);
    s.push_back((uint8_t)abits);
    { BitW bw; std::vector<uint8_t> buf; bw.out = &buf;
      for (uint32_t i = 0; i < nrec; i++) { if (i && attrUid[i] == attrUid[i-1]) continue; bw.put(attrUid[i], abits); }
      bw.align_byte();
      put_uv(s, (uint32_t)buf.size()); s.insert(s.end(), buf.begin(), buf.end()); }
    // flat arrays (zstd)
    put_uv(s, (uint32_t)flatZ.size()); s.insert(s.end(), flatZ.begin(), flatZ.end());
    // combos (zstd): concat of pre[k]+value per (key,value); cOff u32 table
    std::vector<uint8_t> cConcat;
    std::vector<uint32_t> cOff; cOff.reserve(keyVStart[nKeys] + 1);
    for (int k = 0; k < (int)nKeys; k++) {
      for (auto& v : valB[k].vals) {
        cOff.push_back((uint32_t)cConcat.size());
        cConcat.insert(cConcat.end(), pre.begin() + preOff[k], pre.begin() + preOff[k + 1]);
        cConcat.insert(cConcat.end(), v.begin(), v.end());
      }
    }
    cOff.push_back((uint32_t)cConcat.size());
    std::vector<uint8_t> combRaw;
    put_uv(combRaw, (uint32_t)(cOff.size() - 1));
    combRaw.insert(combRaw.end(), (uint8_t*)cOff.data(), (uint8_t*)cOff.data() + 4 * cOff.size());
    combRaw.insert(combRaw.end(), cConcat.begin(), cConcat.end());
    std::vector<uint8_t> combZ;
    zstd_compress_to(combZ, combRaw.data(), combRaw.size(), 3);
    put_uv(s, (uint32_t)combZ.size()); s.insert(s.end(), combZ.begin(), combZ.end());
  }
  // ---------- categories ----------
  std::vector<uint8_t> catSec;
  {
    Bld tokB;
    std::vector<std::vector<uint32_t>> catToks;
    std::vector<uint8_t> nullC = {'n','u','l','l'};
    for (uint32_t i = 0; i < nrec; i++) {
      const std::vector<uint8_t>& v = val[12][i];
      std::vector<uint32_t> toks;
      if (v != nullC) {
        const uint8_t* d = v.data();
        size_t vn = v.size();
        // content between quotes
        const uint8_t* cs = d + 1; const uint8_t* ce = d + vn - 1;
        const uint8_t* q = cs;
        while (q < ce) {
          const uint8_t* comma = (const uint8_t*)memmem(q, (size_t)(ce - q), ", ", 2);
          if (!comma) comma = ce;
          std::vector<uint8_t> tok(q, comma);
          toks.push_back(tokB.intern(tok));
          q = comma + (comma < ce ? 2 : 0);
        }
        // rejoin check
        std::vector<uint8_t> rb;
        for (size_t j = 0; j < toks.size(); j++) { if (j) { rb.push_back(','); rb.push_back(' '); } const auto& t = tokB.vals[toks[j]]; rb.insert(rb.end(), t.begin(), t.end()); }
        if (rb.size() != (size_t)(ce - cs) || memcmp(rb.data(), cs, rb.size()) != 0) { fprintf(stderr, "cat rebuild fail %u\n", i); return -1; }
        catToks.push_back(toks);
      } else {
        catToks.push_back(std::vector<uint32_t>());
      }
    }
    uint32_t nTok = (uint32_t)tokB.vals.size();
    // unique cat contents (raw span incl quotes, or null)
    Bld ucat;
    std::vector<uint32_t> catUid(nrec);
    for (uint32_t i = 0; i < nrec; i++) catUid[i] = ucat.intern(val[12][i]);
    uint32_t nCat = (uint32_t)ucat.vals.size();
    // tokens per unique cat
    std::vector<std::vector<uint32_t>> uToks(nCat);
    std::vector<uint8_t> catRun;
    { BitW bw; bw.out = &catRun; bw.put(0,1); for (uint32_t i = 1; i < nrec; i++) bw.put(catUid[i]==catUid[i-1]?1:0,1); bw.align_byte(); }
    for (uint32_t i = 0; i < nrec; i++) {
      if (!uToks[catUid[i]].empty() || ucat.vals[catUid[i]] == nullC) continue;
      uToks[catUid[i]] = catToks[i];
    }
    // token freqs over unique cats
    std::vector<uint32_t> tfreq(nTok, 0);
    for (uint32_t u = 0; u < nCat; u++) for (uint32_t t : uToks[u]) tfreq[t]++;
    std::vector<uint8_t> tlen; huff_lengths(tfreq.data(), (int)nTok, tlen);
    std::vector<uint32_t> tcode; huff_codes(tlen, tcode);
    // blob
    std::vector<uint8_t> cblob; std::vector<uint32_t> cOff(nCat + 1, 0);
    for (uint32_t u = 0; u < nCat; u++) {
      cOff[u] = (uint32_t)cblob.size();
      if (ucat.vals[u] == nullC) { cblob.push_back(0xFF); continue; }
      cblob.push_back((uint8_t)uToks[u].size());
      BitW bw; bw.out = &cblob;
      for (uint32_t t : uToks[u]) bw.put(tcode[t], tlen[t]);
      bw.align_byte();
    }
    cOff[nCat] = (uint32_t)cblob.size();
    std::vector<uint8_t> cidx;
    put_uv(cidx, cOff[0]);
    for (uint32_t u = 1; u < nCat; u++) put_uv(cidx, cOff[u] - cOff[u-1]);
    { uint32_t nck = (nCat + 255) / 256; put_uv(cidx, nck); for (uint32_t cK = 0; cK < nck; cK++) put_uv(cidx, cOff[cK * 256]); }
    // token dict
    std::vector<uint32_t> toffs; toffs.push_back(0);
    std::vector<uint8_t> tlens, tconcat;
    for (auto& t : tokB.vals) { tconcat.insert(tconcat.end(), t.begin(), t.end()); toffs.push_back((uint32_t)tconcat.size()); tlens.push_back((uint8_t)t.size()); }
    int cbits = bits_for(nCat - 1);
    std::vector<uint8_t> cuid; { BitW bw; bw.out = &cuid; for (uint32_t i = 0; i < nrec; i++) { if (i && catUid[i] == catUid[i-1]) continue; bw.put(catUid[i], cbits); } bw.align_byte(); }
    std::vector<uint8_t>& s = catSec;
    put_uv(s, nTok);
    s.insert(s.end(), (uint8_t*)toffs.data(), (uint8_t*)toffs.data() + 4 * toffs.size());
    s.insert(s.end(), tlens.begin(), tlens.end());
    s.insert(s.end(), tconcat.begin(), tconcat.end());
    s.insert(s.end(), tlen.begin(), tlen.end());
    put_uv(s, nCat); s.push_back((uint8_t)cbits);
    put_uv(s, (uint32_t)catRun.size()); s.insert(s.end(), catRun.begin(), catRun.end());
    put_uv(s, (uint32_t)cuid.size()); s.insert(s.end(), cuid.begin(), cuid.end());
    put_uv(s, (uint32_t)cidx.size()); s.insert(s.end(), cidx.begin(), cidx.end());
    put_uv(s, (uint32_t)cblob.size()); s.insert(s.end(), cblob.begin(), cblob.end());
  }

  // ---------- hours ----------
  std::vector<uint8_t> hoursSec;
  {
    static const char* DAYS[7] = {"Monday","Tuesday","Wednesday","Thursday","Friday","Saturday","Sunday"};
    Bld hvB;
    std::vector<uint8_t> hvMask; // per unique obj
    std::vector<std::vector<uint32_t>> hvVals;
    std::vector<uint8_t> nullH = {'n','u','l','l'};
    std::vector<std::vector<uint8_t>> dayKey(7);
    for (int d = 0; d < 7; d++) { dayKey[d].push_back('"'); dayKey[d].insert(dayKey[d].end(), DAYS[d], DAYS[d] + strlen(DAYS[d])); dayKey[d].push_back('"'); }
    Bld uobj;
    std::vector<uint32_t> uid(nrec);
    for (uint32_t i = 0; i < nrec; i++) {
      const std::vector<uint8_t>& v = val[13][i];
      uint8_t mask = 0; std::vector<uint32_t> vs;
      if (v != nullH) {
        std::vector<std::pair<std::vector<uint8_t>,std::vector<uint8_t>>> pairs;
        split_obj(v.data(), v.size(), pairs);
        int lastday = -1;
        for (auto& pr : pairs) {
          int d = -1;
          for (int j = 0; j < 7; j++) if (pr.first == dayKey[j]) { d = j; break; }
          if (d < 0 || d <= lastday) { fprintf(stderr, "hours day order fail\n"); return -1; }
          lastday = d;
          mask |= (uint8_t)(1u << d);
          vs.push_back(hvB.intern(pr.second));
        }
        std::vector<uint8_t> rb; rb.push_back('{');
        bool first = true;
        size_t j = 0;
        for (int d = 0; d < 7; d++) {
          if (!(mask & (1u << d))) continue;
          if (!first) rb.push_back(',');
          first = false;
          rb.push_back('"'); rb.insert(rb.end(), DAYS[d], DAYS[d] + strlen(DAYS[d])); rb.push_back('"'); rb.push_back(':');
          const auto& vv = hvB.vals[vs[j++]]; rb.insert(rb.end(), vv.begin(), vv.end());
        }
        rb.push_back('}');
        if (rb != v) { fprintf(stderr, "hours rebuild fail rec %u\n", i); return -1; }
      }
      uint32_t u = uobj.intern(v);
      uid[i] = u;
      if (hvVals.size() <= u) { hvVals.resize(u + 1); hvMask.resize(u + 1, 0); }
      if (hvVals[u].empty() && hvMask[u] == 0) { hvMask[u] = mask; hvVals[u] = vs; }
    }
    uint32_t nU = (uint32_t)uobj.vals.size();
    hvVals.resize(nU); hvMask.resize(nU, 0);
    uint32_t nV = (uint32_t)hvB.vals.size();
    std::vector<uint8_t> hrun; { BitW bw; bw.out = &hrun; bw.put(0,1); for (uint32_t i = 1; i < nrec; i++) bw.put(uid[i]==uid[i-1]?1:0,1); bw.align_byte(); }
    std::vector<uint32_t> vfreq(nV, 0);
    for (uint32_t u = 0; u < nU; u++) for (uint32_t t : hvVals[u]) vfreq[t]++;
    std::vector<uint8_t> vlen; huff_lengths(vfreq.data(), (int)nV, vlen);
    std::vector<uint32_t> vcode; huff_codes(vlen, vcode);
    std::vector<uint8_t> blob2; std::vector<uint32_t> oOff(nU + 1, 0);
    for (uint32_t u = 0; u < nU; u++) {
      oOff[u] = (uint32_t)blob2.size();
      if (uobj.vals[u] == nullH) { blob2.push_back(0xFF); continue; }
      blob2.push_back(hvMask[u]);
      BitW bw; bw.out = &blob2;
      for (uint32_t t : hvVals[u]) bw.put(vcode[t], vlen[t]);
      bw.align_byte();
    }
    oOff[nU] = (uint32_t)blob2.size();
    std::vector<uint8_t> oidx;
    put_uv(oidx, oOff[0]);
    for (uint32_t u = 1; u < nU; u++) put_uv(oidx, oOff[u] - oOff[u-1]);
    { uint32_t nck = (nU + 255) / 256; put_uv(oidx, nck); for (uint32_t cK = 0; cK < nck; cK++) put_uv(oidx, oOff[cK * 256]); }
    std::vector<uint32_t> voffs; voffs.push_back(0);
    std::vector<uint8_t> vlens, vconcat;
    for (auto& t : hvB.vals) { vconcat.insert(vconcat.end(), t.begin(), t.end()); voffs.push_back((uint32_t)vconcat.size()); vlens.push_back((uint8_t)t.size()); }
    int hbits = bits_for(nU - 1);
    std::vector<uint8_t> huid; { BitW bw; bw.out = &huid; for (uint32_t i = 0; i < nrec; i++) { if (i && uid[i] == uid[i-1]) continue; bw.put(uid[i], hbits); } bw.align_byte(); }
    std::vector<uint8_t>& s = hoursSec;
    put_uv(s, nU); s.push_back((uint8_t)hbits); put_uv(s, nV);
    put_uv(s, (uint32_t)hrun.size()); s.insert(s.end(), hrun.begin(), hrun.end());
    put_uv(s, (uint32_t)huid.size()); s.insert(s.end(), huid.begin(), huid.end());
    s.insert(s.end(), vlen.begin(), vlen.end());
    put_uv(s, (uint32_t)oidx.size()); s.insert(s.end(), oidx.begin(), oidx.end());
    put_uv(s, (uint32_t)blob2.size()); s.insert(s.end(), blob2.begin(), blob2.end());
    s.insert(s.end(), (uint8_t*)voffs.data(), (uint8_t*)voffs.data() + 4 * voffs.size());
    s.insert(s.end(), vlens.begin(), vlens.end());
    s.insert(s.end(), vconcat.begin(), vconcat.end());
  }

  // ---------- assemble archive ----------
  std::vector<uint8_t> sec[NSEC];
  {
    std::vector<uint8_t>& s = sec[SEC_SKEL];
    for (int k = 0; k < 15; k++) { put_uv(s, (uint32_t)SEG[k].size()); s.insert(s.end(), SEG[k].begin(), SEG[k].end()); }
  }
  sec[SEC_BID] = bid;
  sec[SEC_LOC] = locSec;
  sec[SEC_NUM3] = numSec;
  sec[SEC_LL] = llSec;

  sec[SEC_ATTR] = attrSec;
  sec[SEC_CAT] = catSec;
  sec[SEC_HOURS] = hoursSec;

  std::vector<uint8_t> out;
  uint32_t magic = 0x31504c59; // YLP1
  out.insert(out.end(), (uint8_t*)&magic, (uint8_t*)&magic + 4);
  uint32_t ver = 1;
  out.insert(out.end(), (uint8_t*)&ver, (uint8_t*)&ver + 4);
  out.insert(out.end(), (uint8_t*)&size, (uint8_t*)&size + 8);
  out.insert(out.end(), (uint8_t*)&nrec, (uint8_t*)&nrec + 4);
  uint32_t nsec = NSEC;
  out.insert(out.end(), (uint8_t*)&nsec, (uint8_t*)&nsec + 4);
  uint64_t offs[NSEC];
  uint64_t pos = out.size() + 8 * NSEC;
  for (int i = 0; i < NSEC; i++) { offs[i] = pos; pos += sec[i].size(); }
  out.insert(out.end(), (uint8_t*)offs, (uint8_t*)offs + 8 * NSEC);
  for (int i = 0; i < NSEC; i++) out.insert(out.end(), sec[i].begin(), sec[i].end());
  out.resize(out.size() + 64, 0); // bit-reader slack

  if (out.size() > capacity) return -1;
  memcpy(archive, out.data(), out.size());
  fprintf(stderr, "encoded %zu -> %zu bytes\n", size, out.size());
  for (int i = 0; i < NSEC; i++) fprintf(stderr, "sec %2d: %zu\n", i, sec[i].size());
  return (int64_t)out.size();
}
