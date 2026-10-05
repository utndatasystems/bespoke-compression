#pragma once
#include "phrase.h"
#include <immintrin.h>
#include <new>
namespace phrasefast {
using Header=phrase::Header;
static constexpr uint32_t MAGIC=phrase::MAGIC;
// Every encoder phrase is at most 24 bytes. Its length occupies an unused
// byte of a 32-byte dictionary slot; wide copies may overwrite future output.
struct alignas(32) Entry { uint8_t data[31]; uint8_t len; };
struct State {Header h;const uint8_t* ar;size_t size;const uint8_t*lens,*index,*toks;Entry*dict;};
static uint32_t r32(const void*p){uint32_t v;memcpy(&v,p,4);return v;}
static uint64_t r64(const void*p){uint64_t v;memcpy(&v,p,8);return v;}
#ifdef ENCODER
inline bool encode(const uint8_t*r,size_t n,std::vector<uint8_t>&o){return phrase::encode(r,n,o);}
#endif
inline void* open(const uint8_t*a,size_t n){
 if(n<sizeof(Header))return nullptr;Header h;memcpy(&h,a,sizeof(h));
 if(h.magic!=MAGIC||!h.ndict||h.ndict>65535||h.bits<1||h.bits>16||h.ndict>(1u<<h.bits)||h.nblocks!=(uint64_t(h.nrow)+31)/32||h.nraw>100000000)return nullptr;
 size_t p=sizeof(h),end=p+h.dictbytes;
 if(end>n||h.nrow>n-end)return nullptr;
 size_t idx=(end+h.nrow+3)&~size_t(3);
 if(idx>n||4ull*h.nblocks>n-idx)return nullptr;
 size_t t=idx+4ull*h.nblocks,ts=(uint64_t(h.ntok)*h.bits+7)/8;
 if(ts>n-t||ts+8!=n-t)return nullptr;
 Entry*d=(Entry*)aligned_alloc(32,(1ull<<h.bits)*sizeof(Entry));if(!d)return nullptr;
 memset(d,0,(1ull<<h.bits)*sizeof(Entry));
 for(unsigned i=0;i<h.ndict;i++){if(p>=end||a[p]==0||a[p]>24||size_t(1)+a[p]>end-p){free(d);return nullptr;}unsigned len=a[p++];d[i].len=len;memcpy(d[i].data,a+p,len);p+=len;}
 if(p!=end){free(d);return nullptr;}
 State*s=new(std::nothrow) State{h,a,n,a+end,a+idx,a+t,d};if(!s)free(d);return s;
}
template<unsigned B> inline uint8_t* unpack(const State*s,uint32_t start,uint32_t count,uint8_t*out,uint8_t*end){
 if(start>s->h.ntok||count>s->h.ntok-start)return nullptr;
 uint64_t bit=uint64_t(start)*B;constexpr uint32_t M=(1u<<B)-1;
 constexpr unsigned G=B<=14?4:3;
 while(count>=G&&size_t(end-out)>=32*G){
  uint64_t x=r64(s->toks+(bit>>3))>>(bit&7);
  const Entry&e0=s->dict[x&M];x>>=B;
  const Entry&e1=s->dict[x&M];x>>=B;
  const Entry&e2=s->dict[x&M];x>>=B;
  unsigned l0=e0.len,l1=e1.len,l2=e2.len;
  if constexpr(G==4){
   const Entry&e3=s->dict[x&M];unsigned l3=e3.len;if(!l0||!l1||!l2||!l3)return nullptr;
   memcpy(out,e0.data,32);out+=l0;memcpy(out,e1.data,32);out+=l1;memcpy(out,e2.data,32);out+=l2;memcpy(out,e3.data,32);out+=l3;
  }else{if(!l0||!l1||!l2)return nullptr;memcpy(out,e0.data,32);out+=l0;memcpy(out,e1.data,32);out+=l1;memcpy(out,e2.data,32);out+=l2;}
  bit+=G*B;count-=G;
 }
 while(count--){unsigned v=(r32(s->toks+(bit>>3))>>(bit&7))&M;const Entry&e=s->dict[v];unsigned len=e.len;
  if(!len||len>size_t(end-out))return nullptr;
  if(size_t(end-out)>=32)memcpy(out,e.data,32);else memcpy(out,e.data,len);
  out+=len;bit+=B;
 }
 return out;
}
template<unsigned B> static int64_t decode_bits(State*s,uint8_t*out,size_t cap){
 if(cap<s->h.nraw)return -1;uint8_t*e=unpack<B>(s,0,s->h.ntok,out,out+s->h.nraw);if(!e||size_t(e-out)!=s->h.nraw)return -1;return s->h.nraw;
}
#define PHRASEFAST_CASES(F) case 12:return F<12>(s,out,cap);case 13:return F<13>(s,out,cap);
inline int64_t decode(void*p,uint8_t*out,size_t cap){State*s=(State*)p;if(!s)return -1;switch(s->h.bits){PHRASEFAST_CASES(decode_bits)}return -1;}
#undef PHRASEFAST_CASES
template<unsigned B> static int64_t rows_bits(State*s,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets){
 uint8_t*o=out;size_t q=0;
 while(q<count){uint64_t r=ids[q];if(r>=s->h.nrow||(q&&r<ids[q-1]))return -1;
  uint32_t block=r>>5,t=r32(s->index+4ull*block),prev=block*32;
  if(t>s->h.ntok)return -1;
  do{r=ids[q];if(r>=s->h.nrow||(q&&r<ids[q-1]))return -1;
   // Duplicated sorted IDs are valid as well; restart within this small group.
   if(r<prev){prev=block*32;t=r32(s->index+4ull*block);}
   while(prev<r){unsigned n=s->lens[prev++];if(n>s->h.ntok-t)return -1;t+=n;}
   unsigned n=s->lens[r];o=unpack<B>(s,t,n,o,out+cap);if(!o)return -1;
   offsets[++q]=o-out;t+=n;prev=r+1;
  }while(q<count&&(ids[q]>>5)==block);
 }
 return o-out;
}
inline int64_t rows(void*p,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets){
 State*s=(State*)p;if(!s||!offsets)return -1;offsets[0]=0;if(!count)return 0;
 if(count==s->h.nrow){
  bool all=true;for(size_t i=0;i<count;i++)if(ids[i]!=i){all=false;break;}
  if(all){int64_t n=decode(s,out,cap);if(n<0)return -1;size_t pos=0,oi=0;__m512i nl=_mm512_set1_epi8(10);
   for(;pos+64<=size_t(n);pos+=64){uint64_t m=_mm512_cmpeq_epi8_mask(_mm512_loadu_si512(out+pos),nl);while(m){if(oi>=count)return -1;offsets[++oi]=pos+__builtin_ctzll(m)+1;m&=m-1;}}
   for(;pos<size_t(n);pos++)if(out[pos]==10){if(oi>=count)return -1;offsets[++oi]=pos+1;}
   if(n&&out[n-1]!=10){if(oi>=count)return -1;offsets[++oi]=n;}return oi==count?n:-1;
  }
 }
 #define RF(B) case B:return rows_bits<B>(s,ids,count,out,cap,offsets);
 switch(s->h.bits){RF(12) RF(13)}
 #undef RF
 return -1;
}
inline void close(void*p){State*s=(State*)p;if(s){free(s->dict);delete s;}}
}
