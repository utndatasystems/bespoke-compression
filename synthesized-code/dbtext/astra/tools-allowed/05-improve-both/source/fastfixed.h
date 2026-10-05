#pragma once
#include <cstdint>
#include <cstring>
#include <immintrin.h>
static inline uint64_t ff_u64(const uint8_t*p){uint64_t x;memcpy(&x,p,8);return x;}
template<unsigned W,unsigned N,bool SHORT,class S>
static inline bool fastfixed_emit(S*s,uint64_t word,uint8_t*out,size_t cap,size_t&pos){
 unsigned code[N],len[N];size_t at[N],end=pos;
 #pragma GCC unroll 4
 for(unsigned k=0;k<N;k++)code[k]=(word>>(W*k))&((1u<<W)-1);
 if(s->ndict!=(1u<<W)){
  #pragma GCC unroll 4
  for(unsigned k=0;k<N;k++)if(code[k]>=s->ndict)return false;
 }
 #pragma GCC unroll 4
 for(unsigned k=0;k<N;k++)len[k]=s->lens[code[k]];
 #pragma GCC unroll 4
 for(unsigned k=0;k<N;k++){at[k]=end;end+=len[k];}
 if(end>cap)return false;
 if(cap-at[N-1]>=(SHORT?16:32)){
  #pragma GCC unroll 4
  for(unsigned k=0;k<N;k++){
   if constexpr(SHORT)_mm_storeu_si128((__m128i*)(out+at[k]),_mm_load_si128((const __m128i*)s->dict16[code[k]].b));
   else _mm256_storeu_si256((__m256i*)(out+at[k]),_mm256_load_si256((const __m256i*)s->dict[code[k]].b));
  }
 }else{
  #pragma GCC unroll 4
  for(unsigned k=0;k<N;k++){
   if constexpr(SHORT)memcpy(out+at[k],s->dict16[code[k]].b,len[k]);
   else memcpy(out+at[k],s->dict[code[k]].b,len[k]);
  }
 }
 pos=end;return true;
}
template<unsigned W,bool SHORT,class S>
static inline bool fastfixed_range_impl(S*s,unsigned bit,unsigned end,uint8_t*out,size_t cap,size_t&pos){
 // lab_open constructs monotone offsets from token counts times W.
 // Their divisibility and archive bounds are established once during setup.
 while(end-bit>=4*W){const uint8_t*p=s->data+(bit>>3);uint64_t word;if constexpr(W<=14)word=ff_u64(p)>>(bit&7);else word=(uint64_t)(((__uint128_t)ff_u64(p)|((__uint128_t)p[8]<<64))>>(bit&7));bit+=4*W;if(!fastfixed_emit<W,4,SHORT>(s,word,out,cap,pos))return false;}
 if(bit<end){uint64_t word=ff_u64(s->data+(bit>>3))>>(bit&7);unsigned rem=(end-bit)/W;
  if(rem==3){if(!fastfixed_emit<W,3,SHORT>(s,word,out,cap,pos))return false;}
  else if(rem==2){if(!fastfixed_emit<W,2,SHORT>(s,word,out,cap,pos))return false;}
  else if(!fastfixed_emit<W,1,SHORT>(s,word,out,cap,pos))return false;
 }
 return true;
}
template<unsigned W,class S>
static inline bool fastfixed_range(S*s,unsigned bit,unsigned end,uint8_t*out,size_t cap,size_t&pos){
 if(s->short16)return fastfixed_range_impl<W,true>(s,bit,end,out,cap,pos);
 return fastfixed_range_impl<W,false>(s,bit,end,out,cap,pos);
}
template<bool SHORT,class S>
static inline bool fastfixed_generic(S*s,unsigned bit,unsigned end,uint8_t*out,size_t cap,size_t&pos){
 unsigned width=s->fixed_width;if(!width||width>15)return false;
 unsigned mask=(1u<<width)-1;
 while(bit<end){unsigned id=(ff_u64(s->data+(bit>>3))>>(bit&7))&mask;bit+=width;if(id>=s->ndict)return false;unsigned len=s->lens[id];if(len>cap-pos)return false;
  if constexpr(SHORT){if(cap-pos>=16)_mm_storeu_si128((__m128i*)(out+pos),_mm_load_si128((const __m128i*)s->dict16[id].b));else memcpy(out+pos,s->dict16[id].b,len);}
  else{if(cap-pos>=32)_mm256_storeu_si256((__m256i*)(out+pos),_mm256_load_si256((const __m256i*)s->dict[id].b));else memcpy(out+pos,s->dict[id].b,len);}
  pos+=len;
 }
 return true;
}
template<class S>
static inline bool fastfixed_dispatch(S*s,unsigned bit,unsigned end,uint8_t*out,size_t cap,size_t&pos){
 if(s->fixed_width==12)return fastfixed_range<12>(s,bit,end,out,cap,pos);
 if(s->fixed_width==13)return fastfixed_range<13>(s,bit,end,out,cap,pos);
 if(s->fixed_width==14)return fastfixed_range<14>(s,bit,end,out,cap,pos);
 if(s->fixed_width==15)return fastfixed_range<15>(s,bit,end,out,cap,pos);
 if(s->short16)return fastfixed_generic<true>(s,bit,end,out,cap,pos);
 return fastfixed_generic<false>(s,bit,end,out,cap,pos);
}
template<class S>
static inline bool fastfixed_row(S*s,unsigned rid,uint8_t*out,size_t cap,size_t&pos){return fastfixed_dispatch(s,s->off[rid],s->off[rid+1],out,cap,pos);}
template<class S>
static inline bool fastfixed_bulk(S*s,uint8_t*out,size_t cap,size_t&pos){return fastfixed_dispatch(s,0,s->off[s->h.nrows],out,cap,pos);}
// Select the code width and entry size once for a complete row query.
template<unsigned W,bool SHORT,class S>
static int64_t fastfixed_query_impl(S*s,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets){
 size_t pos=0;offsets[0]=0;
 for(size_t i=0;i<count;i++){
  uint64_t rid=ids[i];if(rid>=s->h.nrows||!fastfixed_range_impl<W,SHORT>(s,s->off[rid],s->off[rid+1],out,cap,pos))return -1;
  offsets[i+1]=pos;
 }
 return pos;
}
template<class S>
static int64_t fastfixed_rows(S*s,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets){
 if(s->short16){switch(s->fixed_width){
  case 12:return fastfixed_query_impl<12,true>(s,ids,count,out,cap,offsets);
  case 13:return fastfixed_query_impl<13,true>(s,ids,count,out,cap,offsets);
  case 14:return fastfixed_query_impl<14,true>(s,ids,count,out,cap,offsets);
  case 15:return fastfixed_query_impl<15,true>(s,ids,count,out,cap,offsets);
 }}else{switch(s->fixed_width){
  case 12:return fastfixed_query_impl<12,false>(s,ids,count,out,cap,offsets);
  case 13:return fastfixed_query_impl<13,false>(s,ids,count,out,cap,offsets);
  case 14:return fastfixed_query_impl<14,false>(s,ids,count,out,cap,offsets);
  case 15:return fastfixed_query_impl<15,false>(s,ids,count,out,cap,offsets);
 }}
 size_t pos=0;offsets[0]=0;
 for(size_t i=0;i<count;i++){if(ids[i]>=s->h.nrows||!fastfixed_row(s,ids[i],out,cap,pos))return -1;offsets[i+1]=pos;}
 return pos;
}
