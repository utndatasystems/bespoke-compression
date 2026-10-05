#pragma once
#include <stdint.h>

#define LB_MAGIC 0x4C424431u /* '1DBL' */
enum { ENG_TOK=0, ENG_CNAME=1, ENG_GENOME=2, ENG_HEXU32=3, ENG_UUID=4, ENG_LOC=5 };
#define LB_HDR 20
#define TOK_LOG_BLOCK 5
#define TOK_BLOCK 32
#define TOK_N1 224

static inline uint32_t rd32(const uint8_t* p){ uint32_t v; __builtin_memcpy(&v,p,4); return v; }
static inline uint64_t rd64(const uint8_t* p){ uint64_t v; __builtin_memcpy(&v,p,8); return v; }
static inline void wr32(uint8_t* p, uint32_t v){ __builtin_memcpy(p,&v,4); }
static inline void wr64(uint8_t* p, uint64_t v){ __builtin_memcpy(p,&v,8); }

/* LSB-first bit writer */
typedef struct { uint8_t* buf; size_t cap; size_t bitpos; } BitW;
static inline void bw_init(BitW& w, uint8_t* buf, size_t cap){ w.buf=buf; w.cap=cap; w.bitpos=0; }
static inline void bw_put(BitW& w, uint64_t v, uint32_t n){ /* n <= 32 */
  size_t b = w.bitpos;
  uint8_t* p = w.buf + (b>>3);
  uint32_t sh = (uint32_t)(b&7);
  uint64_t x = (v & ((n==32)?0xffffffffu:((1ull<<n)-1))) << sh;
  wr64(p, rd64(p) | x);
  w.bitpos = b + n;
}
static inline size_t bw_bytes(const BitW& w){ return (w.bitpos+7)>>3; }

/* LSB-first bit reader (caller guarantees 8-byte readability at p) */
typedef struct { const uint8_t* p; uint64_t acc; uint32_t cnt; } BitR;
static inline void br_init(BitR* r, const uint8_t* p){ r->p=p; r->acc=rd64(p); r->cnt=64; }
static inline uint64_t br_get(BitR* r, uint32_t n){
  uint64_t v = r->acc & ((n==64)?~0ull:((1ull<<n)-1));
  r->acc >>= n; r->cnt -= n;
  if (r->cnt < 32) { r->acc |= rd64(r->p+8) << r->cnt; r->p += 8; r->cnt += 64; }
  return v;
}
