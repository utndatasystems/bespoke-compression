#pragma once
#ifndef DECODE_ONLY
#include "entropy.h"
#include "lzparse.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>
extern "C" {
extern const uint8_t _binary__source_fitted_bin_start[];
extern const uint8_t _binary__source_fitted_bin_end[];
}
namespace fitted_detail {
inline uint32_t read32(const uint8_t*p){uint32_t v;memcpy(&v,p,4);return v;}
}
// The linked byte planes are encoder-only fitted LZ decisions. Every decision is
// checked against this call's supplied input before it is used by the encoder.
inline std::vector<LZSeq> fitted_parse(const uint8_t*raw,size_t n){
 try {
  static_assert(sizeof(LZSeq)==12,"fitted plane layout requires three uint32_t fields");
  if(!raw&&n)return {};
  uintptr_t first=reinterpret_cast<uintptr_t>(_binary__source_fitted_bin_start),last=reinterpret_cast<uintptr_t>(_binary__source_fitted_bin_end);
  if(last<first)return {};size_t size=last-first;const uint8_t*src=_binary__source_fitted_bin_start;
  if(size<60||memcmp(src,"FITLZ001",8))return {};
  uint32_t ns=fitted_detail::read32(src+8);if(!ns||uint64_t(ns)>uint64_t(n)+1)return {};
  size_t off=60;uint32_t sizes[12];for(unsigned j=0;j<12;j++){sizes[j]=fitted_detail::read32(src+12+4*j);if(sizes[j]>size-off)return {};off+=sizes[j];}if(off!=size)return {};
  std::vector<LZSeq> seq(ns);std::vector<uint8_t> plane(ns);off=60;
  for(unsigned j=0;j<12;j++){
   if(!ent::decode(src+off,sizes[j],plane.data(),ns))return {};off+=sizes[j];
   uint8_t*dst=reinterpret_cast<uint8_t*>(seq.data());
   for(size_t i=0;i<ns;i++)memcpy(dst+12*i+j,plane.data()+i,1);
  }
  size_t pos=0;
  for(size_t i=0;i<seq.size();i++){
   const LZSeq&s=seq[i];if(s.literals>n-pos)return {};pos+=s.literals;
   if(!s.length){if(i+1!=seq.size()||pos!=n||s.distance)return {};return seq;}
   if(s.length<3||s.length>n-pos||!s.distance||s.distance>pos)return {};
   if(memcmp(raw+pos,raw+pos-s.distance,s.length))return {};pos+=s.length;
  }
  return {};
 }catch(...){return {};}
}
#endif
