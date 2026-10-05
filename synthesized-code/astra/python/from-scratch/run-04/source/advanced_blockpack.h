
#pragma once
#include "advanced_lzpack.h"
static const uint64_t BLOCKMAGIC=MAGIC+1;
static std::vector<uint8_t> blockpack(size_t n,const std::vector<Seq>&s,const std::vector<uint8_t>&lit,size_t bs){
 std::vector<uint8_t>a;put64(a,BLOCKMAGIC);put64(a,n);put64(a,(s.size()+bs-1)/bs);
 size_t lp=0;
 for(size_t i=0;i<s.size();i+=bs){
  size_t end=std::min(s.size(),i+bs),nl=0,nr=0;
  for(size_t j=i;j<end;++j){nl+=s[j].ll;nr+=s[j].ll+s[j].ml;}
  std::vector<Seq> v(s.begin()+i,s.begin()+end);
  std::vector<uint8_t> l(lit.begin()+lp,lit.begin()+lp+nl);lp+=nl;
  auto z=pack(nr,v,l);put64(a,z.size());a.insert(a.end(),z.begin(),z.end());
 }
 return a;
}
