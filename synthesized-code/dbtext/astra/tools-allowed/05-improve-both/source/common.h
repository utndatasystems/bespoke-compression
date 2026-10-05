#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cstring>
#include <vector>
#include <string>
#include <string_view>
#include <algorithm>
#include <immintrin.h>
#include <zstd.h>
struct Header { uint32_t magic,type,rawsize,nrows,psize,reserved[3]; };
struct GPHeader {uint32_t ndict,dictRaw,dictComp,indexSize,dataSize,mode,indexRaw;};
static constexpr uint32_t MAGIC=0x37425444;
static inline void add32(std::vector<uint8_t>&v,uint32_t n){size_t i=v.size();v.resize(i+4);memcpy(v.data()+i,&n,4);}
static inline uint32_t get32(const uint8_t*p){uint32_t n;memcpy(&n,p,4);return n;}
