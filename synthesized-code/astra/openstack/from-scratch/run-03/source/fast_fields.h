#pragma once
#include <stdint.h>
#include <string.h>
#include <immintrin.h>
// Generic conversion helpers. These tables contain no dataset information.
static constexpr char ff_pairs[] =
"00010203040506070809" "10111213141516171819"
"20212223242526272829" "30313233343536373839"
"40414243444546474849" "50515253545556575859"
"60616263646566676869" "70717273747576777879"
"80818283848586878889" "90919293949596979899";
static inline void ff_pair(uint8_t* d,uint32_t v) { memcpy(d,ff_pairs+2*v,2); }
static inline void ff_decimal(uint8_t* d,uint64_t v,unsigned n) {
    while(n>=2) { uint64_t q=v/100; n-=2; memcpy(d+n,ff_pairs+2*(v-q*100),2);v=q; }
    if(n) d[0]=(uint8_t)('0'+v);
}
static inline void ff_u32_7(uint8_t* d,uint32_t v) {
    uint32_t q=v/100, r=q/100, s=r/100;
    ff_pair(d+5,v-q*100); ff_pair(d+3,q-r*100); ff_pair(d+1,r-s*100); d[0]=(uint8_t)('0'+s);
}
static inline void ff_u32_8(uint8_t* d,uint32_t v) {
    uint32_t q=v/100, r=q/100, s=r/100;
    ff_pair(d+6,v-q*100); ff_pair(d+4,q-r*100); ff_pair(d+2,r-s*100); ff_pair(d,s);
}
static inline void ff_time(uint8_t* d,uint32_t ms) {
    uint32_t s=ms/1000; uint32_t m=s/60; uint32_t h=m/60;
    ff_pair(d,h); d[2]=':'; ff_pair(d+3,m-h*60);d[5]=':';ff_pair(d+6,s-m*60);d[8]='.';
    uint32_t rem=ms-s*1000, hund=rem/100;
    d[9]=(uint8_t)('0'+hund); ff_pair(d+10,rem-hund*100);
}
// UUID bytes are in human-readable big-endian hex order.
static inline void ff_uuid(uint8_t* d,const uint8_t* src) {
    const __m128i lookup=_mm_setr_epi8('0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f');
    const __m128i lowmask=_mm_set1_epi8(15);
    __m128i x=_mm_loadu_si128((const __m128i*)src);
    __m128i hi=_mm_shuffle_epi8(lookup,_mm_and_si128(_mm_srli_epi16(x,4),lowmask));
    __m128i lo=_mm_shuffle_epi8(lookup,_mm_and_si128(x,lowmask));
    __m128i a=_mm_unpacklo_epi8(hi,lo),b=_mm_unpackhi_epi8(hi,lo);
    __m128i p=_mm_shuffle_epi8(a,_mm_setr_epi8(0,1,2,3,4,5,6,7,-128,8,9,10,11,-128,12,13));
    p=_mm_or_si128(p,_mm_setr_epi8(0,0,0,0,0,0,0,0,'-',0,0,0,0,'-',0,0));
    __m128i mid=_mm_alignr_epi8(b,a,14);
    __m128i q=_mm_shuffle_epi8(mid,_mm_setr_epi8(0,1,-128,2,3,4,5,-128,6,7,8,9,10,11,12,13));
    q=_mm_or_si128(q,_mm_setr_epi8(0,0,'-',0,0,0,0,'-',0,0,0,0,0,0,0,0));
    _mm_storeu_si128((__m128i*)d,p);_mm_storeu_si128((__m128i*)(d+16),q);
    uint32_t tail=(uint32_t)_mm_cvtsi128_si32(_mm_srli_si128(b,12));memcpy(d+32,&tail,4);
}
