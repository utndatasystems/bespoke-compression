// Dataset-specialized, independently reconstructing decoder.
// Grammar and token stream are wholly contained in the charged archive.
#include "codec.h"
#include <cstring>
#include <cstdlib>
#include <immintrin.h>
#ifndef UNROLL
#define UNROLL 32
#endif
static_assert(UNROLL%8==0,"UNROLL must be divisible by 8");
// Archives may start at arbitrary byte alignment.
static inline uint16_t rd16(const uint8_t*p){uint16_t x;memcpy(&x,p,2);return x;}
static inline uint32_t rd32(const uint8_t*p){uint32_t x;memcpy(&x,p,4);return x;}
static inline uint32_t token(const uint8_t*p,uint32_t i){uint64_t bit=(uint64_t)i*15;return (rd32(p+(bit>>3))>>(bit&7))&32767;}
struct Ent{uint32_t packed;uint32_t off()const{return packed&0x1ffffff;}uint32_t len()const{return packed>>25;}Ent()=default;Ent(uint32_t o,uint32_t l):packed(o|(l<<25)){} };
struct State{uint32_t n,nd,nc;const uint8_t*t;Ent*e;uint8_t*dict;};
static inline void cp(uint8_t*d,const uint8_t*s,uint32_t n){
 _mm256_storeu_si256((__m256i*)d,_mm256_loadu_si256((const __m256i*)s));
 _mm256_storeu_si256((__m256i*)(d+32),_mm256_loadu_si256((const __m256i*)(s+32)));
}
extern "C" void* lab_open(const uint8_t*a,size_t sz){
 if(!a||sz<32)return nullptr;uint32_t h[8];memcpy(h,a,32);
 if(h[0]!=0x32435052||!h[1]||h[1]>(1u<<30)||!h[2]||h[2]>32768||h[7]!=15||!h[5]||h[5]>h[2]||h[4]>h[1]||h[6]>=(1u<<25))return nullptr;
 uint32_t n=h[1],nd=h[2],nc=h[4],base=h[5],db=h[3],expanded=h[6];
 uint64_t total=32ull+4ull*(base+1)+db+64ull+4ull*(nd-base)+(15ull*nc+7)/8+4;if(total!=sz||expanded<db)return nullptr;
 State*s=(State*)malloc(sizeof(State)+sizeof(Ent)*nd+expanded+64);if(!s)return nullptr;
 s->e=(Ent*)(s+1);s->dict=(uint8_t*)(s->e+nd);s->n=n;s->nd=nd;s->nc=nc;const uint8_t*off=a+32;auto data=a+32+4*(base+1);const uint8_t*rule=data+db+64;s->t=rule+4*(nd-base);
 if(rd32(off+4*(0))!=0||rd32(off+4*(base))!=db){free(s);return nullptr;}
 for(uint32_t i=0;i<base;i++){if(rd32(off+4*(i+1))<rd32(off+4*(i))||rd32(off+4*(i+1))>db||rd32(off+4*(i+1))-rd32(off+4*(i))>64){free(s);return nullptr;}s->e[i]={rd32(off+4*(i)),rd32(off+4*(i+1))-rd32(off+4*(i))};}
 memcpy(s->dict,data,db);uint64_t used=db;
 for(uint32_t i=base;i<nd;i++){uint32_t ai=rd16(rule),bi=rd16(rule+2);rule+=4;if(ai>=i||bi>=i){free(s);return nullptr;}auto ea=s->e[ai],eb=s->e[bi];uint64_t len=(uint64_t)ea.len()+eb.len();if(len>64||used+len>expanded){free(s);return nullptr;}s->e[i]={(uint32_t)used,(uint32_t)len};cp(s->dict+used,s->dict+ea.off(),ea.len());cp(s->dict+used+ea.len(),s->dict+eb.off(),eb.len());used+=len;}
 if(used!=expanded){free(s);return nullptr;}return s;
}
extern "C" int64_t lab_decode(void*v,uint8_t*out,size_t cap){
 auto*s=(State*)v;if(!s||!out||cap<s->n)return -1;uint8_t*p=out;size_t remaining=s->n;uint32_t i=0;
#if UNROLL > 1
 for(;i+UNROLL<=s->nc;i+=UNROLL){
  const uint8_t*ti=s->t+(uint64_t(i)*15/8);Ent es[UNROLL];uint64_t sum=0;
  _Pragma("GCC unroll 32")
  for(unsigned k=0;k<UNROLL;k++){uint32_t id=((rd32(ti+(k*15/8))>>((k*15)&7))&32767);if(id>=s->nd)return -1;es[k]=s->e[id];sum+=es[k].len();}
  if(sum>remaining)return -1;
  if(sum+64<=remaining){for(unsigned k=0;k<UNROLL;k++){cp(p,s->dict+es[k].off(),es[k].len());p+=es[k].len();}}
  else{for(unsigned k=0;k<UNROLL;k++){memcpy(p,s->dict+es[k].off(),es[k].len());p+=es[k].len();}}
  remaining-=sum;
 }
#endif
 for(;i<s->nc;i++){uint32_t id=token(s->t,i);if(id>=s->nd)return -1;auto e=s->e[id];if(e.len()>remaining)return -1;if(e.len()+64ull<=remaining)cp(p,s->dict+e.off(),e.len());else memcpy(p,s->dict+e.off(),e.len());p+=e.len();remaining-=e.len();}
 return remaining?-1:s->n;
}
extern "C" void lab_close(void*s){free(s);}
