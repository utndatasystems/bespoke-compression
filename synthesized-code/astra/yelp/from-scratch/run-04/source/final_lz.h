#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>
#include <immintrin.h>
namespace rootlz {
static inline uint32_t rd32(const uint8_t* p) {uint32_t v;memcpy(&v,p,4);return v;}
static inline uint64_t rd64(const uint8_t* p) {uint64_t v;memcpy(&v,p,8);return v;}
static inline uint32_t hash4(const uint8_t* p) { return (rd32(p)*2654435761u)>>12; }
static inline void ext(std::vector<uint8_t>& out,size_t n) {while(n>=255){out.push_back(255);n-=255;} out.push_back(n);}
static std::vector<uint8_t> encode(const uint8_t* s,size_t n,int depth=512) {
 std::vector<uint32_t> cmds; std::vector<uint8_t> literals;
 std::vector<int> head(1<<20,-1),prev(n,-1);size_t pos=0,lit=0;
 auto insert=[&](size_t x){if(x+4<=n){uint32_t h=hash4(s+x);prev[x]=head[h];head[h]=x;}};
 auto emit=[&](size_t start,size_t ll,size_t ml,size_t d){
  while(ll>63){cmds.push_back(63u<<20);literals.insert(literals.end(),s+start,s+start+63);start+=63;ll-=63;}
  cmds.push_back(uint32_t(d)|(uint32_t(ll)<<20)|(uint32_t(ml?ml-3:0)<<26));
  literals.insert(literals.end(),s+start,s+start+ll);
 };
 while(pos+12<n) {
   uint32_t h=hash4(s+pos);int q=head[h];size_t best=3,bp=0;int lim=depth;
   while(q>=0 && pos-(size_t)q<0x100000 && --lim>=0) {
    if(best<n-pos && s[q+best]==s[pos+best] && rd32(s+q)==rd32(s+pos)) {
     size_t k=4;while(k<66 && pos+k<n && s[pos+k]==s[q+k])++k;
     if(k>best){best=k;bp=q;if(k==66)break;}
    } q=prev[q];
   }
   if(best<6){insert(pos++);continue;}
   if(best<66 && pos+1+66<n) {
    int nq=head[hash4(s+pos+1)],limit=depth;size_t nb=best+1;
    while(nq>=0 && pos+1-(size_t)nq<0x100000 && --limit>=0){
      if(s[nq+nb]==s[pos+1+nb] && rd32(s+nq)==rd32(s+pos+1)){
        size_t k=4;while(k<66 && s[pos+1+k]==s[nq+k])++k;
        if(k>nb){nb=k;break;}
      }nq=prev[nq];
    }
    if(nb>best+1){insert(pos++);continue;}
   }
   emit(lit,pos-lit,best,pos-bp);
   size_t e=pos+best;while(pos<e)insert(pos++);lit=pos;
 }
 emit(lit,n-lit,0,0);
 std::vector<uint8_t>o(12+4*cmds.size()+literals.size()+64);
 uint32_t nn=n,nc=cmds.size(),nl=literals.size();memcpy(o.data(),&nn,4);memcpy(o.data()+4,&nc,4);memcpy(o.data()+8,&nl,4);
 memcpy(o.data()+12,cmds.data(),4*cmds.size());memcpy(o.data()+12+4*cmds.size(),literals.data(),literals.size());
 return o;
}
static inline void cp(uint8_t* o,const uint8_t* p,size_t l) {
 _mm_storeu_si128((__m128i*)o,_mm_loadu_si128((const __m128i*)p));
 if(l>16){_mm_storeu_si128((__m128i*)(o+16),_mm_loadu_si128((const __m128i*)(p+16)));
 if(l>32){_mm_storeu_si128((__m128i*)(o+32),_mm_loadu_si128((const __m128i*)(p+32)));
 if(l>48){_mm_storeu_si128((__m128i*)(o+48),_mm_loadu_si128((const __m128i*)(p+48)));
 if(l>64){_mm_storeu_si128((__m128i*)(o+64),_mm_loadu_si128((const __m128i*)(p+64)));}}}}
}
static inline bool decode(const uint8_t* p,size_t z,uint8_t* dst,size_t n) {
 if(z<76||rd32(p)!=n)return false;
 uint32_t nc=rd32(p+4),nl=rd32(p+8);
 if(12ull+4ull*nc+nl+64!=z)return false;
 const uint8_t* cm=p+12;const uint8_t* lit=cm+4*nc;const uint8_t* lend=lit+nl;uint8_t*o=dst;uint8_t*oe=dst+n;
 for(uint32_t i=0;i<nc;++i) {
  uint32_t c=rd32(cm);cm+=4;
  unsigned l=(c>>20)&63, m=c>>26, d=c&0xfffff;
  m+=m?3:0;
  if(l>(size_t)(lend-lit)||l+m>(size_t)(oe-o))return false;
  cp(o,lit,l);o+=l;lit+=l;
  if(m) {
   if(d==0 || d>(size_t)(o-dst))return false;
   if(d>=16)cp(o,o-d,m);
   else {const uint8_t* q=o-d;for(unsigned j=0;j<m;++j)o[j]=q[j];}
  }
  o+=m;
 }
 return o==oe && lit==lend;
}
}
