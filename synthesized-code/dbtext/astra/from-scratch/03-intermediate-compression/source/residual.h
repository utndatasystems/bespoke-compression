#pragma once
#include <cstdint>
#include <immintrin.h>
namespace row_index {
inline bool restore_residual(uint32_t*offsets,const uint32_t*raw,size_t rows,unsigned alpha,uint64_t max_bits){
 if(!alpha||alpha>255)return false;
 uint32_t prior=0;uint64_t total=0;size_t r=0;
 const __m512i va=_mm512_set1_epi32(alpha),four=_mm512_set1_epi32(4),one=_mm512_set1_epi32(1),zero=_mm512_setzero_si512();
 const __m512i bound=_mm512_set1_epi32((UINT32_MAX-4)/alpha);
 while(r+16<=rows){
  __m512i prefix=_mm512_loadu_si512(offsets+r+1),previous=_mm512_loadu_si512(offsets+r);
  previous=_mm512_mask_mov_epi32(previous,1,_mm512_set1_epi32(prior));
  __m512i v=_mm512_sub_epi32(prefix,previous);
  __m512i lengths=_mm512_sub_epi32(_mm512_loadu_si512(raw+r+1),_mm512_loadu_si512(raw+r));
  if(_mm512_cmp_epu32_mask(lengths,bound,_MM_CMPINT_GT)||_mm512_movepi32_mask(v))break;
  __m512i base=_mm512_srli_epi32(_mm512_add_epi32(_mm512_mullo_epi32(lengths,va),four),3);
  __m512i residual=_mm512_xor_si512(_mm512_srli_epi32(v,1),_mm512_sub_epi32(zero,_mm512_and_si512(v,one)));
  __m512i d=_mm512_add_epi32(base,residual);
  if(_mm512_cmp_epu32_mask(d,_mm512_set1_epi32(0x0fffffff),_MM_CMPINT_GT))break;
  d=_mm512_add_epi32(d,_mm512_maskz_alignr_epi32(0xfffe,d,d,15));
  d=_mm512_add_epi32(d,_mm512_maskz_alignr_epi32(0xfffc,d,d,14));
  d=_mm512_add_epi32(d,_mm512_maskz_alignr_epi32(0xfff0,d,d,12));
  d=_mm512_add_epi32(d,_mm512_maskz_alignr_epi32(0xff00,d,d,8));
  uint32_t sum=_mm_extract_epi32(_mm512_extracti32x4_epi32(d,3),3);
  if(total+sum>max_bits||total+sum>UINT32_MAX)return false;
  prior=offsets[r+16];
  _mm512_storeu_si512(offsets+r+1,_mm512_add_epi32(d,_mm512_set1_epi32(total)));
  total+=sum;r+=16;
 }
 for(;r<rows;r++){uint32_t prefix=offsets[r+1],v=prefix-prior;prior=prefix;int64_t residual=int64_t(v>>1)^-int64_t(v&1);int64_t d=int64_t((uint64_t(raw[r+1]-raw[r])*alpha+4)>>3)+residual;if(d<0||uint64_t(d)>max_bits-total||uint64_t(d)>UINT32_MAX-total)return false;total+=d;offsets[r+1]=total;}
 return true;
}
}
