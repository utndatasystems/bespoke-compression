#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <queue>
#include <algorithm>

// Byte-pair encoder trained across all rows of one column (cross-row dictionary).
struct Bpe {
  std::vector<std::string> toks;                 // token id -> bytes (0..255 are the raw bytes)
  std::vector<std::vector<uint32_t>> row_seq;    // final tokenization per row

  bool train(const std::vector<std::string>& rows, uint32_t max_vocab, size_t max_token_len) {
    toks.clear(); row_seq.clear();
    toks.resize(256);
    for (int i = 0; i < 256; i++) toks[i].assign(1, (char)i);
    size_t n_inst = 0;
    for (auto& r : rows) n_inst += r.size();
    std::vector<uint32_t> itok; itok.reserve(n_inst);
    std::vector<int32_t> iprev, inext; iprev.reserve(n_inst); inext.reserve(n_inst);
    std::vector<uint32_t> irow; irow.reserve(n_inst);
    std::vector<uint8_t> ialive; ialive.reserve(n_inst);
    std::vector<int32_t> head(rows.size(), -1);
    auto new_inst = [&](uint32_t t, uint32_t row) {
      uint32_t id = (uint32_t)itok.size();
      itok.push_back(t); iprev.push_back(-1); inext.push_back(-1);
      irow.push_back(row); ialive.push_back(1);
      return id;
    };
    for (size_t r = 0; r < rows.size(); r++) {
      const std::string& s = rows[r];
      int32_t prev = -1;
      for (size_t i = 0; i < s.size(); i++) {
        uint32_t id = new_inst((uint8_t)s[i], (uint32_t)r);
        if (prev >= 0) { inext[prev] = (int32_t)id; iprev[id] = prev; }
        else head[r] = (int32_t)id;
        prev = (int32_t)id;
      }
    }
    auto pkey = [](uint32_t a, uint32_t b) -> uint64_t { return ((uint64_t)a << 32) | b; };
    std::unordered_map<uint64_t, uint32_t> pcnt;
    std::unordered_map<uint64_t, std::vector<uint32_t>> ppos;
    pcnt.reserve(n_inst * 2); ppos.reserve(n_inst);
    for (uint32_t id = 0; id < itok.size(); id++) {
      int32_t nx = inext[id];
      if (nx >= 0) {
        uint64_t k = pkey(itok[id], itok[nx]);
        pcnt[k]++;
        ppos[k].push_back(id);
      }
    }
    using QE = std::pair<uint32_t, uint64_t>; // (count, key)
    std::priority_queue<QE> heap;
    for (auto& kv : pcnt) if (kv.second > 1) heap.push({kv.second, kv.first});
    size_t max_tok = 256;
    auto grow = [&]() {
      uint32_t id = (uint32_t)itok.size(); // token id == instance-space independent
      (void)id;
    };
    (void)grow;
    while (toks.size() < max_vocab && !heap.empty()) {
      QE top = heap.top(); heap.pop();
      uint64_t key = top.second;
      uint32_t cnt = pcnt[key];
      if (cnt != top.first || cnt < 2) continue;
      uint32_t a = (uint32_t)(key >> 32), b = (uint32_t)(key & 0xffffffffu);
      size_t la = toks[a].size(), lb = toks[b].size();
      if (la + lb > max_token_len) continue;
      int64_t gain = (int64_t)(cnt - 1) * (int64_t)(la + lb) - (int64_t)(la + lb) - 4;
      if (gain <= 0) break;
      uint32_t c = (uint32_t)toks.size();
      toks.push_back(toks[a] + toks[b]);
      pcnt[key] = 0;
      auto it = ppos.find(key);
      std::vector<uint32_t> pos;
      if (it != ppos.end()) { pos.swap(it->second); ppos.erase(it); }
      for (uint32_t i : pos) {
        if (!ialive[i] || itok[i] != a) continue;
        int32_t j = inext[i];
        if (j < 0 || !ialive[j] || itok[j] != b) continue;
        int32_t p = iprev[i], nx2 = inext[j];
        uint32_t m = new_inst(c, irow[i]);
        if (p >= 0) { inext[p] = (int32_t)m; iprev[m] = p; }
        else head[irow[i]] = (int32_t)m;
        if (nx2 >= 0) { iprev[nx2] = (int32_t)m; inext[m] = nx2; }
        ialive[i] = 0; ialive[j] = 0;
        if (p >= 0) {
          uint64_t k2 = pkey(itok[p], a);
          if (pcnt[k2]) pcnt[k2]--;
          uint64_t k3 = pkey(itok[p], c);
          uint32_t c3 = ++pcnt[k3];
          ppos[k3].push_back(p);
          if (c3 >= 2) heap.push({c3, k3});
        }
        if (nx2 >= 0) {
          uint64_t k2 = pkey(b, itok[nx2]);
          if (pcnt[k2]) pcnt[k2]--;
          uint64_t k3 = pkey(c, itok[nx2]);
          uint32_t c3 = ++pcnt[k3];
          ppos[k3].push_back(m);
          if (c3 >= 2) heap.push({c3, k3});
        }
      }
    }
    // materialize rows
    row_seq.resize(rows.size());
    for (size_t r = 0; r < rows.size(); r++) {
      int32_t id = head[r];
      while (id >= 0) { row_seq[r].push_back(itok[id]); id = inext[id]; }
    }
    return true;
  }
};
