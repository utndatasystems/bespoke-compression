#pragma once
#include "gphrase.h"
#include <immintrin.h>
#include <new>
namespace gphrasefast {
using Header=gphrase::Header;
static constexpr uint32_t MAGIC=gphrase::MAGIC;
// Every encoder phrase is at most 24 bytes. Its length occupies an unused
// byte of a 32-byte dictionary slot; wide copies may overwrite future output.
struct alignas(32) Entry { uint8_t data[31]; uint8_t len; };
struct State {Header h;const uint8_t* ar;size_t size;const uint8_t*lens,*index,*toks;Entry*dict;};
static uint32_t r32(const void*p){uint32_t v;memcpy(&v,p,4);return v;}
static uint64_t r64(const void*p){uint64_t v;memcpy(&v,p,8);return v;}
#ifdef ENCODER
inline bool encode(const uint8_t*r,size_t n,std::vector<uint8_t>&o){return gphrase::encode(r,n,o);}
#endif
inline void* open(const uint8_t*a,size_t n){
 if(n<sizeof(Header))return nullptr;Header h;memcpy(&h,a,sizeof(h));
 if(h.magic!=MAGIC||!h.ndict||h.ndict>65535||h.bits<1||h.bits>16||h.ndict>(1u<<h.bits)||h.nblocks!=(uint64_t(h.nrow)+7)/8||h.nraw>100000000)return nullptr;
 size_t p=sizeof(h),end=p+h.dictbytes;
 if(end>n)return nullptr;
 size_t idx=(end+3)&~size_t(3);
 if(idx>n||3ull*h.nblocks>n-idx)return nullptr;
 size_t t=idx+3ull*h.nblocks,ts=(uint64_t(h.ntok)*h.bits+7)/8;
 if(ts>n-t||ts+8!=n-t)return nullptr;
 Entry*d=(Entry*)aligned_alloc(32,(1ull<<h.bits)*sizeof(Entry));if(!d)return nullptr;
 memset(d,0,(1ull<<h.bits)*sizeof(Entry));
 if(h.ndict<256||h.dictbytes!=4*(h.ndict-256)){free(d);return nullptr;}
 for(unsigned i=0;i<256;i++){d[i].data[0]=i;d[i].len=1;d[i].data[30]=i==10;}
 for(unsigned i=256;i<h.ndict;i++){unsigned v=r32(a+p);p+=4;unsigned aa=v&65535,bb=v>>16;if(aa>=i||bb>=i||unsigned(d[aa].len)+d[bb].len>24){free(d);return nullptr;}unsigned al=d[aa].len,bl=d[bb].len;memcpy(d[i].data,d[aa].data,24);memcpy(d[i].data+al,d[bb].data,bl);d[i].len=al+bl;d[i].data[30]=d[i].data[al+bl-1]==10;}
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
#define PHRASEFAST_CASES(F) case 1:return F<1>(s,out,cap);case 2:return F<2>(s,out,cap);case 3:return F<3>(s,out,cap);case 4:return F<4>(s,out,cap);case 5:return F<5>(s,out,cap);case 6:return F<6>(s,out,cap);case 7:return F<7>(s,out,cap);case 8:return F<8>(s,out,cap);case 9:return F<9>(s,out,cap);case 10:return F<10>(s,out,cap);case 11:return F<11>(s,out,cap);case 12:return F<12>(s,out,cap);case 13:return F<13>(s,out,cap);case 14:return F<14>(s,out,cap);case 15:return F<15>(s,out,cap);case 16:return F<16>(s,out,cap);
inline int64_t decode(void*p,uint8_t*out,size_t cap){State*s=(State*)p;if(!s)return -1;switch(s->h.bits){PHRASEFAST_CASES(decode_bits)}return -1;}
#undef PHRASEFAST_CASES
template<unsigned B> static int64_t rows_bits(State*s,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets){
 uint8_t*o=out;size_t q=0;constexpr unsigned M=(1u<<B)-1;
 while(q<count){uint64_t r=ids[q];if(r>=s->h.nrow||(q&&r<ids[q-1]))return -1;
  uint32_t block=r>>3,t=(r32(s->index+3ull*block)&0xffffff),prev=block*8;
  do{r=ids[q];if(r>=s->h.nrow||(q&&r<ids[q-1]))return -1;
   if(r<prev){prev=block*8;t=(r32(s->index+3ull*block)&0xffffff);}
   while(prev<r){bool nl=false;while(t<s->h.ntok){uint64_t bit=uint64_t(t++)*B;unsigned v=(r32(s->toks+(bit>>3))>>(bit&7))&M;const Entry&e=s->dict[v];if(!e.len)return -1;if(e.data[30]){nl=true;break;}}if(!nl)return -1;prev++;}
   if(t>=s->h.ntok)return -1;
   while(t<s->h.ntok){uint64_t bit=uint64_t(t++)*B;unsigned v=(r32(s->toks+(bit>>3))>>(bit&7))&M;const Entry&e=s->dict[v];unsigned len=e.len;if(!len||len>cap-size_t(o-out))return -1;if(cap-size_t(o-out)>=32)memcpy(o,e.data,32);else memcpy(o,e.data,len);o+=len;if(e.data[30])break;}
   offsets[++q]=o-out;prev=r+1;
  }while(q<count&&(ids[q]>>3)==block);
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
 switch(s->h.bits){RF(1) RF(2) RF(3) RF(4) RF(5) RF(6) RF(7) RF(8) RF(9) RF(10) RF(11) RF(12) RF(13) RF(14) RF(15) RF(16)}
 #undef RF
 return -1;
}
inline void close(void*p){State*s=(State*)p;if(s){free(s->dict);delete s;}}
}
