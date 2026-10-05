#include "interface/codec.h"
#include "fixed.h"
#include "location.h"
#include "prefix.h"
#include "text.h"
#include "text_simd_candidate.h"

#include <new>
#include <cstdlib>
#include <lz4.h>
#include <lz4hc.h>
static constexpr uint32_t TXC=0x31435a54;
static uint32_t rd32(const uint8_t*p){uint32_t v;memcpy(&v,p,4);return v;}
#ifdef ENCODER
static std::vector<uint8_t> ziptext(std::vector<uint8_t>&a){
 txt::State s(a.data());size_t pre=s.dict-a.data(),ds=17*s.h->dict,post=a.size()-pre-ds;
 std::vector<uint8_t> c(LZ4_compressBound(ds));int cs=LZ4_compress_HC((const char*)s.dict,(char*)c.data(),ds,c.size(),12);if(cs<=0)return {};
 std::vector<uint8_t> z(16+pre+post+cs);uint32_t hd[4]={TXC,(uint32_t)pre,(uint32_t)post,(uint32_t)cs};memcpy(z.data(),hd,16);memcpy(z.data()+16,a.data(),pre);memcpy(z.data()+16+pre,a.data()+pre+ds,post);memcpy(z.data()+16+pre+post,c.data(),cs);return z;
}
extern "C" int64_t lab_encode(const uint8_t*raw,size_t size,uint8_t*out,size_t cap){
 try{std::vector<uint8_t> a;
 if(!fixed_encode(raw,size,a)){a.clear();if(!loc::encode(raw,size,a)){a.clear();if(!pfx::encode(raw,size,a)){a=txt::encode(raw,size);a=ziptext(a);}}}
 if(a.empty()||a.size()>cap)return -1;memcpy(out,a.data(),a.size());return a.size();
 }catch(...){return -1;}
}
#else
struct LabState{const uint8_t* a;size_t size;int mode;txt::State t;uint8_t*owned;pfx::State*px;};
extern "C" void* lab_open(const uint8_t*a,size_t z){
 if(!a||z<8)return nullptr;int mode=0;uint32_t magic;memcpy(&magic,a,4);
 if(fixed_detect(a)){if(!fixed_valid(a,z))return nullptr;mode=1;}
 else if(magic==0x31434f4c){if(!loc::valid(a,z))return nullptr;mode=2;}
 else if(magic==pfx::MAGIC){if(!pfx::valid(a,z))return nullptr;mode=3;}
 else if(magic==TXC){if(z<16+sizeof(txt::Header))return nullptr;mode=4;}
 else return nullptr;
 auto*s=new(std::nothrow) LabState{a,z,mode,{},nullptr,nullptr};if(!s)return nullptr;
 if(mode==3){s->px=new(std::nothrow)pfx::State(a);if(!s->px || !s->px->ok){delete s->px;delete s;return nullptr;}}
 if(mode==4){
  uint64_t pre=rd32(a+4),post=rd32(a+8),cs=rd32(a+12);
  const auto*h=(const txt::Header*)(a+16);
  if(pre+post+cs+16!=z||h->dict!=4096||((h->shift&255)>16 || (h->shift&~0x1f03ffu) || txt::index_bits(*h)<8 || txt::index_bits(*h)>16)||h->raw>0xffffffffull||h->rows>h->raw||h->tokens>h->raw*2
  ||pre!=sizeof(txt::Header)+4ull*((h->rows>>(h->shift&255))+1)+txt::index_size(*h)||post!=(h->tokens*12ull+7)/8+4||cs>0x7fffffff){delete s;return nullptr;}
  s->owned=(uint8_t*)malloc(((h->shift&512)?25:17)*h->dict);if(!s->owned){delete s;return nullptr;}
  if(LZ4_decompress_safe((const char*)a+16+pre+post,(char*)s->owned,cs,17*h->dict)!=int(17*h->dict)){free(s->owned);delete s;return nullptr;}
  s->t.h=h;s->t.base=(const uint32_t*)(a+16+sizeof(txt::Header));s->t.ix=(const uint8_t*)(s->t.base+((h->rows>>(h->shift&255))+1));s->t.dict=s->owned;s->t.len=s->owned+16*h->dict;s->t.tok=a+16+pre;
  for(unsigned i=0;i<h->dict;i++)if(s->t.len[i]>16 || ((h->shift&512)&&s->t.len[i]>7) || ((h->shift&256) && (s->t.len[i]>15 || s->t.dict[16*i+15]!=s->t.len[i]))){free(s->owned);delete s;return nullptr;}
  if(h->shift&512){uint8_t*lo=s->owned+17*h->dict;s->t.lo=lo;for(unsigned i=0;i<h->dict;i++){memcpy(lo+8*i,s->t.dict+16*i,7);lo[8*i+7]=s->t.len[i];}}
  if(s->t.at(0)!=0||s->t.at(h->rows)!=h->tokens){free(s->owned);delete s;return nullptr;}
 }
 return s;
}
extern "C" int64_t lab_decode(void* v,uint8_t*out,size_t cap){
 if(!v||!out)return -1;auto&s=*(LabState*)v;
 switch(s.mode){case 1:return fixed_decode(s.a,out,cap);case 2:return loc::decode(loc::State(s.a),out,cap);case 3:return pfx::decode(*s.px,out,cap);case 4:return txt::decode_simd_register(s.t,out,cap);}return -1;
}
extern "C" int64_t lab_rows(void*v,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*off){
 if(!v||!off||(!ids&&count)||(!out&&count))return -1;if(!count){off[0]=0;return 0;}auto&s=*(LabState*)v;
 switch(s.mode){case 1:return fixed_rows(s.a,ids,count,out,cap,off);case 2:return loc::rows(loc::State(s.a),ids,count,out,cap,off);case 3:return pfx::rows(*s.px,ids,count,out,cap,off);case 4:return txt::rows(s.t,ids,count,out,cap,off);}return -1;
}
extern "C" void lab_close(void*v){if(v){auto*s=(LabState*)v;free(s->owned);delete s->px;delete s;}}
#endif
