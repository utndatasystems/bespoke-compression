#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <immintrin.h>
#ifdef ENCODER
#include <vector>
#endif

// Independently addressable fixed-width binary records. All templates and
// alphabet expansion tables learned from the input are in the column payload.
static inline uint32_t sp_u24(const uint8_t* p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16); }
static inline uint32_t sp_u32(const uint8_t* p) { uint32_t v; memcpy(&v,p,4); return v; }
static inline uint64_t sp_u64(const uint8_t* p) { uint64_t v; memcpy(&v,p,8); return v; }
static inline void sp_w16(uint8_t* p,uint16_t v) { memcpy(p,&v,2); }
static inline void sp_w32(uint8_t* p,uint32_t v) { memcpy(p,&v,4); }
static inline void sp_w64(uint8_t* p,uint64_t v) { memcpy(p,&v,8); }
static inline int sp_hex(uint8_t c) { if(c>='0'&&c<='9') return c-'0'; if(c>='a'&&c<='f') return c-'a'+10; if(c>='A'&&c<='F') return c-'A'+10; return -1; }
static inline uint16_t sp_dec2(uint32_t n) { return (uint16_t)(('0'+n/10)|(('0'+n%10)<<8)); }

// c_name: twelve-byte fixed prefix, LF, 1024 three-digit expansions, then two10-bit table indices in each three-byte record.
static inline void sp_name(const uint8_t* p,uint32_t i,uint8_t* o) {
 const uint32_t n=sp_u24(p+4109+(size_t)i*3);
 sp_w64(o,sp_u64(p)); sp_w32(o+8,sp_u32(p+8));
 uint32_t hi=n&1023,lo=(n>>10)&1023;
 sp_w32(o+12,sp_u32(p+13+(hi&1023)*4)); sp_w32(o+15,sp_u32(p+13+lo*4));
}
// genome: alphabet + LF + 256 four-character expansions, then 18-bit records.
static inline void sp_genome(const uint8_t* p,uint32_t i,uint8_t* o) {
 const uint8_t* x=p+1029+(size_t)i*3;
 sp_w32(o,sp_u32(p+5+(size_t)x[0]*4));
 sp_w32(o+4,sp_u32(p+5+(size_t)x[1]*4));
 sp_w16(o+8,(uint16_t)(p[x[2]]|((uint16_t)p[4]<<8)));
}
static inline uint64_t sp_hex8(uint32_t x,const uint8_t* table) {
 return (uint64_t)(uint16_t)(table[x>>28]|((uint16_t)table[(x>>24)&15]<<8)) |
 ((uint64_t)(uint16_t)(table[(x>>20)&15]|((uint16_t)table[(x>>16)&15]<<8))<<16) |
 ((uint64_t)(uint16_t)(table[(x>>12)&15]|((uint16_t)table[(x>>8)&15]<<8))<<32) |
 ((uint64_t)(uint16_t)(table[(x>>4)&15]|((uint16_t)table[x&15]<<8))<<48);
}
static inline uint64_t sp_hex8simd(uint32_t x,const uint8_t* table) {
 x=__builtin_bswap32(x);
 const __m128i a=_mm_cvtsi32_si128(x), mask=_mm_set1_epi8(15);
 const __m128i hi=_mm_and_si128(_mm_srli_epi16(a,4),mask),lo=_mm_and_si128(a,mask);
 const __m128i n=_mm_unpacklo_epi8(hi,lo);
 return (uint64_t)_mm_cvtsi128_si64(_mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)table),n));
}
// UUID: 37-byte template, hex alphabet, 11 binary bytes per row, 16 guard bytes.
static inline void sp_uuid(const uint8_t* p,uint32_t i,uint8_t* o) {
 const uint8_t* x=p+53+(size_t)i*11;
 const __m128i a=_mm_loadu_si128((const __m128i*)x), mask=_mm_set1_epi8(15);
 const __m128i lut=_mm_loadu_si128((const __m128i*)(p+37));
 const __m128i hi=_mm_shuffle_epi8(lut,_mm_and_si128(_mm_srli_epi16(a,4),mask));
 const __m128i lo=_mm_shuffle_epi8(lut,_mm_and_si128(a,mask));
 const __m128i c0=_mm_unpacklo_epi8(hi,lo),c1=_mm_unpackhi_epi8(hi,lo);
 const __m256i chars=_mm256_inserti128_si256(_mm256_castsi128_si256(c0),c1,1);
 const __m256i idx=_mm256_setr_epi8(0,0,0,1,2,3,4,5,0,0,0,0,0,0,0,0,0,0,0,6,7,8,9,0,10,11,12,13,14,15,16,17);
 const __m256i packed=_mm256_permutexvar_epi8(idx,chars);
 const __m256i base=_mm256_loadu_si256((const __m256i*)p);
 _mm256_storeu_si256((__m256i*)o,_mm256_mask_blend_epi8((__mmask32)0xff7800fcu,base,packed));
 sp_w32(o+32,(uint32_t)_mm_cvtsi128_si32(_mm_srli_si128(c1,2))); o[36]=p[36];
}
static inline bool special_validate(uint32_t type,const uint8_t* p,size_t z,uint32_t n,size_t raw) {
 if(type==1) return z==4109+(size_t)n*3&&raw==(size_t)n*19;
 if(type==2) return z==1029+(size_t)n*3&&raw==(size_t)n*10;
 if(type==3) return z==17+(size_t)n*4&&raw>=(size_t)n*2&&raw<=(size_t)n*9;
 if(type==4) return z==69+(size_t)n*11&&raw==(size_t)n*37;
 return false;
}
static inline int64_t special_decode(uint32_t type,const uint8_t* p,size_t z,uint32_t n,uint8_t* o,size_t cap) {
 if(type==1) { if(cap<(size_t)n*19) return -1; for(uint32_t i=0;i<n;i++) sp_name(p,i,o+(size_t)i*19); return (size_t)n*19; }
 if(type==2) { if(cap<(size_t)n*10) return -1; for(uint32_t i=0;i<n;i++) sp_genome(p,i,o+(size_t)i*10); return (size_t)n*10; }
 if(type==3) {
  size_t at=0; uint32_t i=0;
  const __m128i reverse=_mm_setr_epi8(3,2,1,0,7,6,5,4,11,10,9,8,15,14,13,12);
  const __m256i lut=_mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i*)p));
  const __m512i positions=_mm512_setr_epi64(0x0706050403020100ull,0x0e0d0c0b0a090800ull,0x151413121110000full,0x1c1b1a1918001716ull,0x00000000001f1e1dull,0,0,0);
  for(;i+4<=n && cap-at>=36;i+=4) {
   const uint8_t* in=p+17+(size_t)i*4;
   __m128i a=_mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)in),reverse);
   __m256i wide=_mm256_cvtepu8_epi16(a);
   __m256i nib=_mm256_or_si256(_mm256_srli_epi16(wide,4),_mm256_slli_epi16(_mm256_and_si256(wide,_mm256_set1_epi16(15)),8));
   __m256i chars=_mm256_shuffle_epi8(lut,nib);
   __m512i rows=_mm512_permutexvar_epi8(positions,_mm512_castsi256_si512(chars));
   rows=_mm512_mask_set1_epi8(rows,(__mmask64)((1ull<<8)|(1ull<<17)|(1ull<<26)|(1ull<<35)),p[16]);
   unsigned s0=__builtin_clz(sp_u32(in)|1)/4,s1=__builtin_clz(sp_u32(in+4)|1)/4,s2=__builtin_clz(sp_u32(in+8)|1)/4,s3=__builtin_clz(sp_u32(in+12)|1)/4;
   uint64_t mask=((511ull<<s0)&511)|(((511ull<<s1)&511)<<9)|(((511ull<<s2)&511)<<18)|(((511ull<<s3)&511)<<27);
   _mm512_mask_compressstoreu_epi8(o+at,mask,rows);at+=36-s0-s1-s2-s3;
  }
  for(;i<n;i++) {
   uint32_t x=sp_u32(p+17+(size_t)i*4); unsigned len=(32-__builtin_clz(x|1)+3)/4;
   if(at+len+1>cap) return -1;
   uint64_t y=sp_hex8simd(x,p)>>((8-len)*8);
   if(cap-at>=8) sp_w64(o+at,y); else memcpy(o+at,&y,len);
   o[at+len]=p[16]; at+=len+1;
  } return at;
 }
 if(type==4) { if(cap<(size_t)n*37) return -1; for(uint32_t i=0;i<n;i++) sp_uuid(p,i,o+(size_t)i*37); return (size_t)n*37; }
 return -1;
}
static inline int64_t special_rows(uint32_t type,const uint8_t* p,size_t z,uint32_t n,const uint64_t* ids,size_t count,uint8_t* o,size_t cap,uint64_t* offsets) {
 offsets[0]=0; if(count&&ids[count-1]>=n) return -1;
 if(type==1||type==2||type==4) {
  const size_t len=type==1?19:type==2?10:37;
  if(count>cap/len) return -1;
  if(type==1) for(size_t j=0;j<count;j++) { if(ids[j]>=n)return -1; sp_name(p,(uint32_t)ids[j],o+j*len); offsets[j+1]=(j+1)*len; }
  else if(type==2) for(size_t j=0;j<count;j++) { if(ids[j]>=n)return -1; sp_genome(p,(uint32_t)ids[j],o+j*len); offsets[j+1]=(j+1)*len; }
  else for(size_t j=0;j<count;j++) { if(ids[j]>=n)return -1; sp_uuid(p,(uint32_t)ids[j],o+j*len); offsets[j+1]=(j+1)*len; }
  return count*len;
 }
 if(type==3) {
  size_t at=0,j=0; bool contiguous=count==n;
  if(contiguous) for(size_t k=0;k<count;k++) if(ids[k]!=k) {contiguous=false;break;}
  const __m128i reverse=_mm_setr_epi8(3,2,1,0,7,6,5,4,11,10,9,8,15,14,13,12);
  const __m256i lut=_mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i*)p));
  const __m512i positions=_mm512_setr_epi64(0x0706050403020100ull,0x0e0d0c0b0a090800ull,0x151413121110000full,0x1c1b1a1918001716ull,0x00000000001f1e1dull,0,0,0);
  for(;j+4<=count && cap-at>=36;j+=4) {
   uint64_t i0=ids[j],i1=ids[j+1],i2=ids[j+2],i3=ids[j+3];
   if(i0>=n||i1>=n||i2>=n||i3>=n) return -1;
   __m128i values=contiguous?_mm_loadu_si128((const __m128i*)(p+17+j*4)):_mm_setr_epi32(sp_u32(p+17+i0*4),sp_u32(p+17+i1*4),sp_u32(p+17+i2*4),sp_u32(p+17+i3*4));
   __m128i a=_mm_shuffle_epi8(values,reverse);
   __m256i wide=_mm256_cvtepu8_epi16(a);
   __m256i nib=_mm256_or_si256(_mm256_srli_epi16(wide,4),_mm256_slli_epi16(_mm256_and_si256(wide,_mm256_set1_epi16(15)),8));
   __m256i chars=_mm256_shuffle_epi8(lut,nib);
   __m512i rows=_mm512_permutexvar_epi8(positions,_mm512_castsi256_si512(chars));
   rows=_mm512_mask_set1_epi8(rows,(__mmask64)((1ull<<8)|(1ull<<17)|(1ull<<26)|(1ull<<35)),p[16]);
   unsigned s0=__builtin_clz((uint32_t)_mm_cvtsi128_si32(values)|1)/4,s1=__builtin_clz((uint32_t)_mm_extract_epi32(values,1)|1)/4,s2=__builtin_clz((uint32_t)_mm_extract_epi32(values,2)|1)/4,s3=__builtin_clz((uint32_t)_mm_extract_epi32(values,3)|1)/4;
   uint64_t mask=((511ull<<s0)&511)|(((511ull<<s1)&511)<<9)|(((511ull<<s2)&511)<<18)|(((511ull<<s3)&511)<<27);
   _mm512_mask_compressstoreu_epi8(o+at,mask,rows);
   offsets[j+1]=at+9-s0;offsets[j+2]=at+18-s0-s1;offsets[j+3]=at+27-s0-s1-s2;at+=36-s0-s1-s2-s3;offsets[j+4]=at;
  }
  for(;j<count;j++) {
   if(ids[j]>=n)return -1;
   uint32_t x=sp_u32(p+17+(size_t)ids[j]*4); unsigned len=(32-__builtin_clz(x|1)+3)/4;
   if(at+len+1>cap) return -1;
   uint64_t y=sp_hex8simd(x,p)>>((8-len)*8);
   if(cap-at>=8) sp_w64(o+at,y); else memcpy(o+at,&y,len);
   o[at+len]=p[16]; at+=len+1; offsets[j+1]=at;
  } return at;
 }
 return -1;
}
#ifdef ENCODER
static inline bool special_encode(const uint8_t* raw,size_t size,std::vector<uint8_t>& payload,uint32_t& type,uint32_t& nrows) {
 if(!size) return false;
 // Try complete layouts; a trial is accepted only after every original byte
 // has been checked against the inverse representation.
 if(size%19==0 && size>=19 && raw[18]=='\n') {
  bool ok=true; uint32_t n=size/19; std::vector<uint8_t> p(4109+(size_t)n*3);
  memcpy(p.data(),raw,12); p[12]=raw[18];
  for(unsigned j=0;j<1024;j++) {p[13+j*4]='0'+(j/100)%10;p[14+j*4]='0'+(j/10)%10;p[15+j*4]='0'+j%10;p[16+j*4]=raw[18];}
  for(uint32_t i=0;i<n&&ok;i++) {
   const uint8_t* x=raw+(size_t)i*19; if(memcmp(x,raw,12)||x[18]!='\n') {ok=false;break;}
   uint32_t v=0; for(int j=12;j<18;j++) {if(x[j]<'0'||x[j]>'9') {ok=false;break;} v=v*10+x[j]-'0';}
   v=(v/1000)|((v%1000)<<10);
   p[4109+(size_t)i*3]=v; p[4110+(size_t)i*3]=v>>8; p[4111+(size_t)i*3]=v>>16;
  }
  if(ok) {payload.swap(p);type=1;nrows=n;return true;}
 }
 if(size%10==0 && size>=10 && raw[9]=='\n') {
  bool ok=true; uint32_t n=size/10; std::vector<uint8_t> p(1029+(size_t)n*3); uint8_t alpha[4]={'a','c','g','t'};
  memcpy(p.data(),alpha,4);p[4]='\n';
  for(unsigned i=0;i<256;i++) for(unsigned j=0;j<4;j++) p[5+i*4+j]=alpha[(i>>(j*2))&3];
  for(uint32_t i=0;i<n&&ok;i++) {
   const uint8_t* x=raw+(size_t)i*10;if(x[9]!='\n') {ok=false;break;}
   uint32_t v=0;for(unsigned j=0;j<9;j++) {unsigned k=0;while(k<4&&alpha[k]!=x[j]) k++;if(k==4) {ok=false;break;}v|=k<<(j*2);}
   p[1029+(size_t)i*3]=v;p[1030+(size_t)i*3]=v>>8;p[1031+(size_t)i*3]=v>>16;
  }
  if(ok) {payload.swap(p);type=2;nrows=n;return true;}
 }
 if(size%37==0 && size>=37 && raw[36]=='\n') {
  bool ok=true; uint32_t n=size/37; std::vector<uint8_t> p(69+(size_t)n*11,0);memcpy(p.data(),raw,37);
  memcpy(p.data()+37,"0123456789abcdef",16);
  const unsigned positions[11]={2,4,6,19,21,24,26,28,30,32,34};
  bool variable[37]={};for(unsigned j:positions) variable[j]=variable[j+1]=true;
  for(uint32_t i=0;i<n&&ok;i++) {
   const uint8_t* x=raw+(size_t)i*37;for(unsigned j=0;j<37;j++) if(!variable[j]&&x[j]!=raw[j]) {ok=false;break;}
   for(unsigned j=0;j<11&&ok;j++) {int a=sp_hex(x[positions[j]]),b=sp_hex(x[positions[j]+1]);if(a<0||b<0||p[37+a]!=x[positions[j]]||p[37+b]!=x[positions[j]+1]) {ok=false;break;}p[53+(size_t)i*11+j]=(a<<4)|b;}
  }
  if(ok) {payload.swap(p);type=4;nrows=n;return true;}
 }
 {
  bool ok=true;std::vector<uint8_t> p(17);memcpy(p.data(),"0123456789ABCDEF",16);p[16]='\n';size_t at=0;uint32_t n=0;
  while(at<size&&ok) {unsigned digits=0;uint32_t v=0;const size_t begin=at;
   while(at<size&&raw[at]!='\n') {int x=sp_hex(raw[at]);if(x<0||raw[at]!=p[x]||digits==8) {ok=false;break;}v=(v<<4)|x;digits++;at++;}
   if(!ok||at==size||!digits||(digits>1&&raw[begin]=='0')) {ok=false;break;}
   at++;size_t old=p.size();p.resize(old+4);memcpy(p.data()+old,&v,4);n++;
  }
  if(ok) {payload.swap(p);type=3;nrows=n;return true;}
 }
 return false;
}
#endif
