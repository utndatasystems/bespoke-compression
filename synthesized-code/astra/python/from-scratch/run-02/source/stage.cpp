#define lab_encode core_encode
#define lab_open core_open
#define lab_decode core_decode
#define lab_close core_close
#include "codec.cpp"
#undef lab_encode
#undef lab_open
#undef lab_decode
#undef lab_close
#include "lexical.h"
#include "grammar_indent_best.h"
namespace ind=indbest;
#define API extern "C" __attribute__((visibility("default")))
#ifndef WORD_LIMIT
#define WORD_LIMIT 8192
#endif
#ifndef WORD_SINGLE
#define WORD_SINGLE 64
#endif
struct Stage { const uint8_t*dict,*ia;void*core;uint32_t n,nb,nt,nd,ds,is; };
API void* lab_open(const uint8_t*a,size_t s){
 if(!a||s<64||memcmp(a,"PYSTG003",8)||rd64(a+8)!=s||rd64(a+16)!=check(a+24,s-24))return nullptr;
 uint32_t n=rd32(a+24),nb=rd32(a+28),nt=rd32(a+32),nd=rd32(a+36),ds=rd32(a+40),is=rd32(a+44),cs=rd32(a+48);
 if(n>1000000000||nb>n||uint64_t(nt)>2ull*nb||nd>10000000||uint64_t(ds)+is+cs!=s-64)return nullptr;
 void*core=core_open(a+64+ds+is,cs);if(!core)return nullptr;
 State*lc=(State*)core;if(lc->n!=nt){core_close(core);return nullptr;}
 Stage*t=(Stage*)malloc(sizeof(Stage));if(!t){core_close(core);return nullptr;}
 *t={a+64,a+64+ds,core,n,nb,nt,nd,ds,is};return t;
}
API void lab_close(void*v){if(v){Stage*t=(Stage*)v;core_close(t->core);free(t);}}
API int64_t lab_decode(void*v,uint8_t*out,size_t cap){
 if(!v)return -1;Stage*t=(Stage*)v;if(cap<t->n||(!out&&t->n))return -1;
 uint8_t*buf=(uint8_t*)malloc(size_t(t->nt)+t->nb+t->nd+1);if(!buf)return -1;
 uint8_t*trans=buf,*body=trans+t->nt,*dict=body+t->nb;
 bool ok=ent::decode(t->dict,t->ds,dict,t->nd)&&core_decode(t->core,trans,t->nt)==t->nt&&lexical::decode(trans,t->nt,dict,t->nd,body,t->nb)&&ind::decode(body,t->nb,t->ia,t->is,out,t->n);
 free(buf);return ok?(int64_t)t->n:-1;
}
#ifndef DECODE_ONLY
API int64_t lab_encode(const uint8_t*raw,size_t n,uint8_t*out,size_t cap){
 try {
 if(n>1000000000u||(!raw&&n)||!out)return -1;
 auto ia=ind::encode(raw,n);auto lx=lexical::encode(ia.body.data(),ia.body.size(),1,WORD_LIMIT,WORD_SINGLE);auto de=ent::encode(lx.dictionary);
 size_t base=64+de.size()+ia.archive.size();if(base>cap)return -1;
 int64_t cs=core_encode(lx.transformed.data(),lx.transformed.size(),out+base,cap-base);if(cs<0)return -1;
 size_t total=base+cs;memset(out,0,64);memcpy(out,"PYSTG003",8);wr64(out+8,total);wr32(out+24,n);wr32(out+28,ia.body.size());wr32(out+32,lx.transformed.size());wr32(out+36,lx.dictionary.size());wr32(out+40,de.size());wr32(out+44,ia.archive.size());wr32(out+48,cs);
 memcpy(out+64,de.data(),de.size());memcpy(out+64+de.size(),ia.archive.data(),ia.archive.size());wr64(out+16,check(out+24,total-24));return total;
 }catch(...){return -1;}
}
#endif
