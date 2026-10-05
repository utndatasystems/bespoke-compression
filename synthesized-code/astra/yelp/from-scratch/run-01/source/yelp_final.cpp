// Yelp business JSONL, specialized lossless bulk codec.
// Fitting (raw lexical parsing, phrase counting, frequency ordering, and slot
// assignment) happens entirely inside lab_encode. The archive holds all fitted
// data; the independent decoder has only format/syntax constants.
//
// Atom IDs address 32-byte slots in a padded 65536-slot pool. ID ranges encode
// copy-width classes; each phrase carries its exact byte length. Ordered phrase
// suffixes include field separators and row endings, preserving original bytes.
// The hot loop reserves 67000 output bytes and 1561 encoded bytes per row;
// those bounds cover all values representable in a row, including wide copies.
// Near either buffer end, safe_row checks each read and exact-length write.

#include "codec.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <immintrin.h>
#ifdef ENCODER
#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept>
#include <cstdio>
#include <algorithm>
#endif
struct Head { uint64_t magic, outsize; uint32_t rows,lastnl; uint32_t meta[3], count[3],stream,pad; };
struct Meta { uint32_t off,len; };
#ifdef ENCODER
struct Dict {
 std::unordered_map<std::string,uint32_t> ids; std::vector<std::string> strs;std::vector<uint32_t> freqs;
 uint16_t add(const std::string&s){auto it=ids.find(s);if(it!=ids.end()){++freqs[it->second];return it->second;} if(strs.size()>=65536)throw std::runtime_error("dict");uint32_t id=strs.size();ids[s]=id;strs.push_back(s);freqs.push_back(1);return id;}
};
static void put16(std::vector<uint8_t>&b,uint16_t x){b.push_back(x);b.push_back(x>>8);}
static const char* scanstring(const char*p){++p;while(*p!='"'){if(*p=='\\')++p;++p;}return p+1;}
static const char* scanval(const char*p){
 if(*p=='"')return scanstring(p);
 if(*p=='{'){int depth=1;++p;while(depth){if(*p=='"')p=scanstring(p);else {if(*p=='{')++depth;else if(*p=='}')--depth;++p;}}return p;}
 while(*p!=','&&*p!='}'&&*p!='\n')++p;return p;
}
static std::vector<std::string> fields(const char*b,const char*e){
 std::vector<std::string> v;const char*p=b+1;while(p<e&&*p!='}'){p=scanstring(p);if(*p++!=':')throw std::runtime_error("key");const char*q=scanval(p);v.emplace_back(p,q);p=q;if(*p==',')++p;else break;}return v;
}
static void pairs(const std::string&s,Dict&d,std::vector<uint8_t>&stream,const std::string&suffix,uint32_t&num){
 std::vector<uint16_t> ids;
 if(s=="null"){ids.push_back(d.add(s+suffix));}
 else {
 const char*b=s.data(),*p=b+1,*q,*start=b;
 while(*p!='}'){p=scanstring(p);++p;q=scanval(p);p=q; if(*p==',')++p; else if(*p=='}')++p; ids.push_back(d.add(std::string(start,p)+(*q=='}'?suffix:"")));start=p;if(*q=='}')break;}
 }
 if(ids.size()>255)throw std::runtime_error("count");
 num+=ids.size();for(auto id:ids)put16(stream,id);
}
static void cats(const std::string&s,Dict&d,std::vector<uint8_t>&stream,const std::string&suffix,uint32_t&num){
 std::vector<uint16_t> ids;
 if(s=="null"){ids.push_back(d.add(s+suffix));}
 else {size_t a=0;while(true){size_t p=s.find(", ",a+1);if(p==std::string::npos){ids.push_back(d.add(s.substr(a)+suffix));break;}ids.push_back(d.add(s.substr(a,p-a)));a=p;}}
 if(ids.size()>255)throw std::runtime_error("count");
 num+=ids.size();for(auto id:ids)put16(stream,id);
}
extern "C" int64_t lab_encode(const uint8_t*raw,size_t size,uint8_t*out,size_t cap) {
 try {
 Dict ds[3];std::vector<uint8_t> stream;stream.reserve(size/5);const char*b=(const char*)raw,*end=b+size;uint32_t rows=0;uint64_t maxrow=0;
 for(const char*p=b;p<end;){
  const char*e=(const char*)memchr(p,'\n',end-p);if(!e)e=end;maxrow=std::max(maxrow,(uint64_t)(e-p));
  auto v=fields(p,e);if(v.size()!=14||v[0].size()!=24)throw std::runtime_error("fields");
  stream.insert(stream.end(),v[0].data()+1,v[0].data()+23);
  for(int j=1;j<=2;++j){if(v[j].size()>255)throw std::runtime_error("literal");stream.push_back(v[j].size());stream.insert(stream.end(),v[j].begin(),v[j].end());}
  put16(stream,ds[0].add(",\"city\":"+v[3]+",\"state\":"+v[4]+",\"postal_code\":"+v[5]+",\"latitude\":"));
  for(int j=6;j<=7;++j){if(v[j].size()>16)throw std::runtime_error("num");stream.push_back(v[j].size());stream.insert(stream.end(),v[j].begin(),v[j].end());}
  put16(stream,ds[0].add(",\"stars\":"+v[8]+",\"review_count\":"+v[9]+",\"is_open\":"+v[10]+",\"attributes\":"));
  size_t countAt=stream.size();stream.push_back(0);uint32_t ng=0;
  pairs(v[11],ds[2],stream,",\"categories\":",ng);cats(v[12],ds[2],stream,",\"hours\":",ng);pairs(v[13],ds[2],stream,e<end?"}\n":"}",ng);if(ng>255)throw std::runtime_error("count");stream[countAt]=ng;
  p=e+(e<end);++rows;
 }
 if(maxrow>65400)throw std::runtime_error("rowlen");
 Head h={};h.magic=0x5354525543543132ull;h.outsize=size;h.rows=rows;h.lastnl=size&&raw[size-1]=='\n';
 std::vector<uint32_t> order,slots(ds[2].strs.size());for(uint32_t i=0;i<slots.size();++i)order.push_back(i);
 std::sort(order.begin(),order.end(),[&](uint32_t a,uint32_t b){return ds[2].freqs[a]!=ds[2].freqs[b]?ds[2].freqs[a]>ds[2].freqs[b]:a<b;});
 uint32_t nextslot[3]={0,16384,32768};for(auto i:order){unsigned len=ds[2].strs[i].size();unsigned kind=len<=64?0:len<=128?1:2;slots[i]=nextslot[kind];nextslot[kind]+=(len+32)/32;}if(nextslot[0]>16384||nextslot[1]>32768||nextslot[2]>65536)throw std::runtime_error("slots");uint32_t atomslots=nextslot[2];
 size_t q=0;
 for(uint32_t r=0;r<rows;++r){
  q+=22;for(int k=0;k<2;++k){unsigned n=stream[q++];q+=n;}q+=2;for(int k=0;k<2;++k){unsigned n=stream[q++];q+=n;}q+=2;
  unsigned n=stream[q++];while(n--){unsigned id=stream[q]|(stream[q+1]<<8);unsigned to=slots[id];stream[q]=to;stream[q+1]=to>>8;q+=2;}
 }
 size_t pos=sizeof(h)+16384*4;if(ds[0].strs.size()>16384)throw std::runtime_error("headslots");
 h.meta[0]=h.meta[1]=sizeof(h);h.count[0]=h.count[1]=16384;h.meta[2]=0;h.count[2]=65536;
 std::vector<Meta> metas(16384,{0,0});
 for(size_t j=0;j<ds[0].strs.size();j++){const auto&v=ds[0].strs[j];if(v.size()>128)throw std::runtime_error("headlen");metas[j]={(uint32_t)pos,(uint32_t)v.size()};pos+=v.size();}
 pos+=128;pos=(pos+63)&~size_t(63);h.pad=pos;pos+=65536*32+256;h.stream=pos;
 size_t total=pos+stream.size()+128;if(total>cap)return -1;
 memset(out,0,pos);memcpy(out,&h,sizeof h);for(size_t j=0;j<16384;j++){if(metas[j].off>=(1u<<21))throw std::runtime_error("headoff");uint32_t x=metas[j].off|(metas[j].len<<21);memcpy(out+sizeof(h)+j*4,&x,4);}
 for(size_t j=0;j<ds[0].strs.size();j++)memcpy(out+metas[j].off,ds[0].strs[j].data(),metas[j].len);
 for(uint32_t i=0;i<slots.size();++i){if(ds[2].strs[i].size()>255)throw std::runtime_error("atomlen");uint8_t len=ds[2].strs[i].size();memcpy(out+h.pad+slots[i]*32,&len,1);memcpy(out+h.pad+slots[i]*32+1,ds[2].strs[i].data(),len);}
 memcpy(out+pos,stream.data(),stream.size());memset(out+pos+stream.size(),0,128);
 fprintf(stderr,"structured rows=%u maxrow=%lu stream=%zu atomslots=%u total=%zu\n",rows,maxrow,stream.size(),atomslots,total);
 return total;
 }catch(const std::exception&e){fprintf(stderr,"error %s\n",e.what());return -1;}
}
#else
struct State {const uint8_t*a,*end,*atoms;Head h;const uint32_t*m;};
extern "C" void*lab_open(const uint8_t*a,size_t size){
 if(!a||size<sizeof(Head)+128)return nullptr;Head h;memcpy(&h,a,sizeof h);
 if(h.magic!=0x5354525543543132ull||h.lastnl>1||!h.rows||h.rows>h.outsize||h.stream>size-128||h.stream<sizeof(Head)+16384*4+65536*32+256)return nullptr;
 if(h.meta[0]!=sizeof(Head)||h.meta[1]!=sizeof(Head)||h.count[0]!=16384||h.count[1]!=16384||h.pad<sizeof(Head)+16384*4||h.pad>h.stream||size_t(h.stream-h.pad)<65536*32+256)return nullptr;
 for(uint32_t i=0;i<16384;i++){uint32_t v;memcpy(&v,a+sizeof(Head)+i*4,4);uint32_t off=v&((1u<<21)-1),len=v>>21;if(len>128||off>h.pad||h.pad-off<128)return nullptr;}
 auto*s=(State*)malloc(sizeof(State));if(!s)return nullptr;s->a=a;s->end=a+size-128;s->atoms=a+h.pad;s->h=h;s->m=(const uint32_t*)(a+sizeof(Head));return s;
}
static inline uint16_t get16(const uint8_t*&p){uint16_t v;memcpy(&v,p,2);p+=2;return v;}
static inline void cp(uint8_t*&o,const uint8_t*p,unsigned n){
 if(n<=64){_mm512_storeu_si512(o,_mm512_loadu_si512(p));o+=n;return;}
 unsigned j=0;do{_mm512_storeu_si512(o+j,_mm512_loadu_si512(p+j));j+=64;}while(j<n);o+=n;
}
template<size_t N> static inline void fix(uint8_t*&o,const char(&s)[N]){memcpy(o,s,N-1);o+=N-1;}
static inline bool safe_copy(uint8_t*&o,uint8_t*oe,const uint8_t*p,size_t n){if(size_t(oe-o)<n)return false;memcpy(o,p,n);o+=n;return true;}
static bool safe_row(uint8_t*&o,uint8_t*oe,const uint8_t*&p,const State*s){
 #define FIX(x) if(!safe_copy(o,oe,(const uint8_t*)x,sizeof(x)-1))return false
 #define LIT(n) if(size_t(s->end-p)<(n)||!safe_copy(o,oe,p,n))return false;p+=(n)
 #define BYTE(v) if(p==s->end)return false;unsigned v=*p++
 #define DICT() if(size_t(s->end-p)<2)return false;uint16_t id=get16(p)&16383;uint32_t x;memcpy(&x,(const uint8_t*)s->m+id*4,4);if(!safe_copy(o,oe,s->a+(x&((1u<<21)-1)),x>>21))return false
 FIX("{\"business_id\":\"");LIT(22);FIX("\",\"name\":");{BYTE(n);LIT(n);}FIX(",\"address\":");{BYTE(n);LIT(n);}
 {DICT();}{BYTE(n);if(n>16)return false;LIT(n);}FIX(",\"longitude\":");{BYTE(n);if(n>16)return false;LIT(n);}{DICT();}
 BYTE(ng);if(size_t(s->end-p)<ng*2)return false;while(ng--){unsigned id=get16(p);const uint8_t*q=s->atoms+id*32;unsigned n=*q++;if(!safe_copy(o,oe,q,n))return false;}return true;
 #undef FIX
 #undef LIT
 #undef BYTE
 #undef DICT
}
extern "C" int64_t lab_decode(void*state,uint8_t*output,size_t capacity){
 if(!state||!output)return -1;auto*s=(State*)state;auto*h=&s->h;if(capacity<h->outsize)return -1;const uint8_t*p=s->a+h->stream;uint8_t*o=output,*oe=output+h->outsize;const uint8_t*archive=s->a,*atoms=s->atoms;const uint32_t*meta=s->m;uint32_t r=0;
 while(r<h->rows&&size_t(s->end-p)>=1561&&size_t(oe-o)>=67000){
  alignas(64) static const uint8_t hx[64]={'{','"','b','u','s','i','n','e','s','s','_','i','d','"',':','"',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'"',',','"','n','a','m','e','"',':'};
  alignas(64) static const uint8_t hi[64]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,38,39,40,41,42,43,44,45,46};
  _mm512_storeu_si512(o,_mm512_permutex2var_epi8(_mm512_load_si512(hx),_mm512_load_si512(hi),_mm512_loadu_si512(p)));o+=47;p+=22;
  unsigned n=*p++;cp(o,p,n);p+=n;fix(o,",\"address\":");n=*p++;cp(o,p,n);p+=n;
  {uint32_t x;unsigned id=get16(p)&16383;memcpy(&x,(const uint8_t*)meta+id*4,4);const uint8_t*q=archive+(x&((1u<<21)-1));_mm512_storeu_si512(o,_mm512_loadu_si512(q));_mm512_storeu_si512(o+64,_mm512_loadu_si512(q+64));o+=x>>21;}
  n=*p++;if(n>16)return -1;_mm_storeu_si128((__m128i*)o,_mm_loadu_si128((const __m128i*)p));o+=n;p+=n;fix(o,",\"longitude\":");n=*p++;if(n>16)return -1;_mm_storeu_si128((__m128i*)o,_mm_loadu_si128((const __m128i*)p));o+=n;p+=n;
  {uint32_t x;unsigned id=get16(p)&16383;memcpy(&x,(const uint8_t*)meta+id*4,4);unsigned len=x>>21;cp(o,archive+(x&((1u<<21)-1)),len);}
  unsigned ng=*p++;while(ng--){unsigned id=get16(p);const uint8_t*q=atoms+id*32;unsigned len=*q++;_mm512_storeu_si512(o,_mm512_loadu_si512(q));if(id>=16384){_mm512_storeu_si512(o+64,_mm512_loadu_si512(q+64));if(id>=32768){_mm512_storeu_si512(o+128,_mm512_loadu_si512(q+128));if(len>192)_mm512_storeu_si512(o+192,_mm512_loadu_si512(q+192));}}o+=len;}++r;
 }
 for(;r<h->rows;r++)if(!safe_row(o,oe,p,s))return -1;
 return o==oe&&p==s->end?h->outsize:-1;
}
extern "C" void lab_close(void*s){free(s);}
#endif
