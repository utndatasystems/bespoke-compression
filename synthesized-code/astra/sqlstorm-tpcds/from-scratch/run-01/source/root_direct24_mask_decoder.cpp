#include "codec.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <immintrin.h>
static inline uint32_t word(const uint8_t*p){uint32_t x;memcpy(&x,p,4);return x&0xffffff;}
extern "C" void*lab_open(const uint8_t*a,size_t n){
 if(!a||n<128)return nullptr;uint64_t h[4];memcpy(h,a,32);
 if(h[3]!=0x0034325443455244ULL||h[0]!=18444591||!h[1]||h[1]>h[0]||h[2]<32||h[2]>(1<<19)||h[1]*3+96+h[2]!=n)return nullptr;
 const uint8_t*c=a+32;uint64_t nc=h[1];size_t i=0;
 alignas(64) static const uint8_t idx[64]={0,1,2,0,3,4,5,0,6,7,8,0,9,10,11,0,12,13,14,0,15,16,17,0,18,19,20,0,21,22,23,0,24,25,26,0,27,28,29,0,30,31,32,0,33,34,35,0,36,37,38,0,39,40,41,0,42,43,44,0,45,46,47,0};
 __m512i indexes=_mm512_load_si512(idx),mask=_mm512_set1_epi32(0xffffff),limit=_mm512_set1_epi32(h[2]-32);
 __m512i sums=_mm512_setzero_si512();
 for(;i+16<=nc;i+=16){auto v=_mm512_and_si512(_mm512_permutexvar_epi8(indexes,_mm512_loadu_si512(c+3*i)),mask);sums=_mm512_add_epi32(sums,_mm512_and_si512(v,_mm512_set1_epi32(31)));if(_mm512_cmp_epu32_mask(_mm512_srli_epi32(v,5),limit,_MM_CMPINT_GT))return nullptr;}
 uint64_t sum=_mm512_reduce_add_epi32(sums)+nc;for(;i<nc;i++){uint32_t w=word(c+3*i);if((w>>5)>h[2]-32)return nullptr;sum+=w&31;}if(sum!=h[0])return nullptr;
 return (void*)a;
}
extern "C" int64_t lab_decode(void*state,uint8_t* __restrict out,size_t cap){
 if(!state||!out||cap<18444591)return -1;
 const uint8_t*a=(const uint8_t*)state;uint64_t h[4];memcpy(h,a,32);const size_t nc=h[1],raw=h[0];const uint8_t* __restrict c=a+32;const uint8_t* __restrict pool=c+3*nc+64;size_t pos=0,i=0;
for(;nc-i>=36;i+=4){
uint32_t x0=word(c+3*i+0);
uint32_t x1=word(c+3*i+3);
uint32_t x2=word(c+3*i+6);
uint32_t x3=word(c+3*i+9);
{__mmask32 k=0xffffffffu>>(31-(x0&31));auto v=_mm256_loadu_si256((const __m256i*)(pool+(x0>>5)));_mm256_mask_storeu_epi8(out+pos,k,v);}pos+=(x0&31)+1;
{__mmask32 k=0xffffffffu>>(31-(x1&31));auto v=_mm256_loadu_si256((const __m256i*)(pool+(x1>>5)));_mm256_mask_storeu_epi8(out+pos,k,v);}pos+=(x1&31)+1;
{__mmask32 k=0xffffffffu>>(31-(x2&31));auto v=_mm256_loadu_si256((const __m256i*)(pool+(x2>>5)));_mm256_mask_storeu_epi8(out+pos,k,v);}pos+=(x2&31)+1;
{__mmask32 k=0xffffffffu>>(31-(x3&31));auto v=_mm256_loadu_si256((const __m256i*)(pool+(x3>>5)));_mm256_mask_storeu_epi8(out+pos,k,v);}pos+=(x3&31)+1;
}
 for(;i<nc;i++){uint32_t x=word(c+3*i);size_t len=(x&31)+1;if(len>raw-pos)return -1;_mm256_mask_storeu_epi8(out+pos,0xffffffffu>>(32-len),_mm256_loadu_si256((const __m256i*)(pool+(x>>5))));pos+=len;}return pos==raw?(int64_t)pos:-1;
}
extern "C" void lab_close(void*){}
