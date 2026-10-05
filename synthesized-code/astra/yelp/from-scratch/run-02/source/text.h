#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <immintrin.h>
#ifdef ENCODER
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <array>
#include <queue>
#include <cmath>
#include <stdio.h>
#endif
namespace textcol {
static inline uint16_t rd16(const uint8_t*p){uint16_t x;memcpy(&x,p,2);return x;}
static inline uint64_t rd64(const uint8_t*p){uint64_t x;memcpy(&x,p,8);return x;}
static inline uint32_t rd32(const uint8_t*p){uint32_t x;memcpy(&x,p,4);return x;}
struct State { uint8_t* blob=nullptr; uint32_t* offsets=nullptr; uint32_t rows=0,bytes=0; };
static inline void close(State&s){free(s.blob);free(s.offsets);s=State();}
static inline uint8_t* emit(const State&s,uint32_t row,uint8_t*out){uint32_t a=s.offsets[row],n=s.offsets[row+1]-a;memcpy(out,s.blob+a,n);return out+n;}
static inline bool expand_node(unsigned id,unsigned depth,unsigned count,const uint8_t*p,uint8_t*dict,uint8_t*lens,uint8_t*flags){
 if(id>=count||depth>32||flags[id]==1)return false;if(flags[id]==2)return true;flags[id]=1;unsigned l=rd16(p+id*4),r=rd16(p+id*4+2);
 if(l==65535){if(r>256)return false;lens[id]=r!=256;if(r<256)dict[id*32]=r;}
 else {if(!expand_node(l,depth+1,count,p,dict,lens,flags)||!expand_node(r,depth+1,count,p,dict,lens,flags)||!lens[l]||!lens[r]||lens[l]+lens[r]>32)return false;lens[id]=lens[l]+lens[r];memcpy(dict+id*32,dict+l*32,lens[l]);memcpy(dict+id*32+lens[l],dict+r*32,lens[r]);}
 flags[id]=2;return true;
}
static inline bool open(const uint8_t*a,size_t size,State&s){
 if(size<24||rd32(a)!=0x34545042)return false;constexpr bool huff=true,inter=true,direct=true;
 uint32_t rows=rd32(a+4),bytes=rd32(a+8),nd=rd32(a+12),ns=rd32(a+16),nb=rd32(a+20);
 if(nd>32511||!ns||ns>32896||uint64_t(24)+nd*4+(direct?1060:ns*(huff?3:2))+nb!=size||rows>10000000||bytes>1000000000)return false;
 uint8_t*dict=(uint8_t*)calloc(size_t(nd+257),32);uint8_t*lens=(uint8_t*)malloc(nd+257);
 uint8_t*rank=direct?dict:(uint8_t*)calloc(size_t(ns),32);uint8_t*rlens=direct?lens:(uint8_t*)malloc(ns);
 if(!dict||!lens||!rank||!rlens){free(dict);free(lens);if(!direct){free(rank);free(rlens);}return false;}
 bool good=true;const uint8_t*p=a+24;
 if(direct){uint8_t*flags=(uint8_t*)calloc(nd+257,1);if(!flags||ns>nd+257)good=false;else for(unsigned i=0;i<nd+257;++i)if(!expand_node(i,0,nd+257,p,dict,lens,flags)){good=false;break;}free(flags);p+=(nd+257)*4;}
 else{
 for(unsigned i=0;i<256;++i){dict[i*32]=i;lens[i]=1;}lens[256]=0;
 for(uint32_t i=257;i<nd+257;++i,p+=4){uint16_t l=rd16(p),r=rd16(p+2);if(l>=i||r>=i||!lens[l]||!lens[r]||lens[l]+lens[r]>32){good=false;break;}lens[i]=lens[l]+lens[r];memcpy(dict+i*32,dict+l*32,lens[l]);memcpy(dict+i*32+lens[l],dict+r*32,lens[r]);}
 if(good)for(uint32_t i=0;i<ns;++i,p+=2){uint16_t id=rd16(p);if(id>=nd+257){good=false;break;}rlens[i]=lens[id];memcpy(rank+i*32,dict+id*32,32);}
 free(dict);free(lens);
 }
 s.blob=(uint8_t*)malloc(size_t(bytes)+32);s.offsets=(uint32_t*)malloc(size_t(rows+1)*4);s.rows=rows;s.bytes=bytes;
 if(!s.blob||!s.offsets)good=false;
 if(good){
 uint32_t*ht=nullptr;
 if(huff){ht=(uint32_t*)calloc(65536,4);if(!ht)good=false;else{unsigned counts[17]={},next[17]={};
 if(direct){unsigned total=0;for(unsigned n=1;n<=16;++n){counts[n]=rd16(p+(n-1)*2);total+=counts[n];}if(total!=ns)good=false;}
 else for(unsigned i=0;i<ns;++i){unsigned n=p[i];if(!n||n>16){good=false;break;}++counts[n];}
 unsigned code=0;for(unsigned i=1;i<=16;++i){code=(code+counts[i-1])<<1;next[i]=code;if(code+counts[i]>(1u<<i))good=false;}
 if(good){if(direct){unsigned i=0;for(unsigned n=1;n<=16;++n)for(unsigned k=0;k<counts[n];++k,++i){unsigned c=next[n]++,r=0;for(unsigned j=0;j<n;++j){r=(r<<1)|(c&1);c>>=1;}for(unsigned j=r;j<65536;j+=1u<<n)ht[j]=(i<<11)|(uint32_t(rlens[i])<<5)|n;}}
 else for(unsigned i=0;i<ns;++i){unsigned n=p[i],c=next[n]++,r=0;for(unsigned j=0;j<n;++j){r=(r<<1)|(c&1);c>>=1;}for(unsigned j=r;j<65536;j+=1u<<n)ht[j]=(i<<11)|(uint32_t(rlens[i])<<5)|n;}}}p+=direct?32:ns;}
 if(good&&inter){
  struct Chunk {const uint8_t*start;size_t bitpos,limit;uint8_t*out,*end;unsigned row,last,nb;};Chunk c[4];
  if(nb<32)good=false;else{const uint8_t*q=p+32;size_t total=32;uint32_t plain=0;
   for(unsigned k=0;k<4;++k){unsigned n=rd32(p+k*8),b=rd32(p+k*8+4);if(n<8||total+n>nb||uint64_t(plain)+b>bytes){good=false;break;}c[k]={q,0,size_t(n-8)*8,s.blob+plain,s.blob+plain+b,unsigned(uint64_t(rows)*k/4),unsigned(uint64_t(rows)*(k+1)/4),n};s.offsets[c[k].row]=plain;plain+=b;q+=n;total+=n;}if(total!=nb||plain!=bytes)good=false;
  }
  auto output=[&](unsigned k,uint32_t v){auto&x=c[k];unsigned b=v&31,id=v>>11;if(!b){good=false;return;}x.bitpos+=b;unsigned n=(v>>5)&63;if(!n){if(x.row>=x.last){good=false;return;}s.offsets[++x.row]=x.out-s.blob;}else{if(x.out+n>x.end){good=false;return;}if(x.end-x.out<32)memcpy(x.out,rank+id*32,n);else{_mm256_storeu_si256((__m256i*)x.out,_mm256_loadu_si256((const __m256i*)(rank+id*32)));}x.out+=n;}};
  if(good){
   while(c[0].row<c[0].last&&c[1].row<c[1].last&&c[2].row<c[2].last&&c[3].row<c[3].last){
    if(c[0].bitpos>c[0].limit||c[1].bitpos>c[1].limit||c[2].bitpos>c[2].limit||c[3].bitpos>c[3].limit){good=false;break;}
    uint32_t a0=ht[(rd64(c[0].start+(c[0].bitpos>>3))>>(c[0].bitpos&7))&65535];
    uint32_t a1=ht[(rd64(c[1].start+(c[1].bitpos>>3))>>(c[1].bitpos&7))&65535];
    uint32_t a2=ht[(rd64(c[2].start+(c[2].bitpos>>3))>>(c[2].bitpos&7))&65535];
    uint32_t a3=ht[(rd64(c[3].start+(c[3].bitpos>>3))>>(c[3].bitpos&7))&65535];
    output(0,a0);output(1,a1);output(2,a2);output(3,a3);if(!good)break;
   }
   for(unsigned k=0;k<4&&good;++k){auto&x=c[k];while(x.row<x.last){if(x.bitpos>x.limit){good=false;break;}uint32_t v=ht[(rd64(x.start+(x.bitpos>>3))>>(x.bitpos&7))&65535];output(k,v);if(!good)break;}if(x.out!=x.end||(x.bitpos+7)/8+8!=x.nb)good=false;}
  }
 }
 if(good&&!inter){const uint8_t*start=p,*end=p+nb;uint8_t*out=s.blob;uint32_t row=0;size_t bitpos=0;s.offsets[0]=0;
 while(huff?row<rows:p<end){uint32_t id;
 if(huff){if((bitpos>>3)+8>nb){good=false;break;}uint32_t v=ht[(rd64(start+(bitpos>>3))>>(bitpos&7))&65535];unsigned bits=v&31;if(!bits){good=false;break;}bitpos+=bits;id=v>>11;}
 else{uint32_t a=*p++,b=p<end?*p:0;uint32_t ext=a>>7,mask=0u-ext;id=a+((((a-128)*255)+b)&mask);p+=ext;if(p>end||id>=ns){good=false;break;}}
 uint32_t n=rlens[id];if(!n){if(row>=rows){good=false;break;}s.offsets[++row]=out-s.blob;}else{if(size_t(out-s.blob)+n>bytes){good=false;break;}_mm_storeu_si128((__m128i*)out,_mm_loadu_si128((const __m128i*)(rank+id*32)));if(n>16)_mm_storeu_si128((__m128i*)(out+16),_mm_loadu_si128((const __m128i*)(rank+id*32+16)));out+=n;}}
 if(row!=rows||out-s.blob!=bytes||s.offsets[rows]!=bytes)good=false;
 if(huff&&((bitpos+7)/8+8!=nb))good=false;
 }
 free(ht);
 }
 free(rank);free(rlens);if(!good)close(s);return good;
}
#ifdef ENCODER
static inline void put32(std::vector<uint8_t>&out,uint32_t x){size_t n=out.size();out.resize(n+4);memcpy(out.data()+n,&x,4);}
static inline void put16(std::vector<uint8_t>&out,uint16_t x){size_t n=out.size();out.resize(n+2);memcpy(out.data()+n,&x,2);}
static inline std::vector<uint8_t> encode(const std::vector<std::string>&rows,unsigned huffmode=4,unsigned maxdict=16384,unsigned batch=128){
 uint32_t nbytes=0;std::vector<uint16_t>seq;for(auto&v:rows){nbytes+=v.size();for(uint8_t c:v)seq.push_back(c);seq.push_back(256);}
 std::vector<uint16_t>left,right;std::vector<uint8_t>len(257,1);len[256]=0;
 if(const char*v=getenv("TEXT_DICT"))maxdict=atoi(v);if(const char*v=getenv("TEXT_BATCH"))batch=atoi(v);
 for(unsigned round=0;left.size()<maxdict;++round){
  std::unordered_map<uint32_t,uint32_t>counts;counts.reserve(seq.size()/2);
  for(size_t i=1;i<seq.size();++i){uint16_t a=seq[i-1],b=seq[i];if(a!=256&&b!=256&&len[a]+len[b]<=32)++counts[uint32_t(a)<<16|b];}
  std::vector<std::pair<uint32_t,uint32_t>>best;best.reserve(counts.size());for(auto&v:counts)if(v.second>=4)best.push_back({v.second,v.first});
  if(best.empty())break;unsigned take=std::min<size_t>(std::min<unsigned>(batch,maxdict-left.size()),best.size());
  std::partial_sort(best.begin(),best.begin()+take,best.end(),[](auto&a,auto&b){return a.first!=b.first?a.first>b.first:a.second<b.second;});
  std::unordered_map<uint32_t,uint16_t>which;which.reserve(take*2);for(unsigned j=0;j<take;++j){uint32_t p=best[j].second;uint16_t a=p>>16,b=p;which[p]=len.size();left.push_back(a);right.push_back(b);len.push_back(len[a]+len[b]);}
  size_t w=0;for(size_t i=0;i<seq.size();++i){if(i+1<seq.size()){auto f=which.find(uint32_t(seq[i])<<16|seq[i+1]);if(f!=which.end()){seq[w++]=f->second;++i;continue;}}seq[w++]=seq[i];}seq.resize(w);
 }
 if(!getenv("TEXT_NOREPARSE")){
  std::vector<std::string>phrases(257);for(unsigned i=0;i<256;++i)phrases[i]=std::string(1,char(i));for(unsigned i=0;i<left.size();++i)phrases.push_back(phrases[left[i]]+phrases[right[i]]);
  struct Trie {std::array<int,256>next;int id;Trie():id(-1){next.fill(-1);}};std::vector<Trie>trie(1);
  for(unsigned id=0;id<phrases.size();++id){if(id==256)continue;int node=0;for(uint8_t c:phrases[id]){int n=trie[node].next[c];if(n<0){n=trie.size();trie[node].next[c]=n;trie.emplace_back();}node=n;}trie[node].id=id;}
  for(unsigned pass=0;pass<3;++pass){
   std::vector<uint32_t>f(len.size());for(auto v:seq)++f[v];std::vector<uint16_t>ord;for(unsigned i=0;i<f.size();++i)ord.push_back(i);std::sort(ord.begin(),ord.end(),[&](unsigned a,unsigned b){return f[a]!=f[b]?f[a]>f[b]:a<b;});
   std::vector<unsigned>cost(len.size());double total=seq.size();for(unsigned i=0;i<f.size();++i)cost[i]=1+unsigned(4096*std::log2(total/std::max(1u,f[i])));
   seq.clear();for(const auto&str:rows){size_t n=str.size();std::vector<uint32_t>dp(n+1,0x3fffffff);std::vector<uint16_t>best(n);dp[n]=0;
    for(size_t j=n;j-->0;){int node=0;for(size_t k=j;k<n;++k){node=trie[node].next[uint8_t(str[k])];if(node<0)break;int id=trie[node].id;if(id>=0&&cost[id]+dp[k+1]<dp[j]){dp[j]=cost[id]+dp[k+1];best[j]=id;}}}
    for(size_t j=0;j<n;){uint16_t id=best[j];seq.push_back(id);j+=len[id];}seq.push_back(256);
   }
  }
 }
 {std::vector<uint8_t>used(len.size());for(auto v:seq)used[v]=1;for(unsigned i=len.size();i-->257;)if(used[i])used[left[i-257]]=used[right[i-257]]=1;
 std::vector<uint16_t>map(len.size());for(unsigned i=0;i<257;++i)map[i]=i;std::vector<uint16_t>nl,nr;std::vector<uint8_t>nlen(len.begin(),len.begin()+257);
 for(unsigned i=257;i<len.size();++i)if(used[i]){map[i]=nlen.size();nl.push_back(map[left[i-257]]);nr.push_back(map[right[i-257]]);nlen.push_back(len[i]);}for(auto&v:seq)v=map[v];left.swap(nl);right.swap(nr);len.swap(nlen);
 }
 std::vector<uint32_t>freq(len.size());for(auto v:seq)++freq[v];std::vector<uint16_t>order;for(unsigned i=0;i<freq.size();++i)if(freq[i])order.push_back(i);
 std::sort(order.begin(),order.end(),[&](unsigned a,unsigned b){return freq[a]!=freq[b]?freq[a]>freq[b]:a<b;});
 std::vector<uint16_t>rank(len.size());for(unsigned i=0;i<order.size();++i)rank[order[i]]=i;
 if(const char*v=getenv("TEXT_HUFF"))huffmode=atoi(v);bool huff=huffmode!=0;bool inter=huffmode==4;std::vector<uint8_t>hbits(order.size());
 if(huff){
  struct Node{uint64_t freq;int left,right;};std::vector<Node>nodes;using Q=std::pair<uint64_t,int>;std::priority_queue<Q,std::vector<Q>,std::greater<Q>>heap;
  for(auto id:order){int ix=nodes.size();nodes.push_back({freq[id],-1,-1});heap.push({freq[id],ix});}
  while(heap.size()>1){auto a=heap.top();heap.pop();auto b=heap.top();heap.pop();int ix=nodes.size();nodes.push_back({a.first+b.first,a.second,b.second});heap.push({a.first+b.first,ix});}
  std::vector<std::pair<int,unsigned>>stack;stack.push_back({int(nodes.size()-1),0});unsigned counts[64]={};while(!stack.empty()){auto v=stack.back();stack.pop_back();auto&n=nodes[v.first];if(n.left<0){hbits[v.first]=std::max(1u,v.second);++counts[hbits[v.first]];}else{stack.push_back({n.left,v.second+1});stack.push_back({n.right,v.second+1});}}
  for(unsigned i=17;i<64;++i){counts[16]+=counts[i];counts[i]=0;}int64_t used=0;for(unsigned i=1;i<=16;++i)used+=uint64_t(counts[i])<<(16-i);while(used>65536){unsigned b=15;while(b&&!counts[b])--b;--counts[b];counts[b+1]+=2;--counts[16];--used;}
  unsigned ix=0;for(unsigned b=1;b<=16;++b)for(unsigned j=0;j<counts[b];++j)hbits[ix++]=b;
 }
 std::vector<uint8_t>stream;stream.reserve(seq.size()*2);
 if(huff){unsigned counts[17]={},next[17]={};for(auto n:hbits)++counts[n];unsigned code=0;for(unsigned i=1;i<=16;++i){code=(code+counts[i-1])<<1;next[i]=code;}std::vector<unsigned>codes(order.size());for(unsigned i=0;i<order.size();++i){unsigned n=hbits[i],c=next[n]++,r=0;for(unsigned j=0;j<n;++j){r=(r<<1)|(c&1);c>>=1;}codes[i]=r;}uint64_t buf=0;unsigned fill=0;if(inter){std::vector<uint8_t>chunks[4];unsigned chunk=0,row=0;for(auto v:seq){unsigned id=rank[v];buf|=uint64_t(codes[id])<<fill;fill+=hbits[id];while(fill>=8){chunks[chunk].push_back(buf);buf>>=8;fill-=8;}if(v==256){++row;if(row==rows.size()*(chunk+1)/4){if(fill)chunks[chunk].push_back(buf);chunks[chunk].resize(chunks[chunk].size()+8);buf=0;fill=0;++chunk;}}}for(unsigned k=0;k<4;++k){put32(stream,chunks[k].size());uint32_t b=0;for(size_t r=rows.size()*k/4;r<rows.size()*(k+1)/4;++r)b+=rows[r].size();put32(stream,b);}for(auto&c:chunks)stream.insert(stream.end(),c.begin(),c.end());}
 else{for(auto v:seq){unsigned id=rank[v];buf|=uint64_t(codes[id])<<fill;fill+=hbits[id];while(fill>=8){stream.push_back(buf);buf>>=8;fill-=8;}}if(fill)stream.push_back(buf);stream.resize(stream.size()+8);}}

 else for(auto v:seq){unsigned r=rank[v];if(r<128)stream.push_back(r);else{r-=128;stream.push_back(128+(r>>8));stream.push_back(r);}}
 std::vector<uint8_t>out;put32(out,inter?0x34545042:huff?0x32545042:0x31545042);put32(out,rows.size());put32(out,nbytes);put32(out,left.size());put32(out,order.size());put32(out,stream.size());
 if(inter){std::vector<uint16_t>all=order,map(len.size());std::vector<uint8_t>used(len.size());for(auto id:order)used[id]=1;for(unsigned i=0;i<len.size();++i)if(!used[i])all.push_back(i);for(unsigned i=0;i<all.size();++i)map[all[i]]=i;for(auto id:all){if(id<257){put16(out,65535);put16(out,id);}else{put16(out,map[left[id-257]]);put16(out,map[right[id-257]]);}}}
 else {for(unsigned i=0;i<left.size();++i){put16(out,left[i]);put16(out,right[i]);}for(auto id:order)put16(out,id);}if(inter){unsigned counts[17]={};for(auto n:hbits)++counts[n];for(unsigned n=1;n<=16;++n)put16(out,counts[n]);}else if(huff)out.insert(out.end(),hbits.begin(),hbits.end());out.insert(out.end(),stream.begin(),stream.end());
 fprintf(stderr,"text bytes=%u tokens=%zu nd=%zu ns=%zu stream=%zu total=%zu\n",nbytes,seq.size(),left.size(),order.size(),stream.size(),out.size());return out;
}
#endif
}
