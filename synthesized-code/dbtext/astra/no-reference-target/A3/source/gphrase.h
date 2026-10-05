#pragma once
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <cstdio>
namespace gphrase {
static constexpr uint32_t MAGIC=0x33475250;
struct Header { uint32_t magic,nraw,nrow,ndict,bits,ntok,dictbytes,nblocks; };
struct Entry { uint8_t data[32]; uint8_t len; };
struct State { Header h; const uint8_t* ar; size_t size; const uint8_t* lens; const uint32_t* index; const uint8_t* toks; Entry *dict; };
#ifdef ENCODER
static void put32(std::vector<uint8_t>& o,uint32_t v){size_t p=o.size();o.resize(p+4);memcpy(o.data()+p,&v,4);}
inline bool encode(const uint8_t* raw,size_t n,std::vector<uint8_t>& out,int maxcodes=16384) {
 std::vector<uint32_t> parents(256);std::vector<std::string> dict(256); for(int i=0;i<256;i++)dict[i]=std::string(1,char(i));
 std::vector<uint16_t> seq; seq.reserve(n+n/10); uint32_t nr=0; for(size_t i=0;i<n;i++){seq.push_back(raw[i]);if(raw[i]==10){seq.push_back(65535);nr++;}} if(n && raw[n-1]!=10){seq.push_back(65535);nr++;}
 #ifndef PHRASE_MAX
#define PHRASE_MAX 8192
#endif
 const int maxlen=24;
 for(int round=0;dict.size()<maxcodes;round++){
  std::unordered_map<uint32_t,uint32_t> freq;freq.reserve(seq.size()/2);
  for(size_t i=1;i<seq.size();i++){unsigned a=seq[i-1],b=seq[i];if(a==65535||b==65535||dict[a].size()+dict[b].size()>maxlen)continue;freq[a|(b<<16)]++;}
  std::vector<std::pair<uint32_t,uint32_t>> cand; cand.reserve(freq.size());
  for(auto [k,v]:freq)if(v>=5)cand.push_back({v,k});
  std::sort(cand.begin(),cand.end(),[](auto a,auto b){return a.first!=b.first?a.first>b.first:a.second<b.second;});
  int take=std::min({int(cand.size()),maxcodes-int(dict.size()),128});if(!take)break;
  std::unordered_map<uint32_t,uint16_t> replace;replace.reserve(take*2);
  for(int j=0;j<take;j++){uint32_t k=cand[j].second;replace[k]=dict.size();parents.push_back(k);dict.push_back(dict[k&65535]+dict[k>>16]);}
  size_t w=0;for(size_t i=0;i<seq.size();i++){if(i+1<seq.size()&&seq[i]!=65535&&seq[i+1]!=65535){auto it=replace.find(seq[i]|uint32_t(seq[i+1])<<16);if(it!=replace.end()){seq[w++]=it->second;i++;continue;}}seq[w++]=seq[i];}seq.resize(w);
 }
#include "reparse.inc"
 std::vector<uint32_t> freq(dict.size());for(auto v:seq)if(v!=65535)freq[v]++;
 std::vector<uint16_t> order;for(unsigned i=0;i<dict.size();i++)order.push_back(i);
 unsigned nd=order.size(), bits=0;while((1u<<bits)<nd)bits++;
 std::vector<uint16_t> remap(dict.size());for(unsigned i=0;i<nd;i++)remap[order[i]]=i;
 Header h{MAGIC,uint32_t(n),nr,nd,bits,uint32_t(seq.size()-nr),0,(nr+7)/8};
 std::vector<uint8_t> ds; for(unsigned id=256;id<parents.size();id++)put32(ds,parents[id]);h.dictbytes=ds.size();
 std::vector<uint8_t> lens;std::vector<uint32_t> index;unsigned rc=0,nrt=0,nt=0;for(auto v:seq){if(v==65535){if(rc%8==0)index.push_back(nt-nrt);if(nrt>255)return false;lens.push_back(nrt);rc++;nrt=0;}else{nrt++;nt++;}}
 out.resize(sizeof(h));memcpy(out.data(),&h,sizeof(h));out.insert(out.end(),ds.begin(),ds.end());while(out.size()%4)out.push_back(0);for(auto v:index){out.push_back(v);out.push_back(v>>8);out.push_back(v>>16);}
 size_t base=out.size();out.resize(base+(uint64_t(nt)*bits+7)/8+8,0);uint64_t bit=0;for(auto v:seq)if(v!=65535){uint64_t val=uint64_t(remap[v])<<(bit&7);size_t p=base+(bit>>3);out[p]|=val;out[p+1]|=val>>8;out[p+2]|=val>>16;bit+=bits;}
 return true;
}
#endif
}
