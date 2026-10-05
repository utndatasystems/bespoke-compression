#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <immintrin.h>
#ifdef ENCODER
#include <vector>
#endif
struct LOC { const uint8_t* data=nullptr; uint32_t n=0, raw=0; };
static inline bool loc_open(const uint8_t* a,size_t sz,LOC& s) {
 if(sz<16 || memcmp(a,"LOC13v1",8)) return false;
 memcpy(&s.n,a+8,4); memcpy(&s.raw,a+12,4);
 if(sz!=16+(size_t)s.n*13) return false;
 uint64_t rawsum=0;
 for(uint32_t i=0;i<s.n;i++) {
  const uint8_t* r=a+16+(size_t)i*13;
  unsigned xc=(r[12]>>1)&7,yc=(r[12]>>4)&7;
  if(xc==7) {rawsum+=5;continue;}
  if(yc>4)return false;
  rawsum+=41-xc-yc;
 }
 if(rawsum!=s.raw)return false;
 s.data=a+16; return true;
}
static inline __m128i loc_decimal16(uint64_t x) {
 uint32_t h=x/100000000, l=x%100000000;
 __m128i a=_mm_setr_epi32(h/10000,h%10000,l/10000,l%10000);
 __m128i hi=_mm_srli_epi16(_mm_mulhi_epu16(a,_mm_set1_epi16(5243)),3);
 __m128i lo=_mm_sub_epi16(a,_mm_mullo_epi16(hi,_mm_set1_epi16(100)));
 __m128i pairs=_mm_or_si128(hi,_mm_slli_epi32(lo,16));
 __m128i tens=_mm_mulhi_epu16(pairs,_mm_set1_epi16(6554));
 __m128i ones=_mm_sub_epi16(pairs,_mm_mullo_epi16(tens,_mm_set1_epi16(10)));
 return _mm_add_epi8(_mm_or_si128(tens,_mm_slli_epi16(ones,8)),_mm_set1_epi8('0'));
}
static inline void loc_store(uint8_t* p,__m128i v,unsigned len) {
#ifdef __AVX512VL__
 _mm_mask_storeu_epi8(p,(__mmask16)((1u<<len)-1),v);
#else
 alignas(16) uint8_t tmp[16];_mm_store_si128((__m128i*)tmp,v);memcpy(p,tmp,len);
#endif
}
static inline uint32_t loc_one(const uint8_t* r,uint8_t* out) {
 uint64_t a,b; memcpy(&a,r,8); memcpy(&b,r+5,8);
 // top bits of the overlapping little-endian words hold lengths and longitude integer.
 unsigned xc=(b>>57)&7, yc=(b>>60)&7;
 if(xc==7) {memcpy(out,"NULL\n",5);return 5;}
 unsigned xl=15-xc, yl=14-yc;
 uint64_t x=a&((1ULL<<50)-1), y=(b>>10)&((1ULL<<47)-1);
 __m128i xv=_mm_srli_si128(loc_decimal16(x),1);
 __m128i yv=_mm_srli_si128(loc_decimal16(y),2);
 memcpy(out,"(40.",4);
 loc_store(out+4,xv,xl);
 uint8_t* p=out+4+xl;
 memcpy(p,", -7",4);p[4]='3'+(b>>63);p[5]='.';
 loc_store(p+6,yv,yl);
 memcpy(p+6+yl,")\n",2);
 return 12+xl+yl;
}
static inline int64_t loc_decode(LOC& s,uint8_t* out,size_t cap) {
 if(cap<s.raw)return -1;
 uint8_t* p=out;
 for(uint32_t i=0;i<s.n;i++) p+=loc_one(s.data+size_t(i)*13,p);
 return p-out;
}
static inline int64_t loc_rows(LOC& s,const uint64_t* ids,size_t count,uint8_t* out,size_t cap,uint64_t* offsets) {
 size_t total=0;offsets[0]=0;size_t k=0;
 for(;k+4<=count;k+=4){uint64_t a=ids[k],b=ids[k+1],c=ids[k+2],d=ids[k+3];if(a>=s.n||b>=s.n||c>=s.n||d>=s.n)return -1;
 const uint8_t*r0=s.data+a*13,*r1=s.data+b*13,*r2=s.data+c*13,*r3=s.data+d*13;
 auto len=[](const uint8_t*r){unsigned xc=(r[12]>>1)&7,yc=(r[12]>>4)&7;return xc==7?5:41-xc-yc;};
 unsigned n0=len(r0),n1=len(r1),n2=len(r2),n3=len(r3);size_t z1=total+n0,z2=z1+n1,z3=z2+n2,z4=z3+n3;if(z4>cap)return -1;
 loc_one(r0,out+total);loc_one(r1,out+z1);loc_one(r2,out+z2);loc_one(r3,out+z3);offsets[k+1]=z1;offsets[k+2]=z2;offsets[k+3]=z3;offsets[k+4]=z4;total=z4;
 }
 for(;k<count;k++){if(ids[k]>=s.n)return -1;const uint8_t*r=s.data+ids[k]*13;unsigned xc=(r[12]>>1)&7,yc=(r[12]>>4)&7,n=xc==7?5:41-xc-yc;if(total+n>cap)return -1;total+=loc_one(r,out+total);offsets[k+1]=total;}return total;
}
#ifdef ENCODER
static inline int loc_encode(const uint8_t* raw,size_t size,std::vector<uint8_t>& out) {
 // Accept only the known syntactic family, validating every original byte.
 std::vector<uint8_t> rec;uint32_t n=0;
 const uint8_t* p=raw,*end=raw+size;
 while(p<end) {
  __uint128_t v=0;
  if(end-p>=5 && memcmp(p,"NULL\n",5)==0) {p+=5;v=(__uint128_t)7<<97;}
  else {
   if(end-p<4 || memcmp(p,"(40.",4)) return 0;p+=4;
   uint64_t x=0,y=0;unsigned xl=0,yl=0;
   while(p<end && *p>='0'&&*p<='9'){if(xl==15)return 0;x=x*10+*p++-'0';xl++;}
   if(xl<9 || end-p<6 || memcmp(p,", -7",4) || (p[4]!='3'&&p[4]!='4') || p[5]!='.')return 0;
   unsigned lng=p[4]-'3';p+=6;
   while(p<end && *p>='0'&&*p<='9'){if(yl==14)return 0;y=y*10+*p++-'0';yl++;}
   if(yl<10||end-p<2||memcmp(p,")\n",2))return 0;p+=2;
   for(unsigned i=xl;i<15;i++)x*=10;
   for(unsigned i=yl;i<14;i++)y*=10;
   v=x|((__uint128_t)y<<50)|((__uint128_t)(15-xl)<<97)|((__uint128_t)(14-yl)<<100)|((__uint128_t)lng<<103);
  }
  size_t old=rec.size();rec.resize(old+13);memcpy(rec.data()+old,&v,13);n++;
 }
 out.resize(16+rec.size());memcpy(out.data(),"LOC13v1",8);
 memcpy(out.data()+8,&n,4);uint32_t sz=size;memcpy(out.data()+12,&sz,4);
 memcpy(out.data()+16,rec.data(),rec.size());return 1;
}
#endif
