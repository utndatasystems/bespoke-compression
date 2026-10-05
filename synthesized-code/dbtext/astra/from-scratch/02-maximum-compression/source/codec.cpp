#include "interface/codec.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <new>
#include "special.h"
#include "context.h"
#include "location.h"
#include "templates.h"

#ifdef LAB_ENCODER
extern "C" int64_t lab_encode(const uint8_t*raw,size_t n,uint8_t*out,size_t cap){
 try {
  if(!raw||!out||!n||n>UINT32_MAX)return -1;
  std::vector<uint8_t> data;
  uint32_t mode=special_encode(raw,n,data);
  if(!mode){ data=loc::encode(raw,n);if(!data.empty())mode=101; }
  if(!mode){
   data=ctx::encode_best(raw,n);mode=103;
   auto td=tpl::encode(raw,n);
   if(!td.empty()&&td.size()<data.size()){data.swap(td);mode=102;}
  }
  if(data.size()+4>cap)return -1;
  memcpy(out,&mode,4);memcpy(out+4,data.data(),data.size());
  return data.size()+4;
 }catch(...){return -1;}
}
#endif

#ifdef LAB_DECODER
struct State {
 uint32_t mode=0;size_t nrows=0,rawbytes=0;void*p=nullptr;SpecialState special;
 __attribute__((noinline)) ~State(){if(mode==103)delete (ctx::State*)p;else if(mode==101)delete (loc::State*)p;else if(mode==102)delete (tpl::State*)p;}
};
extern "C" void* lab_open(const uint8_t*arc,size_t size){
 State*s=nullptr;
 try {
  if(!arc||size<4)return nullptr;
  s=new State;memcpy(&s->mode,arc,4);arc+=4;size-=4;
  if(s->mode>=1&&s->mode<=4){
   if(!special_open(s->special,arc,size,s->mode)){delete s;return nullptr;}
   s->nrows=s->special.nrows;s->rawbytes=s->special.raw_size;
  }else if(s->mode==103){
   auto*t=new ctx::State(arc,size);s->p=t;
   if(!t->valid){delete s;return nullptr;}s->nrows=t->nrows;s->rawbytes=t->rawsize;
  }else if(s->mode==101){
   auto*t=new loc::State(arc,size);s->p=t;
   if(!t->valid){delete s;return nullptr;}s->nrows=t->nrows;s->rawbytes=t->rawbytes;
  }else if(s->mode==102){
   auto*t=new tpl::State(arc,size);s->p=t;
   if(!t->valid){delete s;return nullptr;}s->nrows=t->nrows;s->rawbytes=t->rawbytes;
  }else {delete s;return nullptr;}
  return s;
 }catch(...){delete s;return nullptr;}
}
extern "C" int64_t lab_decode(void*state,uint8_t*out,size_t cap){
 try {
  auto*s=(State*)state;if(!s||!out||cap<s->rawbytes)return -1;
  if(s->mode==103)return ctx::decode((ctx::State*)s->p,out,cap);
  if(s->mode==101)return loc::decode((loc::State*)s->p,out,cap);
  if(s->mode==102)return tpl::decode((tpl::State*)s->p,out,cap);
  return special_decode(s->special,out);
 }catch(...){return -1;}
}
extern "C" int64_t lab_rows(void*state,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets){
 try {
  auto*s=(State*)state;if(!s||!offsets||(!ids&&count)||(!out&&count))return -1;
  for(size_t i=0;i<count;i++)if(ids[i]>=s->nrows||(i&&ids[i]<ids[i-1]))return -1;
  if(s->mode==103)return ctx::rows((ctx::State*)s->p,ids,count,out,cap,offsets);
  if(s->mode==101)return loc::rows((loc::State*)s->p,ids,count,out,cap,offsets);
  if(s->mode==102)return tpl::rows((tpl::State*)s->p,ids,count,out,cap,offsets);
  offsets[0]=0;size_t pos=0;
  for(size_t i=0;i<count;i++){
   size_t len=special_row_size(s->special,ids[i]);if(len>cap-pos)return -1;
   pos+=special_row(s->special,ids[i],out+pos);offsets[i+1]=pos;
  }
  return pos;
 }catch(...){return -1;}
}
extern "C" void lab_close(void*state){delete (State*)state;}
#endif
