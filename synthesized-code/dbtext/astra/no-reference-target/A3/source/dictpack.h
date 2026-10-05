#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <algorithm>
#include <limits>
namespace dictpack {
// Each sequence carries one nibble of literal count and one of match count.
// Counts saturating their nibble continue in base-255 bytes. Matches use a
// little-endian sixteen-bit backwards distance and permit overlapping copies.
#ifdef ENCODER
inline bool encode(const uint8_t*raw,size_t n,std::vector<uint8_t>&out){
 if((!raw&&n)||n>0x7fffffff)return false;out.clear();if(!n)return true;
 const uint32_t N=(uint32_t)n,NONE=UINT32_MAX,HS=1u<<18;
 std::vector<uint32_t> head(HS,NONE),prev(N,NONE),mlen(N),mdist(N);
 auto hash=[](const uint8_t*p){uint32_t x=uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16);return (x*0x1e35a7bdu)>>(32-18);};
 for(uint32_t i=0;i+3<=N;i++){
  uint32_t h=hash(raw+i),q=head[h];prev[i]=q;head[h]=i;uint32_t best=2,dist=0,limit=std::min<uint32_t>(65535,N-i),tries=0;
  while(q!=NONE&&i-q<=65535&&tries++<8192){
   if(raw[q]==raw[i]&&raw[q+1]==raw[i+1]&&raw[q+2]==raw[i+2]&&raw[q+best]==raw[i+best]){
    uint32_t k=3;while(k+8<=limit){uint64_t a,b;memcpy(&a,raw+q+k,8);memcpy(&b,raw+i+k,8);if(a!=b){k+=__builtin_ctzll(a^b)/8;break;}k+=8;}while(k<limit&&raw[q+k]==raw[i+k])k++;
    if(k>best){best=k;dist=i-q;if(k==limit)break;}
   }q=prev[q];
  }
  if(best>=3){mlen[i]=best;mdist[i]=dist;}
 }
 auto extra=[](uint32_t x,uint32_t cutoff){return x<cutoff?0u:1u+(x-cutoff)/255;};
 std::vector<uint32_t> cost(N+1),mcost(N,UINT32_MAX),match(N),next(N,NONE);cost[N]=0;uint32_t following=NONE;
 for(uint32_t i=N;i-->0;){
  uint32_t longest=mlen[i];if(longest){uint32_t best=UINT32_MAX,bestlen=0;auto consider=[&](uint32_t k){uint32_t c=extra(k,18)+cost[i+k];if(c<best||(c==best&&k>bestlen)){best=c;bestlen=k;}};
   uint32_t stop=std::min<uint32_t>(longest,64);for(uint32_t k=3;k<=stop;k++)consider(k);
   if(longest>64){uint32_t begin=std::max<uint32_t>(65,longest>32?longest-32:65);for(uint32_t k=begin;k<=longest;k++)consider(k);}
   mcost[i]=best;match[i]=bestlen;
  }
  uint32_t remain=N-i,best=1+remain+extra(remain,15),choice=NONE;
  uint32_t stop=std::min<uint32_t>(N,i+64);
  for(uint32_t j=i;j<stop;j++)if(match[j]){uint32_t literals=j-i,c=3+literals+extra(literals,15)+mcost[j];if(c<best){best=c;choice=j;}}
  if(following!=NONE&&following>=stop){uint32_t literals=following-i,c=3+literals+extra(literals,15)+mcost[following];if(c<best){best=c;choice=following;}}
  cost[i]=best;next[i]=choice;if(match[i])following=i;
 }
 out.reserve(cost[0]);auto ext=[&](uint32_t x){while(x>=255){out.push_back(255);x-=255;}out.push_back(x);};
 uint32_t p=0;while(p<N){uint32_t j=next[p],lit=j==NONE?N-p:j-p,k=j==NONE?0:match[j];out.push_back(uint8_t(std::min<uint32_t>(lit,15)<<4)|(k?std::min<uint32_t>(k-3,15):0));if(lit>=15)ext(lit-15);out.insert(out.end(),raw+p,raw+p+lit);if(j==NONE)break;uint32_t d=mdist[j];out.push_back(d);out.push_back(d>>8);if(k>=18)ext(k-18);p=j+k;}
 return true;
}
#endif
inline bool decode(const uint8_t*raw,size_t n,uint8_t*out,size_t expected){
 if((!raw&&n)||(!out&&expected))return false;size_t ip=0,op=0;
 while(ip<n){uint8_t token=raw[ip++];size_t lit=token>>4;if(lit==15){uint8_t x;do{if(ip==n)return false;x=raw[ip++];if(lit>expected||size_t(x)>expected-lit)return false;lit+=x;}while(x==255);}
  if(lit>n-ip||lit>expected-op)return false;if(lit){if(lit<=16&&n-ip>=16&&expected-op>=16)memcpy(out+op,raw+ip,16);else memcpy(out+op,raw+ip,lit);}ip+=lit;op+=lit;if(ip==n)return op==expected;
  if(n-ip<2)return false;size_t dist=raw[ip]|(size_t(raw[ip+1])<<8);ip+=2;if(!dist||dist>op)return false;
  size_t len=(token&15)+3;if((token&15)==15){uint8_t x;do{if(ip==n)return false;x=raw[ip++];if(len>expected||size_t(x)>expected-len)return false;len+=x;}while(x==255);}
  if(len>expected-op)return false;
  if(len<=16&&dist>=16&&expected-op>=16)memcpy(out+op,out+op-dist,16);
  else if(dist>=len)memcpy(out+op,out+op-dist,len);
  else if(dist>=8){size_t done=0;for(;done+8<=len;done+=8){uint64_t v;memcpy(&v,out+op+done-dist,8);memcpy(out+op+done,&v,8);}for(;done<len;done++)out[op+done]=out[op+done-dist];}
  else for(size_t j=0;j<len;j++)out[op+j]=out[op+j-dist];op+=len;
 }
 return op==expected;
}
}
