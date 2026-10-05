#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "complex.h"
#ifdef ENCODER
#include "numeric_group_huff.h"
#include <map>
#include <algorithm>
#endif
namespace loccodec {
static inline uint16_t rd16(const uint8_t*p){uint16_t x;memcpy(&x,p,2);return x;}
static inline uint32_t rd32(const uint8_t*p){uint32_t x;memcpy(&x,p,4);return x;}
static inline uint64_t rd64(const uint8_t*p){uint64_t x;memcpy(&x,p,8);return x;}
static inline uint32_t reverse(uint32_t c,unsigned n){uint32_t r=0;while(n--){r=(r<<1)|(c&1);c>>=1;}return r;}
struct State {
 uint16_t*ids=nullptr;uint8_t*blob=nullptr;const uint8_t**str=nullptr;uint16_t*len=nullptr;uint32_t count=0;
 void reset(){free(ids);free(blob);free(str);free(len);ids=nullptr;blob=nullptr;str=nullptr;len=nullptr;count=0;}
 ~State(){reset();}
 State()=default;State(const State&)=delete;State&operator=(const State&)=delete;
 bool open(const uint8_t*p,size_t n,uint32_t rows){
  reset();if(n<48||rd32(p)!=0x3248434c||rd32(p+4)!=rows||!rows||rows>10000000)return false;
  count=rd32(p+8);uint32_t total=rd32(p+12),sz=rd32(p+16),ds=rd32(p+20),bits=rd32(p+24);
  if(!count||count>65535||count>rows||total>10000000||ds>2000000||uint64_t(bits)>uint64_t(rows)*16||size_t(40)+sz+count+(size_t(bits)+7)/8+8!=n)return false;
  str=(const uint8_t**)malloc(size_t(count)*sizeof(void*));len=(uint16_t*)malloc(size_t(count)*2);blob=(uint8_t*)malloc(size_t(total)+32);ids=(uint16_t*)malloc(size_t(rows)*2);
  if(!str||!len||!blob||!ids)return false;
  complexcodec::Bytes temp;if(!complexcodec::unlz(p+40,sz,temp,ds))return false;
  const uint8_t*q=temp.data(),*end=q+ds;uint8_t*out=blob;uint16_t prev=0;
  for(uint32_t i=0;i<count;++i){if(end-q<4)return false;uint16_t pr=rd16(q),su=rd16(q+2);q+=4;if(pr>prev||uint32_t(pr)+su>65535||end-q<su||size_t(out-blob)+pr+su>total)return false;str[i]=out;len[i]=pr+su;if(pr)memcpy(out,str[i-1],pr);memcpy(out+pr,q,su);q+=su;out+=len[i];prev=len[i];}
  if(q!=end||size_t(out-blob)!=total)return false;
  const uint8_t*lengths=p+40+sz,*stream=lengths+count;uint32_t counts[17]={},next[17]={};
  for(unsigned i=0;i<count;++i){unsigned b=lengths[i];if(!b||b>16)return false;++counts[b];}
  uint32_t code=0;for(unsigned b=1;b<=16;++b){code=(code+counts[b-1])*2;next[b]=code;if(code+counts[b]>(1u<<b))return false;}if(code+counts[16]!=65536)return false;
  uint32_t*table=(uint32_t*)malloc(65536*4);if(!table)return false;
  for(unsigned i=0;i<count;++i){unsigned b=lengths[i],c=reverse(next[b]++,b);for(unsigned j=c;j<65536;j+=1u<<b)table[j]=i|(b<<16);}
  size_t bp[4]={0,rd32(p+28),rd32(p+32),rd32(p+36)},be[4]={bp[1],bp[2],bp[3],bits};
  uint32_t rp[4]={0,rows/4,uint32_t(uint64_t(rows)*2/4),uint32_t(uint64_t(rows)*3/4)},re[4]={rp[1],rp[2],rp[3],rows};
  bool ok=bp[0]<=bp[1]&&bp[1]<=bp[2]&&bp[2]<=bp[3]&&bp[3]<=bits;
  if(ok)for(uint32_t j=0;j<rows/4;++j){
   if(bp[0]>=be[0]||bp[1]>=be[1]||bp[2]>=be[2]||bp[3]>=be[3]){ok=false;break;}
   uint32_t a=table[(rd64(stream+(bp[0]>>3))>>(bp[0]&7))&65535];
   uint32_t b=table[(rd64(stream+(bp[1]>>3))>>(bp[1]&7))&65535];
   uint32_t c=table[(rd64(stream+(bp[2]>>3))>>(bp[2]&7))&65535];
   uint32_t d=table[(rd64(stream+(bp[3]>>3))>>(bp[3]&7))&65535];
   ids[rp[0]++]=a;ids[rp[1]++]=b;ids[rp[2]++]=c;ids[rp[3]++]=d;bp[0]+=a>>16;bp[1]+=b>>16;bp[2]+=c>>16;bp[3]+=d>>16;
  }
  for(unsigned k=0;k<4&&ok;++k){while(rp[k]<re[k]){if(bp[k]>=be[k]){ok=false;break;}uint32_t e=table[(rd64(stream+(bp[k]>>3))>>(bp[k]&7))&65535];ids[rp[k]++]=e;bp[k]+=e>>16;}if(bp[k]!=be[k])ok=false;}
  free(table);return ok;
 }
 inline uint32_t get(size_t row)const{return ids[row];}
 inline uint8_t*emit(size_t row,uint8_t*out)const{uint32_t id=ids[row];size_t n=len[id];memcpy(out,str[id],n);return out+n;}
};
#ifdef ENCODER
static inline void put32(std::vector<uint8_t>&o,uint32_t x){size_t n=o.size();o.resize(n+4);memcpy(o.data()+n,&x,4);}
static inline void put16(std::vector<uint8_t>&o,uint16_t x){size_t n=o.size();o.resize(n+2);memcpy(o.data()+n,&x,2);}
static inline std::vector<uint8_t> encode(const std::vector<std::string>&rows){
 if(rows.empty())throw std::runtime_error("empty location column");
 std::map<std::string,uint32_t>mp;for(const auto&s:rows)mp[s]=0;uint32_t count=0,total=0;for(auto&x:mp){x.second=count++;total+=x.first.size();}
 if(count<2||count>65535)throw std::runtime_error("location count");
 std::vector<uint8_t>dict;std::string prev;for(const auto&x:mp){const auto&s=x.first;size_t pr=0;while(pr<prev.size()&&pr<s.size()&&prev[pr]==s[pr])++pr;if(s.size()>65535)throw std::runtime_error("location length");put16(dict,pr);put16(dict,s.size()-pr);dict.insert(dict.end(),s.begin()+pr,s.end());prev=s;}
 auto packed=complexcodec::lz(dict);std::vector<uint64_t>freq(count);std::vector<uint16_t>ids;ids.reserve(rows.size());for(const auto&s:rows){uint16_t id=mp[s];ids.push_back(id);++freq[id];}
 auto h=numgrouphuff::make_huffman(freq);unsigned maxlen=0;for(auto b:h.len)maxlen=std::max<unsigned>(maxlen,b);
 if(maxlen>16){unsigned counts[64]={};for(auto b:h.len)++counts[std::min<unsigned>(b,16)];int64_t used=0;for(unsigned b=1;b<=16;++b)used+=int64_t(counts[b])<<(16-b);while(used>65536){unsigned b=15;while(b&&!counts[b])--b;--counts[b];counts[b+1]+=2;--counts[16];--used;}std::vector<uint16_t>order;for(unsigned i=0;i<count;++i)order.push_back(i);std::sort(order.begin(),order.end(),[&](unsigned a,unsigned b){return freq[a]!=freq[b]?freq[a]>freq[b]:a<b;});unsigned i=0;for(unsigned b=1;b<=16;++b)for(unsigned j=0;j<counts[b];++j)h.len[order[i++]]=b;}
 uint32_t counts[17]={},next[17]={};for(auto b:h.len)++counts[b];uint32_t code=0;for(unsigned b=1;b<=16;++b){code=(code+counts[b-1])*2;next[b]=code;}for(unsigned i=0;i<count;++i)h.code[i]=reverse(next[h.len[i]]++,h.len[i]);
 size_t bits=0;for(auto id:ids)bits+=h.len[id];std::vector<uint8_t>stream((bits+7)/8+8);size_t bitpos=0;uint32_t checkpoints[3]={};for(size_t row=0;row<ids.size();++row){for(unsigned k=0;k<3;++k)if(row==ids.size()*(k+1)/4)checkpoints[k]=bitpos;auto id=ids[row];uint64_t v=uint64_t(h.code[id])<<(bitpos&7);size_t off=bitpos>>3;for(unsigned j=0;j<3;++j)stream[off+j]|=v>>(j*8);bitpos+=h.len[id];}
 std::vector<uint8_t>out;put32(out,0x3248434c);put32(out,rows.size());put32(out,count);put32(out,total);put32(out,packed.size());put32(out,dict.size());put32(out,bits);for(unsigned k=0;k<3;++k)put32(out,checkpoints[k]);
 out.insert(out.end(),packed.begin(),packed.end());out.insert(out.end(),h.len.begin(),h.len.end());out.insert(out.end(),stream.begin(),stream.end());return out;
}
#endif
}
