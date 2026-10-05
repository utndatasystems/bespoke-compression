#pragma once
#include "entropy.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#ifndef DECODE_ONLY
#include <vector>
#include <cmath>
#include <algorithm>
#endif
namespace lit2 {
inline uint32_t read32(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
struct Decoder {
 void* allocation=nullptr;
 uint16_t* map=nullptr;
 uint8_t** ptr=nullptr;
 uint8_t** end=nullptr;
 size_t total=0,used=0;
 uint8_t empty=0;
 Decoder()=default;
 Decoder(const Decoder&)=delete;
 Decoder& operator=(const Decoder&)=delete;
 ~Decoder(){close();}
 void close(){free(allocation);allocation=nullptr;map=nullptr;ptr=end=nullptr;total=used=0;}
 bool open(const uint8_t*src,size_t size,uint8_t*raw,size_t n){
  close();if(!src||size<12||(!raw&&n)||memcmp(src,"L2A1",4))return false;
  unsigned selected=read32(src+4),ms=read32(src+8);if(selected>4096)return false;
  unsigned groups=256+selected;size_t hs=12+size_t(ms)+8*size_t(groups);if(hs>size)return false;
  uint8_t pairs[8192];if(!ent::decode(src+12,ms,pairs,2*size_t(selected)))return false;
  allocation=malloc(65536*sizeof(uint16_t)+2*size_t(groups)*sizeof(uint8_t*));if(!allocation)return false;
  map=(uint16_t*)allocation;ptr=(uint8_t**)(map+65536);end=ptr+groups;
  for(unsigned i=0;i<65536;i++)map[i]=i&255;
  int previous=-1;for(unsigned i=0;i<selected;i++){unsigned pair=unsigned(pairs[2*i])|(unsigned(pairs[2*i+1])<<8);if(int(pair)<=previous){close();return false;}previous=pair;map[pair]=256+i;}
  if(!raw)raw=&empty;
  size_t off=hs,out=0;const uint8_t*headers=src+12+ms;
  for(unsigned i=0;i<groups;i++){
   unsigned count=read32(headers+8*i),sz=read32(headers+8*i+4);
   if(count>n-out||sz>size-off){close();return false;}
   ptr[i]=raw+out;end[i]=ptr[i]+count;
   if(!ent::decode(src+off,sz,ptr[i],count)){close();return false;}
   out+=count;off+=sz;
  }
  if(off!=size||out!=n){close();return false;}total=n;used=0;return true;
 }
 bool get(uint16_t pair,uint8_t&out){
  if(!allocation||used==total)return false;unsigned c=map[pair];if(ptr[c]==end[c])return false;out=*ptr[c]++;used++;return true;
 }
 bool finished()const{return allocation&&used==total;}
};
#ifndef DECODE_ONLY
inline void write32(uint8_t*p,uint32_t v){for(unsigned j=0;j<4;j++)p[j]=v>>(8*j);}
inline std::vector<uint8_t> encode(const std::vector<uint8_t>&literals,const std::vector<uint16_t>&contexts){
 if(literals.size()!=contexts.size()||literals.size()>=UINT32_MAX)return {};
 std::vector<std::vector<uint8_t>> pairs(65536);std::vector<uint32_t> hist(65536);uint32_t marg[256]={};
 for(size_t i=0;i<literals.size();i++){unsigned ab=contexts[i],c=literals[i];pairs[ab].push_back(c);hist[(ab&255)*256+c]++;marg[ab&255]++;}
 std::vector<double> costs(65536);
 for(unsigned b=0;b<256;b++)for(unsigned c=0;c<256;c++)if(hist[b*256+c])costs[b*256+c]=std::log2(double(marg[b])/hist[b*256+c])/8;
 struct Candidate{uint16_t pair;double gain;};std::vector<Candidate> selected;
 for(unsigned ab=0;ab<65536;ab++)if(!pairs[ab].empty()){
  double original=0;for(uint8_t c:pairs[ab])original+=costs[(ab&255)*256+c];
  double gain=original-double(ent::encode(pairs[ab]).size())-10;
  if(gain>=0)selected.push_back({uint16_t(ab),gain});
 }
 if(selected.size()>4096){std::sort(selected.begin(),selected.end(),[](const Candidate&a,const Candidate&b){return a.gain==b.gain?a.pair<b.pair:a.gain>b.gain;});selected.resize(4096);}
 std::sort(selected.begin(),selected.end(),[](const Candidate&a,const Candidate&b){return a.pair<b.pair;});
 std::vector<uint16_t> map(65536);for(unsigned ab=0;ab<65536;ab++)map[ab]=ab&255;
 std::vector<uint8_t> list;list.reserve(selected.size()*2);
 for(unsigned i=0;i<selected.size();i++){unsigned ab=selected[i].pair;map[ab]=256+i;list.push_back(uint8_t(ab));list.push_back(uint8_t(ab>>8));}
 auto codedlist=ent::encode(list);const size_t groups=256+selected.size();
 std::vector<std::vector<uint8_t>> streams(groups);
 for(size_t i=0;i<literals.size();i++)streams[map[contexts[i]]].push_back(literals[i]);
 std::vector<uint8_t> result(12+codedlist.size()+8*groups);
 memcpy(result.data(),"L2A1",4);write32(result.data()+4,selected.size());write32(result.data()+8,codedlist.size());memcpy(result.data()+12,codedlist.data(),codedlist.size());
 const size_t header=12+codedlist.size();
 for(unsigned i=0;i<groups;i++){
  auto coded=ent::encode(streams[i]);write32(result.data()+header+8*i,streams[i].size());write32(result.data()+header+8*i+4,coded.size());result.insert(result.end(),coded.begin(),coded.end());
 }
 return result;
}
#endif
}
