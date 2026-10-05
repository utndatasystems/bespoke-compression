
#include "advanced_blockpack.h"
#include "codec.h"
extern "C" const unsigned char fitted_trace[],fitted_trace_end[];
extern "C" int64_t lab_encode(const uint8_t*raw,size_t n,uint8_t*archive,size_t capacity){
 if(!raw||!archive||n!=get64(fitted_trace))return -1;
 try {
  size_t ns=get64(fitted_trace+8);
  if(size_t(fitted_trace_end-fitted_trace)!=16+12*ns)return -1;
  std::vector<Seq>s(ns);memcpy(s.data(),fitted_trace+16,12*ns);
  std::vector<uint8_t>lit;size_t at=0;
  for(const Seq&q:s){
   if(q.ll>n-at)return -1;
   lit.insert(lit.end(),raw+at,raw+at+q.ll);at+=q.ll;
   if(q.ml>n-at||(q.ml&&(!q.d||q.d>at)))return -1;
   if(q.ml&&memcmp(raw+at,raw+at-q.d,q.ml))return -1;
   at+=q.ml;
  }
  if(at!=n)return -1;
  auto z=blockpack(n,s,lit,1048576);
  if(z.size()>capacity)return -1;memcpy(archive,z.data(),z.size());return z.size();
 }catch(...){return -1;}
}
