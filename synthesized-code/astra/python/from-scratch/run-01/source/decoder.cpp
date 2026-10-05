#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <immintrin.h>
#include "codec.h"
typedef uint32_t U32 __attribute__((aligned(1),may_alias));
typedef uint16_t U16 __attribute__((aligned(1),may_alias));
struct __attribute__((packed)) Header{uint64_t magic,rawsize,tokens,dictsize;};
extern "C" void* lab_open(const uint8_t*a,size_t size){if(!a||size<sizeof(Header)+262144)return nullptr;Header h;memcpy(&h,a,sizeof h);if(h.magic!=0x314650424d415247ULL||h.dictsize<64||h.dictsize>10000000||size<sizeof h+262144+h.dictsize||h.tokens>(size-sizeof h-262144-h.dictsize)/2)return nullptr;const U32*m=(const U32*)(a+sizeof h);for(unsigned i=0;i<65536;i++){uint32_t n=m[i]>>24,off=m[i]&0xffffff;if(!n||n>16||off+64>h.dictsize)return nullptr;}return (void*)a;}
static inline void fastcopy(uint8_t*o,const uint8_t*p,uint32_t){_mm_storeu_si128((__m128i*)o,_mm_loadu_si128((const __m128i*)p));}
extern "C" int64_t lab_decode(void* __restrict__ state,uint8_t* __restrict__ output,size_t capacity){if(!state)return -1;const Header*h=(const Header*)state;if(capacity<h->rawsize)return -1;const U32*meta=(const U32*)((const uint8_t*)state+sizeof(Header));const uint8_t*data=(const uint8_t*)meta+262144;const U16*ip=(const U16*)(data+h->dictsize),*end=ip+h->tokens;uint8_t*op=output;while(end-ip>=8 && size_t(op-output)+128<=h->rawsize){uint32_t m[8];for(int j=0;j<8;j++)m[j]=meta[ip[j]];for(int j=0;j<8;j++){fastcopy(op,data+(m[j]&0xffffff),m[j]>>24);op+=m[j]>>24;}ip+=8;}while(ip<end){uint32_t m=meta[*ip++],n=m>>24;if(size_t(op-output)+n>h->rawsize)return -1;if(size_t(op-output)+64<=h->rawsize)fastcopy(op,data+(m&0xffffff),n);else memcpy(op,data+(m&0xffffff),n);op+=n;}return op-output==h->rawsize?h->rawsize:-1;}
extern "C" void lab_close(void*){}
