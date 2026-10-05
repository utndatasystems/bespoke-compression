#pragma once
#include <stdint.h>
#include <vector>
#include <algorithm>

// Optimal length-limited Huffman code lengths via natural Huffman + package-merge
// (only when the natural depths exceed 'cap').
inline void huff_lengths(const std::vector<uint64_t>& freq, int cap, std::vector<uint8_t>& lens) {
  int n = (int)freq.size();
  lens.assign(n, 0);
  std::vector<uint32_t> syms;
  for (int i = 0; i < n; i++) if (freq[i]) syms.push_back((uint32_t)i);
  if (syms.empty()) return;
  if (syms.size() == 1) { lens[syms[0]] = 1; return; }
  size_t m = syms.size();
  if ((int)cap < 1) cap = 1;
  if ((1ull << cap) < m) cap = (int)sizeof(uint32_t) * 8 - 1; // cannot happen for our vocab sizes

  std::vector<uint64_t> lw(m);
  std::vector<uint32_t> ord(m);
  for (size_t i = 0; i < m; i++) { lw[i] = freq[syms[i]]; ord[i] = (uint32_t)i; }
  std::sort(ord.begin(), ord.end(), [&](uint32_t a, uint32_t b){ return lw[a] < lw[b]; });

  std::vector<uint64_t> node_w(2 * m);
  for (size_t i = 0; i < m; i++) node_w[i] = lw[ord[i]];
  std::vector<uint32_t> parent(2 * m, 0xffffffffu);
  size_t lo = 0, nlo = m, nmade = m;
  for (size_t made = 0; made + 1 < m; made++) {
    uint32_t a, b;
    if (lo < m && (nlo >= nmade || node_w[ord[lo]] <= node_w[nlo])) a = ord[lo++];
    else a = (uint32_t)nlo++;
    if (lo < m && (nlo >= nmade || node_w[ord[lo]] <= node_w[nlo])) b = ord[lo++];
    else b = (uint32_t)nlo++;
    uint32_t nid = (uint32_t)(m + made);
    parent[a] = nid; parent[b] = nid;
    node_w[nid] = node_w[a] + node_w[b];
    nmade++;
  }
  uint32_t maxlen = 0;
  for (size_t i = 0; i < m; i++) {
    uint32_t d = 0, cur = (uint32_t)i;
    while (parent[cur] != 0xffffffffu) { d++; cur = parent[cur]; }
    lens[syms[i]] = (uint8_t)d;
    if (d > maxlen) maxlen = d;
  }
  if (maxlen <= (uint32_t)cap) return;

  // package-merge, levels 1..cap
  struct PM { uint64_t w; int32_t leaf; int32_t a, b; };
  std::vector<std::vector<PM>> L(cap + 1);
  L[1].reserve(m);
  for (size_t i = 0; i < m; i++) L[1].push_back({lw[ord[i]], (int32_t)ord[i], -1, -1});
  for (int l = 2; l <= cap; l++) {
    const std::vector<PM>& prev = L[l - 1];
    L[l].reserve(prev.size() + m);
    size_t pi = 0, li = 0;
    auto emit_pkg = [&](size_t i) {
      L[l].push_back({prev[i].w + prev[i + 1].w, -1, (int32_t)i, (int32_t)(i + 1)});
    };
    // merge leaves (lw sorted via ord) with packages (in order) by weight
    size_t npk = prev.size() / 2;
    size_t pk_made = 0;
    uint64_t next_pkg_w = 0;
    while (li < m || pk_made < npk) {
      if (pk_made < npk) next_pkg_w = prev[2 * pk_made].w + prev[2 * pk_made + 1].w;
      bool take_leaf;
      if (li < m && pk_made < npk) take_leaf = prev[2 * pk_made].w + prev[2 * pk_made + 1].w >= lw[ord[li]];
      else take_leaf = li < m;
      if (take_leaf) { L[l].push_back({lw[ord[li]], (int32_t)ord[li], -1, -1}); li++; }
      else { emit_pkg(2 * pk_made); pk_made++; }
    }
  }
  // index-based reconstruction: replace packages by their children down the levels
  std::vector<uint8_t> len2(n, 0);
  std::vector<int64_t> sel;
  int64_t need = 2 * ((int64_t)m - 1);
  if (need > (int64_t)L[cap].size()) need = (int64_t)L[cap].size();
  for (int64_t i = 0; i < need; i++) sel.push_back(i);
  for (int l = cap; l >= 1; l--) {
    std::vector<int64_t> next;
    for (int64_t idx : sel) {
      const PM& it = L[l][idx];
      if (it.leaf >= 0) len2[syms[it.leaf]]++;
      else { next.push_back(it.a); next.push_back(it.b); }
    }
    sel.swap(next);
  }
  lens = len2;
}

// Canonical codes: symbols sorted by (len asc, id asc)
inline void canonical_codes(const std::vector<uint8_t>& lens, std::vector<uint32_t>& codes) {
  int n = (int)lens.size();
  codes.assign(n, 0);
  std::vector<uint32_t> order;
  for (int i = 0; i < n; i++) if (lens[i]) order.push_back((uint32_t)i);
  std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
    if (lens[a] != lens[b]) return lens[a] < lens[b];
    return a < b;
  });
  uint32_t code = 0; int prev = order.empty() ? 0 : lens[order[0]];
  for (uint32_t s : order) {
    int L = lens[s];
    code <<= (L - prev); prev = L;
    codes[s] = code; code++;
  }
}
