#ifndef DBTEXT_GENERAL_H
#define DBTEXT_GENERAL_H
#include "huffman.h"
#include "grammar.h"
#include "context.h"
#ifdef ENCODER
#include "reparse.h"
#endif
#include <algorithm>
#include <cstring>
#include <string>
#include <memory>
#include <numeric>
#include <unordered_map>
#include <cstdio>
#include <cstdlib>
static uint32_t gr32(const uint8_t* p){uint32_t x;memcpy(&x,p,4);return x;}
static void gp32(std::vector<uint8_t>& v,uint32_t x){size_t p=v.size();v.resize(p+4);memcpy(v.data()+p,&x,4);}
static void gp16(std::vector<uint8_t>& v,uint16_t x){v.push_back(x);v.push_back(x>>8);}
struct GState{
 uint32_t raw,rows,ns,base,stride,sorted,pbits,maxrow,indexlog,indexcount;
 uint64_t nbits;
 const uint8_t *index,*perm,*stream;
 HDec h;std::unique_ptr<CDec> context;
 std::vector<uint32_t> off;
 std::vector<uint16_t> len;
 std::vector<uint8_t> end,dict;
};
#ifdef ENCODER
struct GPair {uint32_t a,b;};
struct GRecord{int count=0;std::vector<int> pos;uint32_t epoch=0;};
static std::vector<uint8_t> general_candidate(const uint8_t* raw,size_t size,bool sorted){
 std::vector<std::string> rows; size_t start=0;
 for(size_t i=0;i<size;i++)if(raw[i]==10){rows.emplace_back((const char*)raw+start,i+1-start);start=i+1;}
 if(start<size) rows.emplace_back((const char*)raw+start,size-start);
 uint32_t nr=rows.size(),base=sorted?512:256;
 unsigned stride=32;
 const char* es=getenv("GSTRIDE");if(es)stride=std::max(1,atoi(es));
 // Sorted front coding uses short restart groups to bound any partial query.
 if(sorted)stride=16;
 std::vector<uint32_t> order(nr);std::iota(order.begin(),order.end(),0);
 if(sorted)std::sort(order.begin(),order.end(),[&](uint32_t a,uint32_t b){return rows[a]<rows[b];});
 std::vector<uint32_t> inv(nr);for(uint32_t i=0;i<nr;i++)inv[order[i]]=i;
 std::vector<uint32_t> sym; sym.reserve(size);
 uint32_t maxrow=0;
 for(uint32_t i=0;i<nr;i++){
  auto& r=rows[order[i]];maxrow=std::max<uint32_t>(maxrow,r.size());
  size_t pre=0;
  if(sorted){
   if(i%stride){auto&pr=rows[order[i-1]];while(pre<255&&pre<r.size()&&pre<pr.size()&&r[pre]==pr[pre]&&r[pre]!=10)++pre;}
   sym.push_back(256+pre);
  }
  for(size_t j=pre;j<r.size();j++)sym.push_back((uint8_t)r[j]);
 }
 size_t n=sym.size();if(n>INT32_MAX)throw std::runtime_error("input too large");
 std::vector<int> next(n),prev(n);for(size_t i=0;i<n;i++){next[i]=i+1<n?i+1:-1;prev[i]=i?i-1:-1;}
 std::vector<uint32_t> lens(base,1);std::vector<uint8_t> ends(base,0);ends[10]=1;
 std::vector<GPair> pairs;
 std::unordered_map<uint64_t,uint32_t> map;map.reserve(1<<20);
 std::vector<GRecord> rec;rec.reserve(1<<20);
 auto valid=[&](uint32_t a,uint32_t b){return !ends[a] && !(a>=256&&a<base) && !(b>=256&&b<base) && lens[a]+lens[b]<=256;};
 auto get=[&](uint32_t a,uint32_t b)->uint32_t{
  uint64_t key=(uint64_t(a)<<32)|b;auto it=map.find(key);if(it!=map.end())return it->second;
  uint32_t id=rec.size();map.emplace(key,id);rec.emplace_back();return id;
 };
 for(size_t i=0;i+1<n;i++)if(valid(sym[i],sym[i+1])){uint32_t id=get(sym[i],sym[i+1]);rec[id].count++;rec[id].pos.push_back(i);}
 using Q=std::pair<int,uint32_t>;std::priority_queue<Q> pq;
 for(uint32_t i=0;i<rec.size();i++)if(rec[i].count>=3)pq.emplace(rec[i].count,i);
 uint32_t epoch=0;std::vector<uint32_t> dirty;
 unsigned maxmerge=60000;const char* em=getenv("GMERGES");if(em)maxmerge=atoi(em);
 for(unsigned it=0;it<maxmerge;it++){
  while(!pq.empty()&&pq.top().first!=rec[pq.top().second].count)pq.pop();
  if(pq.empty()||pq.top().first<4)break;
  uint32_t id=pq.top().second;pq.pop();auto positions=std::move(rec[id].pos);
  int p0=-1;for(int p:positions)if(sym[p]!=UINT32_MAX&&next[p]>=0&&valid(sym[p],sym[next[p]])&&get(sym[p],sym[next[p]])==id){p0=p;break;}
  if(p0<0){rec[id].count=0;continue;}
  uint32_t aa=sym[p0],bb=sym[next[p0]],ns=base+pairs.size();pairs.push_back({aa,bb});
  lens.push_back(lens[aa]+lens[bb]);ends.push_back(ends[bb]);
  ++epoch;dirty.clear();
  auto change=[&](int p,int delta){
   if(p<0||next[p]<0)return;uint32_t a=sym[p],b=sym[next[p]];if(!valid(a,b))return;
   uint32_t z=get(a,b);rec[z].count+=delta;
   if(delta>0)rec[z].pos.push_back(p);
   if(rec[z].epoch!=epoch){rec[z].epoch=epoch;dirty.push_back(z);}
  };
  for(int p:positions){
   if(sym[p]!=aa||next[p]<0||sym[next[p]]!=bb)continue;
   int q=next[p],l=prev[p],r=next[q];
   change(l,-1);change(p,-1);change(q,-1);
   sym[p]=ns;sym[q]=UINT32_MAX;next[p]=r;if(r>=0)prev[r]=p;
   change(l,1);change(p,1);
  }
  for(uint32_t z:dirty)if(rec[z].count>=4)pq.emplace(rec[z].count,z);
 }
 std::vector<uint32_t> final; for(int p=0;p>=0;p=next[p])final.push_back(sym[p]);
 // Release the training graph before evaluating grammar-size candidates.
 map.clear();map.rehash(0);rec.clear();rec.shrink_to_fit();next.clear();prev.clear();sym.clear();
 std::vector<uint8_t> best;std::vector<uint32_t> besttok;unsigned bestlim=0;
 std::vector<uint16_t> first(base),last(base);
 for(uint32_t i=0;i<base;i++){first[i]=last[i]=i<256?i:256;}
 for(auto p:pairs){first.push_back(first[p.a]);last.push_back(last[p.b]);}
 auto package=[&](const std::vector<uint32_t>& tok,unsigned lim,const HEnc& h,const CEnc* c)->std::vector<uint8_t>{
  uint32_t ns=base+lim;HBitWriter w;std::vector<uint32_t> index;
  uint32_t row=0;bool atstart=true;uint16_t context=10;
  for(uint32_t v:tok){
   if(atstart){if(row%stride==0)index.push_back(w.bit_size);atstart=false;}
   if(c)c->put(w,v,context);else w.put(h,v);
   if(ends[v]){row++;atstart=true;}
  }
  uint64_t nbits=w.bit_size;w.finish();
  unsigned pbits=0;while((1u<<pbits)<nr)++pbits;
  HBitWriter perm;if(sorted){for(uint32_t rank:inv)perm.put(rank,pbits);perm.finish();}
  unsigned ilog=0;
  for(unsigned l=2;l<=15;l++){
   bool fits=true;for(uint32_t i=0;i<index.size();i++)if(index[i]-index[(i>>l)<<l]>65535){fits=false;break;}
   if(!fits)break;ilog=l;
  }
  std::vector<uint8_t> out;
  gp32(out,c?102:101);gp32(out,size);gp32(out,nr);gp32(out,ns);gp32(out,base);gp32(out,stride|(ilog<<16));
  gp32(out,sorted);gp32(out,pbits);gp32(out,maxrow);gp32(out,nbits);gp32(out,index.size());gp32(out,perm.bytes.size());
  auto metadata=grammar_pack(base,c?c->lengths:h.lengths,pairs.data());
  out.insert(out.end(),metadata.begin(),metadata.end());
  if(c)out.insert(out.end(),c->blob.begin(),c->blob.end());
  if(ilog){
   for(uint32_t i=0;i<index.size();i+=(1u<<ilog))gp32(out,index[i]);
   for(uint32_t i=0;i<index.size();i++)gp16(out,index[i]-index[(i>>ilog)<<ilog]);
  }else for(uint32_t v:index)gp32(out,v);
  out.insert(out.end(),perm.bytes.begin(),perm.bytes.end());
  out.insert(out.end(),w.bytes.begin(),w.bytes.end());
  return out;
 };
 auto fit=[&](const std::vector<uint32_t>& tok,unsigned lim,bool direct=false)->std::vector<uint8_t>{
  uint32_t ns=base+lim;std::vector<uint64_t> freq(ns);for(uint32_t v:tok)freq[v]++;
  HEnc h(freq);auto out=package(tok,lim,h,nullptr);
  std::vector<uint16_t> f(first.begin(),first.begin()+ns),l(last.begin(),last.begin()+ns);
  CEnc c(tok,f,l);auto co=package(tok,lim,h,&c);if(co.size()<out.size())out.swap(co);
  if(direct){CEnc d(tok,f,l,1);auto dd=package(tok,lim,h,&d);if(dd.size()<out.size())out.swap(dd);}
  return out;
 };
 std::vector<unsigned> limits{256,512,1024,2048,3072,4096,6144,8192,10240,12288,16384,20480,24576,28672,32768,40000,49152,60000,unsigned(pairs.size())};
 for(unsigned lim:limits){
  if(lim>pairs.size())continue;
  uint32_t ns=base+lim;std::vector<uint32_t> tok;tok.reserve(final.size()*2);
  auto expand=[&](auto&& self,uint32_t v)->void{
   if(v<ns){tok.push_back(v);return;}auto p=pairs[v-base];self(self,p.a);self(self,p.b);
  };
  for(uint32_t v:final)expand(expand,v);
  auto out=fit(tok,lim);
  if(best.empty()||out.size()<best.size()){best.swap(out);besttok.swap(tok);bestlim=lim;}
 }
 if(!sorted&&!best.empty()){
  auto directout=fit(besttok,bestlim,true);if(directout.size()<best.size())best.swap(directout);
  uint32_t ns=base+bestlim;ReparseDictionary parser(base,ns,pairs.data());
  std::vector<uint16_t> f(first.begin(),first.begin()+ns),l(last.begin(),last.begin()+ns);
  for(unsigned it=0;it<4;it++){
   std::vector<uint64_t> freq(ns);for(uint32_t v:besttok)freq[v]++;
   HEnc h(freq);bool contextual=gr32(best.data())==102;
   unsigned kind=contextual?unsigned(gr32(best.data()+48+gr32(best.data()+48)+4)>>31):0;
   CEnc c(besttok,f,l,kind);
   auto tok=parser.parse(raw,size,[&](uint32_t v,uint8_t prev)->uint32_t{
    if(!contextual)return h.lengths[v]?h.lengths[v]:24;
    uint32_t cost=c.cost(v,prev);return cost==UINT32_MAX?32:cost;
   });
   auto out=fit(tok,bestlim,true);if(out.size()>=best.size())break;
   best.swap(out);besttok.swap(tok);
  }
 }
 if(!sorted&&!best.empty()){
  pairs.resize(bestlim);auto remap=prune_grammar(base,pairs,besttok);
  prune_attributes(first,remap);prune_attributes(last,remap);prune_attributes(lens,remap);prune_attributes(ends,remap);
  bestlim=pairs.size();auto out=fit(besttok,bestlim,true);if(out.size()<best.size())best.swap(out);
 }
 if(best.empty())throw std::runtime_error("empty grammar");
 fprintf(stderr," general sorted=%d input=%zu archive=%zu ns=%u\n",sorted,size,best.size(),gr32(best.data()+12));
 return best;
}
static bool general_encode(const uint8_t* raw,size_t size,std::vector<uint8_t>& out){
 auto best=general_candidate(raw,size,false);
 const char* sort=getenv("GSORT");
 if(sort&&atoi(sort)){auto s=general_candidate(raw,size,true);if(s.size()<best.size())best.swap(s);}
 out.swap(best);return true;
}
#endif
static void* general_open(const uint8_t* a,size_t size){
 if(size<52||(gr32(a)!=100&&gr32(a)!=101&&gr32(a)!=102))return nullptr;
 bool packed=gr32(a)!=100,contextual=gr32(a)==102;
 auto s=std::make_unique<GState>();
 s->raw=gr32(a+4);s->rows=gr32(a+8);s->ns=gr32(a+12);s->base=gr32(a+16);s->stride=gr32(a+20);
 s->indexlog=s->stride>>16;s->stride&=65535;
 s->sorted=gr32(a+24);s->pbits=gr32(a+28);s->maxrow=gr32(a+32);s->nbits=gr32(a+36);
 uint32_t ni=gr32(a+40),np=gr32(a+44);s->indexcount=ni;
 if(s->indexlog>15||!s->rows||s->rows>s->raw||s->raw>100000000||s->base!=(s->sorted?512u:256u)||s->sorted!=0||s->ns<s->base||s->ns>65535||!s->stride||s->stride>4096||s->maxrow>s->raw||s->pbits>31)return nullptr;
 if(ni!=(s->rows+s->stride-1)/s->stride||np!=(s->sorted?((uint64_t(s->rows)*s->pbits+7)/8+8):0))return nullptr;
 size_t pos=48;uint32_t nd=s->ns-s->base;
 uint64_t indexbytes=s->indexlog?uint64_t(ni)*2+((ni+(1u<<s->indexlog)-1)>>s->indexlog)*4:uint64_t(ni)*4;
 uint64_t md=packed?gr32(a+48):uint64_t(nd)*4+s->ns;
 if(md>size-48)return nullptr;
 uint32_t cb=0;if(contextual){if(48+md+4>size)return nullptr;cb=gr32(a+48+md);}
 uint64_t need=pos+md+cb+indexbytes+np+(s->nbits+7)/8+8;
 std::vector<GrammarPair> pairs;std::vector<uint8_t> lengths;
 size_t metab=packed?grammar_unpack(a+48,size-48,s->base,s->ns,pairs,lengths):0;
 if(packed&&!metab)return nullptr;
 if(need!=size)return nullptr;
 s->dict.reserve(s->ns*256);s->off.resize(s->ns);s->len.resize(s->ns);s->end.resize(s->ns);
 for(uint32_t i=0;i<s->base;i++){s->off[i]=s->dict.size();s->len[i]=1;s->dict.push_back(i<256?i:0);s->end[i]=i==10;}
 for(uint32_t i=s->base;i<s->ns;i++){
  uint32_t x,y;
  if(packed){x=pairs[i-s->base].a;y=pairs[i-s->base].b;}
  else{x=uint32_t(a[pos])|(uint32_t(a[pos+1])<<8);y=uint32_t(a[pos+2])|(uint32_t(a[pos+3])<<8);pos+=4;}
  if(x>=i||y>=i||(x>=256&&x<s->base)||(y>=256&&y<s->base)||s->end[x]||unsigned(s->len[x])+s->len[y]>256)return nullptr;
  s->off[i]=s->dict.size();s->len[i]=s->len[x]+s->len[y];s->end[i]=s->end[y];
  // vector growth can invalidate source pointers: reserve before copying.
  s->dict.reserve(s->dict.size()+s->len[i]);
  size_t dp=s->dict.size();s->dict.resize(dp+s->len[i]);
  memcpy(s->dict.data()+dp,s->dict.data()+s->off[x],s->len[x]);
  memcpy(s->dict.data()+dp+s->len[x],s->dict.data()+s->off[y],s->len[y]);
 }
 if(packed){
  pos+=metab;
  if(contextual){
   std::vector<uint16_t> first(s->ns),last(s->ns);
   for(uint32_t i=0;i<s->ns;i++){if(i>=256&&i<s->base){first[i]=last[i]=256;}else{first[i]=s->dict[s->off[i]];last[i]=s->dict[s->off[i]+s->len[i]-1];}}
   s->context=std::make_unique<CDec>();size_t read=s->context->build(a+pos,size-pos,first,last,lengths);if(read!=cb)return nullptr;pos+=read;
  }else s->h.build(lengths.data(),s->ns);
 }else{s->h.build(a+pos,s->ns);pos+=s->ns;}s->index=a+pos;pos+=indexbytes;s->perm=a+pos;pos+=np;s->stream=a+pos;
 uint32_t last=0;for(uint32_t i=0;i<ni;i++){uint32_t b;if(s->indexlog){uint32_t nb=(ni+(1u<<s->indexlog)-1)>>s->indexlog;const uint8_t* rel=s->index+nb*4+i*2;b=gr32(s->index+4*(i>>s->indexlog))+rel[0]+(uint32_t(rel[1])<<8);}else b=gr32(s->index+4*i);if(b>=s->nbits||(!i&&b)||(i&&b<last))return nullptr;last=b;}
 return s.release();
}
static uint32_t gindex(GState* s,uint32_t i){
 if(!s->indexlog)return gr32(s->index+4*i);
 uint32_t nb=(s->indexcount+(1u<<s->indexlog)-1)>>s->indexlog;
 const uint8_t* p=s->index+nb*4+i*2;
 return gr32(s->index+4*(i>>s->indexlog))+p[0]+(uint32_t(p[1])<<8);
}
static uint32_t gtoken(GState* s,uint64_t& bit,uint16_t& context){
 if(bit>s->nbits||(!s->context&&bit==s->nbits))return UINT32_MAX;uint32_t x=s->context?s->context->decode(s->stream,bit,context,s->nbits):s->h.decode(s->stream,bit);
 return bit>s->nbits?UINT32_MAX:x;
}
static int64_t general_decode(void* st,uint8_t* out,size_t cap){
 auto s=(GState*)st;if(cap<s->raw)return -1;
 uint64_t bit=0;size_t n=0;uint16_t context=10;
 while(n<s->raw){uint32_t x=gtoken(s,bit,context);if(x>=s->ns||n+s->len[x]>cap)return -1;memcpy(out+n,s->dict.data()+s->off[x],s->len[x]);n+=s->len[x];}
 return n==s->raw&&bit==s->nbits?int64_t(n):-1;
}

static int64_t general_rows(void* st,const uint64_t* ids,size_t count,uint8_t* out,size_t cap,uint64_t* offsets){
 auto s=(GState*)st;offsets[0]=0;if(count>s->rows)return -1;
 for(size_t i=0;i<count;i++)if(ids[i]>=s->rows||(i&&ids[i]<ids[i-1]))return -1;
 if(count==s->rows&&ids[0]==0&&ids[count-1]==s->rows-1){
  bool consecutive=true;for(size_t i=0;i<count;i++)if(ids[i]!=i){consecutive=false;break;}
  if(consecutive){int64_t n=general_decode(s,out,cap);if(n<0)return -1;size_t r=1;for(size_t i=0;i<size_t(n);i++)if(out[i]==10){if(r>count)return -1;offsets[r++]=i+1;}if(r==count)offsets[r++]=n;return r==count+1?n:-1;}
 }
 size_t n=0;
  uint32_t current=UINT32_MAX;uint64_t bit=0;uint16_t context=10;
  for(size_t i=0;i<count;i++){
   uint32_t target=ids[i],group=target/s->stride;
   if(current==UINT32_MAX||target<current||current/s->stride!=group){current=group*s->stride;bit=gindex(s,group);context=10;}
   while(current<=target){
    bool selected=current==target;uint32_t rowbytes=0;
    while(s->context||bit<s->nbits){uint32_t x=gtoken(s,bit,context);if(x>=s->ns||rowbytes+s->len[x]>s->maxrow)return -1;rowbytes+=s->len[x];if(selected){if(n+s->len[x]>cap)return -1;memcpy(out+n,s->dict.data()+s->off[x],s->len[x]);n+=s->len[x];}if(s->end[x])break;}
    current++;
   }
   offsets[i+1]=n;
  }
 return n;
}
static void general_close(void* st){delete (GState*)st;}
#endif
