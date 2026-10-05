#pragma once
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <cstdio>
#include "dictpack.h"
#ifndef ENTROPY_MAX_CODES
#define ENTROPY_MAX_CODES 4096
#endif
#ifndef ENTROPY_BATCH
#define ENTROPY_BATCH 128
#endif
namespace entropy {
static constexpr uint32_t MAGIC=0x33544e45;
static constexpr unsigned HBITS=15;
struct Header { uint32_t magic,nraw,nrow,ndict,ntok,dictbytes,nblocks,group,nbits; uint32_t laneBit[3],laneRaw[3]; };
struct Entry { uint8_t data[31]; uint8_t len; };
struct State { Header h; const uint8_t* index; const uint8_t* toks; Entry* dict; uint32_t* table; };
static uint32_t reverse_code(uint32_t code,unsigned n) {uint32_t v=0;for(unsigned j=0;j<n;++j){v=(v<<1)|(code&1);code>>=1;}return v;}
static void make_codes(const uint8_t* lens,unsigned n,uint16_t* codes) {
 unsigned count[16]={},next[16]={};for(unsigned i=0;i<n;++i)++count[lens[i]];
 unsigned c=0;for(unsigned j=1;j<=15;++j){c=(c+count[j-1])<<1;next[j]=c;}
 for(unsigned i=0;i<n;++i)codes[i]=reverse_code(next[lens[i]]++,lens[i]);
}
#ifdef ENCODER
static std::vector<uint8_t> huffman_lengths(const std::vector<uint32_t>& weights) {
 struct Node { uint64_t w; int a,b,sym; };
 const unsigned n=weights.size();std::vector<uint8_t> result(n,0);
 if(n==1){result[0]=1;return result;}
 std::vector<Node> nodes;nodes.reserve(n*16);
 for(unsigned i=0;i<n;++i)nodes.push_back({weights[i],-1,-1,int(i)});
 std::vector<int> leaves(n);for(unsigned i=0;i<n;++i)leaves[i]=i;
 std::sort(leaves.begin(),leaves.end(),[&](int a,int b){return nodes[a].w!=nodes[b].w?nodes[a].w<nodes[b].w:a<b;});
 std::vector<int> current=leaves;
 for(unsigned level=1;level<HBITS;++level) {
   std::vector<int> packages;packages.reserve(current.size()/2);
   for(size_t j=1;j<current.size();j+=2){int a=current[j-1],b=current[j];packages.push_back(nodes.size());nodes.push_back({nodes[a].w+nodes[b].w,a,b,-1});}
   std::vector<int> merged;merged.reserve(leaves.size()+packages.size());size_t a=0,b=0;
   while((a<leaves.size()||b<packages.size())&&merged.size()<2*n-2){
     if(b==packages.size()||(a<leaves.size()&&nodes[leaves[a]].w<=nodes[packages[b]].w))merged.push_back(leaves[a++]);
     else merged.push_back(packages[b++]);
   }
   current.swap(merged);
 }
 std::vector<int> stack;for(unsigned j=0;j<2*n-2;++j)stack.push_back(current[j]);
 while(!stack.empty()){int v=stack.back();stack.pop_back();if(nodes[v].sym>=0)++result[nodes[v].sym];else {stack.push_back(nodes[v].a);stack.push_back(nodes[v].b);}}
 return result;
}
static void put32(std::vector<uint8_t>& o,uint32_t v){size_t p=o.size();o.resize(p+4);memcpy(o.data()+p,&v,4);}
inline bool encode(const uint8_t* raw,size_t n,std::vector<uint8_t>& out,int maxcodes=ENTROPY_MAX_CODES,int batch=ENTROPY_BATCH,unsigned group=8) {
 if(!n||maxcodes<256||maxcodes>32768||!batch||!group||group>64)return false;
 std::vector<std::string> dict(256); for(int i=0;i<256;i++)dict[i]=std::string(1,char(i));
 std::vector<uint16_t> seq; seq.reserve(n+n/10); uint32_t nr=0; for(size_t i=0;i<n;i++){seq.push_back(raw[i]);if(raw[i]==10){seq.push_back(65535);nr++;}} if(n && raw[n-1]!=10){seq.push_back(65535);nr++;}
 const int maxlen=24;
 for(int round=0;dict.size()<maxcodes;round++){
  std::unordered_map<uint32_t,uint32_t> freq;freq.reserve(seq.size()/2);
  for(size_t i=1;i<seq.size();i++){unsigned a=seq[i-1],b=seq[i];if(a==65535||b==65535||dict[a].size()+dict[b].size()>maxlen)continue;freq[a|(b<<16)]++;}
  std::vector<std::pair<uint32_t,uint32_t>> cand; cand.reserve(freq.size());
  for(auto [k,v]:freq)if(v>=5)cand.push_back({v,k});
  std::sort(cand.begin(),cand.end(),[](auto a,auto b){return a.first!=b.first?a.first>b.first:a.second<b.second;});
  int take=std::min({int(cand.size()),maxcodes-int(dict.size()),batch});if(!take)break;
  std::unordered_map<uint32_t,uint16_t> replace;replace.reserve(take*2);
  for(int j=0;j<take;j++){uint32_t k=cand[j].second;replace[k]=dict.size();dict.push_back(dict[k&65535]+dict[k>>16]);}
  size_t w=0;for(size_t i=0;i<seq.size();i++){if(i+1<seq.size()&&seq[i]!=65535&&seq[i+1]!=65535){auto it=replace.find(seq[i]|uint32_t(seq[i+1])<<16);if(it!=replace.end()){seq[w++]=it->second;i++;continue;}}seq[w++]=seq[i];}seq.resize(w);
 }
#ifdef ENTROPY_REPARSE
#include "reparse.inc"
#endif
 std::vector<uint32_t> freq(dict.size());for(auto v:seq)if(v!=65535)freq[v]++;
 std::vector<uint16_t> order;for(unsigned i=0;i<dict.size();i++)if(freq[i])order.push_back(i);
 std::sort(order.begin(),order.end(),[&](auto a,auto b){return freq[a]!=freq[b]?freq[a]>freq[b]:a<b;});
 unsigned nd=order.size(), bits=0;while((1u<<bits)<nd)bits++;
 std::vector<uint16_t> remap(dict.size());for(unsigned i=0;i<nd;i++)remap[order[i]]=i;

 std::vector<uint32_t> weights(nd);for(unsigned i=0;i<nd;++i)weights[i]=freq[order[i]];
 std::vector<uint8_t> cl=huffman_lengths(weights);std::vector<uint16_t> codes(nd);make_codes(cl.data(),nd,codes.data());
 Header h{MAGIC,uint32_t(n),nr,nd,uint32_t(seq.size()-nr),0,(nr+group-1)/group,group,0};
 std::vector<uint8_t> ds;for(unsigned i=0;i<nd;++i){const auto& s=dict[order[i]];ds.push_back(s.size());ds.push_back(cl[i]);ds.insert(ds.end(),s.begin(),s.end());}h.dictbytes=ds.size();
 std::vector<uint8_t> packed;dictpack::encode(ds.data(),ds.size(),packed);
 if(packed.size()+4<ds.size()){std::vector<uint8_t> tmp;put32(tmp,ds.size());tmp.insert(tmp.end(),packed.begin(),packed.end());ds.swap(tmp);h.dictbytes=uint32_t(ds.size())|0x80000000u;}
 std::vector<uint32_t> index;unsigned rc=0,nrb=0,nb=0;
 for(auto v:seq){if(v==65535){if(rc%group==0)index.push_back(nb-nrb);++rc;nrb=0;}else{nrb+=cl[remap[v]];nb+=cl[remap[v]];}}
 if(nb>=(1u<<24))return false;h.nbits=nb;out.resize(sizeof(h));memcpy(out.data(),&h,sizeof(h));out.insert(out.end(),ds.begin(),ds.end());while(out.size()%4)out.push_back(0);for(auto v:index){out.push_back(uint8_t(v));out.push_back(uint8_t(v>>8));out.push_back(uint8_t(v>>16));}
 size_t base=out.size();out.resize(base+(uint64_t(nb)+7)/8+8,0);uint64_t bit=0;unsigned tk=0,rawpos=0,lane=0,step=h.ntok/4;for(auto v:seq)if(v!=65535){unsigned id=remap[v];uint64_t val=uint64_t(codes[id])<<(bit&7);size_t p=base+(bit>>3);out[p]|=val;out[p+1]|=val>>8;out[p+2]|=val>>16;bit+=cl[id];rawpos+=dict[v].size();++tk;if(lane<3&&tk==step*(lane+1)){h.laneBit[lane]=bit;h.laneRaw[lane]=rawpos;++lane;}}
 memcpy(out.data(),&h,sizeof(h));
 return true;
}
#endif
inline void* open(const uint8_t* a,size_t n) {
 if(n<sizeof(Header))return nullptr;Header h;memcpy(&h,a,sizeof(h));
 if(h.magic!=MAGIC||h.ndict>32768||!h.ndict||!h.group||h.group>64||h.nblocks!=(uint64_t(h.nrow)+h.group-1)/h.group||h.nraw>100000000||h.ntok>h.nraw||h.nrow>h.nraw||!h.nbits||h.nbits>=(1u<<24))return nullptr;
 size_t packedlen=h.dictbytes&0x7fffffffu,archiveend=sizeof(h)+packedlen;
 if(archiveend>n)return nullptr;
 std::vector<uint8_t> unpacked;const uint8_t* ds=a+sizeof(h);size_t dsize=packedlen;
 if(h.dictbytes&0x80000000u){if(packedlen<4)return nullptr;uint32_t rawlen;memcpy(&rawlen,ds,4);if(rawlen>h.ndict*26ull)return nullptr;unpacked.resize(rawlen);if(!dictpack::decode(ds+4,packedlen-4,unpacked.data(),rawlen))return nullptr;ds=unpacked.data();dsize=rawlen;}
 Entry* dict=(Entry*)calloc(h.ndict,sizeof(Entry));uint32_t* table=(uint32_t*)calloc(1u<<HBITS,sizeof(uint32_t));
 if(!dict||!table){free(dict);free(table);return nullptr;}
 std::vector<uint8_t> cl(h.ndict);std::vector<uint16_t> codes(h.ndict);size_t p=0;unsigned kraft=0;
 for(unsigned i=0;i<h.ndict;++i){if(p+2>dsize||!ds[p]||ds[p]>24||!ds[p+1]||ds[p+1]>HBITS||p+2+ds[p]>dsize){free(dict);free(table);return nullptr;}dict[i].len=ds[p++];cl[i]=ds[p++];memcpy(dict[i].data,ds+p,dict[i].len);if(dict[i].len>1&&memchr(dict[i].data,10,dict[i].len-1)){free(dict);free(table);return nullptr;}p+=dict[i].len;kraft+=1u<<(HBITS-cl[i]);}
 if(p!=dsize||(kraft!=(1u<<HBITS)&&!(h.ndict==1&&cl[0]==1))){free(dict);free(table);return nullptr;}
 p=(archiveend+3)&~size_t(3);size_t afterindex=p+3ull*h.nblocks;
 if(afterindex+(uint64_t(h.nbits)+7)/8+8!=n){free(dict);free(table);return nullptr;}
 const uint8_t* index=a+p;
 uint32_t previous=0;for(unsigned i=0;i<h.nblocks;++i){const uint8_t* ip=index+3ull*i;uint32_t value=uint32_t(ip[0])|(uint32_t(ip[1])<<8)|(uint32_t(ip[2])<<16);if(value>=h.nbits||(i&&value<previous)||(!i&&value)){free(dict);free(table);return nullptr;}previous=value;}
 for(unsigned i=0;i<3;++i)if(h.laneBit[i]>h.nbits||h.laneRaw[i]>h.nraw||(i&&(h.laneBit[i]<h.laneBit[i-1]||h.laneRaw[i]<h.laneRaw[i-1]))){free(dict);free(table);return nullptr;}
 make_codes(cl.data(),h.ndict,codes.data());
 for(unsigned i=0;i<h.ndict;++i){unsigned len=cl[i],step=1u<<len;uint32_t ent=i|(uint32_t(len)<<16);for(unsigned j=codes[i];j<(1u<<HBITS);j+=step)table[j]=ent;}
 return new State{h,index,a+afterindex,dict,table};
}
static inline uint32_t index_at(const State* s,unsigned rowgroup) {uint32_t v;memcpy(&v,s->index+3ull*rowgroup,4);return v&0xffffffu;}
static inline uint32_t lookup(const State* s,uint64_t bit) {
 uint32_t v;memcpy(&v,s->toks+(bit>>3),4);return s->table[(v>>(bit&7))&((1u<<HBITS)-1)];
}
static inline uint8_t* one(const State*s,uint64_t&bit,uint8_t*p,uint8_t*end) {
 if(bit>=s->h.nbits)return nullptr;uint32_t v=lookup(s,bit);if(!(v>>16)||bit+(v>>16)>s->h.nbits)return nullptr;bit+=v>>16;const Entry&e=s->dict[v&65535];if(size_t(end-p)<e.len)return nullptr;if(size_t(end-p)>=32)memcpy(p,e.data,32);else memcpy(p,e.data,e.len);return p+e.len;
}
inline int64_t decode(void* state,uint8_t* out,size_t cap) {
 State* s=(State*)state;if(!s||cap<s->h.nraw)return -1;
 const unsigned q=s->h.ntok/4;uint64_t b0=0,b1=s->h.laneBit[0],b2=s->h.laneBit[1],b3=s->h.laneBit[2];
 uint8_t *p0=out,*p1=out+s->h.laneRaw[0],*p2=out+s->h.laneRaw[1],*p3=out+s->h.laneRaw[2];
 uint8_t *e0=p1,*e1=p2,*e2=p3,*e3=out+s->h.nraw;
 for(unsigned j=0;j<q;++j){p0=one(s,b0,p0,e0);p1=one(s,b1,p1,e1);p2=one(s,b2,p2,e2);p3=one(s,b3,p3,e3);if(!p0||!p1||!p2||!p3)return -1;}
 for(unsigned j=q*4;j<s->h.ntok;++j){p3=one(s,b3,p3,e3);if(!p3)return -1;}
 if(p0!=e0||p1!=e1||p2!=e2||p3!=e3||b0!=s->h.laneBit[0]||b1!=s->h.laneBit[1]||b2!=s->h.laneBit[2]||b3!=s->h.nbits)return -1;return s->h.nraw;
}
inline int64_t rows(void* state,const uint64_t* ids,size_t count,uint8_t* out,size_t cap,uint64_t* offsets) {
 State*s=(State*)state;if(!s||!offsets)return -1;offsets[0]=0;uint8_t*o=out;
 uint64_t current=~uint64_t(0),bit=0;
 for(size_t q=0;q<count;++q){
  uint64_t r=ids[q];if(r>=s->h.nrow||(q&&r<ids[q-1]))return -1;uint64_t begin=(r/s->h.group)*s->h.group;
  if(current>r||current<begin){current=begin;bit=index_at(s,r/s->h.group);}
  while(current<r){bool ended=false;while(bit<s->h.nbits){uint32_t v=lookup(s,bit);if(!(v>>16)||bit+(v>>16)>s->h.nbits)return -1;bit+=v>>16;const Entry&e=s->dict[v&65535];if(e.data[e.len-1]==10){ended=true;break;}}if(!ended)return -1;++current;}
  bool ended=false;while(bit<s->h.nbits){uint32_t v=lookup(s,bit);if(!(v>>16)||bit+(v>>16)>s->h.nbits)return -1;bit+=v>>16;const Entry&e=s->dict[v&65535];size_t left=cap-size_t(o-out);if(left<e.len)return -1;if(left>=32)memcpy(o,e.data,32);else memcpy(o,e.data,e.len);o+=e.len;if(e.data[e.len-1]==10){ended=true;break;}}
  if(!ended&&r+1!=s->h.nrow)return -1;current=r+1;offsets[q+1]=o-out;
 }
 return o-out;
}
inline void close(void* state){auto*s=(State*)state;if(s){free(s->dict);free(s->table);delete s;}}
}
