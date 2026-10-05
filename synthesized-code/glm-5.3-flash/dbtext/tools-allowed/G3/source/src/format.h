#pragma once
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

// Archive container: all integers little-endian, raw byte arrays.
// magic u32, scheme u8, rsv u8 x3, u32 payload_len, payload
static const uint32_t ARC_MAGIC = 0x31584244; // "DBX1"

enum Scheme : uint8_t {
  SC_CNAME = 1,
  SC_GENOME = 2,
  SC_HEX = 3,
  SC_UUID = 4,
  SC_LOC = 5,
  SC_ROWDICT = 6,
  SC_FLZ = 7,
  SC_EMAIL = 8,
  SC_FSST = 9,
};

// bit writer: LSB-first within bytes
struct BitW {
  uint8_t* p; size_t bitpos;
  void put(uint32_t v, int nbits) {
    for (int i = 0; i < nbits; i++) {
      if (v >> i & 1) p[bitpos >> 3] |= (uint8_t)(1u << (bitpos & 7));
      bitpos++;
    }
  }
};
struct BitR {
  const uint8_t* p; size_t bitpos;
  uint32_t get(int nbits) {
    uint32_t v = 0;
    for (int i = 0; i < nbits; i++) {
      if (p[bitpos >> 3] >> (bitpos & 7) & 1) v |= 1u << i;
      bitpos++;
    }
    return v;
  }
};

static inline void put_u32(uint8_t*& p, uint32_t v) { memcpy(p, &v, 4); p += 4; }
static inline uint32_t get_u32(const uint8_t*& p) { uint32_t v; memcpy(&v, p, 4); p += 4; return v; }
static inline void put_u64(uint8_t*& p, uint64_t v) { memcpy(p, &v, 8); p += 8; }
static inline uint64_t get_u64(const uint8_t*& p) { uint64_t v; memcpy(&v, p, 8); p += 8; return v; }

// FLZ tier limits (shared by encoder and decoder)
static const int FLZ_HOT_MAX = 192;      // 1-byte codes 0..191
static const int FLZ_COLD2 = 15872;      // 2-byte codes, first byte 192..253
static const int FLZ_COLD_EXT = 65536;   // 3-byte codes, first byte 254
static const int FLZ_COLD_TOTAL = FLZ_COLD2 + FLZ_COLD_EXT; // 81408
static const int FLZ_COLD_MAX = 16128;   // legacy selection cap (compat)
