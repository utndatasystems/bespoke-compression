#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <array>
#include <immintrin.h>
#include <new>

static constexpr uint32_t LOCATION_MAGIC = 0x31434f4c; // LOC1
static inline uint64_t location_u64(const uint8_t*p) {uint64_t x;memcpy(&x,p,8);return x;}
static inline uint32_t location_u32(const uint8_t*p) {uint32_t x;memcpy(&x,p,4);return x;}
static inline bool location_is(const uint8_t*p,size_t n){return n>=24&&location_u32(p)==LOCATION_MAGIC;}
struct LocationState {const uint8_t*data; uint64_t size; uint32_t count;};
static inline LocationState*location_open(const uint8_t*p,size_t n) {
 if(!location_is(p,n))return nullptr;
 uint32_t nr=location_u32(p+4);uint64_t size=location_u64(p+8);
 if(n!=24+size_t(nr)*12 || size>uint64_t(nr)*42 || size<uint64_t(nr)*5)return nullptr;
 auto*s=new(std::nothrow) LocationState;if(s)*s={p+24,size,nr};return s;
}
static inline void location_close(LocationState*s){delete s;}
static constexpr std::array<uint32_t,10000> location_make4() {
 std::array<uint32_t,10000> a{};
 for(unsigned i=0;i<10000;++i)a[i]=(48+i/1000)|((48+(i/100)%10)<<8)|((48+(i/10)%10)<<16)|((48+i%10)<<24);
 return a;
}
static constexpr auto location_digits4=location_make4();
static inline __m128i location_decimal16(uint64_t n){
 uint32_t hi=n/100000000,lo=n%100000000;
 uint32_t ha=hi/10000,hb=hi-ha*10000,la=lo/10000,lb=lo-la*10000;
 return _mm_set_epi32(location_digits4[lb],location_digits4[la],location_digits4[hb],location_digits4[ha]);
}
static inline size_t location_one(const uint8_t*p,uint8_t*out,size_t cap) {
 uint64_t a=location_u64(p);uint32_t b=location_u32(p+8);
 if(b>>31){if(cap<5)return size_t(-1);memcpy(out,"NULL\n",5);return 5;}
 uint64_t lat=(a&((1ull<<49)-1))+400000000000000ull;
 uint64_t lon=((a>>49)|(uint64_t(b)<<15))+70000000000000ull;
 bool l74=lon>=100000000000000ull;if(l74)lon-=100000000000000ull;
 __m128i x=location_decimal16(lat),y=location_decimal16(lon),zero=_mm_set1_epi8('0');
 unsigned mx=(~_mm_movemask_epi8(_mm_cmpeq_epi8(x,zero)))&65535;
 unsigned my=(~_mm_movemask_epi8(_mm_cmpeq_epi8(y,zero)))&65535; if(!my)return size_t(-1);
 unsigned nx=31-__builtin_clz(mx),ny=30-__builtin_clz(my); // remove leading 1/2 padding bytes
 size_t n=12+nx+ny;if(n>cap)return size_t(-1);
 memcpy(out,"(40.",4);out+=4;
 _mm_mask_storeu_epi8(out,__mmask16((1u<<nx)-1),_mm_srli_si128(x,1));out+=nx;
 memcpy(out,l74?", -74.":", -73.",6);out+=6;
 _mm_mask_storeu_epi8(out,__mmask16((1u<<ny)-1),_mm_srli_si128(y,2));out+=ny;
 memcpy(out,")\n",2);return n;
}
static inline int64_t location_decode(LocationState*s,uint8_t*out,size_t cap){
 if(!s||cap<s->size)return -1;
 size_t pos=0;
 for(uint32_t i=0;i<s->count;i++){size_t n=location_one(s->data+size_t(i)*12,out+pos,cap-pos);if(n==size_t(-1))return -1;pos+=n;}
 return pos==s->size?int64_t(pos):-1;
}
static inline int64_t location_rows(LocationState*s,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets){
 if(!s||!offsets)return -1;size_t pos=0;offsets[0]=0;
 for(size_t j=0;j<count;j++){if(ids[j]>=s->count)return -1;size_t n=location_one(s->data+ids[j]*12,out+pos,cap-pos);if(n==size_t(-1))return -1;pos+=n;offsets[j+1]=pos;}
 return pos;
}
#ifdef ENCODER
static inline bool location_encode(std::vector<uint8_t>&out,const uint8_t*raw,size_t n){
 size_t nr=0;for(size_t i=0;i<n;i++)nr+=raw[i]=='\n';
 if(nr!=67751 || n<2000000 || raw[n-1]!='\n')return false;
 std::vector<uint8_t> a(24+nr*12);uint32_t magic=LOCATION_MAGIC,c=nr;memcpy(a.data(),&magic,4);memcpy(a.data()+4,&c,4);uint64_t nb=n;memcpy(a.data()+8,&nb,8);
 size_t at=0;
 for(size_t row=0;row<nr;row++){
  uint8_t*dst=a.data()+24+12*row;
  if(at+5<=n&&!memcmp(raw+at,"NULL\n",5)){uint32_t null=0x80000000;memcpy(dst+8,&null,4);at+=5;continue;}
  if(at+4>n||memcmp(raw+at,"(40.",4))return false;at+=4;
  uint64_t lat=0,lon=0;unsigned nl=0,nr=0;
  while(at<n&&raw[at]>='0'&&raw[at]<='9'){lat=lat*10+raw[at++]-'0';nl++;}
  if(nl<1||nl>15||raw[at-1]=='0'||at+6>n||raw[at]!=','||raw[at+1]!=' '||raw[at+2]!='-'||raw[at+3]!='7'||(raw[at+4]!='3'&&raw[at+4]!='4')||raw[at+5]!='.')return false;
  bool l74=raw[at+4]=='4';at+=6;
  while(at<n&&raw[at]>='0'&&raw[at]<='9'){lon=lon*10+raw[at++]-'0';nr++;}
  if(nr<1||nr>14||raw[at-1]=='0'||at+2>n||raw[at]!=')'||raw[at+1]!='\n')return false;at+=2;
  for(;nl<15;nl++)lat*=10;for(;nr<14;nr++)lon*=10;if(l74)lon+=100000000000000ull;
  if(lat<400000000000000ull||lon<70000000000000ull)return false;lat-=400000000000000ull;lon-=70000000000000ull;
  if(lat>=(1ull<<49)||lon>=(1ull<<46))return false;
  uint64_t lo=lat|(lon<<49);uint32_t hi=lon>>15;memcpy(dst,&lo,8);memcpy(dst+8,&hi,4);
 }
 if(at!=n)return false;out.swap(a);return true;
}
#endif
