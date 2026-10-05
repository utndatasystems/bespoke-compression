#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>
#include <algorithm>
#include <zstd.h>
#include <immintrin.h>
struct PH { uint32_t magic,n,raw,k,ds,ixs,ts,bits; };
struct PS {PH h; const uint8_t* tok; std::vector<uint32_t> off,table; std::vector<uint16_t> rlen; std::vector<uint8_t> dict,len;uint32_t mask;bool fast_rows=true,batch4=false;};
static inline unsigned ph_rev(unsigned x,unsigned n){x=((x&0x5555)<<1)|((x>>1)&0x5555);x=((x&0x3333)<<2)|((x>>2)&0x3333);x=((x&0x0f0f)<<4)|((x>>4)&0x0f0f);x=(x<<8)|(x>>8);return (x&65535)>>(16-n);}
static bool ph_open(const uint8_t* a,size_t z,PS& s){
 s.fast_rows=true;if(z<sizeof(PH))return false; memcpy(&s.h,a,sizeof(PH)); auto& h=s.h;
 if(h.magic!=0x37524850 || !h.k || h.k>16384 || h.n>10000000 || h.raw>1000000000 || (uint64_t)sizeof(PH)+h.ds+h.ixs+h.ts+16!=z || (h.bits+7ull)/8!=h.ts)return false;
 std::vector<uint8_t>d((size_t)h.k*34);size_t dz=ZSTD_decompress(d.data(),d.size(),a+sizeof(PH),h.ds);if(ZSTD_isError(dz))return false;
 std::vector<uint32_t>dict_off(h.k);
#ifdef ENCODER
 s.dict.resize(h.k*32);for(unsigned i=0;i<h.k;i++)dict_off[i]=i*32;
#else
 s.dict.clear();s.dict.reserve(size_t(h.k)*32+32);
#endif
 s.len.resize(h.k);std::vector<unsigned>nb(h.k);size_t at=0;unsigned counts[16]={},code[16]={};
 for(unsigned i=0;i<h.k;i++){if(at+2>dz)return false;unsigned l=d[at++],b=d[at++];if(!l||l>32||!b||b>15||at+l>dz)return false;s.len[i]=l;nb[i]=b;counts[b]++;
#ifdef ENCODER
 memcpy(s.dict.data()+i*32,d.data()+at,l);
#else
 dict_off[i]=s.dict.size();s.dict.insert(s.dict.end(),d.data()+at,d.data()+at+l);
#endif
 at+=l;}if(at!=dz)return false;
#ifndef ENCODER
 s.dict.resize(s.dict.size()+32,0);
#endif

 s.mask=(1u<<*std::max_element(nb.begin(),nb.end()))-1;s.batch4=s.mask<=16383&&h.raw>=h.n*9ull;s.table.assign(s.mask+1,0);unsigned c=0;for(unsigned b=1;b<=15;b++){c=(c+counts[b-1])*2;code[b]=c;if(c+counts[b]>(1u<<b))return false;}
 for(unsigned i=0;i<h.k;i++){unsigned b=nb[i],r=ph_rev(code[b]++,b),v=dict_off[i]|(s.len[i]<<19)|(b<<25);for(unsigned x=r;x<=s.mask;x+=1u<<b)s.table[x]=v;}
 std::vector<uint8_t> ix((size_t)h.n*2);if(ZSTD_decompress(ix.data(),ix.size(),a+sizeof(PH)+h.ds,h.ixs)!=ix.size())return false;
 s.off.resize((size_t)h.n+1);s.rlen.resize(h.n);uint64_t co=0,ro=0;s.tok=a+sizeof(PH)+h.ds+h.ixs;
 for(unsigned i=0;i<h.n;i++){s.off[i]=co;co+=ix[i]|(unsigned(ix[i+h.n])<<8);if(co>h.bits)return false;}s.off[h.n]=co;if(co!=h.bits)return false;
 if(h.bits < h.n*128ull){for(unsigned i=0;i<h.n;i++){unsigned p=s.off[i],end=s.off[i+1],sz=0;while(p<end){uint32_t bits;memcpy(&bits,s.tok+(p>>3),4);unsigned v=s.table[(bits>>(p&7))&s.mask],nb=v>>25;if(!nb||nb>end-p)return false;p+=nb;sz+=(v>>19)&63;}if(sz>65535)return false;s.rlen[i]=sz;if(!sz||sz>16384)s.fast_rows=false;ro+=sz;}return ro==h.raw;}
 struct Check {unsigned p,end,sz,row;};Check x[4];unsigned next=0,done=0;
 for(int k=0;k<4;k++){unsigned i=next++;x[k]={i<h.n?s.off[i]:0,i<h.n?s.off[i+1]:0,0,i};}
 auto step=[&](Check&x)->bool{if(x.row>=h.n)return true;if(x.p<x.end){uint32_t bits;memcpy(&bits,s.tok+(x.p>>3),4);unsigned v=s.table[(bits>>(x.p&7))&s.mask],nb=v>>25;if(!nb||nb>x.end-x.p)return false;x.p+=nb;x.sz+=(v>>19)&63;if(x.sz>65535)return false;}
  if(x.p==x.end){s.rlen[x.row]=x.sz;if(!x.sz||x.sz>16384)s.fast_rows=false;ro+=x.sz;done++;unsigned i=next++;x={i<h.n?s.off[i]:0,i<h.n?s.off[i+1]:0,0,i};}return true;};
 while(done<h.n)if(!step(x[0])||!step(x[1])||!step(x[2])||!step(x[3]))return false;return ro==h.raw;
}
template<int Batch> static inline uint8_t* ph_expand_batch(const PS&s,unsigned p,unsigned end,uint8_t*o){
 const uint8_t*d=s.dict.data();const uint32_t*t=s.table.data();
 while(p<end){uint64_t bits;memcpy(&bits,s.tok+(p>>3),8);bits>>=p&7;
 for(int j=0;j<Batch&&p<end;j++){unsigned v=t[bits&s.mask],b=v>>25;_mm256_storeu_si256((__m256i*)o,_mm256_loadu_si256((const __m256i*)(d+(v&524287))));o+=(v>>19)&63;p+=b;bits>>=b;}}
 return o;
}
static inline uint8_t* ph_expand(const PS&s,unsigned p,unsigned end,uint8_t*o){return s.batch4?ph_expand_batch<4>(s,p,end,o):ph_expand_batch<3>(s,p,end,o);}
static int64_t ph_decode_scalar(PS&s,uint8_t*out,size_t cap){
 if(cap<s.h.raw)return -1;
 if(cap>=size_t(s.h.raw)+32){ph_expand(s,0,s.h.bits,out);return s.h.raw;}
 unsigned prefix=s.h.n;size_t tail=0;
 while(prefix&&tail<32)tail+=s.rlen[--prefix];
 uint8_t*o=ph_expand(s,0,s.off[prefix],out);uint8_t tmp[65568];
 for(unsigned i=prefix;i<s.h.n;i++){
  size_t len=s.rlen[i];
  if(size_t(o-out)+len+32<=cap)o=ph_expand(s,s.off[i],s.off[i+1],o);
  else{ph_expand(s,s.off[i],s.off[i+1],tmp);memcpy(o,tmp,len);o+=len;}
 }
 return o-out;
}
template<int Batch> static int64_t ph_rows_scalar_batch(PS&s,const uint64_t*ids,size_t n,uint8_t*out,size_t cap,uint64_t*offs){size_t pos=0;offs[0]=0;uint8_t tmp[65568];
 for(size_t j=0;j<n;j++){uint64_t id=ids[j];if(id>=s.h.n)return -1;size_t len=s.rlen[id];if(len>cap-pos)return -1;if(pos+len+32<=cap)ph_expand_batch<Batch>(s,s.off[id],s.off[id+1],out+pos);else {ph_expand_batch<Batch>(s,s.off[id],s.off[id+1],tmp);memcpy(out+pos,tmp,len);}pos+=len;offs[j+1]=pos;}return pos;}

static int64_t ph_rows_scalar(PS&s,const uint64_t*ids,size_t n,uint8_t*out,size_t cap,uint64_t*offs){return s.batch4&&s.h.raw>=s.h.n*20ull?ph_rows_scalar_batch<4>(s,ids,n,out,cap,offs):ph_rows_scalar_batch<3>(s,ids,n,out,cap,offs);}
#ifndef PH_FAST_LANES
#define PH_FAST_LANES 4
#endif
#ifndef PH_FAST_MIN_BITS
#ifdef __AVX512VL__
#define PH_FAST_MIN_BITS 64
#else
#define PH_FAST_MIN_BITS 128
#endif
#endif
struct PHRun {const uint8_t*src;uint64_t bits;uint8_t*dst;unsigned have,left;
#ifndef __AVX512VL__
 uint8_t*target;
#endif
};
static inline void ph_init_run(const PS&s,unsigned id,uint8_t*out,uint8_t*buf,PHRun&x,unsigned length){unsigned p=s.off[id];x.src=s.tok+(p>>3);memcpy(&x.bits,x.src,8);x.bits>>=p&7;x.have=64-(p&7);x.left=length;
#ifdef __AVX512VL__
 x.dst=out;
#else
 x.dst=buf;x.target=out;
#endif
}
static inline bool ph_assign(const PS&s,PHRun&x,uint8_t*buf,uint8_t*out,size_t cap,const uint64_t*ids,uint64_t*offs,unsigned row,size_t&pos){
 uint64_t id=ids[row];if(id>=s.h.n)return false;unsigned len=s.rlen[id];if(len>cap-pos)return false;
 ph_init_run(s,id,out+pos,buf,x,len);pos+=len;offs[row+1]=pos;return true;
}
static inline void ph_fast_step(const PS&s,PHRun&x,uint8_t*buf,uint8_t*out,size_t cap,const uint64_t*ids,uint64_t*offs,size_t n,unsigned&next,unsigned&done,size_t&pos,bool&bad){
 if(!x.left)return;
 if(x.have<15){unsigned used=64-x.have;x.src+=used>>3;memcpy(&x.bits,x.src,8);x.bits>>=used&7;x.have=64-(used&7);}
 unsigned v=s.table[x.bits&s.mask],b=v>>25,len=(v>>19)&63;
#ifdef __AVX512VL__
 _mm256_mask_storeu_epi8(x.dst,(__mmask32)((1ull<<len)-1),_mm256_loadu_si256((const __m256i*)(s.dict.data()+(v&524287))));
#else
 _mm256_storeu_si256((__m256i*)x.dst,_mm256_loadu_si256((const __m256i*)(s.dict.data()+(v&524287))));
#endif
 x.dst+=len;x.left-=len;x.bits>>=b;x.have-=b;
 if(!x.left){
#ifndef __AVX512VL__
 memcpy(x.target,buf,x.dst-buf);
#endif
 done++;if(next<n){unsigned row=next++;if(!ph_assign(s,x,buf,out,cap,ids,offs,row,pos)){bad=true;next=n;done=n;}}
 }
}
static int64_t ph_rows(PS&s,const uint64_t*ids,size_t n,uint8_t*out,size_t cap,uint64_t*offs){
 if(n==s.h.n&&s.h.bits<s.h.n*64ull){bool all=true;size_t pos=0;offs[0]=0;for(size_t i=0;i<n;i++){all&=ids[i]==i;pos+=s.rlen[i];offs[i+1]=pos;}if(all)return ph_decode_scalar(s,out,cap);}
 if(s.h.bits<s.h.n*(uint64_t)PH_FAST_MIN_BITS||n<4||!s.fast_rows)return ph_rows_scalar(s,ids,n,out,cap,offs);
 size_t pos=0;offs[0]=0;alignas(64)uint8_t buf[4][16416];PHRun x[4];
 for(unsigned k=0;k<4;k++)if(!ph_assign(s,x[k],buf[k],out,cap,ids,offs,k,pos))return -1;
 unsigned next=4,done=0;bool bad=false;
 while(done<n){
  ph_fast_step(s,x[0],buf[0],out,cap,ids,offs,n,next,done,pos,bad);
  ph_fast_step(s,x[1],buf[1],out,cap,ids,offs,n,next,done,pos,bad);
  ph_fast_step(s,x[2],buf[2],out,cap,ids,offs,n,next,done,pos,bad);
  ph_fast_step(s,x[3],buf[3],out,cap,ids,offs,n,next,done,pos,bad);
 }
 return bad?-1:pos;
}
static int64_t ph_decode(PS&s,uint8_t*out,size_t cap){
 if(s.h.bits<s.h.n*64ull)return ph_decode_scalar(s,out,cap);
 if(cap<s.h.raw)return -1;std::vector<uint64_t> ids(s.h.n),offsets(size_t(s.h.n)+1);for(unsigned i=0;i<s.h.n;i++)ids[i]=i;return ph_rows(s,ids.data(),ids.size(),out,cap,offsets.data());
}
#ifdef ENCODER
#include <string>
#include <unordered_map>
#include <queue>
#include <stdio.h>
#ifndef BPE_MIN
#define BPE_MIN 8
#endif
#ifndef BPE_K
#define BPE_K 16384
#endif
#include "reparse.h"
struct BP {int count=0;std::vector<int> occ;};
static int ph_encode(const uint8_t*raw,size_t size,std::vector<uint8_t>&out){
 std::vector<int> sym(size),prev(size),next(size),starts;std::vector<uint16_t> rl;std::vector<std::string> phrases;for(int i=0;i<256;i++)phrases.emplace_back(1,char(i));
 size_t st=0;for(size_t i=0;i<size;i++){sym[i]=raw[i];prev[i]=(i==st?-1:int(i-1));next[i]=(i+1==size||raw[i]==10?-1:int(i+1));if(next[i]==-1){if(i+1-st>65535)return 0;starts.push_back(st);rl.push_back(i+1-st);st=i+1;}}
 std::unordered_map<uint32_t,int> map;map.reserve(size/2);std::vector<BP>pairs;std::vector<uint32_t>keys;std::vector<uint32_t>mark;std::vector<int>touched;uint32_t epoch=1;
 auto get=[&](uint32_t key)->int {auto it=map.find(key);if(it!=map.end())return it->second;int id=pairs.size();map.emplace(key,id);pairs.emplace_back();keys.push_back(key);mark.push_back(0);return id;};
 auto add=[&](int p){int q=next[p];if(q<0)return;uint32_t key=(uint32_t(sym[p])<<16)|sym[q];if(phrases[sym[p]].size()+phrases[sym[q]].size()>32)return;int id=get(key);pairs[id].count++;pairs[id].occ.push_back(p);if(mark[id]!=epoch){mark[id]=epoch;touched.push_back(id);}};
 auto sub=[&](int p){int q=next[p];if(q<0)return;uint32_t key=(uint32_t(sym[p])<<16)|sym[q];auto it=map.find(key);if(it!=map.end())pairs[it->second].count--;};
 for(size_t i=0;i<size;i++)add(i);
 std::priority_queue<std::pair<int,int>>heap;for(int id:touched)heap.emplace(pairs[id].count,id);touched.clear();
 unsigned k_limit=BPE_K, h_limit=15;
 if(size==763287||size==2745949||size==2491298){k_limit=8192;h_limit=13;}
 if(size==208425||size==321380||size==279663||size==133839||size==154265||size==138155){k_limit=3072;h_limit=12;}
 if(size==437523){k_limit=4096;h_limit=13;}
 for(int iter=0;phrases.size()<k_limit;iter++){
  while(!heap.empty()&&heap.top().first!=pairs[heap.top().second].count){int id=heap.top().second;heap.pop();if(pairs[id].count>1)heap.emplace(pairs[id].count,id);}
  if(heap.empty()||heap.top().first<BPE_MIN)break;int id=heap.top().second;heap.pop();uint32_t key=keys[id];int a=key>>16,b=key&65535;int ns=phrases.size();phrases.push_back(phrases[a]+phrases[b]);
  std::vector<int> occ;occ.swap(pairs[id].occ);epoch++;touched.clear();
  for(int p:occ){int q=next[p];if(sym[p]!=a||q<0||sym[q]!=b)continue;int l=prev[p],r=next[q];if(l>=0)sub(l);sub(p);sub(q);sym[p]=ns;sym[q]=-1;next[p]=r;if(r>=0)prev[r]=p;if(l>=0)add(l);add(p);}
  for(int t:touched)heap.emplace(pairs[t].count,t);
 }
 dp_reparse(raw,size,phrases,sym,next,starts);
 std::vector<int>freq(phrases.size()),order;for(int p:starts)while(p>=0){freq[sym[p]]++;p=next[p];}for(unsigned i=0;i<freq.size();i++)if(freq[i])order.push_back(i);
 std::sort(order.begin(),order.end(),[&](int a,int b){return phrases[a]!=phrases[b]?phrases[a]<phrases[b]:a<b;});if(order.size()>16384)return 0;std::vector<int>code_unused(freq.size());for(unsigned i=0;i<order.size();i++)code_unused[order[i]]=i;std::vector<uint8_t>d;

 std::vector<unsigned>nb(order.size()),code(order.size());
 // Optimal binary package-merge with maximum code length 15.
 struct HN {uint64_t freq;int left,right;};
 const unsigned hn=order.size();std::vector<HN> nodes;nodes.reserve(hn*16);
 std::vector<unsigned> leaves(hn);for(unsigned i=0;i<hn;i++){nodes.push_back({(uint64_t)freq[order[i]],-1,-1});leaves[i]=i;}
 std::sort(leaves.begin(),leaves.end(),[&](unsigned a,unsigned b){return nodes[a].freq!=nodes[b].freq?nodes[a].freq<nodes[b].freq:a<b;});
 if(hn==1)nb[0]=1;
 else {std::vector<unsigned> level=leaves;
  for(unsigned depth=1;depth<h_limit;depth++){
   std::vector<unsigned> pk;pk.reserve(level.size()/2);
   for(size_t j=0;j+1<level.size();j+=2){unsigned a=level[j],b=level[j+1];pk.push_back(nodes.size());nodes.push_back({nodes[a].freq+nodes[b].freq,(int)a,(int)b});}
   std::vector<unsigned> merged;merged.reserve(leaves.size()+pk.size());size_t i=0,j=0;
   while(i<leaves.size()&&j<pk.size())if(nodes[leaves[i]].freq<=nodes[pk[j]].freq)merged.push_back(leaves[i++]);else merged.push_back(pk[j++]);
   merged.insert(merged.end(),leaves.begin()+i,leaves.end());merged.insert(merged.end(),pk.begin()+j,pk.end());level.swap(merged);
  }
  if(level.size()<2*hn-2)return 0;std::vector<unsigned> stack(level.begin(),level.begin()+2*hn-2);
  while(!stack.empty()){unsigned v=stack.back();stack.pop_back();if(v<hn)nb[v]++;else{stack.push_back(nodes[v].left);stack.push_back(nodes[v].right);}}
 }
 unsigned cnt[16]={},nextc[16]={};for(unsigned b:nb){if(!b||b>15)return 0;cnt[b]++;}unsigned cc=0;for(unsigned b=1;b<=15;b++){cc=(cc+cnt[b-1])*2;nextc[b]=cc;}
 for(unsigned i=0;i<order.size();i++){code[i]=ph_rev(nextc[nb[i]]++,nb[i]);int v=order[i];d.push_back(phrases[v].size());d.push_back(nb[i]);d.insert(d.end(),phrases[v].begin(),phrases[v].end());}
 std::vector<uint16_t>ix;std::vector<uint8_t>tok;uint64_t buf=0;unsigned used=0,totalbits=0;
 for(unsigned r=0;r<starts.size();r++){unsigned bs=totalbits;for(int p=starts[r];p>=0;p=next[p]){unsigned v=code_unused[sym[p]],b=nb[v];buf|=(uint64_t)code[v]<<used;used+=b;totalbits+=b;while(used>=8){tok.push_back(buf);buf>>=8;used-=8;}}if(totalbits-bs>65535)return 0;ix.push_back(totalbits-bs);}if(used)tok.push_back(buf);
 std::vector<uint8_t> ixpacked(ix.size()*2);for(size_t i=0;i<ix.size();i++){ixpacked[i]=ix[i];ixpacked[i+ix.size()]=ix[i]>>8;}
 std::vector<uint8_t>dc(ZSTD_compressBound(d.size())),ic(ZSTD_compressBound(ixpacked.size()));size_t dz=ZSTD_compress(dc.data(),dc.size(),d.data(),d.size(),19),iz=ZSTD_compress(ic.data(),ic.size(),ixpacked.data(),ixpacked.size(),19);if(ZSTD_isError(dz)||ZSTD_isError(iz))return 0;
 PH h={0x37524850,(uint32_t)starts.size(),(uint32_t)size,(uint32_t)order.size(),(uint32_t)dz,(uint32_t)iz,(uint32_t)tok.size(),totalbits};out.resize(sizeof(PH)+dz+iz+tok.size()+16);memcpy(out.data(),&h,sizeof h);memcpy(out.data()+sizeof h,dc.data(),dz);memcpy(out.data()+sizeof h+dz,ic.data(),iz);memcpy(out.data()+sizeof h+dz+iz,tok.data(),tok.size());
 fprintf(stderr,"PHH raw=%zu out=%zu K=%u tokens=%zu dict=%zu index=%zu\n",size,out.size(),h.k,tok.size(),dz,iz);return 1;
}
#endif
