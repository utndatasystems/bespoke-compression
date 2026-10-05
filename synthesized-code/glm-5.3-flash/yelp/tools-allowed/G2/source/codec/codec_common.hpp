#pragma once
// Shared format definitions for the Yelp-business JSONL columnar codec.
// Template constants live in 64-byte buffers so 16/32-byte over-reads stay in-bounds.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const char T_PREFIX_B[64] = "{\"business_id\":\"";              // 16
#define T_PREFIX (T_PREFIX_B)

static const char T_NAMESEP_B[64] = ",\"name\":\"";                // 9
#define T_NAMESEP (T_NAMESEP_B)

static const char T_NAME_B[64] = "\",\"address\":\"";                 // 13
#define T_NAME (T_NAME_B)

static const char T_ADDR_B[64] = "\",\"city\":\"";                    // 10
#define T_ADDR (T_ADDR_B)

static const char T_STATE_B[64] = ",\"state\":\"";                  // 10
#define T_STATE (T_STATE_B)

static const char T_ZIP_B[64] = ",\"postal_code\":\"";              // 16
#define T_ZIP (T_ZIP_B)

static const char T_LAT_B[64] = ",\"longitude\":";                    // 13
#define T_LAT (T_LAT_B)

static const char T_LNG_B[64] = ",\"stars\":";                        // 9
#define T_LNG (T_LNG_B)

static const char T_OPEN_B[64] = ",\"is_open\":";                     // 11
#define T_OPEN (T_OPEN_B)

static const char T_ATTR_B[64] = ",\"attributes\":";                  // 14
#define T_ATTR (T_ATTR_B)

static const char T_CAT_B[64] = ",\"categories\":";                   // 14
#define T_CAT (T_CAT_B)

static const char T_HRS_B[64] = ",\"hours\":";                        // 9
#define T_HRS (T_HRS_B)

static const char T_NULL_ATTR_B[64] = "null,\"categories\":"                ; // 18
#define T_NULL_ATTR (T_NULL_ATTR_B)

static const char T_NULL_CAT_B[64] = "null,\"hours\":";               // 13
#define T_NULL_CAT (T_NULL_CAT_B)

static const char T_NULL_HRS_B[64] = "null}\n";                                   // 6
#define T_NULL_HRS (T_NULL_HRS_B)

static const char T_ATTR_END_B[64] = "},\"categories\":";                // 15
#define T_ATTR_END (T_ATTR_END_B)

static const char T_CATQ_B[64] = "\",\"hours\":";                   // 10
#define T_CATQ (T_CATQ_B)

static const char T_HRS_END_B[64] = "}}\n";                                  // 3
#define T_HRS_END (T_HRS_END_B)


// star text fused with the following key label; each piece is exactly 19 bytes
static const char STAR_PIECES_B[9][64] = {
  "1.0,\"review_count\":",
  "1.5,\"review_count\":",
  "2.0,\"review_count\":",
  "2.5,\"review_count\":",
  "3.0,\"review_count\":",
  "3.5,\"review_count\":",
  "4.0,\"review_count\":",
  "4.5,\"review_count\":",
  "5.0,\"review_count\":"
};
#define STAR_PIECES ((const char (*)[64])STAR_PIECES_B)

static const char* const DAYS[7] = {"Monday","Tuesday","Wednesday","Thursday","Friday","Saturday","Sunday"};

#define LAB_EXPORT __attribute__((visibility("default")))

// Blob codecs
enum { BC_RAW = 0, BC_ZSTD = 1, BC_BROTLI = 2 };

#pragma pack(push, 1)
struct BlobHeader {
  uint8_t  codec;
  uint32_t csize;
  uint32_t dsize;
};
struct ArchiveHeader {
  uint8_t  magic[6];   // Y, B, I, Z, 1, NUL
  uint32_t nrows;
  uint16_t nblobs;
  uint16_t reserved;   // zero
  uint64_t out_size;   // exact reconstructed size in bytes
  uint64_t checksum;   // xxh64.hpp hash of all bytes after this header
};
#pragma pack(pop)