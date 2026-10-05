#define PERCHUNK 1

#include "codec.h"
#include <immintrin.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#ifdef ENCODER
#include <string>
#include <vector>
#include <unordered_map>
#include <regex>
#include <algorithm>
#endif
struct Header {
 uint64_t magic, rawsize, rows, nt, metaend, idend, payloadend, total;
};
static constexpr uint64_t magic=0x34454c504d45544cULL;
struct __attribute__((packed)) Tmpl {uint32_t len; uint16_t nv,nc; uint64_t off;};
struct __attribute__((packed)) Chunk {uint8_t base[64]; uint64_t mask; uint16_t off,n; uint8_t pad[52];};
static_assert(sizeof(Chunk)==128);
static uint64_t align64(uint64_t n){return (n+63)&~uint64_t(63);}
#ifdef ENCODER
struct Group{std::string first,key;std::vector<uint32_t> pos;std::vector<uint8_t> vary;std::vector<uint64_t> rows;};
extern "C" int64_t lab_encode(const uint8_t*raw,size_t size,uint8_t*archive,size_t capacity) {
 try {
  if(!raw||!archive||size>UINT32_MAX) return -1;
  std::regex pat("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}|[0-9a-f]{32,40}|[0-9]{2}:[0-9]{2}:[0-9]{2}\\.[0-9]{3}|[0-9]+");
  std::unordered_map<std::string,uint32_t> map;
  std::vector<Group> groups; std::vector<uint16_t> ids; std::vector<std::pair<uint32_t,uint32_t>> lines;
  size_t start=0;
  while(start<size) {
    size_t end=start; while(end<size&&raw[end]!='\n') ++end; if(end<size)++end;
    std::string line((const char*)raw+start,end-start),key=line;
    for(std::sregex_iterator i(line.begin(),line.end(),pat),e;i!=e;++i) {
     size_t a=i->position(),b=a+i->length();for(;a<b;++a)if((key[a]>='0'&&key[a]<='9')||(key[a]>='a'&&key[a]<='f'))key[a]='#';
    }
    auto found=map.find(key);uint32_t id;
    if(found==map.end()) {id=groups.size();if(id>=65536)return -1;map.emplace(key,id);Group g;g.first=line;g.key=key;g.vary.resize(line.size());groups.push_back(std::move(g));}
    else id=found->second;
    auto &g=groups[id];for(size_t j=0;j<line.size();++j)g.vary[j]|=line[j]!=g.first[j];
    ids.push_back(id);lines.push_back({start,end-start});start=end;
  }
  std::vector<Tmpl> table(groups.size());std::vector<Chunk> chunks;
  for(size_t i=0;i<groups.size();++i) {
    auto &g=groups[i];for(size_t j=0;j<g.vary.size();++j)if(g.vary[j])g.pos.push_back(j);
    if(g.pos.size()>192||g.first.size()>65535)return -1;
    auto&t=table[i];t.len=g.first.size();t.nv=g.pos.size();t.nc=(t.len+63)/64;t.off=chunks.size()*sizeof(Chunk);
    unsigned used=0;
    for(unsigned j=0;j<t.nc;++j) {
     Chunk ch{};ch.off=used;
     for(unsigned k=0;k<64;++k) {
      unsigned o=j*64+k;ch.base[k]=o<t.len?g.first[o]:0;
      if(o<t.len&&g.vary[o]){ch.mask|=uint64_t(1)<<k;++used;++ch.n;}
     }
     chunks.push_back(ch);
    }
  }
  Header h{};h.magic=magic;h.rawsize=size;h.rows=lines.size();h.nt=groups.size();
  uint64_t tabend=align64(sizeof(Header)+table.size()*sizeof(Tmpl));
  h.metaend=tabend+chunks.size()*sizeof(Chunk);h.idend=h.metaend+ids.size()*2;
  h.payloadend=h.idend;for(auto id:ids)h.payloadend+=(table[id].nv+1)/2;
  h.total=h.payloadend+96;
  if(h.total>capacity)return -1;
  memset(archive,0,h.total);memcpy(archive,&h,sizeof h);
  for(auto&t:table)t.off+=tabend;
  memcpy(archive+sizeof(Header),table.data(),table.size()*sizeof(Tmpl));
  memcpy(archive+tabend,chunks.data(),chunks.size()*sizeof(Chunk));memcpy(archive+h.metaend,ids.data(),ids.size()*2);
  auto p=archive+h.idend;
  for(size_t r=0;r<lines.size();++r) {
   auto &g=groups[ids[r]];auto ptr=raw+lines[r].first;
   for(size_t k=0;k<g.pos.size();++k) {
    uint8_t c=ptr[g.pos[k]],v;
    if(c>='0'&&c<='9')v=c-'0';else if(c>='a'&&c<='f')v=c-'a'+10;else return -1;
    p[k/2]|=v<<((k&1)*4);
   }
   p+=(g.pos.size()+1)/2;
  }
  return h.total;
 } catch(...) {return -1;}
}
#else
struct State {const uint8_t* a; Header h; const Tmpl* table;};
static inline __m512i expand(const uint8_t*p) {
#ifdef MULTISHIFT
 auto wide=_mm512_cvtepu32_epi64(_mm256_loadu_si256((const __m256i*)p));
 auto n=_mm512_and_si512(_mm512_multishift_epi64_epi8(_mm512_set1_epi64(0x1c1814100c080400),wide),_mm512_set1_epi8(15));
#else
 auto wide=_mm512_cvtepu8_epi16(_mm256_loadu_si256((const __m256i*)p));
 auto n=_mm512_and_si512(_mm512_or_si512(wide,_mm512_slli_epi16(wide,4)),_mm512_set1_epi8(15));
#endif
 const auto tab=_mm512_set_epi64(0x6665646362613938,0x3736353433323130,0x6665646362613938,0x3736353433323130,0x6665646362613938,0x3736353433323130,0x6665646362613938,0x3736353433323130);
 return _mm512_shuffle_epi8(tab,n);
}
extern "C" void* lab_open(const uint8_t*a,size_t size) {
 if(!a||size<sizeof(Header))return nullptr;
 Header h;memcpy(&h,a,sizeof h);
 if(h.magic!=magic||h.total!=size||h.nt>65535||h.rawsize>INT64_MAX||h.rows>h.rawsize||h.nt>h.rows||
 h.metaend<sizeof(Header)+h.nt*sizeof(Tmpl)||h.metaend>size||h.idend<h.metaend||h.idend>size||h.rows>(size-h.metaend)/2||h.idend!=h.metaend+h.rows*2||
 h.payloadend<h.idend||h.payloadend>size||size-h.payloadend!=96)return nullptr;
 auto table=(const Tmpl*)(a+sizeof(Header));
 uint64_t tabend=align64(sizeof(Header)+h.nt*sizeof(Tmpl));
 for(size_t i=0;i<h.nt;++i) {
  auto t=table[i];
  if(!t.len||t.len>65535||t.nv>192||t.nc!=(t.len+63)/64||t.off<tabend||t.off>h.metaend||uint64_t(t.nc)*sizeof(Chunk)>h.metaend-t.off||t.off%64)return nullptr;
  auto chunks=(const Chunk*)(a+t.off);unsigned off=0;
  for(unsigned j=0;j<t.nc;++j) {
   auto&c=chunks[j];unsigned n=__builtin_popcountll(c.mask);
   if(c.off!=off||c.n!=n||off+n>t.nv)return nullptr;
   #ifdef PERCHUNK
   if(n+(off&1)>64)return nullptr;
#endif
   for(uint64_t bits=c.mask;bits;bits&=bits-1) {unsigned k=__builtin_ctzll(bits);if(j*64+k>=t.len)return nullptr;++off;}
  }
  if(off!=t.nv)return nullptr;
 }
 State*s=(State*)malloc(sizeof(State));if(!s)return nullptr;s->a=a;s->h=h;s->table=table;return s;
}
template<bool Tail> static __attribute__((always_inline)) inline void render(const Chunk* ch,const uint8_t*p,uint8_t*out,const Tmpl&t,uint64_t remain) {
#ifndef PERCHUNK
  auto a=expand(p),b=t.nv>64?expand(p+32):a,c=t.nv>128?expand(p+64):a;
#endif
  for(unsigned j=0;j<t.nc;++j) {
   auto&x=ch[j];auto v=_mm512_loadu_si512(x.base);
   if(x.mask) {
#ifdef PERCHUNK
    auto a=expand(p+x.off/2);
    if(x.off&1){const auto idx=_mm512_set_epi64(0x403f3e3d3c3b3a39,0x3837363534333231,0x302f2e2d2c2b2a29,0x2827262524232221,0x201f1e1d1c1b1a19,0x1817161514131211,0x100f0e0d0c0b0a09,0x0807060504030201);a=_mm512_permutexvar_epi8(idx,a);}
    v=_mm512_mask_expand_epi8(v,x.mask,a);
#else
    auto index=_mm512_loadu_si512(x.index);
    if(x.off+x.n<=64)v=_mm512_mask_permutexvar_epi8(v,x.mask,index,a);
    else if(x.off>=64&&x.off+x.n<=128)v=_mm512_mask_permutexvar_epi8(v,x.mask,index,b);
    else if(x.off>=128)v=_mm512_mask_permutexvar_epi8(v,x.mask,index,c);
    else {auto w=_mm512_permutex2var_epi8(a,index,b);v=_mm512_mask_mov_epi8(v,x.mask,w);
     if(t.nv>128){auto cm=_mm512_movepi8_mask(index)&x.mask;v=_mm512_mask_permutexvar_epi8(v,cm,index,c);}
    }
#endif
   }
   uint64_t offset=j*64;
   if(!Tail || remain-offset>=64)_mm512_storeu_si512(out+offset,v);
   else _mm512_mask_storeu_epi8(out+offset,~uint64_t(0)>>(64-(remain-offset)),v);
  }
}
extern "C" int64_t lab_decode(void*st,uint8_t*out,size_t capacity) {
 if(!st)return -1;auto&s=*(State*)st;const auto h=s.h;if(capacity<h.rawsize||(!out&&h.rawsize))return -1;
 auto ids=s.a+h.metaend,p=s.a+h.idend,pend=s.a+h.payloadend;uint64_t remain=h.rawsize;
 for(uint64_t i=0;i<h.rows;++i) {
  unsigned id;uint16_t id16;memcpy(&id16,ids,2);id=id16;ids+=2;if(id>=h.nt)return -1;
  const auto t=s.table[id];unsigned nb=(t.nv+1)/2;if(size_t(pend-p)<nb||t.len>remain)return -1;
  auto ch=(const Chunk*)(s.a+t.off);
  if (remain>=uint64_t(t.nc)*64)render<false>(ch,p,out,t,remain);else render<true>(ch,p,out,t,remain);
  out+=t.len;remain-=t.len;p+=nb;
 }
 if(remain||p!=pend)return -1;return h.rawsize;
}
extern "C" void lab_close(void*st){free(st);}
#endif
