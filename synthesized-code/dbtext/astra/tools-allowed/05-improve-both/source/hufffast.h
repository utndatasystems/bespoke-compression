#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <immintrin.h>

// Expanded Huffman prefix codebook, derived solely from the archive dictionary.
// Entries concatenate whole symbols determined by twelve lookahead bits.
// There are no row IDs, row strings, or query outputs in this table.
#ifndef HUFF_FAST_BITS
#define HUFF_FAST_BITS 12
#endif
struct HuffFast {
 static constexpr unsigned width=HUFF_FAST_BITS, count=1u<<width, mask=count-1;
 struct alignas(32) Bytes {uint8_t b[32];};
 std::vector<Bytes> bytes;
 std::vector<uint16_t> meta; // low 5 bits consumed; remaining bits output length
};
template<class DictEntry>
static bool huff_fast_build(HuffFast&dst,const std::vector<uint32_t>&htable,
                           const std::vector<DictEntry>&dict,
                           const std::vector<uint8_t>&lens){
 if(htable.empty()||(htable.size()&(htable.size()-1))||dict.size()!=lens.size())return false;
 dst.bytes.resize(HuffFast::count);dst.meta.resize(HuffFast::count);
 unsigned hmask=unsigned(htable.size()-1);
 for(unsigned prefix=0;prefix<HuffFast::count;prefix++){
  unsigned bits=0,len=0;
  while(bits<HuffFast::width){
   unsigned h=htable[(prefix>>bits)&hmask],nb=h>>16,id=h&65535;
   if(!nb||nb>HuffFast::width-bits)break;
   if(id>=dict.size()||!lens[id]||lens[id]>32)return false;
   unsigned n=lens[id];if(n>32-len)break;
   memcpy(dst.bytes[prefix].b+len,dict[id].b,n);len+=n;bits+=nb;
  }
  dst.meta[prefix]=uint16_t(bits|(len<<5));
 }
 return true;
}
// Return consumed bits, zero for slow fallback, or UINT32_MAX for capacity error.
static inline unsigned huff_fast_emit(const HuffFast&src,uint32_t window,
                                     unsigned remaining,uint8_t*out,
                                     size_t capacity,size_t&position){
 unsigned index=window&HuffFast::mask,meta=src.meta[index],bits=meta&31;
 if(!bits||bits>remaining)return 0;
 unsigned len=meta>>5;
 if(capacity-position>=32){
  _mm256_storeu_si256((__m256i*)(out+position),
                     _mm256_load_si256((const __m256i*)src.bytes[index].b));
 }else{
  if(len>capacity-position)return UINT32_MAX;
  memcpy(out+position,src.bytes[index].b,len);
 }
 position+=len;
 return bits;
}
