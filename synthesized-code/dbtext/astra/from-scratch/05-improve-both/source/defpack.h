#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
// Dictionary node i refers only to children [0,i). Store both child IDs in
// ceil(log2(i)) bits each. Eight zero padding bytes make each decoder load safe.
namespace defpack {
static inline unsigned width(uint32_t i) { return i<=1?0:32u-__builtin_clz(i-1); }
static inline uint64_t bits(uint32_t start,uint32_t end) {
 uint64_t n=0;
 for(unsigned w=1;w<=16;w++) {
  uint32_t lo=(1u<<(w-1))+1,hi=(1u<<w)+1;
  if(lo<start)lo=start;if(hi>end)hi=end;
  if(hi>lo)n+=uint64_t(hi-lo)*2*w;
 }
 return n;
}
static inline size_t size(uint32_t start,uint32_t end) {
 return size_t((bits(start,end)+7)/8)+8;
}
static inline void encode(const uint16_t*left,const uint16_t*right,
                          uint32_t start,uint32_t end,uint8_t*out) {
 std::memset(out,0,size(start,end));uint64_t bit=0;
 for(uint32_t i=start;i<end;i++) {
  unsigned w=width(i),shift=bit&7;
  uint64_t v=(uint64_t(left[i])|(uint64_t(right[i])<<w))<<shift;
  unsigned n=(2*w+shift+7)/8;
  for(unsigned j=0;j<n;j++)out[(bit>>3)+j]|=uint8_t(v>>(8*j));
  bit+=2*w;
 }
}
struct Reader {
 const uint8_t*data;uint64_t bit=0;
 explicit Reader(const uint8_t*p):data(p){}
 inline void pair(uint32_t i,unsigned&a,unsigned&b) {
  unsigned w=width(i);uint64_t v;std::memcpy(&v,data+(bit>>3),8);v>>=bit&7;
  uint32_t mask=(1u<<w)-1;a=uint32_t(v)&mask;b=uint32_t(v>>w)&mask;bit+=2*w;
 }
};
}
