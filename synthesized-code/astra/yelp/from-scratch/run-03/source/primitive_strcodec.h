#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>
#include <algorithm>
// Self-contained LZ byte-stream codec. Literal/match records use 2-bit literal and 6-bit match lengths
// followed by literal bytes and a three-byte backwards distance. Extended
// lengths use 255-byte increments. Archive begins with uncompressed uint32 size.
namespace strcodec {
static inline uint32_t rd32(const void*p){uint32_t v;memcpy(&v,p,4);return v;}
static inline uint64_t rd64(const void*p){uint64_t v;memcpy(&v,p,8);return v;}
static inline void ext(std::vector<uint8_t>&o,size_t x){while(x>=255){o.push_back(255);x-=255;}o.push_back(x);}
static inline std::vector<uint8_t> compress(const uint8_t*s,size_t n,unsigned depth=96){
 std::vector<uint8_t> o(4);uint32_t nn=n;memcpy(o.data(),&nn,4);if(n==0)return o;
 std::vector<int32_t> head(1<<20,-1),prev(n,-1);
 auto hash=[&](size_t p){return (rd32(s+p)*2654435761u)>>12;};
 auto insert=[&](size_t p){auto h=hash(p);prev[p]=head[h];head[h]=p;};
 auto match=[&](size_t p,uint32_t&dist){size_t best=3;auto c=head[hash(p)];unsigned count=0;size_t remain=n-p;
  while(c>=0&&p-size_t(c)<=0xffffff&&count++<depth){if(s[c+best]==s[p+best]&&rd32(s+c)==rd32(s+p)){
   size_t l=4;while(l+8<=remain&&rd64(s+c+l)==rd64(s+p+l))l+=8;while(l<remain&&s[c+l]==s[p+l])++l;
   if(l>best){best=l;dist=p-c;if(l==remain)break;}}
   c=prev[c];
  }return best;
 };
 auto emit=[&](size_t lit,size_t ll,size_t ml,uint32_t d){o.push_back((std::min(ll,size_t(3))<<6)|std::min(ml-4,size_t(63)));if(ll>=3)ext(o,ll-3);o.insert(o.end(),s+lit,s+lit+ll);o.push_back(d);o.push_back(d>>8);o.push_back(d>>16);if(ml>=67)ext(o,ml-67);};
 size_t p=0,anchor=0;
 while(p+8<n){uint32_t d=0;size_t l=match(p,d);insert(p);if(l<5){++p;continue;}
  if(p+1+8<n){uint32_t d2=0;auto l2=match(p+1,d2);if(l2>l+1){++p;continue;}}
  emit(anchor,p-anchor,l,d);size_t end=p+l;for(++p;p<end&&p+4<=n;++p)insert(p);p=end;anchor=p;
 }
 size_t ll=n-anchor;if(ll){o.push_back(std::min(ll,size_t(3))<<6);if(ll>=3)ext(o,ll-3);o.insert(o.end(),s+anchor,s+n);}return o;
}
static __attribute__((noinline)) bool decompress(const uint8_t*s,size_t sn,uint8_t*out,size_t on){
 if(sn<4||rd32(s)!=on)return false;const uint8_t*se=s+sn;s+=4;uint8_t*p=out,*end=out+on;
 while(p<end){if(s==se)return false;unsigned t=*s++;size_t l=t>>6;if(l==3){unsigned c;do{if(s==se)return false;c=*s++;l+=c;if(l>on)return false;}while(c==255);}if(l>size_t(end-p)||l>size_t(se-s))return false;if(l<=16&&end-p>=16&&se-s>=16){uint64_t a=rd64(s),b=rd64(s+8);memcpy(p,&a,8);memcpy(p+8,&b,8);}else{memcpy(p,s,l);}p+=l;s+=l;if(p==end)return s==se;
  if(se-s<3)return false;unsigned d=s[0]|(s[1]<<8)|(s[2]<<16);s+=3;size_t m=(t&63)+4;if(m==67){unsigned c;do{if(s==se)return false;c=*s++;m+=c;if(m>on)return false;}while(c==255);}
  if(!d||d>size_t(p-out)||m>size_t(end-p))return false;const uint8_t*q=p-d;
  if(m<=32&&d>=32&&end-p>=32){memcpy(p,q,32);p+=m;}
  else if(d>=m){memcpy(p,q,m);p+=m;}else{while(m--)*p++=*q++;}
 }return s==se;
}
}
