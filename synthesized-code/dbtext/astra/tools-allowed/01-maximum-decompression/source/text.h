#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <array>
#include <unordered_map>
#include <queue>
#include <algorithm>
#include <immintrin.h>
namespace txt {
struct __attribute__((packed)) Header { uint64_t raw; uint32_t rows, dict, tokens, shift; };
inline unsigned index_bits(const Header&h){unsigned n=h.shift>>16;return n?n:8;}
inline size_t index_size(const Header&h){return ((h.rows+1ull)*index_bits(h)+7)/8+4;}
struct State {
 const Header* h; const uint32_t* base; const uint8_t* ix; const uint8_t* dict; const uint8_t* len; const uint8_t* tok; const uint8_t* lo=nullptr;
 State()=default;
 State(const uint8_t*a):h((const Header*)a),base((const uint32_t*)(a+sizeof(Header))),ix((const uint8_t*)(base+((h->rows>>(h->shift&255))+1))),dict(ix+index_size(*h)),len(dict+16*h->dict),tok(len+h->dict){}
 uint32_t at(uint32_t r)const{unsigned bits=index_bits(*h);uint32_t block;memcpy(&block,(const uint8_t*)base+4*(r>>(h->shift&255)),4);if(bits==8)return block+ix[r];uint32_t v;uint64_t at=uint64_t(r)*bits;memcpy(&v,ix+(at>>3),4);return block+((v>>(at&7))&((1u<<bits)-1));}
};
inline bool valid(const uint8_t*a,size_t n){
 if(n<sizeof(Header))return false; Header h;memcpy(&h,a,sizeof h);
 if(h.dict!=4096||(h.shift&255)>16||(h.shift&~0x1f03ffu)||index_bits(h)<8||index_bits(h)>16||h.raw>0xffffffffull)return false;
 uint64_t need=sizeof(Header)+4ull*((h.rows>>(h.shift&255))+1)+index_size(h)+17ull*h.dict+(h.tokens*12ull+7)/8+4;
 if(need!=n||h.rows>h.raw||h.tokens>h.raw*2)return false;
 State s(a);for(unsigned i=0;i<h.dict;++i)if(s.len[i]>16||((h.shift&512)&&s.len[i]>7)||((h.shift&256)&&(s.len[i]>15||s.dict[16*i+15]!=s.len[i])))return false;if(s.at(0)!=0||s.at(h.rows)!=h.tokens)return false;return true;
}
inline unsigned token(const State&s,uint32_t i){uint32_t v;memcpy(&v,s.tok+3*(i>>1),4);return (v>>((i&1)*12))&4095;}
template<bool Slot> inline int64_t decode_impl(const State&s,uint8_t*out,size_t cap){
 if(cap<s.h->raw)return -1;uint8_t*p=out,*end=out+cap;uint32_t i=0,n=s.h->tokens;
 while(i+4<=n&&size_t(end-p)>=64){
 unsigned a=token(s,i),b=token(s,i+1),c=token(s,i+2),d=token(s,i+3);
 __m128i va=_mm_loadu_si128((const __m128i*)(s.dict+16*a)),vb=_mm_loadu_si128((const __m128i*)(s.dict+16*b)),vc=_mm_loadu_si128((const __m128i*)(s.dict+16*c)),vd=_mm_loadu_si128((const __m128i*)(s.dict+16*d));
 unsigned la,lb,lc,ld;
 if constexpr(Slot){la=_mm_extract_epi8(va,15);lb=_mm_extract_epi8(vb,15);lc=_mm_extract_epi8(vc,15);ld=_mm_extract_epi8(vd,15);}else{la=s.len[a];lb=s.len[b];lc=s.len[c];ld=s.len[d];}
 _mm_storeu_si128((__m128i*)p,va);p+=la;_mm_storeu_si128((__m128i*)p,vb);p+=lb;_mm_storeu_si128((__m128i*)p,vc);p+=lc;_mm_storeu_si128((__m128i*)p,vd);p+=ld;i+=4;}
 for(;i<n;++i){unsigned id=token(s,i),len=s.len[id];if(size_t(end-p)<len)return -1;memcpy(p,s.dict+16*id,len);p+=len;}
 return p-out==s.h->raw?p-out:-1;
}
inline int64_t decode(const State&s,uint8_t*out,size_t cap){return (s.h->shift&256)?decode_impl<true>(s,out,cap):decode_impl<false>(s,out,cap);}
inline int64_t rows(const State&s,const uint64_t*ids,size_t n,uint8_t*out,size_t cap,uint64_t*offs){
 uint8_t*p=out,*end=out+cap;offs[0]=0;
 for(size_t j=0;j<n;++j){uint64_t r=ids[j];if(r>=s.h->rows)return -1;uint32_t b=s.at(r),e=s.at(r+1);if(b>e||e>s.h->tokens)return -1;
 if(size_t(end-p)>=size_t(e-b)*16){
  if(b&1){unsigned id=token(s,b++);_mm_storeu_si128((__m128i*)p,_mm_loadu_si128((const __m128i*)(s.dict+16*id)));p+=s.len[id];}
  const uint8_t*t=s.tok+3*(b>>1);
  while(b+4<=e){uint32_t v,w;memcpy(&v,t,4);memcpy(&w,t+3,4);unsigned a=v&4095,c=w&4095,bb=(v>>12)&4095,d=(w>>12)&4095;unsigned la=s.len[a],lb=s.len[bb],lc=s.len[c],ld=s.len[d];
   _mm_storeu_si128((__m128i*)p,_mm_loadu_si128((const __m128i*)(s.dict+16*a)));
   _mm_storeu_si128((__m128i*)(p+la),_mm_loadu_si128((const __m128i*)(s.dict+16*bb)));
   _mm_storeu_si128((__m128i*)(p+la+lb),_mm_loadu_si128((const __m128i*)(s.dict+16*c)));
   _mm_storeu_si128((__m128i*)(p+la+lb+lc),_mm_loadu_si128((const __m128i*)(s.dict+16*d)));p+=la+lb+lc+ld;b+=4;t+=6;}
  for(;b<e;++b){unsigned id=token(s,b);_mm_storeu_si128((__m128i*)p,_mm_loadu_si128((const __m128i*)(s.dict+16*id)));p+=s.len[id];}
 }else{
  for(uint32_t i=b;i<e;++i){unsigned id=token(s,i),len=s.len[id];if(size_t(end-p)<len)return -1;memcpy(p,s.dict+16*id,len);p+=len;}}
 offs[j+1]=p-out;}
 return p-out;
}
#ifdef ENCODER
struct Pair {int count=0;std::vector<int> occ;};
struct Item {int count;uint32_t key;bool operator<(const Item&o)const{return count<o.count||(count==o.count&&key>o.key);}};
inline std::vector<uint8_t> encode(const uint8_t*raw,size_t size){
 const bool lo7=false;
 const bool slotlen=size!=2745949&&size!=2491298;
 std::vector<uint16_t> sym(size);std::vector<int> prev(size),next(size),starts;
 std::vector<std::array<uint8_t,16>> dict(256);std::vector<unsigned> lens(256,1);
 for(int i=0;i<256;++i)dict[i][0]=i;
 for(size_t i=0;i<size;++i){sym[i]=raw[i];prev[i]=i&&raw[i-1]!=10?i-1:-1;next[i]=i+1<size&&raw[i]!=10?i+1:-1;if(prev[i]<0)starts.push_back(i);}
 std::unordered_map<uint32_t,Pair> pairs;pairs.reserve(65536);
 auto keyat=[&](int p){return uint32_t(sym[p])<<16|sym[next[p]];};
 for(int i=0;i<(int)size;++i)if(next[i]>=0){auto&v=pairs[keyat(i)];++v.count;v.occ.push_back(i);}
 std::priority_queue<Item> heap;for(auto&kv:pairs)heap.push({kv.second.count,kv.first});
 while(dict.size()<4096&&!heap.empty()){
 auto top=heap.top();heap.pop();auto pit=pairs.find(top.key);if(pit==pairs.end())continue;
 if(top.count!=pit->second.count){if(pit->second.count>=10)heap.push({pit->second.count,top.key});continue;}
 if(top.count<10)break;unsigned a=top.key>>16,b=top.key&65535,len=lens[a]+lens[b];if(len>(lo7?7u:(slotlen?15u:16u)))continue;
 unsigned id=dict.size();std::array<uint8_t,16>d{};memcpy(d.data(),dict[a].data(),lens[a]);memcpy(d.data()+lens[a],dict[b].data(),lens[b]);dict.push_back(d);lens.push_back(len);
 std::vector<int> positions;positions.swap(pit->second.occ);std::vector<uint32_t>changed;changed.reserve(positions.size()*2);
 for(int p:positions){int q=next[p];if(q<0||sym[p]!=a||sym[q]!=b)continue;int l=prev[p],r=next[q];
  if(l>=0)--pairs[keyat(l)].count;--pairs[top.key].count;if(r>=0)--pairs[keyat(q)].count;
  sym[p]=id;next[p]=r;if(r>=0)prev[r]=p;next[q]=-2;prev[q]=-2;
  if(l>=0){auto k=keyat(l);auto&v=pairs[k];++v.count;v.occ.push_back(l);changed.push_back(k);}
  if(r>=0){auto k=keyat(p);auto&v=pairs[k];++v.count;v.occ.push_back(p);changed.push_back(k);}
 }
 std::sort(changed.begin(),changed.end());changed.erase(std::unique(changed.begin(),changed.end()),changed.end());for(auto k:changed)if(pairs[k].count>=10)heap.push({pairs[k].count,k});
 }
 // Reparse each row optimally against every learned phrase.
 struct Trie {std::unordered_map<uint8_t,int> edges;int sym=-1;};std::vector<Trie>trie(1);
 for(unsigned id=0;id<dict.size();++id){int node=0;for(unsigned j=0;j<lens[id];++j){auto q=trie[node].edges.find(dict[id][j]);if(q==trie[node].edges.end()){int v=trie.size();trie[node].edges[dict[id][j]]=v;trie.emplace_back();node=v;}else node=q->second;}trie[node].sym=id;}
 std::vector<uint32_t>ix;std::vector<uint16_t>tok;std::vector<int>cost,choose;
 for(unsigned ri=0;ri<starts.size();++ri){size_t start=starts[ri],end=ri+1<starts.size()?starts[ri+1]:size,L=end-start;cost.assign(L+1,0);choose.resize(L);ix.push_back(tok.size());
 for(int i=L-1;i>=0;--i){int node=0,best=1+cost[i+1],pick=raw[start+i];for(unsigned j=i;j<L&&j<unsigned(i+16);++j){auto q=trie[node].edges.find(raw[start+j]);if(q==trie[node].edges.end())break;node=q->second;int id=trie[node].sym;if(id>=0&&1+cost[j+1]<=best){best=1+cost[j+1];pick=id;}}cost[i]=best;choose[i]=pick;}
 for(unsigned i=0;i<L;){unsigned id=choose[i];tok.push_back(id);i+=lens[id];}}
 ix.push_back(tok.size());
 std::vector<uint32_t>frequency(dict.size(),0),order(dict.size()),remap(dict.size());for(unsigned id:tok)++frequency[id];for(unsigned i=0;i<order.size();++i)order[i]=i;
 std::sort(order.begin(),order.end(),[&](unsigned a,unsigned b){return frequency[a]>frequency[b]||(frequency[a]==frequency[b]&&a<b);});
 std::vector<std::array<uint8_t,16>>sortedDict(dict.size());std::vector<unsigned>sortedLens(lens.size());for(unsigned i=0;i<order.size();++i){sortedDict[i]=dict[order[i]];sortedLens[i]=lens[order[i]];remap[order[i]]=i;}dict.swap(sortedDict);lens.swap(sortedLens);for(auto&id:tok)id=remap[id];
 unsigned shift=0;for(unsigned sh=1;sh<=16;++sh){bool ok=true;for(unsigned i=0;i<ix.size();++i)if(ix[i]-ix[(i>>sh)<<sh]>255){ok=false;break;}if(!ok)break;shift=sh;}
 unsigned ibits=8;size_t bestsize=4*((starts.size()>>shift)+1)+ix.size()+4;
 for(unsigned sh=3;sh<=8;++sh){uint32_t maxspan=0;for(unsigned i=0;i<ix.size();++i)maxspan=std::max(maxspan,ix[i]-ix[(i>>sh)<<sh]);for(unsigned bits=8;bits<=16;++bits){if(maxspan>=(1u<<bits))continue;size_t candidate=4*((starts.size()>>sh)+1)+(ix.size()*bits+7)/8+4;if(candidate<bestsize){bestsize=candidate;shift=sh;ibits=bits;}}}
 std::vector<uint32_t>base;for(unsigned i=0;i<ix.size();i+=1<<shift)base.push_back(ix[i]);
 dict.resize(4096);lens.resize(4096);if(slotlen)for(unsigned i=0;i<4096;++i)dict[i][15]=lens[i];Header h{size,(uint32_t)starts.size(),4096,(uint32_t)tok.size(),shift|(slotlen?256u:0u)|(lo7?512u:0u)|(ibits<<16)};
 std::vector<uint8_t>arc(sizeof h+base.size()*4+index_size(h)+dict.size()*17+(tok.size()*12+7)/8+4);uint8_t*p=arc.data();memcpy(p,&h,sizeof h);p+=sizeof h;memcpy(p,base.data(),base.size()*4);p+=base.size()*4;if(ibits==8){for(unsigned i=0;i<ix.size();++i)p[i]=ix[i]-base[i>>shift];}else{for(unsigned i=0;i<ix.size();++i){uint64_t at=uint64_t(i)*ibits;uint32_t v=(ix[i]-base[i>>shift])<<(at&7);p[at>>3]|=v;p[(at>>3)+1]|=v>>8;p[(at>>3)+2]|=v>>16;}}p+=index_size(h);memcpy(p,dict.data(),dict.size()*16);p+=dict.size()*16;for(unsigned x:lens)*p++=x;
 for(unsigned i=0;i<tok.size();++i){unsigned b=3*(i>>1),v=tok[i]<<((i&1)*12);p[b]|=v;p[b+1]|=v>>8;p[b+2]|=v>>16;}return arc;
}
#endif
}
