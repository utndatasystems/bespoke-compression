#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include "codec.h"
#define API(P) \
extern "C" int64_t P##_encode(const uint8_t*,size_t,uint8_t*,size_t); \
extern "C" void* P##_open(const uint8_t*,size_t); \
extern "C" int64_t P##_decode(void*,uint8_t*,size_t); \
extern "C" int64_t P##_rows(void*,const uint64_t*,size_t,uint8_t*,size_t,uint64_t*); \
extern "C" void P##_close(void*);
API(spec) API(url) API(dict)
#ifdef ENCODER
extern "C" int64_t lab_encode(const uint8_t* p,size_t n,uint8_t* q,size_t cap) {
 try {
 int64_t k=spec_encode(p,n,q,cap); if(k!=-2)return k;
 k=url_encode(p,n,q,cap); if(k!=-2)return k;
 return dict_encode(p,n,q,cap);
 }catch(...){return -1;}
}
#endif
#ifdef DECODER
struct State { uint32_t kind; void* p; };
extern "C" void* lab_open(const uint8_t* p,size_t n) {
 if(!p||n<4)return nullptr;
 uint32_t kind; memcpy(&kind,p,4);
 void* v=nullptr;
 try {
 if(kind==0x31504353)v=spec_open(p,n);
 else if(kind==0x314c5255)v=url_open(p,n);
 else if(kind==0x31544344)v=dict_open(p,n);
 else return nullptr;
 if(!v)return nullptr;
 State* s=(State*)malloc(sizeof(State));
 if(!s){if(kind==0x31504353)spec_close(v);else if(kind==0x314c5255)url_close(v);else dict_close(v);return nullptr;}
 s->kind=kind;s->p=v;return s;
 }catch(...){return nullptr;}
}
extern "C" int64_t lab_decode(void* v,uint8_t* p,size_t n) {
 if(!v)return -1; State*s=(State*)v;
 if(s->kind==0x31504353)return spec_decode(s->p,p,n);
 if(s->kind==0x314c5255)return url_decode(s->p,p,n);
 return dict_decode(s->p,p,n);
}
extern "C" int64_t lab_rows(void* v,const uint64_t*ids,size_t count,uint8_t*p,size_t n,uint64_t*offs) {
 if(!v||!offs||(count&&!ids))return -1;State*s=(State*)v;
 if(s->kind==0x31504353)return spec_rows(s->p,ids,count,p,n,offs);
 if(s->kind==0x314c5255)return url_rows(s->p,ids,count,p,n,offs);
 return dict_rows(s->p,ids,count,p,n,offs);
}
extern "C" void lab_close(void*v){
 if(!v)return;State*s=(State*)v;
 if(s->kind==0x31504353)spec_close(s->p);
 else if(s->kind==0x314c5255)url_close(s->p);
 else dict_close(s->p);
 free(s);
}
#endif
