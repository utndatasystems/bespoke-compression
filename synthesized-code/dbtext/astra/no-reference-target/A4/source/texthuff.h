#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <array>
#include <algorithm>
#include <functional>
#include <queue>
#include <immintrin.h>
#ifdef ENCODER
#include "train.h"
#endif
namespace th {
static constexpr uint32_t MAGIC=0x33485844;
struct Header{uint32_t magic,rows,raw,ns,bits,nbits,rowbytes,bytes,quarter[3];};
#ifdef ENCODER
static std::vector<uint8_t> lengths(const std::vector<uint32_t>&freq){
 struct Item{uint64_t f;uint32_t i;};struct Cmp{bool operator()(Item a,Item b)const{return a.f!=b.f?a.f>b.f:a.i>b.i;}};
 std::priority_queue<Item,std::vector<Item>,Cmp>pq;std::vector<int>parent(freq.size(),-1);std::vector<unsigned>rank;for(unsigned i=0;i<freq.size();i++)if(freq[i]){pq.push({freq[i],i});rank.push_back(i);}std::vector<uint8_t>lens(freq.size());if(rank.size()==1){lens[rank[0]]=1;return lens;}
 while(pq.size()>1){auto a=pq.top();pq.pop();auto b=pq.top();pq.pop();unsigned id=parent.size();parent.push_back(-1);parent[a.i]=parent[b.i]=id;pq.push({a.f+b.f,id});}
 unsigned counts[16]={};for(auto i:rank){unsigned d=0;for(int p=i;parent[p]>=0;p=parent[p])++d;counts[std::min(d,15u)]++;}
 unsigned slots=0;for(unsigned d=1;d<=15;d++)slots+=counts[d]<<(15-d);
 while(slots>32768){int d=14;while(!counts[d])--d;--counts[d];counts[d+1]+=2;--counts[15];--slots;}
 std::sort(rank.begin(),rank.end(),[&](unsigned a,unsigned b){return freq[a]!=freq[b]?freq[a]<freq[b]:a>b;});size_t pos=0;for(int d=15;d>0;--d)for(unsigned j=0;j<counts[d];j++)lens[rank[pos++]]=d;return lens;
}
static bool encode(const uint8_t*raw,size_t n,std::vector<uint8_t>&out){
 if(!n||n>UINT32_MAX)return false;auto t=train(raw,n,16384,32);size_t ns0=t.parents.size()+256,best=SIZE_MAX;std::vector<uint32_t>rawpos{0};for(size_t i=0;i<n;i++)if(raw[i]=='\n')rawpos.push_back(i+1);if(rawpos.back()!=n)rawpos.push_back(n);
 for(unsigned lim:{1024,2048,4096,8192,16384}){
  lim=std::min<size_t>(lim,ns0);std::vector<std::vector<uint16_t>>rows;rows.reserve(t.rows.size());std::vector<uint8_t>used(lim);std::vector<uint32_t>freq(lim);
  std::function<void(unsigned,std::vector<uint16_t>&)>split=[&](unsigned c,std::vector<uint16_t>&r){if(c<lim)r.push_back(c);else{split(t.parents[c-256][0],r);split(t.parents[c-256][1],r);}};
  for(auto&r:t.rows){std::vector<uint16_t>v;for(auto c:r)split(c,v);for(auto c:v){used[c]=1;freq[c]++;}rows.push_back(std::move(v));}
  for(int c=lim-1;c>=256;c--)if(used[c]){used[t.parents[c-256][0]]=1;used[t.parents[c-256][1]]=1;}
  std::vector<uint16_t>order,map(lim);std::vector<uint32_t>freqmap;for(unsigned c=0;c<lim;c++)if(used[c]){map[c]=order.size();order.push_back(c);freqmap.push_back(freq[c]);}
  unsigned ns=order.size(),bits=8;while((1u<<bits)<ns)++bits;auto lens=lengths(freqmap);uint64_t nbits=0;size_t rowbytes=0;bool ok=true;std::vector<uint16_t>rowlens;
  for(auto&r:rows){unsigned len=0;for(auto c:r)len+=lens[map[c]];if(len>65535){ok=false;break;}rowlens.push_back(len);rowbytes+=len<255?1:3;nbits+=len;}if(!ok||nbits>UINT32_MAX)continue;
  size_t gb=(size_t(ns)*2*bits+7)/8,cb=(ns+1)/2,bytes=(nbits+7)/8+16,size=sizeof(Header)+gb+cb+rowbytes+bytes;if(size>=best)continue;best=size;
  Header h{MAGIC,(uint32_t)rows.size(),(uint32_t)n,ns,bits,(uint32_t)nbits,(uint32_t)rowbytes,(uint32_t)bytes,{rawpos[rows.size()/4],rawpos[rows.size()/2],rawpos[rows.size()*3/4]}};out.assign(size,0);memcpy(out.data(),&h,sizeof(h));uint8_t*p=out.data()+sizeof(h);size_t bit=0;
  for(unsigned i=0;i<order.size();i++){unsigned c=order[i];unsigned a=c<256?i:map[t.parents[c-256][0]],b=c<256?c:map[t.parents[c-256][1]];for(unsigned v:{a,b}){uint32_t w=v<<(bit&7);p[bit/8]|=w;p[bit/8+1]|=w>>8;p[bit/8+2]|=w>>16;bit+=bits;}}
  p+=gb;for(unsigned i=0;i<ns;i++)p[i/2]|=lens[i]<<((i&1)*4);p+=cb;for(auto l:rowlens){if(l<255)*p++=l;else{*p++=255;*p++=l;*p++=l>>8;}}
  unsigned counts[16]={},next[16]={};for(auto l:lens)if(l)counts[l]++;unsigned code=0;for(unsigned b=1;b<=15;b++){code=(code+counts[b-1])<<1;next[b]=code;}
  std::vector<uint16_t>codes(ns);for(unsigned i=0;i<ns;i++)if(lens[i]){unsigned c=next[lens[i]]++,r=0;for(unsigned j=0;j<lens[i];j++){r=(r<<1)|(c&1);c>>=1;}codes[i]=r;}
  bit=0;for(auto&r:rows)for(auto c:r){unsigned v=map[c],w=unsigned(codes[v])<<(bit&7);p[bit/8]|=w;p[bit/8+1]|=w>>8;p[bit/8+2]|=w>>16;bit+=lens[v];}
 }
 return best!=SIZE_MAX;
}
#else
struct State{Header h;const uint8_t*data;std::vector<std::array<uint8_t,32>>dict;std::vector<uint8_t>len;std::vector<uint32_t>offset,table;};
static State*open(const uint8_t*a,size_t size){
 if(size<sizeof(Header))return nullptr;Header h;memcpy(&h,a,sizeof(h));size_t gb=(size_t(h.ns)*2*h.bits+7)/8,cb=(h.ns+1)/2;
 if(h.magic!=MAGIC||!h.ns||h.bits<8||h.bits>14||h.ns>(1u<<h.bits)||!h.rows||h.rowbytes<h.rows||h.bytes!=(uint64_t(h.nbits)+7)/8+16||uint64_t(sizeof(h))+gb+cb+h.rowbytes+h.bytes!=size||h.quarter[0]>h.quarter[1]||h.quarter[1]>h.quarter[2]||h.quarter[2]>h.raw)return nullptr;
 State*s=new State;s->h=h;s->dict.resize(h.ns);s->len.resize(h.ns);s->offset.resize(size_t(h.rows)+1);s->table.resize(32768);const uint8_t*p=a+sizeof(h);unsigned mask=(1u<<h.bits)-1;size_t bit=0;
 for(unsigned i=0;i<h.ns;i++){uint32_t w;memcpy(&w,p+bit/8,4);unsigned l=(w>>(bit&7))&mask;bit+=h.bits;memcpy(&w,p+bit/8,4);unsigned r=(w>>(bit&7))&mask;bit+=h.bits;if(l==i){if(r>255){delete s;return nullptr;}s->dict[i][0]=r;s->len[i]=1;}else{if(l>=i||r>=i||s->len[l]+s->len[r]>32){delete s;return nullptr;}unsigned n=s->len[l],m=s->len[r];_mm256_storeu_si256((__m256i*)s->dict[i].data(),_mm256_maskz_loadu_epi8((__mmask32)((1ull<<n)-1),s->dict[l].data()));_mm256_mask_storeu_epi8(s->dict[i].data()+n,(__mmask32)((1ull<<m)-1),_mm256_loadu_si256((__m256i*)s->dict[r].data()));s->len[i]=n+m;}}
 p+=gb;unsigned counts[16]={},next[16]={};for(unsigned i=0;i<h.ns;i++){unsigned l=(p[i/2]>>((i&1)*4))&15;if(l)counts[l]++;}unsigned code=0;for(unsigned b=1;b<=15;b++){code=(code+counts[b-1])<<1;next[b]=code;if(code+counts[b]>(1u<<b)){delete s;return nullptr;}}
 for(unsigned i=0;i<h.ns;i++){unsigned l=(p[i/2]>>((i&1)*4))&15;if(!l)continue;unsigned c=next[l]++,rev=0;for(unsigned j=0;j<l;j++){rev=(rev<<1)|(c&1);c>>=1;}uint32_t v=(i<<5)|l|(unsigned(s->len[i])<<19);for(unsigned at=rev;at<32768;at+=1u<<l)s->table[at]=v;}
 p+=cb;const uint8_t*end=p+h.rowbytes;uint64_t pos=0;for(size_t i=0;i<h.rows;i++){s->offset[i]=pos;if(p==end){delete s;return nullptr;}unsigned l=*p++;if(l==255){if(end-p<2){delete s;return nullptr;}l=p[0]|(unsigned(p[1])<<8);p+=2;}pos+=l;if(pos>h.nbits){delete s;return nullptr;}}s->offset[h.rows]=pos;s->data=p;if(pos!=h.nbits||p!=end){delete s;return nullptr;}return s;
}
static inline uint32_t symbol(const State*s,uint32_t&bit){uint32_t word;memcpy(&word,s->data+(bit>>3),4);uint32_t e=s->table[(word>>(bit&7))&32767];bit+=e&31;return e;}
static inline uint8_t*block(const State*s,uint32_t bit,uint32_t end,uint8_t*out,uint8_t*limit){while(bit<end){uint32_t e=symbol(s,bit);if(!(e&31)||bit>end)return nullptr;unsigned n=e>>19;const uint8_t*phrase=(const uint8_t*)s->dict.data()+(e&0x7ffe0);if(size_t(limit-out)<n)return nullptr;if(limit-out>=32)_mm256_storeu_si256((__m256i*)out,_mm256_loadu_si256((const __m256i*)phrase));else memcpy(out,phrase,n);out+=n;}return out;}
static int64_t decode(State*s,uint8_t*out,size_t cap){
 if(!s||cap<s->h.raw)return -1;uint32_t bit[4],end[4];uint8_t*p[4],*limit[4];for(unsigned j=0;j<4;j++){bit[j]=s->offset[size_t(s->h.rows)*j/4];end[j]=s->offset[size_t(s->h.rows)*(j+1)/4];p[j]=out+(j?s->h.quarter[j-1]:0);limit[j]=out+(j<3?s->h.quarter[j]:s->h.raw);}
 while(bit[0]<end[0]&&bit[1]<end[1]&&bit[2]<end[2]&&bit[3]<end[3]&&limit[0]-p[0]>=32&&limit[1]-p[1]>=32&&limit[2]-p[2]>=32&&limit[3]-p[3]>=32){
  uint32_t a=symbol(s,bit[0]),b=symbol(s,bit[1]),c=symbol(s,bit[2]),d=symbol(s,bit[3]);if(!(a&31)||!(b&31)||!(c&31)||!(d&31))return -1;
  #define TH_PUT(J,E) {_mm256_storeu_si256((__m256i*)p[J],_mm256_loadu_si256((const __m256i*)((const uint8_t*)s->dict.data()+((E)&0x7ffe0))));p[J]+=(E)>>19;}
  TH_PUT(0,a) TH_PUT(1,b) TH_PUT(2,c) TH_PUT(3,d)
  #undef TH_PUT
 }
 for(unsigned j=0;j<4;j++){if(bit[j]>end[j])return -1;p[j]=block(s,bit[j],end[j],p[j],limit[j]);if(p[j]!=limit[j])return -1;}return s->h.raw;
}
static int64_t rows(State*s,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets){
 if(!s||!offsets)return -1;uint8_t*p=out;offsets[0]=0;if(!count)return 0;size_t i=0;
 alignas(32) uint8_t tmp[4][512];
 for(;i+4<=count;i+=4){
  uint32_t bit[4],end[4];uint8_t*q[4]={tmp[0],tmp[1],tmp[2],tmp[3]};for(unsigned j=0;j<4;j++){uint64_t r=ids[i+j];if(r>=s->h.rows)return -1;bit[j]=s->offset[r];end[j]=s->offset[r+1];}
  while(bit[0]<end[0]&&bit[1]<end[1]&&bit[2]<end[2]&&bit[3]<end[3]&&q[0]-tmp[0]<=480&&q[1]-tmp[1]<=480&&q[2]-tmp[2]<=480&&q[3]-tmp[3]<=480){
   uint32_t a=symbol(s,bit[0]),b=symbol(s,bit[1]),c=symbol(s,bit[2]),d=symbol(s,bit[3]);if(!(a&31)||!(b&31)||!(c&31)||!(d&31))return -1;
   #define TH_ROW(J,E) {_mm256_storeu_si256((__m256i*)q[J],_mm256_loadu_si256((const __m256i*)((const uint8_t*)s->dict.data()+((E)&0x7ffe0))));q[J]+=(E)>>19;}
   TH_ROW(0,a) TH_ROW(1,b) TH_ROW(2,c) TH_ROW(3,d)
   #undef TH_ROW
  }
  bool ok=true;for(unsigned j=0;j<4;j++){if(bit[j]>end[j])return -1;q[j]=block(s,bit[j],end[j],q[j],tmp[j]+512);if(!q[j])ok=false;}
  if(ok){for(unsigned j=0;j<4;j++){size_t n=q[j]-tmp[j];if(n>size_t(out+cap-p))return -1;memcpy(p,tmp[j],n);p+=n;offsets[i+j+1]=p-out;}}
  else{for(unsigned j=0;j<4;j++){uint64_t r=ids[i+j];p=block(s,s->offset[r],s->offset[r+1],p,out+cap);if(!p)return -1;offsets[i+j+1]=p-out;}}
 }
 for(;i<count;i++){uint64_t r=ids[i];if(r>=s->h.rows)return -1;p=block(s,s->offset[r],s->offset[r+1],p,out+cap);if(!p)return -1;offsets[i+1]=p-out;}return p-out;
}
#endif
}
