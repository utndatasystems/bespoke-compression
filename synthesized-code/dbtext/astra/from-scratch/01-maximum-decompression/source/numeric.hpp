#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <immintrin.h>
#ifdef ENCODER
#include <vector>
#include <string>
#include <map>
#endif
namespace nm {
struct __attribute__((packed)) Header {
 uint32_t mode, rows;
 uint64_t rawsize, total;
 uint32_t templates, stride;
};
struct __attribute__((packed)) Template {
 uint8_t text[64];
 uint64_t digits;
 uint64_t output;
 uint32_t len;
 uint8_t padding[44];
};
static_assert(sizeof(Header)==32 && alignof(Header)==1);
static_assert(sizeof(Template)==128 && alignof(Template)==1);
#ifdef ENCODER
inline std::vector<uint8_t> encode(const uint8_t*p,size_t n) {
 if(n<10||p[0]!='('||p[3]!='.')return {};
 std::vector<Template> ts;
 std::map<std::string,unsigned> dict;
 std::vector<uint8_t> records;
 for(size_t off=0;off<n;) {
  size_t end=off;while(end<n&&p[end]!='\n')end++;if(end<n)end++;
  size_t len=end-off;if(len>63)return {};
  const uint8_t*s=p+off;
  Template t{};t.len=len;t.output=(uint64_t(1)<<len)-1;
  unsigned digits=0;uint8_t bcd[16]{};
  bool fraction=false;
  for(unsigned j=0;j<len;j++) {
   if(s[j]=='.')fraction=true;
   else if(s[j]<'0'||s[j]>'9')fraction=false;
   if(fraction&&s[j]>='0'&&s[j]<='9') {
    if(digits>=30)return {};
    bcd[digits/2]|=(s[j]-'0')<<((digits&1)*4);
    digits++;t.digits|=uint64_t(1)<<j;
   } else t.text[j]=s[j];
  }
  // This specialization retains every nonfractional byte in its templates.
  // Permit the exceptional literal NULL row, otherwise require two decimals.
  if(digits==0&&!(len==5&&!memcmp(s,"NULL\n",5)))return {};
  if(digits>29)return {};
  std::string key((const char*)&t,sizeof t);
  auto z=dict.find(key);unsigned id;
  if(z==dict.end()) {id=ts.size();if(id>=256)return {};dict.emplace(key,id);ts.push_back(t);}else id=z->second;
  bcd[15]=id;records.insert(records.end(),bcd,bcd+16);off=end;
 }
 Header h{};h.mode=200;h.rows=records.size()/16;h.rawsize=n;h.templates=ts.size();h.stride=16;
 h.total=64+ts.size()*sizeof(Template)+records.size();
 std::vector<uint8_t>a(h.total);memcpy(a.data(),&h,sizeof h);
 memcpy(a.data()+64,ts.data(),ts.size()*sizeof(Template));memcpy(a.data()+64+ts.size()*sizeof(Template),records.data(),records.size());return a;
}
#endif
#ifdef DECODER
inline void* open(const uint8_t*p,size_t n) {
 if(n<64)return nullptr;const Header*h=(const Header*)p;
 if(h->mode!=200||!h->templates||h->templates>256||h->stride!=16||h->total!=64+uint64_t(h->templates)*sizeof(Template)+uint64_t(h->rows)*16||h->total>n)return nullptr;
 const Template*ts=(const Template*)(p+64);
 for(unsigned i=0;i<h->templates;i++){const Template&t=ts[i];if(!t.len||t.len>64)return nullptr;uint64_t mask=t.len==64?~uint64_t(0):(uint64_t(1)<<t.len)-1;if(t.output!=mask||(t.digits&~mask)||__builtin_popcountll(t.digits)>30)return nullptr;}
 return (void*)p;
}
inline __m512i expand(const uint8_t*s,const Template*t) {
 __m256i x=_mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i*)s));
 x=_mm256_and_si256(_mm256_or_si256(x,_mm256_slli_epi16(x,4)),_mm256_set1_epi16(0x0f0f));
 x=_mm256_add_epi8(x,_mm256_set1_epi8('0'));
 return _mm512_mask_expand_epi8(_mm512_loadu_si512(t->text),(__mmask64)t->digits,_mm512_castsi256_si512(x));
}
inline int64_t decode(void*state,uint8_t*out,size_t cap) {
 const Header*h=(const Header*)state;if(!h||cap<h->rawsize)return -1;
 const Template*ts=(const Template*)((const uint8_t*)h+64);
 const uint8_t*s=(const uint8_t*)(ts+h->templates);
 uint8_t*d=out;
 for(unsigned r=0;r<h->rows;r++,s+=16) {
  if(s[15]>=h->templates)return -1;const Template*t=ts+s[15];if(t->len>cap-size_t(d-out))return -1;
  _mm512_mask_storeu_epi8(d,(__mmask64)t->output,expand(s,t));
  d+=t->len;
 }
 return size_t(d-out)==h->rawsize?int64_t(d-out):-1;
}
inline int64_t rows(void*state,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offs) {
 const Header*h=(const Header*)state;if(!h||!offs)return -1;
 if(count&&ids[count-1]>=h->rows)return -1;offs[0]=0;
 const Template*ts=(const Template*)((const uint8_t*)h+64);
 const uint8_t*base=(const uint8_t*)(ts+h->templates);
 size_t size=0;
 for(size_t i=0;i<count;i++) {
  if(ids[i]>=h->rows)return -1;const uint8_t*s=base+ids[i]*16;if(s[15]>=h->templates)return -1;const Template*t=ts+s[15];
  if(t->len>cap-size)return -1;
  _mm512_mask_storeu_epi8(out+size,(__mmask64)t->output,expand(s,t));
  size+=t->len;offs[i+1]=size;
 }
 return size;
}
#endif
}
#ifdef ENCODER
inline std::vector<uint8_t> numeric_encode(const uint8_t*p,size_t n){return nm::encode(p,n);}
#endif
#ifdef DECODER
inline void* numeric_open(const uint8_t*p,size_t n){return nm::open(p,n);}
inline int64_t numeric_decode(void*p,uint8_t*out,size_t cap){return nm::decode(p,out,cap);}
inline int64_t numeric_rows(void*p,const uint64_t*ids,size_t n,uint8_t*out,size_t cap,uint64_t*off){return nm::rows(p,ids,n,out,cap,off);}
inline void numeric_close(void*){}
#endif
