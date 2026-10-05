#pragma once
#include <immintrin.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
// Include the selected text.h before this file. No representation changes.
// Uses AVX512F/BW/VL/VBMI2 and POPCNT; compile for icelake-server.
namespace txt {
#ifdef __AVX512VBMI2__
struct SimdFour { __m512i bytes; __mmask64 mask; unsigned len; };
static inline SimdFour simd_four(const State&s,const uint8_t*t){
 uint64_t v;memcpy(&v,t,8);
 unsigned a=v&4095,b=(v>>12)&4095,c=(v>>24)&4095,d=(v>>36)&4095;
 __m128i x0=_mm_loadu_si128((const __m128i*)(s.dict+16*a));
 __m128i x1=_mm_loadu_si128((const __m128i*)(s.dict+16*b));
 __m128i x2=_mm_loadu_si128((const __m128i*)(s.dict+16*c));
 __m128i x3=_mm_loadu_si128((const __m128i*)(s.dict+16*d));
 __m256i lo=_mm256_inserti128_si256(_mm256_castsi128_si256(x0),x1,1);
 __m256i hi=_mm256_inserti128_si256(_mm256_castsi128_si256(x2),x3,1);
 __m512i x=_mm512_inserti64x4(_mm512_castsi256_si512(lo),hi,1);
 __m512i lens=_mm512_shuffle_epi8(x,_mm512_set1_epi8(15));
 __m512i positions=_mm512_broadcast_i32x4(_mm_setr_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15));
 __mmask64 m=_mm512_cmp_epu8_mask(positions,lens,_MM_CMPINT_LT);
 return {x,m,(unsigned)_mm_popcnt_u64(m)};
}
static inline SimdFour simd_four_gather(const State&s,const uint8_t*t){
 uint64_t v;memcpy(&v,t,8);
 __m512i packed=_mm512_set1_epi64(v);
 __m512i shifts=_mm512_setr_epi64(0,0,12,12,24,24,36,36);
 __m512i ids=_mm512_and_si512(_mm512_srlv_epi64(packed,shifts),_mm512_set1_epi64(4095));
 __m512i offsets=_mm512_or_si512(_mm512_slli_epi64(ids,4),_mm512_setr_epi64(0,8,0,8,0,8,0,8));
 __m512i x=_mm512_i64gather_epi64(offsets,s.dict,1);
 __m512i lens=_mm512_shuffle_epi8(x,_mm512_set1_epi8(15));
 __m512i positions=_mm512_broadcast_i32x4(_mm_setr_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15));
 __mmask64 m=_mm512_cmp_epu8_mask(positions,lens,_MM_CMPINT_LT);
 return {x,m,(unsigned)_mm_popcnt_u64(m)};
}
// Variant A: compress in registers and issue one ordinary unaligned store.
// Eight-token main loop exposes independent dictionary-load work.
inline int64_t decode_simd_register(const State&s,uint8_t*out,size_t cap){
 if(!(s.h->shift&256))return decode(s,out,cap);
 if(cap<s.h->raw)return -1;
 uint8_t*p=out,*end=out+cap;uint32_t i=0,n=s.h->tokens;
 while(i+8<=n&&size_t(end-p)>=128){
  const uint8_t*t=s.tok+3*(i>>1);
  SimdFour a=simd_four(s,t),b=simd_four(s,t+6);
  _mm512_storeu_si512(p,_mm512_maskz_compress_epi8(a.mask,a.bytes));p+=a.len;
  _mm512_storeu_si512(p,_mm512_maskz_compress_epi8(b.mask,b.bytes));p+=b.len;i+=8;
 }
 while(i+4<=n&&size_t(end-p)>=64){SimdFour a=simd_four(s,s.tok+3*(i>>1));_mm512_storeu_si512(p,_mm512_maskz_compress_epi8(a.mask,a.bytes));p+=a.len;i+=4;}
 for(;i<n;i++){unsigned id=token(s,i),len=s.len[id];if(size_t(end-p)<len)return -1;memcpy(p,s.dict+16*id,len);p+=len;}
 return size_t(p-out)==s.h->raw?p-out:-1;
}
inline int64_t decode_simd_gather(const State&s,uint8_t*out,size_t cap){
 if(!(s.h->shift&256))return decode(s,out,cap);
 if(cap<s.h->raw)return -1;
 uint8_t*p=out,*end=out+cap;uint32_t i=0,n=s.h->tokens;
 while(i+8<=n&&size_t(end-p)>=128){
  const uint8_t*t=s.tok+3*(i>>1);
  SimdFour a=simd_four_gather(s,t),b=simd_four_gather(s,t+6);
  _mm512_storeu_si512(p,_mm512_maskz_compress_epi8(a.mask,a.bytes));p+=a.len;
  _mm512_storeu_si512(p,_mm512_maskz_compress_epi8(b.mask,b.bytes));p+=b.len;i+=8;
 }
 while(i+4<=n&&size_t(end-p)>=64){SimdFour a=simd_four_gather(s,s.tok+3*(i>>1));_mm512_storeu_si512(p,_mm512_maskz_compress_epi8(a.mask,a.bytes));p+=a.len;i+=4;}
 for(;i<n;i++){unsigned id=token(s,i),len=s.len[id];if(size_t(end-p)<len)return -1;memcpy(p,s.dict+16*id,len);p+=len;}
 return size_t(p-out)==s.h->raw?p-out:-1;
}
// Variant B: compress directly to memory, writing precisely the phrase bytes.
inline int64_t decode_simd_store(const State&s,uint8_t*out,size_t cap){
 if(!(s.h->shift&256))return decode(s,out,cap);
 if(cap<s.h->raw)return -1;
 uint8_t*p=out,*end=out+cap;uint32_t i=0,n=s.h->tokens;
 while(i+8<=n){
  const uint8_t*t=s.tok+3*(i>>1);SimdFour a=simd_four(s,t),b=simd_four(s,t+6);
  if(size_t(end-p)<a.len+b.len)return -1;
  _mm512_mask_compressstoreu_epi8(p,a.mask,a.bytes);p+=a.len;
  _mm512_mask_compressstoreu_epi8(p,b.mask,b.bytes);p+=b.len;i+=8;
 }
 while(i+4<=n){SimdFour a=simd_four(s,s.tok+3*(i>>1));if(size_t(end-p)<a.len)return -1;_mm512_mask_compressstoreu_epi8(p,a.mask,a.bytes);p+=a.len;i+=4;}
 for(;i<n;i++){unsigned id=token(s,i),len=s.len[id];if(size_t(end-p)<len)return -1;memcpy(p,s.dict+16*id,len);p+=len;}
 return size_t(p-out)==s.h->raw?p-out:-1;
}
#else
inline int64_t decode_simd_register(const State&s,uint8_t*out,size_t cap){return decode(s,out,cap);}
inline int64_t decode_simd_store(const State&s,uint8_t*out,size_t cap){return decode(s,out,cap);}
inline int64_t decode_simd_gather(const State&s,uint8_t*out,size_t cap){return decode(s,out,cap);}
#endif
}
