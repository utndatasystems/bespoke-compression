#include "codec.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <queue>
#include <cstdio>
#ifndef MAXTOK
#define MAXTOK 32768
#endif
#ifndef MAXLEN
#define MAXLEN 64
#endif
struct Pair{uint32_t a,b;int count=0;std::vector<int> pos;};
struct Q{int c;uint32_t id;bool operator<(const Q&b)const{return c<b.c;}};
static bool word(unsigned c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9');}
static bool white(unsigned c){return c==32||c==10||c==13||c==9;}
extern "C" int64_t lab_encode(const uint8_t*raw,size_t n,uint8_t*out,size_t cap){
 std::unordered_map<std::string,uint32_t> words;std::vector<std::string> dict;std::vector<int> tok;
 for(size_t p=0;p<n;){size_t e=p+1; std::string s((const char*)raw+p,e-p);auto z=words.emplace(s,dict.size());if(z.second)dict.push_back(s);tok.push_back(z.first->second);p=e;}
 words.clear();words.rehash(0); size_t N=tok.size(); std::vector<int>next(N),prev(N),freq(MAXTOK);for(size_t i=0;i<N;i++){next[i]=i+1<N?i+1:-1;prev[i]=i?i-1:-1;freq[tok[i]]++;}
 std::unordered_map<uint64_t,uint32_t> pm;pm.reserve(500000);std::vector<Pair>pairs;pairs.reserve(500000);std::priority_queue<Q> q;
 auto add=[&](int p){if(p<0||next[p]<0)return;uint32_t a=tok[p],b=tok[next[p]];uint64_t k=(uint64_t(a)<<32)|b;auto z=pm.emplace(k,pairs.size());if(z.second)pairs.push_back(Pair{a,b,0,{}});Pair&v=pairs[z.first->second];v.count++;v.pos.push_back(p);q.push(Q{v.count,z.first->second});};
 auto sub=[&](int p){if(p<0||next[p]<0)return;uint64_t k=(uint64_t(tok[p])<<32)|uint32_t(tok[next[p]]);pairs[pm.find(k)->second].count--;};
 for(size_t i=0;i+1<N;i++)add(i);
 const uint32_t base=dict.size();std::vector<uint16_t> rules; size_t D=0;for(size_t i=0;i<dict.size();i++)if(freq[i])D+=dict[i].size()+4;size_t nt=N,best=nt*2+D;std::vector<int>besttok;std::vector<int>bestfreq;
 fprintf(stderr,"initial tokens=%zu dict=%zu bytes=%zu\n",N,dict.size(),best);
 while(dict.size()<MAXTOK&&!q.empty()){
  Q top=q.top();q.pop();if(top.c!=pairs[top.id].count)continue;if(top.c<2)break;
  uint32_t a=pairs[top.id].a,b=pairs[top.id].b;if(dict[a].size()+dict[b].size()>MAXLEN)continue;
  std::vector<int> pos;pos.swap(pairs[top.id].pos);
  uint32_t id=dict.size();dict.push_back(dict[a]+dict[b]);rules.push_back(a);rules.push_back(b);
  for(int p:pos){if(tok[p]!=(int)a||next[p]<0||tok[next[p]]!=(int)b)continue;int r=next[p],l=prev[p],rr=next[r];sub(l);sub(p);sub(r);
   if(--freq[a]==0)D-=dict[a].size()+4;if(--freq[b]==0)D-=dict[b].size()+4;if(freq[id]++==0)D+=dict[id].size()+4;
   tok[p]=id;tok[r]=-1;next[p]=rr;if(rr>=0)prev[rr]=p;next[r]=-1;nt--;add(l);add(p);
  }
  size_t sz=2*nt+D;if(sz<best){best=sz;if(dict.size()%128==0){besttok=tok;bestfreq=freq;}}
  if(false)fprintf(stderr,"tokens=%zu dict=%zu cost=%zu best=%zu pairs=%zu heap=%zu\n",nt,dict.size(),sz,best,pairs.size(),q.size());
 }
 uint32_t nd=dict.size(),db=0,nc=0,expanded=0;for(uint32_t i=0;i<nd;i++){expanded+=dict[i].size();if(i<base)db+=dict[i].size();}for(int t:tok)if(t>=0)nc++;
 uint64_t total=32ull+(base+1)*4ull+db+64ull+(nd-base)*4ull+(nc*15ull+7)/8+4;if(total>cap)return -1;uint32_t h[8]={0x32435052,(uint32_t)n,nd,db,nc,base,expanded,15};memcpy(out,h,32);uint32_t off=0;uint8_t*data=out+32+4*(base+1);for(uint32_t i=0;i<base;i++){memcpy(out+32+4*i,&off,4);memcpy(data+off,dict[i].data(),dict[i].size());off+=dict[i].size();}memcpy(out+32+4*base,&off,4);memset(data+db,0,64);uint8_t*rp=data+db+64;memcpy(rp,rules.data(),rules.size()*2);uint8_t*stream=rp+rules.size()*2;uint64_t acc=0;unsigned bits=0;for(int t:tok)if(t>=0){acc|=(uint64_t)t<<bits;bits+=15;while(bits>=8){*stream++=acc;acc>>=8;bits-=8;}}if(bits)*stream++=acc;memset(stream,0,4);
 fprintf(stderr,"final dict=%u base=%u dictbytes=%u tokens=%u expanded=%u total=%llu\n",nd,base,db,nc,expanded,(unsigned long long)total);return total;
}
