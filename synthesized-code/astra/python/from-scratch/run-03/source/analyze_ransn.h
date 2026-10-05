#pragma once
#include <immintrin.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>
namespace aransn {
#ifndef ARANS_LANES
#define ARANS_LANES 64
#endif
static constexpr unsigned LANES=ARANS_LANES;
static constexpr uint32_t LOW=65536;
// Wire: 256 LE u16 normalized frequencies, LANES LE u32 states, reverse-produced
// LE u16 renormalization words, 32 zero padding bytes. Symbol count is external.
static inline std::vector<uint8_t> encode(const uint8_t*p,size_t n,unsigned bits){
 uint64_t cnt[256]={};for(size_t i=0;i<n;i++)cnt[p[i]]++;uint16_t f[256]={},base[256];unsigned sum=0,tot=1u<<bits;
 for(unsigned c=0;c<256;c++)if(cnt[c]){f[c]=std::max<uint64_t>(1,cnt[c]*tot/std::max<size_t>(n,1));sum+=f[c];}if(!n){f[0]=tot;sum=tot;}
 while(sum<tot){unsigned best=0;int64_t score=INT64_MIN;for(unsigned c=0;c<256;c++)if(cnt[c]){int64_t e=(int64_t)(cnt[c]*tot)-(int64_t)(uint64_t(f[c])*n);if(e>score){score=e;best=c;}}f[best]++;sum++;}
 while(sum>tot){unsigned best=0;int64_t score=INT64_MIN;for(unsigned c=0;c<256;c++)if(f[c]>1){int64_t e=(int64_t)(uint64_t(f[c])*n)-(int64_t)(cnt[c]*tot);if(e>score){score=e;best=c;}}f[best]--;sum--;}
 sum=0;for(unsigned c=0;c<256;c++){base[c]=sum;sum+=f[c];}
 uint32_t x[LANES];for(auto&v:x)v=LOW;std::vector<uint16_t> rev;rev.reserve(n/2);
 for(size_t i=n;i-->0;){uint32_t&v=x[i&(LANES-1)];unsigned c=p[i],fr=f[c];uint64_t lim=(uint64_t(LOW>>bits)<<16)*fr;if(v>=lim){rev.push_back(v);v>>=16;}v=(v/fr<<bits)+(v%fr)+base[c];}
 std::vector<uint8_t> out(544+LANES*4+rev.size()*2);memcpy(out.data(),f,512);memcpy(out.data()+512,x,LANES*4);for(size_t i=0;i<rev.size();i++)memcpy(out.data()+512+LANES*4+i*2,&rev[rev.size()-1-i],2);return out;
}
struct Decoder {
 alignas(64) uint32_t table[2048];__m512i x[LANES/16];const uint8_t*p;const uint8_t*end;unsigned bits,mask;size_t lane=0;
 bool init(const uint8_t*src,size_t size,unsigned precision){
  if(size<544+LANES*4||precision<8||precision>11)return false;bits=precision;mask=(1u<<bits)-1;unsigned base=0;
  for(unsigned c=0;c<256;c++){uint16_t f;memcpy(&f,src+c*2,2);if(base+f>mask+1)return false;for(unsigned j=0;j<f;j++)table[base+j]=(uint32_t(f)<<20)|(j<<8)|c;base+=f;}if(base!=mask+1)return false;
  for(unsigned j=0;j<LANES/16;j++){x[j]=_mm512_loadu_si512(src+512+j*64);if(_mm512_cmplt_epu32_mask(x[j],_mm512_set1_epi32(LOW)))return false;}p=src+512+LANES*4;end=src+size-32;lane=0;return true;
 }
 bool finish()const{if(p!=end)return false;for(unsigned j=0;j<LANES/16;j++)if(_mm512_cmpeq_epi32_mask(x[j],_mm512_set1_epi32(LOW))!=65535)return false;return true;}
 bool decode(uint8_t*out,size_t n){
  size_t i=0;__m512i xm[LANES/16],slots[LANES/16],e[LANES/16];
  for(unsigned j=0;j<LANES/16;j++)xm[j]=x[j];
  const __m512i vm=_mm512_set1_epi32(mask),vb=_mm512_set1_epi32(bits),v4095=_mm512_set1_epi32(4095),low=_mm512_set1_epi32(LOW);
  if(lane){alignas(64) uint32_t states[LANES];for(unsigned j=0;j<LANES/16;j++)_mm512_store_si512(states+j*16,xm[j]);while(i<n&&lane){uint32_t v=states[lane],code=table[v&mask];out[i++]=code;v=(code>>20)*(v>>bits)+((code>>8)&4095);if(v<LOW){if(p>end||size_t(end-p)<2)return false;uint16_t w;memcpy(&w,p,2);p+=2;v=(v<<16)|w;}states[lane]=v;lane=(lane+1)&(LANES-1);}for(unsigned j=0;j<LANES/16;j++)xm[j]=_mm512_load_si512(states+j*16);}
  for(;i+LANES<=n;i+=LANES){
   #pragma GCC unroll 8
   for(unsigned j=0;j<LANES/16;j++){slots[j]=_mm512_and_si512(xm[j],vm);e[j]=_mm512_i32gather_epi32(slots[j],table,4);}
   #pragma GCC unroll 8
   for(unsigned j=0;j<LANES/16;j++){__m512i freq=_mm512_srli_epi32(e[j],20),base=_mm512_and_si512(_mm512_srli_epi32(e[j],8),v4095);xm[j]=_mm512_add_epi32(_mm512_mullo_epi32(freq,_mm512_srlv_epi32(xm[j],vb)),base);_mm_storeu_si128((__m128i*)(out+i+j*16),_mm512_cvtepi32_epi8(e[j]));}
   #pragma GCC unroll 8
   for(unsigned j=0;j<LANES/16;j++){__mmask16 need=_mm512_cmplt_epu32_mask(xm[j],low);if(p>end)return false;__m512i words=_mm512_cvtepu16_epi32(_mm256_loadu_si256((const __m256i*)p));words=_mm512_maskz_expand_epi32(need,words);xm[j]=_mm512_mask_mov_epi32(xm[j],need,_mm512_or_si512(_mm512_slli_epi32(xm[j],16),words));p+=__builtin_popcount((unsigned)need)*2;}
  }
  if(i<n){alignas(64) uint32_t states[LANES];for(unsigned j=0;j<LANES/16;j++)_mm512_store_si512(states+j*16,xm[j]);while(i<n){uint32_t v=states[lane],code=table[v&mask];out[i++]=code;v=(code>>20)*(v>>bits)+((code>>8)&4095);if(v<LOW){if(p>end||size_t(end-p)<2)return false;uint16_t w;memcpy(&w,p,2);p+=2;v=(v<<16)|w;}states[lane]=v;lane=(lane+1)&(LANES-1);}for(unsigned j=0;j<LANES/16;j++)xm[j]=_mm512_load_si512(states+j*16);}
  for(unsigned j=0;j<LANES/16;j++)x[j]=xm[j];return p<=end;
 }
};
static inline bool decode(const uint8_t*src,size_t size,uint8_t*out,size_t n,unsigned bits){Decoder d;return d.init(src,size,bits)&&d.decode(out,n)&&d.finish();}
}
