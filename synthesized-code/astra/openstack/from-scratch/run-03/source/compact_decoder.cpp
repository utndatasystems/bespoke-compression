#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include "codec.h"
#include "fast_fields.h"
static uint16_t r16(const uint8_t*p){uint16_t x;memcpy(&x,p,2);return x;}
static uint32_t r32(const uint8_t*p){uint32_t x;memcpy(&x,p,4);return x;}
static uint64_t r64(const uint8_t*p){uint64_t x;memcpy(&x,p,8);return x;}
struct T;struct S;using Fn=bool(*)(T&,S&,uint8_t*,const uint8_t*,uint32_t);
struct F{const uint8_t*dict;uint16_t off,n;uint8_t width,type,nb;};
struct T{Fn fn;const uint8_t*lit;F*f;uint16_t len,toff,nf,rb;};
struct S{const uint8_t*rows,*end;uint8_t*uu;T*t;F*arena;uint64_t raw;uint32_t nl,nt,nu;};
#include "compact_generated.h"
extern "C" void lab_close(void*v){S*s=(S*)v;if(!s)return;free(s->arena);free(s->t);free(s->uu);free(s);}
extern "C" void*lab_open(const uint8_t*a,size_t n){
 if(!a||n<40||r64(a)!=0x31474f4c504d5453ull)return nullptr;uint64_t raw=r64(a+8),ro=r64(a+32);uint32_t nl=r32(a+16),nt=r32(a+20),nu=r32(a+24);
 if(!raw||raw>1000000000||!nl||nl>raw||!nt||nt>65535||nu>65535||ro>n||ro<40+nu*16ull)return nullptr;
 S*s=(S*)calloc(1,sizeof(S));if(!s)return nullptr;s->raw=raw;s->nl=nl;s->nt=nt;s->nu=nu;s->end=a+n;s->rows=a+ro;
 s->t=(T*)calloc(nt,sizeof(T));s->uu=(uint8_t*)malloc(nu*36ull+1);s->arena=(F*)malloc((ro/8+1)*sizeof(F));if(!s->t||!s->uu||!s->arena){lab_close(s);return nullptr;}
 const uint8_t*p=a+40;for(uint32_t i=0;i<nu;i++,p+=16)ff_uuid(s->uu+i*36,p);
 F*cursor=s->arena;for(uint32_t i=0;i<nt;i++){T&t=s->t[i];if((size_t)(a+ro-p)<8){lab_close(s);return nullptr;}t.len=r16(p);t.toff=r16(p+2);t.nf=r16(p+4);t.rb=r16(p+6);p+=8;
  if(t.len<12||t.toff+12>t.len||t.nf>t.len||t.len>(size_t)(a+ro-p)){lab_close(s);return nullptr;}t.lit=p;p+=t.len;if(t.nf>(ro/8+1)-(size_t)(cursor-s->arena)){lab_close(s);return nullptr;}t.f=cursor;cursor+=t.nf;uint32_t rb=0;
  for(unsigned j=0;j<t.nf;j++){if((size_t)(a+ro-p)<8){lab_close(s);return nullptr;}F&f=t.f[j];f.off=r16(p);f.width=p[2];f.type=p[3];f.nb=p[4];f.n=r16(p+6);p+=8;rb+=f.nb;
   if(f.off+f.width>t.len||f.width==0||f.type>3||(f.type==0&&(f.width>9||f.nb<1||f.nb>4))||(f.type==1&&(f.width!=36||f.nb!=16))||(f.type==2&&(f.width!=36||f.nb!=2))||(f.type==3&&((f.nb!=1&&f.nb!=2)||!f.n))){lab_close(s);return nullptr;}
   if(f.type==3){size_t dn=(size_t)f.n*f.width;if(dn>(size_t)(a+ro-p)){lab_close(s);return nullptr;}f.dict=p;p+=dn;}
  }uint64_t hash=14695981039346656037ull;auto hh=[&](unsigned x){hash=(hash^x)*1099511628211ull;};hh(t.len);hh(t.toff);hh(t.nf);for(unsigned j=0;j<t.nf;j++){F&f=t.f[j];hh(f.off);hh(f.width);hh(f.type);hh(f.nb);}t.fn=alt_lookup(hash,t);if(rb!=t.rb){lab_close(s);return nullptr;}
 }if(p!=a+ro){lab_close(s);return nullptr;}return s;
}
static inline void smallcopy(uint8_t*d,const uint8_t*s,unsigned n){switch(n){case 1:*d=*s;break;case 2:memcpy(d,s,2);break;case 3:memcpy(d,s,2);d[2]=s[2];break;case 4:memcpy(d,s,4);break;case 5:memcpy(d,s,4);d[4]=s[4];break;case 7:memcpy(d,s,4);memcpy(d+3,s+3,4);break;case 8:memcpy(d,s,8);break;case 36:memcpy(d,s,32);memcpy(d+32,s+32,4);break;default:memcpy(d,s,n);}}
static inline void linecopy(uint8_t*d,const uint8_t*s,unsigned n){
 if(n>=64){unsigned z=0;for(;z+64<n;z+=64)_mm512_storeu_si512(d+z,_mm512_loadu_si512(s+z));_mm512_storeu_si512(d+n-64,_mm512_loadu_si512(s+n-64));}
 else memcpy(d,s,n);
}
extern "C" int64_t lab_decode(void*v,uint8_t*out,size_t cap){S*s=(S*)v;if(!s||!out||cap<s->raw)return -1;const uint8_t*p=s->rows,*end=s->end;uint8_t*d=out;size_t remain=s->raw;uint32_t ms=0;
 for(uint32_t i=0;i<s->nl;i++){if(end-p<3)return -1;uint32_t id=*p++;if(id==255){if(end-p<4)return -1;id=r16(p);p+=2;}if(id>=s->nt)return -1;T&t=s->t[id];if(t.len>remain)return -1;uint32_t delta=r16(p);p+=2;if(delta==65535){if(end-p<4)return -1;ms=r32(p);p+=4;}else ms+=delta;if(ms>=86400000||(size_t)(end-p)<t.rb)return -1;
  if(t.fn){if(!t.fn(t,*s,d,p,ms))return -1;p+=t.rb;}else {linecopy(d,t.lit,t.len);ff_time(d+t.toff,ms);
  for(unsigned j=0;j<t.nf;j++){F&f=t.f[j];uint8_t*dst=d+f.off;if(f.type==1){ff_uuid(dst,p);}else{uint32_t x;if(f.nb==1)x=*p;else if(f.nb==2)x=r16(p);else if(f.nb==3)x=r16(p)|(uint32_t)p[2]<<16;else x=r32(p);
    if(f.type==0){if(f.width==7)ff_u32_7(dst,x);else if(f.width==8){if(x>=100000000)return -1;ff_u32_8(dst,x);}else ff_decimal(dst,x,f.width);}else if(f.type==2){if(x>=s->nu)return -1;smallcopy(dst,s->uu+x*36,36);}else{if(x>=f.n)return -1;smallcopy(dst,f.dict+x*f.width,f.width);}}
   p+=f.nb;
  }}d+=t.len;remain-=t.len;
 }if(remain||p!=end)return -1;return s->raw;
}
