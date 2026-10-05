#pragma once
#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <immintrin.h>
#include "phrase_dict.h"
namespace phrase_grammar {
inline std::vector<uint8_t> pack(const std::vector<uint32_t>&parents){
 size_t n=parents.size();std::vector<uint32_t>freq(n);for(size_t i=256;i<n;i++){freq[parents[i]>>16]++;freq[parents[i]&65535]++;}
 std::vector<uint16_t>order(n);for(size_t i=0;i<n;i++)order[i]=i;
 std::sort(order.begin(),order.end(),[&](uint16_t a,uint16_t b){return freq[a]>freq[b]||(freq[a]==freq[b]&&a<b);});
 std::vector<uint16_t>small(n,65535);std::vector<uint8_t>raw(256);for(unsigned i=0;i<128;i++){uint16_t s=order[i];small[s]=i;memcpy(raw.data()+i*2,&s,2);}
 auto put=[&](unsigned s){if(small[s]!=65535)raw.push_back(small[s]);else{raw.push_back(128|(s&127));raw.push_back(s>>7);}};
 for(size_t i=256;i<n;i++){put(parents[i]>>16);put(parents[i]&65535);}
 return phrase_dict::pack(raw);
}
inline bool unpack(const uint8_t*p,size_t bytes,uint8_t*dict,uint8_t*lens,unsigned ns){
 if(ns<256||ns>32768)return false;std::vector<uint8_t>raw;if(!phrase_dict::unpack(p,bytes,raw)||raw.size()<256)return false;
 uint16_t common[128];memcpy(common,raw.data(),256);for(unsigned c:common)if(c>=ns)return false;
 const uint8_t*q=raw.data()+256,*end=raw.data()+raw.size();memset(dict,0,256*32);for(unsigned i=0;i<256;i++){dict[i*32]=i;lens[i]=1;}
 auto get=[&]()->unsigned{if(q==end)return ns;unsigned a=*q++;if(a<128)return common[a];if(q==end)return ns;return(a&127)|(unsigned(*q++)<<7);};
 for(unsigned i=256;i<ns;i++){unsigned a=get(),b=get();if(a>=i||b>=i)return false;unsigned na=lens[a],nb=lens[b];if(na+nb>32)return false;lens[i]=na+nb;__m256i av=_mm256_loadu_si256((const __m256i*)(dict+a*32)),bv=_mm256_loadu_si256((const __m256i*)(dict+b*32));_mm256_storeu_si256((__m256i*)(dict+i*32),av);_mm256_mask_storeu_epi8(dict+i*32+na,(__mmask32)((uint64_t(1)<<nb)-1),bv);}
 return q==end;
}
}
