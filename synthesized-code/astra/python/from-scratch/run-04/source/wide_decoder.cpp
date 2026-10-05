
#include <cstdlib>
#include <immintrin.h>
#include "advanced_lzpack.h"
#include "codec.h"
struct State{const uint8_t*p[6];size_t sz[6],raw[6],n,ns;};

static bool parseState(State&s,const uint8_t*a,size_t size){
 if(size<120||get64(a)!=MAGIC)return false;
 size_t n=get64(a+8),ns=get64(a+16);
 if(n>1000000000||!ns||ns>n+1)return false;
 s.n=n;s.ns=ns;size_t pos=120;
 for(int k=0;k<6;++k){
  size_t raw=get64(a+24+k*16),sz=get64(a+32+k*16);
  if(sz>size-pos||raw>n*4+64)return false;
  s.raw[k]=raw;s.sz[k]=sz;s.p[k]=a+pos;pos+=sz;
 }
 return pos==size&&s.raw[0]==ns&&s.raw[1]==ns&&s.raw[2]==ns&&s.raw[3]==s.sz[3]&&s.raw[4]==s.sz[4]&&s.sz[4]>=8&&s.raw[5]<=n;
}
struct BulkState {size_t n,nb,scratch;State block[1];};
extern "C" void* lab_open(const uint8_t*a,size_t size){
 if(size<24||get64(a)!=MAGIC+1)return nullptr;
 size_t n=get64(a+8),nb=get64(a+16);
 if(n>1000000000||!nb||nb>(size-24)/128)return nullptr;
 BulkState*s=(BulkState*)malloc(sizeof(BulkState)+(nb-1)*sizeof(State));if(!s)return nullptr;
 s->n=n;s->nb=nb;s->scratch=0;size_t pos=24,total=0;
 for(size_t i=0;i<nb;++i){
  if(size-pos<8){free(s);return nullptr;}
  size_t len=get64(a+pos);pos+=8;
  State&t=s->block[i];
  if(len>size-pos||!parseState(t,a+pos,len)||t.n>n-total){free(s);return nullptr;}
  pos+=len;total+=t.n;s->scratch=std::max(s->scratch,t.ns*7+t.raw[5]+128);
 }
 if(pos!=size||total!=n){free(s);return nullptr;}
 return s;
}
static inline void cp8(void*d,const void*s){uint64_t v;memcpy(&v,s,8);memcpy(d,&v,8);}
static inline void cp16(void*d,const void*s){auto v=_mm_loadu_si128((const __m128i*)s);_mm_storeu_si128((__m128i*)d,v);}
static const uint8_t masks[16][16]={
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1},
{0,1,2,0,1,2,0,1,2,0,1,2,0,1,2,0},
{0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3},
{0,1,2,3,4,0,1,2,3,4,0,1,2,3,4,0},
{0,1,2,3,4,5,0,1,2,3,4,5,0,1,2,3},
{0,1,2,3,4,5,6,0,1,2,3,4,5,6,0,1},
{0,1,2,3,4,5,6,7,0,1,2,3,4,5,6,7},
{0,1,2,3,4,5,6,7,8,0,1,2,3,4,5,6},
{0,1,2,3,4,5,6,7,8,9,0,1,2,3,4,5},
{0,1,2,3,4,5,6,7,8,9,10,0,1,2,3,4},
{0,1,2,3,4,5,6,7,8,9,10,11,0,1,2,3},
{0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2},
{0,1,2,3,4,5,6,7,8,9,10,11,12,13,0,1},
{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,0}
};
static inline void fastcopy(uint8_t*p,size_t m,size_t d){
 const uint8_t*q=p-d;
 if(d>=16){cp16(p,q);if(m<=16)return;size_t j=16;do{cp16(p+j,q+j);j+=16;}while(j<m);}
 else {auto v=_mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)q),_mm_loadu_si128((const __m128i*)masks[d]));_mm_storeu_si128((__m128i*)p,v);if(m<=16)return;d*=((15+d)/d);size_t j=16;do{cp16(p+j,p+j-d);j+=16;}while(j<m);}
}
static inline void lzcopy(uint8_t*o,size_t at,size_t n,size_t m,size_t d){
 if(at+m+16<=n){fastcopy(o+at,m,d);return;}
 for(size_t j=0;j<m;++j)o[at+j]=o[at+j-d];
}
#ifndef BATCH
#define BATCH 64
#endif

static bool unpack_offsets(const uint8_t*cl,size_t ns,const uint8_t*bits,size_t sz,uint32_t*out){
 if(sz<8||sz>=(size_t(1)<<29))return false;size_t bp=0,i=0;
 const __m512i v1=_mm512_set1_epi32(1);
 const __m512i id1=_mm512_setr_epi32(0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14);
 const __m512i id2=_mm512_setr_epi32(0,0,0,1,2,3,4,5,6,7,8,9,10,11,12,13);
 const __m512i id4=_mm512_setr_epi32(0,0,0,0,0,1,2,3,4,5,6,7,8,9,10,11);
 const __m512i id8=_mm512_setr_epi32(0,0,0,0,0,0,0,0,0,1,2,3,4,5,6,7);
 for(;i+16<=ns;i+=16){
  __m512i k=_mm512_cvtepu8_epi32(_mm_loadu_si128((const __m128i*)(cl+i)));
  if(_mm512_cmp_epu32_mask(k,_mm512_set1_epi32(25),_MM_CMPINT_GT)){
   for(unsigned j=0;j<16;++j){unsigned kk=cl[i+j];if(kk>30||(bp>>3)>sz-8)return false;out[i+j]=(1u<<kk)+((get64(bits+(bp>>3))>>(bp&7))&((1u<<kk)-1));bp+=kk;}continue;
  }
  __m512i pref=k;
  pref=_mm512_mask_add_epi32(pref,0xfffe,pref,_mm512_permutexvar_epi32(id1,pref));
  pref=_mm512_mask_add_epi32(pref,0xfffc,pref,_mm512_permutexvar_epi32(id2,pref));
  pref=_mm512_mask_add_epi32(pref,0xfff0,pref,_mm512_permutexvar_epi32(id4,pref));
  pref=_mm512_mask_add_epi32(pref,0xff00,pref,_mm512_permutexvar_epi32(id8,pref));
  unsigned sum=_mm_extract_epi32(_mm512_extracti32x4_epi32(pref,3),3);
  if(bp+sum>uint64_t(sz-8)*8)return false;
  __m512i pos=_mm512_add_epi32(_mm512_sub_epi32(pref,k),_mm512_set1_epi32(bp));
  __m512i vals=_mm512_i32gather_epi32(_mm512_srli_epi32(pos,3),bits,1);
  vals=_mm512_srlv_epi32(vals,_mm512_and_si512(pos,_mm512_set1_epi32(7)));
  __m512i base=_mm512_sllv_epi32(v1,k);
  vals=_mm512_add_epi32(base,_mm512_and_si512(vals,_mm512_sub_epi32(base,v1)));
  _mm512_storeu_si512(out+i,vals);bp+=sum;
 }
 for(;i<ns;++i){unsigned k=cl[i];if(k>30||(bp>>3)>sz-8)return false;out[i]=(1u<<k)+((get64(bits+(bp>>3))>>(bp&7))&((1u<<k)-1));bp+=k;}
 return (bp+7)/8+8==sz;
}
static inline __m512i prefix16(__m512i pref){
 const __m512i id1=_mm512_setr_epi32(0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14);
 const __m512i id2=_mm512_setr_epi32(0,0,0,1,2,3,4,5,6,7,8,9,10,11,12,13);
 const __m512i id4=_mm512_setr_epi32(0,0,0,0,0,1,2,3,4,5,6,7,8,9,10,11);
 const __m512i id8=_mm512_setr_epi32(0,0,0,0,0,0,0,0,0,1,2,3,4,5,6,7);
 pref=_mm512_mask_add_epi32(pref,0xfffe,pref,_mm512_permutexvar_epi32(id1,pref));
 pref=_mm512_mask_add_epi32(pref,0xfffc,pref,_mm512_permutexvar_epi32(id2,pref));
 pref=_mm512_mask_add_epi32(pref,0xfff0,pref,_mm512_permutexvar_epi32(id4,pref));
 return _mm512_mask_add_epi32(pref,0xff00,pref,_mm512_permutexvar_epi32(id8,pref));
}
static inline unsigned last16(__m512i v){return _mm_extract_epi32(_mm512_extracti32x4_epi32(v,3),3);}
static int64_t reconstruct(State&s,uint8_t*out,size_t globaln,size_t start,uint8_t*mem){
 const size_t ns=s.ns,end=start+s.n;if(end>globaln||end<start)return -1;
 uint8_t *ll=mem,*ml=ll+ns,*dc=ml+ns,*lit=dc+ns;
 if(!ent::decompress(s.p[0],s.sz[0],ll,ns)||!ent::decompress(s.p[1],s.sz[1],ml,ns)||!ent::decompress(s.p[2],s.sz[2],dc,ns)||!ent::decompress(s.p[5],s.sz[5],lit,s.raw[5]))return -1;
 uint32_t*dist=(uint32_t*)((uintptr_t(lit+s.raw[5]+64)+3)&~uintptr_t(3));
 if(!unpack_offsets(dc,ns,s.p[4],s.sz[4],dist))return -1;
 const uint8_t*ext=s.p[3],*ee=ext+s.sz[3];size_t at=start,lp=0;
 alignas(64) uint32_t tl[BATCH],tm[BATCH],tp[BATCH],tli[BATCH];
 for(size_t i=0;i<ns;i+=BATCH){
  unsigned count=std::min(size_t(BATCH),ns-i),j=0;
  for(;j+16<=count;j+=16){
   __m512i vl=_mm512_cvtepu8_epi32(_mm_loadu_si128((const __m128i*)(ll+i+j)));
   __m512i vm=_mm512_cvtepu8_epi32(_mm_loadu_si128((const __m128i*)(ml+i+j)));
   if(_mm512_cmpeq_epi32_mask(vl,_mm512_set1_epi32(255))||_mm512_cmpeq_epi32_mask(vm,_mm512_set1_epi32(255))||_mm512_cmpeq_epi32_mask(vm,_mm512_setzero_si512())){
    for(unsigned z=0;z<16;++z){size_t l=ll[i+j+z],m=ml[i+j+z];
     if(l==255){if(ee-ext<4)return -1;l=get32(ext);ext+=4;}
     if(m==255){if(ee-ext<4)return -1;m=get32(ext);ext+=4;}if(m)m+=3;
     uint32_t d=dist[i+j+z];if(at+l+m>end||lp+l>s.raw[5]||(m&&d>at+l))return -1;
     tl[j+z]=l;tm[j+z]=m;tp[j+z]=at;tli[j+z]=lp;
     if(m)__builtin_prefetch(out+at+l-d,0,1);lp+=l;at+=l+m;
    }
    continue;
   }
   vm=_mm512_add_epi32(vm,_mm512_set1_epi32(3));
   __m512i vi=_mm512_add_epi32(vl,vm),pi=prefix16(vi),pl=prefix16(vl);
   unsigned sum=last16(pi),lsum=last16(pl);
   if(at+sum>end||lp+lsum>s.raw[5])return -1;
   __m512i pos=_mm512_add_epi32(_mm512_set1_epi32(at),_mm512_sub_epi32(pi,vi));
   __m512i lpos=_mm512_add_epi32(_mm512_set1_epi32(lp),_mm512_sub_epi32(pl,vl));
   __m512i vd=_mm512_loadu_si512(dist+i+j);
   if(_mm512_cmp_epu32_mask(vd,_mm512_add_epi32(pos,vl),_MM_CMPINT_GT))return -1;
   _mm512_store_si512(tl+j,vl);_mm512_store_si512(tm+j,vm);_mm512_store_si512(tp+j,pos);_mm512_store_si512(tli+j,lpos);
   at+=sum;lp+=lsum;
   for(unsigned z=0;z<16;++z)__builtin_prefetch(out+tp[j+z]+tl[j+z]-dist[i+j+z],0,1);
  }
  for(;j<count;++j){size_t l=ll[i+j],m=ml[i+j];
   if(l==255){if(ee-ext<4)return -1;l=get32(ext);ext+=4;}
   if(m==255){if(ee-ext<4)return -1;m=get32(ext);ext+=4;}if(m)m+=3;
   uint32_t d=dist[i+j];if(at+l+m>end||lp+l>s.raw[5]||(m&&d>at+l))return -1;
   tl[j]=l;tm[j]=m;tp[j]=at;tli[j]=lp;if(m)__builtin_prefetch(out+at+l-d,0,1);lp+=l;at+=l+m;
  }
  if(at+16<=globaln){
   for(unsigned z=0;z<count;++z){uint8_t*p=out+tp[z];if(tl[z]<=16)cp16(p,lit+tli[z]);else memcpy(p,lit+tli[z],tl[z]);if(tm[z])fastcopy(p+tl[z],tm[z],dist[i+z]);}
  }else{
   for(unsigned z=0;z<count;++z){uint8_t*p=out+tp[z];memcpy(p,lit+tli[z],tl[z]);if(tm[z])lzcopy(out,tp[z]+tl[z],globaln,tm[z],dist[i+z]);}
  }
 }

 return at==end&&lp==s.raw[5]&&ext==ee?(int64_t)s.n:-1;
}

extern "C" int64_t lab_decode(void*st,uint8_t*out,size_t cap){
 if(!st)return -1;BulkState&s=*(BulkState*)st;if(cap<s.n)return -1;
 uint8_t*mem=(uint8_t*)malloc(s.scratch);if(!mem)return -1;
 size_t at=0;
 for(size_t i=0;i<s.nb;++i){
  int64_t r=reconstruct(s.block[i],out,s.n,at,mem);
  if(r<0){free(mem);return -1;}at+=r;
 }
 free(mem);return at;
}
extern "C" void lab_close(void*s){free(s);}
