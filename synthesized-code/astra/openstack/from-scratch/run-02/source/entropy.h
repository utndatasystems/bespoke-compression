#pragma once
// Independent, from-scratch byte rANS codec. No external dependencies.
// Stream format: mode byte, ULEB128 output size, followed by:
//   0: raw bytes; 1: one repeated byte;
//   2: ULEB128 alphabet size; sorted (symbol, LE16 frequency) entries,
//      omitting the final frequency (the total is 4096);
//      four LE32 rANS states, then renormalization bytes.
// All data required by decoding is in the returned byte vector.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace entropy {
namespace detail {
constexpr uint32_t scale_bits = 12;
constexpr uint32_t scale = 1u << scale_bits;
constexpr uint32_t lower = 1u << 23;

inline void put_var(std::vector<uint8_t>& out, uint64_t n) {
  while (n >= 128) { out.push_back(uint8_t(n) | 128); n >>= 7; }
  out.push_back(uint8_t(n));
}
inline bool get_var(const uint8_t*& p, const uint8_t* end, uint64_t& v) {
  v = 0;
  for (unsigned shift = 0; shift <= 63; shift += 7) {
    if (p == end) return false;
    uint8_t b = *p++;
    if (shift == 63 && (b & 0xfe)) return false;
    v |= uint64_t(b & 127) << shift;
    if (!(b & 128)) return true;
  }
  return false;
}
inline void put16(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(uint8_t(v)); out.push_back(uint8_t(v >> 8));
}
inline void put32(std::vector<uint8_t>& out, uint32_t v) {
  for (unsigned j = 0; j != 4; ++j) out.push_back(uint8_t(v >> (8*j)));
}
inline uint32_t get32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 |
         uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
}

// Encoding may be substantially slower than decoding. Fits a fresh histogram.
inline std::vector<uint8_t> encode(const uint8_t* data, size_t size) {
  std::vector<uint8_t> raw;
  raw.reserve(size + 11);
  raw.push_back(0);
  detail::put_var(raw, size);
  if (size == 0) return raw;
  raw.insert(raw.end(), data, data + size);

  std::array<uint64_t, 256> counts{};
  for (size_t i = 0; i < size; ++i) ++counts[data[i]];
  unsigned alphabet = 0, last_symbol = 0;
  for (unsigned s = 0; s < 256; ++s)
    if (counts[s]) { ++alphabet; last_symbol = s; }
  if (alphabet == 1) {
    std::vector<uint8_t> out{1};
    detail::put_var(out, size);
    out.push_back(uint8_t(last_symbol));
    return out;
  }
  if (size < 32) return raw;

  std::array<uint32_t, 256> freq{}, start{};
  uint32_t total = 0;
  for (unsigned s = 0; s < 256; ++s) {
    if (!counts[s]) continue;
    freq[s] = std::max<uint32_t>(1, uint32_t(
      (static_cast<unsigned __int128>(counts[s]) * detail::scale) / size));
    total += freq[s];
  }
  // Correct rounding by choosing the largest signed residual each time.
  while (total < detail::scale) {
    unsigned best = 0;
    __int128 best_error = -(static_cast<__int128>(1) << 126);
    for (unsigned s = 0; s < 256; ++s) if (counts[s]) {
      __int128 error = static_cast<__int128>(counts[s]) * detail::scale -
                       static_cast<__int128>(freq[s]) * size;
      if (error > best_error) { best_error = error; best = s; }
    }
    ++freq[best]; ++total;
  }
  while (total > detail::scale) {
    unsigned best = 0;
    __int128 best_error = static_cast<__int128>(1) << 126;
    for (unsigned s = 0; s < 256; ++s) if (freq[s] > 1) {
      __int128 error = static_cast<__int128>(counts[s]) * detail::scale -
                       static_cast<__int128>(freq[s]) * size;
      if (error < best_error) { best_error = error; best = s; }
    }
    --freq[best]; --total;
  }
  total = 0;
  for (unsigned s = 0; s < 256; ++s) {
    start[s] = total; total += freq[s];
  }

  // Every encoded byte consumes at most two renormalization bytes.
  // Reserve 2*n so this remains correct even for tiny, badly skewed inputs.
  std::vector<uint8_t> payload(size * 2 + 16);
  uint8_t* p = payload.data() + payload.size();
  uint32_t state[4] = {detail::lower, detail::lower,
                       detail::lower, detail::lower};
  for (size_t i = size; i-- != 0;) {
    unsigned s = data[i];
    uint32_t f = freq[s];
    uint32_t x = state[i & 3];
    const uint32_t limit = ((detail::lower >> detail::scale_bits) << 8) * f;
    while (x >= limit) { *--p = uint8_t(x); x >>= 8; }
    state[i & 3] = (x / f << detail::scale_bits) + (x % f) + start[s];
  }
  std::vector<uint8_t> out;
  out.reserve(16 + size_t(payload.data() + payload.size() - p) + alphabet*3 + 20);
  out.push_back(2);
  detail::put_var(out, size);
  detail::put_var(out, alphabet);
  unsigned entries = 0;
  for (unsigned s = 0; s < 256; ++s) if (freq[s]) {
    out.push_back(uint8_t(s));
    if (++entries != alphabet) detail::put16(out, freq[s]);
  }
  for (unsigned j = 0; j != 4; ++j) detail::put32(out, state[j]);
  out.insert(out.end(), p, payload.data() + payload.size());
  return out.size() < raw.size() ? std::move(out) : std::move(raw);
}
inline std::vector<uint8_t> encode(const std::vector<uint8_t>& data) {
  return encode(data.data(), data.size());
}

// Returns false for malformed/truncated streams or exceeding max_output.
// Exact consumption is required. max_output should be the caller's limit.
inline bool decode(const uint8_t* data, size_t size, std::vector<uint8_t>& out,
                   size_t max_output = std::numeric_limits<size_t>::max()) {
  if (size < 2 || !data) return false;
  const uint8_t* p = data;
  const uint8_t* end = data + size;
  const uint8_t mode = *p++;
  uint64_t length = 0;
  if (!detail::get_var(p, end, length) || length > max_output ||
      length > std::numeric_limits<size_t>::max()) return false;
  size_t n = size_t(length);
  if (mode == 0) {
    if (size_t(end - p) != n) return false;
    out.assign(p, end);
    return true;
  }
  if (mode == 1) {
    if (end - p != 1 || n == 0) return false;
    out.assign(n, *p);
    return true;
  }
  if (mode != 2 || n == 0) return false;

  uint64_t alphabet64 = 0;
  if (!detail::get_var(p, end, alphabet64) ||
      alphabet64 < 2 || alphabet64 > 256) return false;
  unsigned alphabet = unsigned(alphabet64);
  alignas(64) uint32_t lookup[detail::scale];
  uint32_t sum = 0;
  int previous = -1;
  for (unsigned j = 0; j < alphabet; ++j) {
    if (p == end) return false;
    unsigned s = *p++;
    if (int(s) <= previous) return false;
    previous = int(s);
    uint32_t f;
    if (j + 1 == alphabet) f = detail::scale - sum;
    else {
      if (end - p < 2) return false;
      f = uint32_t(p[0]) | uint32_t(p[1]) << 8;
      p += 2;
    }
    if (!f || f >= detail::scale || sum + f > detail::scale) return false;
    // Layout: frequency in high 12 bits, within-symbol offset in next 12.
    // Constant streams use mode 1, so frequency 4096 never needs packing.
    for (uint32_t k = 0; k < f; ++k)
      lookup[sum + k] = (f << 20) | (k << 8) | s;
    sum += f;
  }
  if (sum != detail::scale || end - p < 16) return false;
  uint32_t state[4];
  for (unsigned j = 0; j != 4; ++j) {
    state[j] = detail::get32(p); p += 4;
    if (state[j] < detail::lower || state[j] >= (detail::lower << 8)) return false;
  }
  out.resize(n);
  uint8_t* dst = out.data();
  size_t i = 0;
  // Fast bulk loop reserves the maximum 8 renormalization bytes per group.
  // No unchecked read can pass end, including in corrupt streams.
  while (n - i >= 4 && end - p >= 8) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t ea = lookup[a & (detail::scale - 1)];
    uint32_t eb = lookup[b & (detail::scale - 1)];
    uint32_t ec = lookup[c & (detail::scale - 1)];
    uint32_t ed = lookup[d & (detail::scale - 1)];
    dst[i] = uint8_t(ea); dst[i+1] = uint8_t(eb);
    dst[i+2] = uint8_t(ec); dst[i+3] = uint8_t(ed);
    a = (ea >> 20) * (a >> detail::scale_bits) + ((ea >> 8) & 4095);
    b = (eb >> 20) * (b >> detail::scale_bits) + ((eb >> 8) & 4095);
    c = (ec >> 20) * (c >> detail::scale_bits) + ((ec >> 8) & 4095);
    d = (ed >> 20) * (d >> detail::scale_bits) + ((ed >> 8) & 4095);
    if (a < detail::lower) { a = (a << 8) | *p++; if (a < detail::lower) a = (a << 8) | *p++; }
    if (b < detail::lower) { b = (b << 8) | *p++; if (b < detail::lower) b = (b << 8) | *p++; }
    if (c < detail::lower) { c = (c << 8) | *p++; if (c < detail::lower) c = (c << 8) | *p++; }
    if (d < detail::lower) { d = (d << 8) | *p++; if (d < detail::lower) d = (d << 8) | *p++; }
    state[0] = a; state[1] = b; state[2] = c; state[3] = d;
    i += 4;
  }
  for (; i < n; ++i) {
    uint32_t x = state[i & 3];
    const uint32_t e = lookup[x & (detail::scale - 1)];
    dst[i] = uint8_t(e);
    x = (e >> 20) * (x >> detail::scale_bits) + ((e >> 8) & 4095);
    while (x < detail::lower) {
      if (p == end) return false;
      x = (x << 8) | *p++;
    }
    state[i & 3] = x;
  }
  if (p != end) return false;
  for (uint32_t x : state) if (x != detail::lower) return false;
  return true;
}
inline bool decode(const std::vector<uint8_t>& data, std::vector<uint8_t>& out,
                   size_t max_output = std::numeric_limits<size_t>::max()) {
  return decode(data.data(), data.size(), out, max_output);
}
} // namespace entropy
