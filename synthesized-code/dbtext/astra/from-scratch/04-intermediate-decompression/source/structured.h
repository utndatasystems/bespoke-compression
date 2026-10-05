#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <immintrin.h>
#include <array>
#ifdef ENCODER
#include <vector>
#include <algorithm>
#endif

namespace dbs {
static constexpr uint32_t magic = 0x53544244u;
enum { NAME=1, GENOME=2, HEX=3, UUID=4 };
struct Header { uint32_t magic, type; uint64_t bytes, rows; uint32_t base, reserved; };
static_assert(sizeof(Header)==32,"header");
inline uint32_t get32(const void*p){uint32_t v;memcpy(&v,p,4);return v;}
inline uint64_t get64(const void*p){uint64_t v;memcpy(&v,p,8);return v;}
inline void put32(void*p,uint32_t v){memcpy(p,&v,4);}
inline void put64(void*p,uint64_t v){memcpy(p,&v,8);}
inline uint32_t get18(const uint8_t*p,uint64_t i){
  uint64_t bit=i*18;return (get32(p+(bit>>3))>>(bit&7))&262143;
}
inline uint32_t get21(const uint8_t*p,uint64_t i){
  uint64_t bit=i*21;return (get32(p+(bit>>3))>>(bit&7))&2097151;
}
constexpr std::array<uint32_t,1000> make_digits(){
  std::array<uint32_t,1000> a{};
  for(unsigned i=0;i<1000;i++)a[i]=('0'+i/100)|(('0'+i/10%10)<<8)|(('0'+i%10)<<16)|(10u<<24);
  return a;
}
constexpr std::array<uint32_t,256> make_bases(){
  std::array<uint32_t,256>a{}; const char b[]="acgt";
  for(unsigned i=0;i<256;i++)a[i]=uint32_t(b[i&3])|(uint32_t(b[(i>>2)&3])<<8)|(uint32_t(b[(i>>4)&3])<<16)|(uint32_t(b[i>>6])<<24);
  return a;
}
static constexpr auto digits=make_digits();
static constexpr auto bases=make_bases();
struct State { Header h; const uint8_t *p,*q; };
inline bool is(const uint8_t*p,size_t n){return p&&n>=4&&get32(p)==magic;}
inline State* open(const uint8_t*p,size_t n){
  if(!p||n<sizeof(Header))return nullptr;
  Header h;memcpy(&h,p,sizeof(h));
  if(h.magic!=magic||h.reserved||!h.rows||h.rows>SIZE_MAX/64||h.bytes>INT64_MAX)return nullptr;
  uint64_t a=0,b=0;
  if(h.type==NAME){if(h.bytes!=h.rows*19)return nullptr;a=(h.rows*18+7)/8;}
  else if(h.type==GENOME){if(h.bytes!=h.rows*10)return nullptr;a=(h.rows*18+7)/8;}
  else if(h.type==HEX){if(h.bytes<h.rows*2||h.bytes>h.rows*9)return nullptr;a=h.rows*4;}
  else if(h.type==UUID){if(h.bytes!=h.rows*37||h.base>UINT32_MAX-2621430u)return nullptr;a=(h.rows*18+7)/8;b=(h.rows*60+7)/8;}
  else return nullptr;
  if(a+b+sizeof(Header)+16!=n)return nullptr;
  State*s=new State;s->h=h;s->p=p+sizeof(Header);s->q=s->p+a;return s;
}
inline void name_one(uint32_t v,uint8_t*out){
  put64(out,0x72656d6f74737543ULL); // Customer
  put32(out+8,0x30303023u);       // #000
  put32(out+12,digits[v/1000]);put32(out+15,digits[v%1000]);
}
inline void genome_one(uint32_t v,uint8_t*out){
  put64(out,uint64_t(bases[v&255])|(uint64_t(bases[(v>>8)&255])<<32));
  out[8]="acgt"[v>>16];out[9]='\n';
}
inline __m128i hex8(uint32_t v){
  __m128i x=_mm_cvtsi32_si128(__builtin_bswap32(v));
  __m128i hi=_mm_and_si128(_mm_srli_epi16(x,4),_mm_set1_epi8(15));
  __m128i lo=_mm_and_si128(x,_mm_set1_epi8(15));
  return _mm_shuffle_epi8(_mm_setr_epi8('0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'),_mm_unpacklo_epi8(hi,lo));
}
inline unsigned hexlen(uint32_t v){return v ? 8-(__builtin_clz(v)>>2):1;}
inline void hex_one(uint32_t v,unsigned l,uint8_t*out){
  uint64_t text=(uint64_t)_mm_cvtsi128_si64(hex8(v))>>((8-l)*8);
  if(l==8)put64(out,text);
  else {for(unsigned k=0;k<l;k++){out[k]=uint8_t(text);text>>=8;}}
  out[l]='\n';
}
inline void uuid_one(uint32_t first,uint64_t last,uint8_t*out){
  last=_pdep_u64(last,0x3ffffcffffffffffULL)|0x8000030000000000ULL;
  __m128i x=_mm_set_epi64x(__builtin_bswap64(last),__builtin_bswap32(first));
  __m128i hi=_mm_and_si128(_mm_srli_epi16(x,4),_mm_set1_epi8(15));
  __m128i lo=_mm_and_si128(x,_mm_set1_epi8(15));
  __m128i abc=_mm_setr_epi8('0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f');
  __m128i f=_mm_shuffle_epi8(abc,_mm_unpacklo_epi8(hi,lo));
  __m128i t=_mm_shuffle_epi8(abc,_mm_unpackhi_epi8(hi,lo));
  __m128i mid=_mm_setr_epi8('-','2','d','a','5','-','1','1',0,0,0,0,0,0,0,0);
  _mm_storeu_si128((__m128i*)out,_mm_unpacklo_epi64(f,mid));
  __m128i sh=_mm_setr_epi8(-1,-1,-1,0,1,2,3,-1,4,5,6,7,8,9,10,11);
  __m128i fixed=_mm_setr_epi8('e','8','-',0,0,0,0,'-',0,0,0,0,0,0,0,0);
  _mm_storeu_si128((__m128i*)(out+16),_mm_or_si128(_mm_shuffle_epi8(t,sh),fixed));
  put32(out+32,(uint32_t)_mm_extract_epi32(t,3));out[36]='\n';
}
inline int64_t decode(State*s,uint8_t*out,size_t cap){
  if(!s||!out||cap<s->h.bytes)return -1;
  uint64_t n=s->h.rows;
  if(s->h.type==NAME){for(uint64_t i=0;i<n;i++)name_one(get18(s->p,i),out+i*19);}
  else if(s->h.type==GENOME){for(uint64_t i=0;i<n;i++)genome_one(get18(s->p,i),out+i*10);}
  else if(s->h.type==UUID){for(uint64_t i=0;i<n;i++)uuid_one(s->h.base+get18(s->p,i)*10,(get64(s->q+((i*15)>>1))>>((i&1)*4))&0xfffffffffffffffULL,out+i*37);}
  else{
    size_t at=0;
    for(uint64_t i=0;i<n;i++){uint32_t v=get32(s->p+i*4);unsigned l=hexlen(v);if(at+l+1>cap)return -1;hex_one(v,l,out+at);at+=l+1;}
    if(at!=s->h.bytes)return -1;
  }
  return s->h.bytes;
}
inline int64_t rows(State*s,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offs){
  if(!s||!offs||count>INT64_MAX/37||(count&&(!ids||!out)))return -1;
  offs[0]=0;
  uint64_t n=s->h.rows;size_t at=0;
  if(s->h.type==NAME){
    if(count>cap/19)return -1;
    for(size_t j=0;j<count;j++){uint64_t i=ids[j];if(i>=n)return -1;name_one(get18(s->p,i),out+j*19);offs[j+1]=(j+1)*19;}
    return count*19;
  }else if(s->h.type==GENOME){
    if(count>cap/10)return -1;
    for(size_t j=0;j<count;j++){uint64_t i=ids[j];if(i>=n)return -1;genome_one(get18(s->p,i),out+j*10);offs[j+1]=(j+1)*10;}
    return count*10;
  }else if(s->h.type==UUID){
    if(count>cap/37)return -1;
    for(size_t j=0;j<count;j++){uint64_t i=ids[j];if(i>=n)return -1;uuid_one(s->h.base+get18(s->p,i)*10,(get64(s->q+((i*15)>>1))>>((i&1)*4))&0xfffffffffffffffULL,out+j*37);offs[j+1]=(j+1)*37;}
    return count*37;
  }else{
    for(size_t j=0;j<count;j++){uint64_t i=ids[j];if(i>=n)return -1;uint32_t v=get32(s->p+i*4);unsigned l=hexlen(v);if(at+l+1>cap)return -1;hex_one(v,l,out+at);at+=l+1;offs[j+1]=at;}
    return at;
  }
}
inline void close(State*s){delete s;}

#ifdef ENCODER
inline unsigned lowerhex(uint8_t c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:255;}
inline unsigned upperhex(uint8_t c){return c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:255;}
inline void bits(std::vector<uint8_t>&out,size_t start,uint64_t bit,uint64_t v,unsigned width){
  for(unsigned j=0;j<width;j+=8){unsigned take=std::min(8u,width-j);uint32_t x=uint32_t((v>>j)&((1u<<take)-1));uint64_t pos=bit+j;out[start+(pos>>3)]|=uint8_t(x<<(pos&7));if((pos&7)+take>8)out[start+(pos>>3)+1]|=uint8_t(x>>(8-(pos&7)));}
}
inline bool encode(std::vector<uint8_t>&out,const uint8_t*raw,size_t n){
  Header h{magic,0,n,0,0,0};
  std::vector<uint64_t>a,b;
  if(n>=19&&n%19==0&&!memcmp(raw,"Customer#",9)){
    h.type=NAME;h.rows=n/19;a.reserve(h.rows);
    for(size_t i=0;i<n;i+=19){if(memcmp(raw+i,"Customer#",9)||raw[i+18]!='\n')return false;uint32_t v=0;for(int j=9;j<18;j++){if(raw[i+j]<'0'||raw[i+j]>'9')return false;v=v*10+raw[i+j]-'0';}if(v>262143)return false;a.push_back(v);}
  }else if(n>=10&&n%10==0&&(raw[0]=='a'||raw[0]=='c'||raw[0]=='g'||raw[0]=='t')&&raw[9]=='\n'){
    h.type=GENOME;h.rows=n/10;a.reserve(h.rows);
    for(size_t i=0;i<n;i+=10){if(raw[i+9]!='\n')return false;uint32_t v=0;for(int j=0;j<9;j++){uint32_t c=raw[i+j]=='a'?0:raw[i+j]=='c'?1:raw[i+j]=='g'?2:raw[i+j]=='t'?3:255;if(c==255)return false;v|=c<<(j*2);}a.push_back(v);}
  }else if(n>=37&&n%37==0&&!memcmp(raw+8,"-2da5-11e8-",11)){
    h.type=UUID;h.rows=n/37;a.reserve(h.rows);b.reserve(h.rows);h.base=UINT32_MAX;
    for(size_t i=0;i<n;i+=37){
      if(memcmp(raw+i+8,"-2da5-11e8-",11)||raw[i+23]!='-'||raw[i+36]!='\n')return false;
      uint32_t first=0;for(int j=0;j<8;j++){unsigned c=lowerhex(raw[i+j]);if(c>15)return false;first=(first<<4)|c;}
      uint64_t last=0;for(int j=19;j<36;j++){if(j==23)continue;unsigned c=lowerhex(raw[i+j]);if(c>15)return false;last=(last<<4)|c;}
      if((last&0xc000030000000000ULL)!=0x8000030000000000ULL)return false;
      a.push_back(first);b.push_back(_pext_u64(last,0x3ffffcffffffffffULL));h.base=std::min(h.base,first);
    }
    for(uint64_t&v:a){v-=h.base;if(v%10||v>2621430)return false;v/=10;}
  }else{
    h.type=HEX;
    for(size_t i=0;i<n;){
      uint32_t v=0;unsigned len=0;bool zero=raw[i]=='0';
      while(i<n&&raw[i]!='\n'){unsigned c=upperhex(raw[i++]);if(c>15||len==8)return false;v=(v<<4)|c;len++;}
      if(i==n||!len||(zero&&len>1))return false;i++;a.push_back(v);
    }
    h.rows=a.size();if(!h.rows)return false;
  }
  unsigned width=h.type==HEX?32:18;
  size_t sa=(h.rows*width+7)/8,sb=h.type==UUID?(h.rows*60+7)/8:0;
  out.assign(sizeof(Header)+sa+sb+16,0);memcpy(out.data(),&h,sizeof(h));
  for(uint64_t i=0;i<h.rows;i++)bits(out,sizeof(Header),i*width,a[i],width);
  if(sb)for(uint64_t i=0;i<h.rows;i++)bits(out,sizeof(Header)+sa,i*60,b[i],60);
  return true;
}
#endif
}
