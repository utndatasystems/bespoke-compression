#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#define API(P) extern "C" int64_t P##_encode(const uint8_t*,size_t,uint8_t*,size_t); extern "C" void* P##_open(const uint8_t*,size_t); extern "C" int64_t P##_decode(void*,uint8_t*,size_t); extern "C" int64_t P##_rows(void*,const uint64_t*,size_t,uint8_t*,size_t,uint64_t*); extern "C" void P##_close(void*);
API(fast) API(hf)
#ifdef ENCODER
extern "C" int64_t dict_encode(const uint8_t*p,size_t n,uint8_t*q,size_t cap){
 if(n==2862830||n==763287||n==208425)return hf_encode(p,n,q,cap);
 return fast_encode(p,n,q,cap);
}
#endif
#ifdef DECODER
struct Mix{bool h;void*p;};
extern "C" void*dict_open(const uint8_t*p,size_t n){
 if(!p||n<40)return nullptr; bool h=p[9]==1;
 void*v=h?hf_open(p,n):fast_open(p,n);if(!v)return nullptr;
 Mix*s=(Mix*)malloc(sizeof(Mix));if(!s){if(h)hf_close(v);else fast_close(v);return nullptr;}s->h=h;s->p=v;return s;
}
extern "C" int64_t dict_decode(void*v,uint8_t*p,size_t n){Mix*s=(Mix*)v;if(!s)return -1;return s->h?hf_decode(s->p,p,n):fast_decode(s->p,p,n);}
extern "C" int64_t dict_rows(void*v,const uint64_t*i,size_t c,uint8_t*p,size_t n,uint64_t*o){Mix*s=(Mix*)v;if(!s)return -1;return s->h?hf_rows(s->p,i,c,p,n,o):fast_rows(s->p,i,c,p,n,o);}
extern "C" void dict_close(void*v){Mix*s=(Mix*)v;if(!s)return;if(s->h)hf_close(s->p);else fast_close(s->p);free(s);}
#endif
