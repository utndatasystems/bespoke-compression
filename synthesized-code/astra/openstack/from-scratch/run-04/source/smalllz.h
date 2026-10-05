#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#ifndef SMALLLZ_DECODER_ONLY
#include <vector>
#include <algorithm>
#endif
// Original 64KiB-window LZ77 helper. Byte-order is little endian.
// mode 0: u8 mode, u32 raw size, raw bytes.
// mode 1: u8 mode, u32 raw size, repeated sequences:
// varuint literal length, literals, [varuint(match length-4), u16 distance].
// The last sequence omits match fields when literals finish the output.
// Variable integers store seven bits per byte, least significant group first.
namespace smalllz {
static inline uint32_t get32(const uint8_t *p) {uint32_t v;memcpy(&v,p,4);return v;}
static inline void put32(uint8_t *p,uint32_t v) {memcpy(p,&v,4);}
static inline size_t decoded_size(const uint8_t *p,size_t n) {return n>=5?get32(p+1):0;}
static inline bool readvar(const uint8_t *&p,const uint8_t *end,uint32_t& v) {
 v=0; for(unsigned shift=0;shift<35;shift+=7) {if(p==end)return false;unsigned b=*p++;
  if(shift==28&&(b&240))return false;v|=uint32_t(b&127)<<shift;if(!(b&128))return true;}
 return false;
}
static inline bool decode(const uint8_t *src,size_t bytes,uint8_t *dst,size_t capacity) {
 if(bytes<5)return false;size_t n=get32(src+1);if(n>capacity)return false;
 if(src[0]==0) {if(bytes!=n+5)return false;if(n)memcpy(dst,src+5,n);return true;}
 if(src[0]!=1)return false;const uint8_t *p=src+5,*end=src+bytes;size_t at=0;
 while(at<n) {
  uint32_t lit,len;if(!readvar(p,end,lit)||lit>n-at||lit>size_t(end-p))return false;
  if(lit)memcpy(dst+at,p,lit);at+=lit;p+=lit;if(at==n)break;
  if(n-at<4||!readvar(p,end,len)||len>n-at-4||end-p<2)return false;
  len+=4;unsigned distance=p[0]|(unsigned(p[1])<<8);p+=2;if(!distance||distance>at)return false;
  uint8_t *out=dst+at;
  if(distance>=len)memcpy(out,out-distance,len);
  else if(distance==1)memset(out,out[-1],len);
  else {
   memcpy(out,out-distance,distance);size_t done=distance;
   while(done<len) {size_t take=done;if(take>len-done)take=len-done;memcpy(out+done,out,take);done+=take;}
  }
  at+=len;
 }
 return p==end;
}
#ifndef SMALLLZ_DECODER_ONLY
static inline void writevar(std::vector<uint8_t>& out,uint32_t n) {while(n>=128){out.push_back((n&127)|128);n>>=7;}out.push_back(n);}
static inline std::vector<uint8_t> encode(const uint8_t *src,size_t n) {
 if(n>0xffffffffu)return {};
 std::vector<uint8_t> stored(n+5);stored[0]=0;put32(stored.data()+1,n);if(n)memcpy(stored.data()+5,src,n);
 if(n<16)return stored;
 std::vector<int> head(65536,-1),prev(n,-1);
 auto hash=[&](size_t pos) {return (get32(src+pos)*2654435761u)>>16;};
 auto insert=[&](size_t pos) {if(pos+4<=n){unsigned h=hash(pos);prev[pos]=head[h];head[h]=pos;}};
 auto find=[&](size_t pos,unsigned& distance) {
  unsigned best=3;distance=0;if(pos+4>n)return 0u;int candidate=head[hash(pos)];
  unsigned limit=unsigned(std::min<size_t>(65535,n-pos));
  for(unsigned depth=0;candidate>=0&&depth<128;++depth) {
   size_t dist=pos-size_t(candidate);if(dist>65535)break;
   if(src[candidate+best]==src[pos+best]&&get32(src+candidate)==get32(src+pos)) {
    unsigned len=4;
    while(len+8<=limit){uint64_t a,b;memcpy(&a,src+candidate+len,8);memcpy(&b,src+pos+len,8);if(a!=b)break;len+=8;}
    while(len<limit&&src[candidate+len]==src[pos+len])++len;
    if(len>best){best=len;distance=dist;if(best==limit)break;}
   }
   candidate=prev[candidate];
  }
  return distance?best:0u;
 };
 std::vector<uint8_t> out(5);out[0]=1;put32(out.data()+1,n);size_t pos=0,anchor=0;
 while(pos+4<=n) {
  unsigned distance;unsigned len=find(pos,distance);insert(pos);
  if(len) {unsigned nextdist;if(pos+5<=n&&find(pos+1,nextdist)>len+1){++pos;continue;}
   writevar(out,pos-anchor);out.insert(out.end(),src+anchor,src+pos);writevar(out,len-4);
   out.push_back(distance&255);out.push_back(distance>>8);
   for(unsigned k=1;k<len;++k)insert(pos+k);pos+=len;anchor=pos;
  } else ++pos;
 }
 if(anchor<n){writevar(out,n-anchor);out.insert(out.end(),src+anchor,src+n);}
 return out.size()<stored.size()?out:stored;
}
#endif
}
