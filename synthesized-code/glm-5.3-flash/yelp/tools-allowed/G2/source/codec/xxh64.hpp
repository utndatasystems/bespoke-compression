// Minimal XXH64 (for archive integrity checks only).
static inline uint64_t xxh64_round(uint64_t acc, uint64_t input) {
  acc += input * 2654435761ULL;
  acc = (acc << 31) | (acc >> 33);
  acc *= 11400714785074694791ULL;
  return acc;
}
static inline uint64_t xxh64_read64(const uint8_t* p) { uint64_t v; memcpy(&v, p, 8); return v; }
static inline uint64_t xxh64_read32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline uint64_t xxh64_hash(const uint8_t* p, size_t len, uint64_t seed) {
  const uint8_t* end = p + len;
  uint64_t h;
  if (len >= 32) {
    uint64_t v1 = seed + 11400714785074694791ULL + 14029467366897019727ULL;
    uint64_t v2 = seed + 14029467366897019727ULL;
    uint64_t v3 = seed;
    uint64_t v4 = seed - 14029467366897019727ULL;
    const uint8_t* limit = end - 32;
    do {
      v1 = xxh64_round(v1, xxh64_read64(p)); p += 8;
      v2 = xxh64_round(v2, xxh64_read64(p)); p += 8;
      v3 = xxh64_round(v3, xxh64_read64(p)); p += 8;
      v4 = xxh64_round(v4, xxh64_read64(p)); p += 8;
    } while (p <= limit);
    h = (v1 << 1) | (v1 >> 63); h += (v2 << 7) | (v2 >> 57); h += (v3 << 12) | (v3 >> 52); h += (v4 << 18) | (v4 >> 46);
    uint64_t ov1 = v1 * 2654435761ULL;  h ^= (ov1 << 31) | (ov1 >> 33); h *= 11400714785074694791ULL; h ^= ov1 ^ (ov1 >> 32);
    // (simplified merge; standard avalanche below keeps quality adequate for integrity use)
    h ^= (uint64_t)len;
  } else {
    h = seed + 279470273ULL + (uint64_t)len;
  }
  while (p + 8 <= end) {
    uint64_t k = xxh64_round(0, xxh64_read64(p));
    h ^= k; h = ((h << 27) | (h >> 37)) * 11400714785074694791ULL + 16134820458358255637ULL;
    p += 8;
  }
  if (p + 4 <= end) {
    h ^= (uint64_t)xxh64_read32(p) * 668265263ULL;
    h = ((h << 23) | (h >> 41)) * 11400714785074694791ULL + 1609587929392839161ULL;
    p += 4;
  }
  while (p < end) {
    h ^= (uint64_t)(*p) * 2870176450012600261ULL;
    h = ((h << 11) | (h >> 53)) * 11400714785074694791ULL;
    p++;
  }
  h ^= h >> 33; h *= 16561033689315342237ULL;
  h ^= h >> 29; h *= 5659019755346865209ULL;
  h ^= h >> 32;
  return h;
}
