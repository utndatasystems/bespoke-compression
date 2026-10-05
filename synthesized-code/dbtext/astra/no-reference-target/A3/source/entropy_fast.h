#pragma once
#include "entropy.h"
#include <immintrin.h>
namespace entropyfast {
using State=entropy::State;
using Header=entropy::Header;
using Entry=entropy::Entry;
static inline uint64_t get64(const void*p){uint64_t x;memcpy(&x,p,8);return x;}
static inline uint32_t get32(const void*p){uint32_t x;memcpy(&x,p,4);return x;}
inline void* open(const uint8_t* a,size_t n) {
 if(n<sizeof(Header))return nullptr;Header h;memcpy(&h,a,sizeof(h));
 if(h.magic!=entropy::MAGIC||h.ndict>32768||!h.ndict||!h.group||h.group>64||h.nblocks!=(uint64_t(h.nrow)+h.group-1)/h.group||h.nraw>100000000||h.ntok>h.nraw||h.nrow>h.nraw||(!h.nbits||h.nbits>=1u<<24))return nullptr;
 size_t packedlen=h.dictbytes&0x7fffffffu,archiveend=sizeof(h)+packedlen;
 if(archiveend>n)return nullptr;
 std::vector<uint8_t> unpacked;const uint8_t* ds=a+sizeof(h);size_t dsize=packedlen;
 if(h.dictbytes&0x80000000u){if(packedlen<4)return nullptr;uint32_t rawlen;memcpy(&rawlen,ds,4);if(rawlen>h.ndict*26ull)return nullptr;unpacked.resize(rawlen);if(!dictpack::decode(ds+4,packedlen-4,unpacked.data(),rawlen))return nullptr;ds=unpacked.data();dsize=rawlen;}
 Entry* dict=(Entry*)calloc(h.ndict,sizeof(Entry));uint32_t* table=(uint32_t*)calloc(1u<<entropy::HBITS,sizeof(uint32_t));
 if(!dict||!table){free(dict);free(table);return nullptr;}
 std::vector<uint8_t> cl(h.ndict);std::vector<uint16_t> codes(h.ndict);size_t p=0;unsigned kraft=0;
 for(unsigned i=0;i<h.ndict;++i){if(p+2>dsize||!ds[p]||ds[p]>24||!ds[p+1]||ds[p+1]>entropy::HBITS||p+2+ds[p]>dsize){free(dict);free(table);return nullptr;}dict[i].len=ds[p++];cl[i]=ds[p++];memcpy(dict[i].data,ds+p,dict[i].len);if(dict[i].len>1&&memchr(dict[i].data,10,dict[i].len-1)){free(dict);free(table);return nullptr;}p+=dict[i].len;kraft+=1u<<(entropy::HBITS-cl[i]);}
 if(p!=dsize||(kraft!=(1u<<entropy::HBITS)&&!(h.ndict==1&&cl[0]==1))){free(dict);free(table);return nullptr;}
 p=(archiveend+3)&~size_t(3);size_t afterindex=p+3ull*h.nblocks;
 if(afterindex+(uint64_t(h.nbits)+7)/8+8!=n){free(dict);free(table);return nullptr;}
 const uint8_t* index=a+p;
 uint32_t previndex=0;for(unsigned i=0;i<h.nblocks;++i){const uint8_t* entryindex=index+3ull*i;uint32_t currentindex=uint32_t(entryindex[0])|(uint32_t(entryindex[1])<<8)|(uint32_t(entryindex[2])<<16);if(currentindex>=h.nbits||(i&&currentindex<previndex)||(!i&&currentindex)){free(dict);free(table);return nullptr;}previndex=currentindex;}
 for(unsigned i=0;i<3;++i)if(h.laneBit[i]>h.nbits||h.laneRaw[i]>h.nraw||(i&&(h.laneBit[i]<h.laneBit[i-1]||h.laneRaw[i]<h.laneRaw[i-1]))){free(dict);free(table);return nullptr;}
 entropy::make_codes(cl.data(),h.ndict,codes.data());
 for(unsigned i=0;i<h.ndict;++i){unsigned len=cl[i],step=1u<<len;uint32_t ent=i|(uint32_t(len)<<16)|(dict[i].data[dict[i].len-1]==10?0x8000u:0u);for(unsigned j=codes[i];j<(1u<<entropy::HBITS);j+=step)table[j]=ent;}
 return new State{h,index,a+afterindex,dict,table};
}

static inline bool emit(const State*s,uint64_t&bit,uint64_t&word,uint64_t stop,uint8_t*&out,uint8_t*end){
 uint32_t v=s->table[word&32767];unsigned nb=v>>16;if(!nb||nb>stop-bit)return false;
 const Entry&e=s->dict[v&32767];unsigned len=e.len;if(!len||len>size_t(end-out))return false;
 if(size_t(end-out)>=32)memcpy(out,&e,32);else memcpy(out,&e,len);
 bit+=nb;word>>=nb;out+=len;return true;
}
inline int64_t decode_scalar(void*state,uint8_t*out,size_t cap){
 State*s=(State*)state;if(!s||cap<s->h.nraw)return -1;
 const unsigned q=s->h.ntok/4;uint64_t b0=0,b1=s->h.laneBit[0],b2=s->h.laneBit[1],b3=s->h.laneBit[2];
 if(b1>b2||b2>b3||b3>s->h.nbits||s->h.laneRaw[0]>s->h.laneRaw[1]||s->h.laneRaw[1]>s->h.laneRaw[2]||s->h.laneRaw[2]>s->h.nraw)return -1;
 const uint64_t z0=b1,z1=b2,z2=b3,z3=s->h.nbits;
 uint8_t*p0=out,*p1=out+s->h.laneRaw[0],*p2=out+s->h.laneRaw[1],*p3=out+s->h.laneRaw[2];
 uint8_t*e0=p1,*e1=p2,*e2=p3,*e3=out+s->h.nraw;
 unsigned j=0;
 for(;j+3<=q;j+=3){
  uint64_t w0=get64(s->toks+(b0>>3))>>(b0&7),w1=get64(s->toks+(b1>>3))>>(b1&7),w2=get64(s->toks+(b2>>3))>>(b2&7),w3=get64(s->toks+(b3>>3))>>(b3&7);
  for(int k=0;k<3;k++){if(!emit(s,b0,w0,z0,p0,e0)||!emit(s,b1,w1,z1,p1,e1)||!emit(s,b2,w2,z2,p2,e2)||!emit(s,b3,w3,z3,p3,e3))return -1;}
 }
 for(;j<q;j++){
  uint64_t w0=get64(s->toks+(b0>>3))>>(b0&7),w1=get64(s->toks+(b1>>3))>>(b1&7),w2=get64(s->toks+(b2>>3))>>(b2&7),w3=get64(s->toks+(b3>>3))>>(b3&7);
  if(!emit(s,b0,w0,z0,p0,e0)||!emit(s,b1,w1,z1,p1,e1)||!emit(s,b2,w2,z2,p2,e2)||!emit(s,b3,w3,z3,p3,e3))return -1;
 }
 for(unsigned i=q*4;i<s->h.ntok;i++){uint64_t w=get64(s->toks+(b3>>3))>>(b3&7);if(!emit(s,b3,w,z3,p3,e3))return -1;}
 if(b0!=z0||b1!=z1||b2!=z2||b3!=z3||p0!=e0||p1!=e1||p2!=e2||p3!=e3)return -1;return s->h.nraw;
}

static inline bool outentry(const Entry&e,uint8_t*&p,uint8_t*end){unsigned n=e.len;if(!n||n>size_t(end-p))return false;if(size_t(end-p)>=32)memcpy(p,&e,32);else memcpy(p,&e,n);p+=n;return true;}
inline int64_t decode_simd(void*state,uint8_t*out,size_t cap){
 State*s=(State*)state;if(!s||cap<s->h.nraw)return -1;unsigned q=s->h.ntok/4;
 uint64_t bp[4]={0,s->h.laneBit[0],s->h.laneBit[1],s->h.laneBit[2]},endbit[4]={bp[1],bp[2],bp[3],s->h.nbits};
 if(bp[1]>bp[2]||bp[2]>bp[3]||bp[3]>s->h.nbits||s->h.laneRaw[0]>s->h.laneRaw[1]||s->h.laneRaw[1]>s->h.laneRaw[2]||s->h.laneRaw[2]>s->h.nraw)return -1;
 uint8_t*p0=out,*p1=out+s->h.laneRaw[0],*p2=out+s->h.laneRaw[1],*p3=out+s->h.laneRaw[2],*e0=p1,*e1=p2,*e2=p3,*e3=out+s->h.nraw;
 __m256i bits=_mm256_loadu_si256((const __m256i*)bp),stops=_mm256_loadu_si256((const __m256i*)endbit);
 unsigned j=0;for(;j+3<=q;j+=3){
  __m256i words=_mm256_i64gather_epi64((const long long*)s->toks,_mm256_srli_epi64(bits,3),1);
  words=_mm256_srlv_epi64(words,_mm256_and_si256(bits,_mm256_set1_epi64x(7)));
  for(int k=0;k<3;k++){
   __m128i ix=_mm256_cvtepi64_epi32(_mm256_and_si256(words,_mm256_set1_epi64x(32767)));
   __m128i v=_mm_i32gather_epi32((const int*)s->table,ix,4);
   __m128i n=_mm_srli_epi32(v,16);if(_mm_movemask_epi8(_mm_cmpeq_epi32(n,_mm_setzero_si128())))return -1;
   __m256i lens=_mm256_cvtepu32_epi64(n);words=_mm256_srlv_epi64(words,lens);bits=_mm256_add_epi64(bits,lens);
   if(!outentry(s->dict[_mm_cvtsi128_si32(v)&32767],p0,e0)||!outentry(s->dict[_mm_extract_epi32(v,1)&32767],p1,e1)||!outentry(s->dict[_mm_extract_epi32(v,2)&32767],p2,e2)||!outentry(s->dict[_mm_extract_epi32(v,3)&32767],p3,e3))return -1;
  }
  if(_mm256_cmp_epu64_mask(bits,stops,_MM_CMPINT_GT))return -1;
 }
 _mm256_storeu_si256((__m256i*)bp,bits);
 for(;j<q;j++){uint64_t w0=get64(s->toks+(bp[0]>>3))>>(bp[0]&7),w1=get64(s->toks+(bp[1]>>3))>>(bp[1]&7),w2=get64(s->toks+(bp[2]>>3))>>(bp[2]&7),w3=get64(s->toks+(bp[3]>>3))>>(bp[3]&7);
  if(!emit(s,bp[0],w0,endbit[0],p0,e0)||!emit(s,bp[1],w1,endbit[1],p1,e1)||!emit(s,bp[2],w2,endbit[2],p2,e2)||!emit(s,bp[3],w3,endbit[3],p3,e3))return -1;}
 for(unsigned i=q*4;i<s->h.ntok;i++){uint64_t w=get64(s->toks+(bp[3]>>3))>>(bp[3]&7);if(!emit(s,bp[3],w,endbit[3],p3,e3))return -1;}
 for(int i=0;i<4;i++)if(bp[i]!=endbit[i])return -1;if(p0!=e0||p1!=e1||p2!=e2||p3!=e3)return -1;return s->h.nraw;
}


inline int64_t decode_unchecked(void*state,uint8_t*out,size_t cap){
 State*s=(State*)state;if(!s||cap<s->h.nraw)return -1;
 const unsigned q=s->h.ntok/4;uint64_t b0=0,b1=s->h.laneBit[0],b2=s->h.laneBit[1],b3=s->h.laneBit[2];
 const uint64_t z0=b1,z1=b2,z2=b3,z3=s->h.nbits;
 uint8_t*p0=out,*p1=out+s->h.laneRaw[0],*p2=out+s->h.laneRaw[1],*p3=out+s->h.laneRaw[2];
 uint8_t*e0=p1,*e1=p2,*e2=p3,*e3=out+s->h.nraw;
 unsigned j=0;
 if(s->h.ndict==1)return decode_scalar(state,out,cap);
 while(j+3<=q){
  if(size_t(e0-p0)<80||size_t(e1-p1)<80||size_t(e2-p2)<80||size_t(e3-p3)<80)break;
  size_t safe=std::min({size_t(q-j),(size_t(e0-p0)-8)/24,(size_t(e1-p1)-8)/24,(size_t(e2-p2)-8)/24,(size_t(e3-p3)-8)/24,size_t(z0-b0)/15,size_t(z1-b1)/15,size_t(z2-b2)/15,size_t(z3-b3)/15})/3;
  if(!safe)break;
  unsigned limit=j+safe*3;
  for(;j<limit;j+=3){
  uint64_t w0=get64(s->toks+(b0>>3))>>(b0&7),w1=get64(s->toks+(b1>>3))>>(b1&7),w2=get64(s->toks+(b2>>3))>>(b2&7),w3=get64(s->toks+(b3>>3))>>(b3&7);
  for(int k=0;k<3;k++){
   uint32_t v0=s->table[w0&32767],v1=s->table[w1&32767],v2=s->table[w2&32767],v3=s->table[w3&32767];
   unsigned n0=v0>>16,n1=v1>>16,n2=v2>>16,n3=v3>>16;
   const Entry&d0=s->dict[v0&32767],&d1=s->dict[v1&32767],&d2=s->dict[v2&32767],&d3=s->dict[v3&32767];
   w0>>=n0;w1>>=n1;w2>>=n2;w3>>=n3;b0+=n0;b1+=n1;b2+=n2;b3+=n3;
   memcpy(p0,&d0,32);memcpy(p1,&d1,32);memcpy(p2,&d2,32);memcpy(p3,&d3,32);
   p0+=d0.len;p1+=d1.len;p2+=d2.len;p3+=d3.len;
  }
  }
 }
 for(;j<q;j++){
  uint64_t w0=get64(s->toks+(b0>>3))>>(b0&7),w1=get64(s->toks+(b1>>3))>>(b1&7),w2=get64(s->toks+(b2>>3))>>(b2&7),w3=get64(s->toks+(b3>>3))>>(b3&7);
  if(!emit(s,b0,w0,z0,p0,e0)||!emit(s,b1,w1,z1,p1,e1)||!emit(s,b2,w2,z2,p2,e2)||!emit(s,b3,w3,z3,p3,e3))return -1;
 }
 for(unsigned i=q*4;i<s->h.ntok;i++){uint64_t w=get64(s->toks+(b3>>3))>>(b3&7);if(!emit(s,b3,w,z3,p3,e3))return -1;}
 if(b0!=z0||b1!=z1||b2!=z2||b3!=z3||p0!=e0||p1!=e1||p2!=e2||p3!=e3)return -1;return s->h.nraw;
}
inline int64_t decode(void*s,uint8_t*o,size_t n){return decode_unchecked(s,o,n);}
inline int64_t rows(void*p,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets){
 State*s=(State*)p;if(!s||!offsets)return -1;offsets[0]=0;if(!count)return 0;
 if(count==s->h.nrow){
  bool all=true;for(size_t i=0;i<count;i++)if(ids[i]!=i){all=false;break;}
  if(all){int64_t n=entropyfast::decode(s,out,cap);if(n<0)return -1;size_t pos=0,oi=0;__m512i nl=_mm512_set1_epi8(10);
   for(;pos+64<=size_t(n);pos+=64){uint64_t m=_mm512_cmpeq_epi8_mask(_mm512_loadu_si512(out+pos),nl);while(m){if(oi>=count)return -1;offsets[++oi]=pos+__builtin_ctzll(m)+1;m&=m-1;}}
   for(;pos<size_t(n);pos++)if(out[pos]==10){if(oi>=count)return -1;offsets[++oi]=pos+1;}
   if(n&&out[n-1]!=10){if(oi>=count)return -1;offsets[++oi]=n;}return oi==count?n:-1;
  }
 }
 uint8_t*o=out;uint64_t current=~uint64_t(0),bit=0,word=0;unsigned avail=0;
 for(size_t q=0;q<count;++q){
  uint64_t r=ids[q];if(r>=s->h.nrow||(q&&r<ids[q-1]))return -1;uint64_t begin=(r/s->h.group)*s->h.group;
  if(current>r||current<begin){current=begin;bit=entropy::index_at(s,r/s->h.group);avail=0;}
  while(current<r){bool ended=false;while(bit<s->h.nbits){
   if(avail<15){word=get64(s->toks+(bit>>3))>>(bit&7);avail=64-(bit&7);}
   uint32_t v=s->table[word&32767];unsigned nb=v>>16;if(!nb||nb>s->h.nbits-bit)return -1;
   word>>=nb;avail-=nb;bit+=nb;if(v&0x8000u){ended=true;break;}
  }if(!ended)return -1;++current;}
  bool ended=false;while(bit<s->h.nbits){
   if(avail<15){word=get64(s->toks+(bit>>3))>>(bit&7);avail=64-(bit&7);}
   uint32_t v=s->table[word&32767];unsigned nb=v>>16;if(!nb||nb>s->h.nbits-bit)return -1;
   word>>=nb;avail-=nb;bit+=nb;const Entry&e=s->dict[v&32767];unsigned len=e.len;size_t left=cap-size_t(o-out);if(left<len)return -1;if(left>=32)memcpy(o,e.data,32);else memcpy(o,e.data,len);o+=len;
   if(v&0x8000u){ended=true;break;}
  }
  if(!ended&&r+1!=s->h.nrow)return -1;current=r+1;offsets[q+1]=o-out;
 }
 return o-out;
}
inline void close(void*s){entropy::close(s);}
}
