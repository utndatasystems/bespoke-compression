#pragma once
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <cstdio>
namespace phrase {
static constexpr uint32_t MAGIC=0x31524850;
struct Header { uint32_t magic,nraw,nrow,ndict,bits,ntok,dictbytes,nblocks; };
struct Entry { uint8_t data[32]; uint8_t len; };
struct State { Header h; const uint8_t* ar; size_t size; const uint8_t* lens; const uint32_t* index; const uint8_t* toks; Entry *dict; };
#ifdef ENCODER
static void put32(std::vector<uint8_t>& o,uint32_t v){size_t p=o.size();o.resize(p+4);memcpy(o.data()+p,&v,4);}
inline bool encode(const uint8_t* raw,size_t n,std::vector<uint8_t>& out) {
 std::vector<std::string> dict(256); for(int i=0;i<256;i++)dict[i]=std::string(1,char(i));
 std::vector<uint16_t> seq; seq.reserve(n+n/10); uint32_t nr=0; for(size_t i=0;i<n;i++){seq.push_back(raw[i]);if(raw[i]==10){seq.push_back(65535);nr++;}} if(n && raw[n-1]!=10){seq.push_back(65535);nr++;}
 #ifndef PHRASE_MAX
#define PHRASE_MAX 8192
#endif
 const int maxcodes=PHRASE_MAX; const int maxlen=24;
 for(int round=0;dict.size()<maxcodes;round++){
  std::unordered_map<uint32_t,uint32_t> freq;freq.reserve(seq.size()/2);
  for(size_t i=1;i<seq.size();i++){unsigned a=seq[i-1],b=seq[i];if(a==65535||b==65535||dict[a].size()+dict[b].size()>maxlen)continue;freq[a|(b<<16)]++;}
  std::vector<std::pair<uint32_t,uint32_t>> cand; cand.reserve(freq.size());
  for(auto [k,v]:freq)if(v>=5)cand.push_back({v,k});
  std::sort(cand.begin(),cand.end(),[](auto a,auto b){return a.first!=b.first?a.first>b.first:a.second<b.second;});
  int take=std::min({int(cand.size()),maxcodes-int(dict.size()),128});if(!take)break;
  std::unordered_map<uint32_t,uint16_t> replace;replace.reserve(take*2);
  for(int j=0;j<take;j++){uint32_t k=cand[j].second;replace[k]=dict.size();dict.push_back(dict[k&65535]+dict[k>>16]);}
  size_t w=0;for(size_t i=0;i<seq.size();i++){if(i+1<seq.size()&&seq[i]!=65535&&seq[i+1]!=65535){auto it=replace.find(seq[i]|uint32_t(seq[i+1])<<16);if(it!=replace.end()){seq[w++]=it->second;i++;continue;}}seq[w++]=seq[i];}seq.resize(w);
 }
 std::vector<uint32_t> freq(dict.size());for(auto v:seq)if(v!=65535)freq[v]++;
 std::vector<uint16_t> order;for(unsigned i=0;i<dict.size();i++)if(freq[i])order.push_back(i);
 std::sort(order.begin(),order.end(),[&](auto a,auto b){return freq[a]!=freq[b]?freq[a]>freq[b]:a<b;});
 unsigned nd=order.size(), bits=0;while((1u<<bits)<nd)bits++;
 std::vector<uint16_t> remap(dict.size());for(unsigned i=0;i<nd;i++)remap[order[i]]=i;
 Header h{MAGIC,uint32_t(n),nr,nd,bits,uint32_t(seq.size()-nr),0,(nr+31)/32};
 std::vector<uint8_t> ds; for(unsigned id:order){ds.push_back(dict[id].size());ds.insert(ds.end(),dict[id].begin(),dict[id].end());}h.dictbytes=ds.size();
 std::vector<uint8_t> lens;std::vector<uint32_t> index;unsigned rc=0,nrt=0,nt=0;for(auto v:seq){if(v==65535){if(rc%32==0)index.push_back(nt-nrt);if(nrt>255)return false;lens.push_back(nrt);rc++;nrt=0;}else{nrt++;nt++;}}
 out.resize(sizeof(h));memcpy(out.data(),&h,sizeof(h));out.insert(out.end(),ds.begin(),ds.end());out.insert(out.end(),lens.begin(),lens.end());while(out.size()%4)out.push_back(0);for(auto v:index)put32(out,v);
 size_t base=out.size();out.resize(base+(uint64_t(nt)*bits+7)/8+8,0);uint64_t bit=0;for(auto v:seq)if(v!=65535){uint64_t val=uint64_t(remap[v])<<(bit&7);size_t p=base+(bit>>3);out[p]|=val;out[p+1]|=val>>8;out[p+2]|=val>>16;bit+=bits;}
 return true;
}
#endif
inline void* open(const uint8_t* a,size_t n){
 if(n<sizeof(Header))return nullptr;Header h;memcpy(&h,a,sizeof(h));if(h.magic!=MAGIC||h.ndict>65535||h.ndict==0||h.bits>16||h.bits<1||h.nblocks!=(h.nrow+31)/32||h.ndict>(1u<<h.bits)||h.nraw>100000000)return nullptr;
 size_t p=sizeof(h),end=p+h.dictbytes;if(end>n)return nullptr;Entry* d=(Entry*)calloc(h.ndict,sizeof(Entry));if(!d)return nullptr;
 for(unsigned i=0;i<h.ndict;i++){if(p>=end||a[p]==0||a[p]>32||p+1+a[p]>end){free(d);return nullptr;}d[i].len=a[p++];memcpy(d[i].data,a+p,d[i].len);p+=d[i].len;}
 if(p!=end||p+h.nrow>n){free(d);return nullptr;} const uint8_t* lens=a+p;p+=h.nrow;p=(p+3)&~size_t(3);if(p+4ull*h.nblocks>n){free(d);return nullptr;}const uint32_t* index=(const uint32_t*)(a+p);p+=4ull*h.nblocks; if(p+(uint64_t(h.ntok)*h.bits+7)/8+8!=n){free(d);return nullptr;}
 uint64_t total=0;for(unsigned r=0;r<h.nrow;r++){if(r%32==0 && index[r/32]!=total){free(d);return nullptr;}total+=lens[r];}if(total!=h.ntok){free(d);return nullptr;}
 const uint8_t* toks=a+p;uint64_t rawsum=0,bit=0;unsigned mask=(1u<<h.bits)-1;for(unsigned i=0;i<h.ntok;i++){uint32_t v;memcpy(&v,toks+(bit>>3),4);v=(v>>(bit&7))&mask;if(v>=h.ndict){free(d);return nullptr;}rawsum+=d[v].len;bit+=h.bits;}if(rawsum!=h.nraw){free(d);return nullptr;}
 State* s=new State{h,a,n,lens,index,toks,d};return s;
}
inline uint32_t token(const State* s,uint64_t i){uint64_t b=i*s->h.bits;uint32_t v;memcpy(&v,s->toks+(b>>3),4);return (v>>(b&7))&((1u<<s->h.bits)-1);}
inline uint8_t* unpack(const State* s,uint32_t start,uint32_t count,uint8_t* out,uint8_t* end){
 for(uint32_t j=0;j<count;j++){const Entry& e=s->dict[token(s,start+j)];if(size_t(end-out)>=32)memcpy(out,e.data,32);else memcpy(out,e.data,e.len);out+=e.len;}return out;
}
inline int64_t decode(void* p,uint8_t* out,size_t cap){State*s=(State*)p;if(!s||cap<s->h.nraw)return -1;return unpack(s,0,s->h.ntok,out,out+cap)-out;}
inline int64_t rows(void* p,const uint64_t* ids,size_t count,uint8_t* out,size_t cap,uint64_t* offsets){State*s=(State*)p;if(!s||!offsets)return -1;offsets[0]=0;uint8_t*o=out;for(size_t q=0;q<count;q++){uint64_t r=ids[q];if(r>=s->h.nrow||(q&&r<ids[q-1]))return -1;uint32_t t=s->index[r/32];for(unsigned i=r&~31ull;i<r;i++)t+=s->lens[i];unsigned ct=s->lens[r];size_t len=0;for(unsigned j=0;j<ct;j++)len+=s->dict[token(s,t+j)].len;if(len>cap-size_t(o-out))return -1;o=unpack(s,t,ct,o,out+cap);offsets[q+1]=o-out;}return o-out;}
inline void close(void*p){auto*s=(State*)p;if(s){free(s->dict);delete s;}}
}
