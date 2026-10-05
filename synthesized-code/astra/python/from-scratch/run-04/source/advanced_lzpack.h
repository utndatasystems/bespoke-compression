
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>
#include <algorithm>
#ifndef ENT_HEADER
#define ENT_HEADER "advanced_entropy.h"
#endif
#include ENT_HEADER
struct Seq {uint32_t ll,ml,d;};
static const uint64_t MAGIC=0x3130305a4c595050ull;
static inline uint32_t get32(const void*p){uint32_t v;memcpy(&v,p,4);return v;}
static inline uint64_t get64(const void*p){uint64_t v;memcpy(&v,p,8);return v;}
static inline void put32(std::vector<uint8_t>&a,uint32_t v){auto p=a.size();a.resize(p+4);memcpy(a.data()+p,&v,4);}
static inline void put64(std::vector<uint8_t>&a,uint64_t v){auto p=a.size();a.resize(p+8);memcpy(a.data()+p,&v,8);}
static std::vector<uint8_t> pack(size_t n,const std::vector<Seq>&s,const std::vector<uint8_t>&lit){
 std::vector<uint8_t> v[6];size_t ns=s.size();for(int k=0;k<3;++k)v[k].reserve(ns);
 uint64_t accum=0;unsigned b=0;
 for(size_t i=0;i<ns;++i){
  const Seq&q=s[i];unsigned ml=q.ml?q.ml-3:0;v[0].push_back(std::min(q.ll,255u));v[1].push_back(std::min(ml,255u));
  if(q.ll>=255)put32(v[3],q.ll);
  if(ml>=255)put32(v[3],ml);
  unsigned k=q.d?31-__builtin_clz(q.d):0;v[2].push_back(k);
  uint32_t lo=q.d? q.d-(1u<<k):0;accum|=uint64_t(lo)<<b;b+=k;
  while(b>=8){v[4].push_back(accum);accum>>=8;b-=8;}
 }
 if(b)v[4].push_back(accum);v[4].resize(v[4].size()+8);
 v[5]=lit;
 std::vector<uint8_t> a;put64(a,MAGIC);put64(a,n);put64(a,ns);
 std::vector<uint8_t> z[6];
 for(int k=0;k<6;++k){z[k]=(k==3||k==4)?v[k]:ent::compress(v[k].data(),v[k].size());put64(a,v[k].size());put64(a,z[k].size());}
 for(int k=0;k<6;++k)a.insert(a.end(),z[k].begin(),z[k].end());
 return a;
}
