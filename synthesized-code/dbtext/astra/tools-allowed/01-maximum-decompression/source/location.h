#pragma once
#include <vector>
#include <cstdint>
#include <cstring>
#include <immintrin.h>
namespace loc {
struct __attribute__((packed)) Head { uint32_t magic, n, raw, reserved; char pre[4],sep[4],end[2],nil[6]; };
static bool encode(const uint8_t* r,size_t z,std::vector<uint8_t>& v) {
 if(z<30 || memcmp(r,"(40.",4))return false;
 std::vector<uint64_t> dat;
 const uint8_t* p=r,*e=r+z;
 Head h{};h.magic=0x31434f4c;h.raw=z;
 memcpy(h.pre,r,4);memcpy(h.sep,", -7",4);memcpy(h.end,")\n",2);memcpy(h.nil,"NULL\n",5);
 while(p<e){
  const uint8_t* q=(const uint8_t*)memchr(p,'\n',e-p); if(!q)return false; ++q;
  if(q-p==5 && !memcmp(p,"NULL\n",5)){dat.push_back(0);dat.push_back(0);p=q;++h.n;continue;}
  if(memcmp(p,h.pre,4))return false;
  const uint8_t* a=p+4;const uint8_t* b=a;
  while(b<q && *b>='0'&&*b<='9')++b;
  int na=b-a;if(na<1||na>15 || q-b<7 || memcmp(b,h.sep,4) || b[5]!='.')return false;
  const uint8_t* c=b+6;const uint8_t* d=c;
  while(d<q && *d>='0'&&*d<='9')++d;
  int nb=d-c;if(nb<1||nb>14||q-d!=2||memcmp(d,h.end,2)||b[4]<'0'||b[4]>'9')return false;
  uint64_t x=uint64_t(na)<<60,y=(uint64_t(nb)<<60)|(b[4]-'0');
  for(int i=0;i<na;i++)x|=uint64_t(a[i]-'0')<<(i*4);
  for(int i=0;i<nb;i++)y|=uint64_t(c[i]-'0')<<((i+1)*4);
  dat.push_back(x);dat.push_back(y);++h.n;p=q;
 }
 v.resize(sizeof(h)+dat.size()*8);memcpy(v.data(),&h,sizeof(h));memcpy(v.data()+sizeof(h),dat.data(),dat.size()*8);return true;
}
static bool valid(const uint8_t* p,size_t z){if(z<sizeof(Head))return false;Head h;memcpy(&h,p,sizeof(h));return h.magic==0x31434f4c && h.n<=10000000 && z==sizeof(Head)+size_t(h.n)*16 && h.raw>=h.n*5ull && h.raw<=h.n*42ull;}
struct State { const Head* h; const uint8_t* d; uint32_t raw,n; State(const uint8_t* p):h((const Head*)p),d(p+sizeof(Head)),raw(h->raw),n(h->n){} };
static inline unsigned len(const uint8_t* s){unsigned a=s[7]>>4;return a ? 12+a+(s[15]>>4):5;}
static inline unsigned one(const State& s,const uint8_t* p,uint8_t* o){
 unsigned a=p[7]>>4;if(!a){memcpy(o,s.h->nil,5);return 5;}
 unsigned b=p[15]>>4;
 __m128i x=_mm_loadu_si128((const __m128i*)p);
 __m128i lo=_mm_and_si128(x,_mm_set1_epi8(15));
 __m128i hi=_mm_and_si128(_mm_srli_epi16(x,4),_mm_set1_epi8(15));
 __m128i u=_mm_add_epi8(_mm_unpacklo_epi8(lo,hi),_mm_set1_epi8('0'));
 __m128i w=_mm_add_epi8(_mm_unpackhi_epi8(lo,hi),_mm_set1_epi8('0'));
 memcpy(o,s.h->pre,4);_mm_storeu_si128((__m128i*)(o+4),u);
 memcpy(o+4+a,s.h->sep,4);o[8+a]=p[8]%16+'0';o[9+a]='.';
 _mm_storeu_si128((__m128i*)(o+10+a),_mm_srli_si128(w,1));
 memcpy(o+10+a+b,s.h->end,2);return 12+a+b;
}
static int64_t decode(const State& s,uint8_t* o,size_t cap){
 if(cap<s.raw)return -1;size_t at=0;
 for(uint32_t i=0;i<s.n;i++){const uint8_t* p=s.d+16*i;unsigned z=len(p);if(z>cap-at)return -1;
 if(cap-at>=44)at+=one(s,p,o+at);else{uint8_t b[64];one(s,p,b);memcpy(o+at,b,z);at+=z;}}
 return at==s.raw ? at : -1;
}
static int64_t rows(const State&s,const uint64_t* ids,size_t n,uint8_t*o,size_t cap,uint64_t* off){
 size_t at=0;off[0]=0;
 for(size_t i=0;i<n;i++){if(ids[i]>=s.n)return -1;const uint8_t*p=s.d+16*ids[i];unsigned z=len(p);if(z>cap-at)return -1;
 if(cap-at>=44)at+=one(s,p,o+at);else{uint8_t b[64];one(s,p,b);memcpy(o+at,b,z);at+=z;}off[i+1]=at;}
 return at;
}
}
