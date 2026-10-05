#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>
#include <algorithm>
#include <immintrin.h>
namespace ent_base {
static constexpr uint32_t SCALE=4096, L=65536;
static inline std::vector<uint8_t> compress(const uint8_t* src,size_t n){
 std::vector<uint8_t> raw;raw.reserve(n+1);raw.push_back(0);raw.insert(raw.end(),src,src+n);if(n<512)return raw;
 uint64_t count[256]={};for(size_t i=0;i<n;++i)++count[src[i]];
 uint16_t freq[256]={},cum[256]={};unsigned total=0,nsym=0;int onesym=0;
 for(int s=0;s<256;++s)if(count[s]){++nsym;onesym=s;freq[s]=std::max<uint64_t>(1,count[s]*SCALE/n);total+=freq[s];}
 if(nsym==1)return std::vector<uint8_t>{2,(uint8_t)onesym};
 while(total<SCALE){int best=-1;int64_t err=INT64_MIN;for(int s=0;s<256;++s)if(count[s]){int64_t e=int64_t(count[s]*SCALE)-int64_t(freq[s])*n;if(e>err){err=e;best=s;}}++freq[best];++total;}
 while(total>SCALE){int best=-1;int64_t err=INT64_MIN;for(int s=0;s<256;++s)if(freq[s]>1){int64_t e=int64_t(freq[s])*n-int64_t(count[s]*SCALE);if(e>err){err=e;best=s;}}--freq[best];--total;}
 unsigned c=0;for(int s=0;s<256;++s){cum[s]=c;c+=freq[s];}
 std::vector<uint8_t> dst(529);dst[0]=4;memcpy(dst.data()+1,freq,512);
 for(int k=0;k<4;++k){uint32_t state[16];for(auto &x:state)x=L;std::vector<uint16_t> words;words.reserve(n/8);
  for(size_t block=(n+63)/64;block-->0;)for(int lane=16;lane-->0;){size_t i=block*64+k*16+lane;if(i>=n)continue;unsigned s=src[i],f=freq[s];uint32_t x=state[lane];if(x>=(uint64_t(f)<<20)){words.push_back(x);x>>=16;}state[lane]=(x/f)*SCALE+(x%f)+cum[s];}
  uint32_t size=64+words.size()*2+32;memcpy(dst.data()+513+k*4,&size,4);size_t off=dst.size();dst.resize(off+size);memcpy(dst.data()+off,state,64);uint8_t* p=dst.data()+off+64;for(size_t i=words.size();i-->0;){uint16_t w=words[i];memcpy(p,&w,2);p+=2;}
 }
 if(dst.size()>=raw.size())return raw;return dst;
}
static inline bool decompress(const uint8_t* src,size_t sz,uint8_t* out,size_t n){
 if(!sz)return false;if(src[0]==0){if(sz!=n+1)return false;if(n)memcpy(out,src+1,n);return true;}if(src[0]==2){if(sz!=2)return false;if(n)memset(out,src[1],n);return true;}
 if(src[0]!=4||sz<529+4*96)return false;uint16_t freq[256];memcpy(freq,src+1,512);alignas(64) uint32_t tab[4096];unsigned sum=0;
 for(unsigned s=0;s<256;++s){unsigned f=freq[s];if(f>4095||sum+f>4096)return false;for(unsigned j=0;j<f;++j)tab[sum+j]=(f<<20)|(j<<8)|s;sum+=f;}if(sum!=4096)return false;
 const __m512i vm=_mm512_set1_epi32(4095),vl=_mm512_set1_epi32(L);
 __m512i x[4];const uint8_t*p[4],*end[4];size_t off=529;
 for(int k=0;k<4;++k){uint32_t size;memcpy(&size,src+513+k*4,4);if(size<96||size>sz-off)return false;x[k]=_mm512_loadu_si512(src+off);if(_mm512_cmp_epu32_mask(x[k],vl,_MM_CMPINT_LT))return false;p[k]=src+off+64;end[k]=src+off+size-32;off+=size;}if(off!=sz)return false;
 size_t i=0;
 #define RANS_STEP(K) { \
  __m512i v=_mm512_i32gather_epi32(_mm512_and_si512(x[K],vm),tab,4); \
  _mm_storeu_si128((__m128i*)(out+i+K*16),_mm512_cvtepi32_epi8(v)); \
  x[K]=_mm512_add_epi32(_mm512_mullo_epi32(_mm512_srli_epi32(x[K],12),_mm512_srli_epi32(v,20)),_mm512_and_si512(_mm512_srli_epi32(v,8),vm)); \
  __mmask16 m=_mm512_cmp_epu32_mask(x[K],vl,_MM_CMPINT_LT);unsigned bytes=_mm_popcnt_u32(m)*2;if(size_t(end[K]-p[K])<bytes)return false; \
  __m256i r=_mm256_maskz_expandloadu_epi16(m,p[K]);p[K]+=bytes; \
  x[K]=_mm512_mask_mov_epi32(x[K],m,_mm512_or_si512(_mm512_slli_epi32(x[K],16),_mm512_cvtepu16_epi32(r))); \
 }
 while(i+64<=n){RANS_STEP(0) RANS_STEP(1) RANS_STEP(2) RANS_STEP(3) i+=64;}
 #undef RANS_STEP
 for(int k=0;k<4;++k){alignas(64) uint32_t states[16];_mm512_store_si512(states,x[k]);
  for(unsigned lane=0;lane<16&&i+lane<n;++lane){uint32_t v=tab[states[lane]&4095];out[i+lane]=v;states[lane]=(states[lane]>>12)*(v>>20)+((v>>8)&4095);if(states[lane]<L){if(end[k]-p[k]<2)return false;uint16_t w;memcpy(&w,p[k],2);p[k]+=2;states[lane]=(states[lane]<<16)|w;}}i+=16;
  if(p[k]!=end[k])return false;for(auto v:states)if(v!=L)return false;
 }
 return true;
}
}

namespace ent {
static inline std::vector<uint8_t> compress(const uint8_t* src,size_t n){
 auto base=ent_base::compress(src,n);if(n<1024||n>0xffffffffu)return base;size_t nz=0;for(size_t i=0;i<n;++i)nz+=src[i]!=0;if(nz>n/3||!nz)return base;
 std::vector<uint8_t>runs,values;size_t run=0;for(size_t i=0;i<n;++i)if(src[i]){while(run>=255){runs.push_back(255);run-=255;}runs.push_back(run);run=0;values.push_back(src[i]-1);}else++run;
 auto rc=ent_base::compress(runs.data(),runs.size()),vc=ent_base::compress(values.data(),values.size());if(13+rc.size()+vc.size()>=base.size())return base;
 std::vector<uint8_t> out(13);out[0]=5;uint32_t r=runs.size(),v=values.size(),z=rc.size();memcpy(out.data()+1,&r,4);memcpy(out.data()+5,&v,4);memcpy(out.data()+9,&z,4);out.insert(out.end(),rc.begin(),rc.end());out.insert(out.end(),vc.begin(),vc.end());return out;
}
static inline bool decompress(const uint8_t* src,size_t sz,uint8_t* out,size_t n){
 if(!sz)return false;if(src[0]!=5)return ent_base::decompress(src,sz,out,n);if(sz<13)return false;uint32_t nr,nv,rs;memcpy(&nr,src+1,4);memcpy(&nv,src+5,4);memcpy(&rs,src+9,4);if(nr>n||nv>n||nr<nv||rs>sz-13)return false;
 std::vector<uint8_t>runs(nr),values(nv);if(!ent_base::decompress(src+13,rs,runs.data(),nr)||!ent_base::decompress(src+13+rs,sz-13-rs,values.data(),nv))return false;if(n)memset(out,0,n);size_t p=0,q=0;
 for(size_t i=0;i<nv;++i){if(q>=nr)return false;unsigned r=runs[q++];while(r==255){p+=255;if(q>=nr)return false;r=runs[q++];}p+=r;if(p>=n||values[i]==255)return false;out[p++]=values[i]+1;}return q==nr;
}
}
