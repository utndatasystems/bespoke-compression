
#pragma once
#include <stdint.h>
#include <immintrin.h>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <limits>
#include "repair_train.h"
#include "optparse.h"
#include "dpack.h"
namespace txt {
struct Header {uint32_t magic,raw,rows,nd,ds,is,cs,maxrow;};
constexpr uint32_t magic=0x43545854;
static bool is(const uint8_t*p,size_t n){uint32_t m=0;if(n>=4)memcpy(&m,p,4);return m==magic;}
static void putv(std::vector<uint8_t>&v,uint32_t x){while(x>=128){v.push_back((x&127)|128);x>>=7;}v.push_back(x);}
static bool getv(const uint8_t*&p,const uint8_t*e,uint32_t&x){x=0;for(int s=0;s<35;s+=7){if(p==e)return false;uint32_t b=*p++;if(s==28&&b>15)return false;x|=(b&127)<<s;if(b<128)return true;}return false;}
#ifdef ENCODER
static bool encode_with_target(std::vector<uint8_t>&out,const uint8_t*raw,size_t n,unsigned target,bool precise=false){
 if(n>0xffffffffU)return false;
 std::vector<std::vector<uint16_t>> rows;std::vector<std::string> dict;for(int i=0;i<256;i++)dict.emplace_back(1,char(i));
 size_t st=0;uint32_t maxrow=0;
 for(size_t j=0;j<n;j++)if(raw[j]==10||j+1==n){rows.emplace_back();auto&r=rows.back();for(size_t k=st;k<=j;k++)r.push_back(raw[k]);maxrow=std::max(maxrow,uint32_t(j+1-st));st=j+1;}
 #ifndef TEX_MINCOUNT
#define TEX_MINCOUNT 3
#endif

 if(precise)repair_train(rows,dict,target);else {
 for(unsigned step=0;dict.size()<target;step++){
  std::unordered_map<uint32_t,uint32_t> counts;counts.reserve(std::min(size_t(1000000),n));
  for(auto&r:rows)for(size_t j=1;j<r.size();j++){uint16_t a=r[j-1],b=r[j];if(dict[a].size()+dict[b].size()<=16)counts[uint32_t(a)*65536+b]++;}
  std::vector<std::pair<uint32_t,uint32_t>> pairs; pairs.reserve(counts.size());
  for(auto&kv:counts)if(kv.second>=TEX_MINCOUNT)pairs.emplace_back(kv.second,kv.first);
  if(pairs.empty())break;
  size_t batch=std::min(size_t(128),std::min(pairs.size(),size_t(target-dict.size())));
  std::partial_sort(pairs.begin(),pairs.begin()+batch,pairs.end(),[](auto&a,auto&b){return a.first>b.first||(a.first==b.first&&a.second<b.second);});
  std::unordered_map<uint32_t,uint16_t> merged;
  for(size_t k=0;k<batch;k++){auto key=pairs[k].second;merged[key]=dict.size();dict.push_back(dict[key>>16]+dict[key&65535]);}
  size_t saving=0;
  for(auto&r:rows){size_t w=0;for(size_t j=0;j<r.size();){if(j+1<r.size()){auto it=merged.find(uint32_t(r[j])*65536+r[j+1]);if(it!=merged.end()){r[w++]=it->second;j+=2;saving++;continue;}}r[w++]=r[j++];}r.resize(w);}
  if(saving<8)break;
 }
 }
 optimize_parse(rows,dict,raw,n);
 std::vector<uint32_t>freq(dict.size());for(auto&r:rows)for(auto s:r)freq[s]++;
 std::vector<uint16_t> order;for(size_t i=0;i<dict.size();i++)if(freq[i])order.push_back(i);
 std::sort(order.begin(),order.end(),[&](auto a,auto b){return dict[a]<dict[b]||(dict[a]==dict[b]&&a<b);});
 std::vector<uint16_t> remap(dict.size());std::vector<uint8_t>d,ix,data;
 std::string previous;std::vector<uint16_t> canonical;
 for(size_t i=0;i<order.size();i++){auto&str=dict[order[i]];if(str==previous){remap[order[i]]=canonical.size()-1;continue;}remap[order[i]]=canonical.size();canonical.push_back(order[i]);unsigned prefix=0;while(prefix<str.size()&&prefix<previous.size()&&str[prefix]==previous[prefix])prefix++;unsigned suffix=str.size()-prefix;d.push_back((prefix<<4)|(suffix-1));d.insert(d.end(),str.begin()+prefix,str.end());previous=str;}
 order.swap(canonical);
 unsigned bits=0;while((1u<<bits)<order.size())bits++;if(!bits)bits=1;
 uint64_t pos=0;data.resize(n*2+8,0);
 std::unordered_map<unsigned,unsigned> countfreq;for(auto&r:rows)countfreq[r.size()]++;
 std::vector<std::pair<unsigned,unsigned>> countorder;for(auto kv:countfreq)countorder.push_back({kv.second,kv.first});
 std::sort(countorder.begin(),countorder.end(),[](auto a,auto b){return a.first>b.first||(a.first==b.first&&a.second<b.second);});
 unsigned cb[15]={};for(unsigned j=0;j<15;j++){if(j<countorder.size())cb[j]=countorder[j].second;putv(ix,cb[j]);}
 size_t indexstart=ix.size();ix.resize(indexstart+(rows.size()+1)/2,0);size_t ri=0;
 for(auto&r:rows){for(auto sym:r){uint64_t c=remap[sym];uint64_t bit=pos*bits;unsigned shift=bit&7;uint64_t x=c<<shift;for(unsigned k=0;k<3;k++)data[(bit>>3)+k]|=x>>(8*k);pos++;}unsigned rl=r.size(),code=15;for(unsigned j=0;j<15;j++)if(cb[j]==rl){code=j;break;}ix[indexstart+ri/2]|=code<<((ri&1)*4);if(code==15)putv(ix,rl);ri++;}
 data.resize((pos*bits+7)/8+8);
 d=dpack::pack(d);
 Header h{magic,(uint32_t)n,(uint32_t)rows.size(),(uint32_t)order.size(),(uint32_t)d.size(),(uint32_t)ix.size(),(uint32_t)pos,maxrow};
 out.resize(sizeof(h));memcpy(out.data(),&h,sizeof(h));out.insert(out.end(),d.begin(),d.end());out.insert(out.end(),ix.begin(),ix.end());out.insert(out.end(),data.begin(),data.end());
 return true;
}
#include "fit_settings.h"
static bool encode(std::vector<uint8_t>&out,const uint8_t*raw,size_t n){
 unsigned target=16384;bool precise=true;fit_select(n,target,precise);
 return encode_with_target(out,raw,n,target,precise);
}

#endif
struct State {Header h;const uint8_t*data;uint32_t*ix;uint8_t*dict;uint8_t*len;unsigned bits,mask;};
static void close(State*s){if(s){free(s->ix);free(s->dict);free(s->len);free(s);}}
static State*open(const uint8_t*p,size_t n){
 if(n<sizeof(Header))return nullptr;Header h;memcpy(&h,p,sizeof(h));
 if(h.magic!=magic||!h.nd||h.nd>32768||h.raw>1000000000||h.rows>h.raw||h.maxrow>h.raw||false)return nullptr;
 auto*s=(State*)calloc(1,sizeof(State));if(!s)return nullptr;s->h=h;s->bits=0;while((1u<<s->bits)<h.nd)s->bits++;if(!s->bits)s->bits=1;s->mask=(1u<<s->bits)-1;if(uint64_t(sizeof(h))+h.ds+h.is+(uint64_t(h.cs)*s->bits+7)/8+8!=n){close(s);return nullptr;}
 s->dict=(uint8_t*)malloc(size_t(h.nd)*16);s->len=(uint8_t*)malloc(h.nd);s->ix=(uint32_t*)malloc((size_t(h.rows)+1)*4);
 if(!s->dict||!s->len||!s->ix){close(s);return nullptr;}
 std::vector<uint8_t> decoded;if(!dpack::unpack(p+sizeof(h),h.ds,size_t(h.nd)*17,decoded)){close(s);return nullptr;}const uint8_t*q=decoded.data(),*e=q+decoded.size();
 __m128i previous=_mm_setzero_si128();
 for(unsigned i=0;i<h.nd;i++){if(q==e){close(s);return nullptr;}unsigned v=*q++,prefix=v>>4,suffix=(v&15)+1,l=prefix+suffix;if(l>16||size_t(e-q)<suffix||(!i&&prefix)||(i&&prefix>s->len[i-1])){close(s);return nullptr;}s->len[i]=l;previous=_mm_mask_loadu_epi8(previous,__mmask16(((1u<<suffix)-1)<<prefix),q-prefix);_mm_storeu_si128((__m128i*)(s->dict+i*16),previous);q+=suffix;}
 if(q!=e){close(s);return nullptr;}q=p+sizeof(h)+h.ds;e=q+h.is;uint32_t off=0;s->ix[0]=0;
 uint32_t cb[15];for(unsigned j=0;j<15;j++)if(!getv(q,e,cb[j])||cb[j]>h.cs){close(s);return nullptr;}const uint8_t*packed=q;if(size_t(e-q)<(size_t(h.rows)+1)/2){close(s);return nullptr;}q+=(size_t(h.rows)+1)/2;for(unsigned i=0;i<h.rows;i++){uint32_t c=(packed[i/2]>>((i&1)*4))&15,l;if(c==15){if(!getv(q,e,l)){close(s);return nullptr;}}else l=cb[c];if(l>h.cs-off){close(s);return nullptr;}off+=l;s->ix[i+1]=off;}
 if(q!=e||off!=h.cs){close(s);return nullptr;}s->data=e;return s;
}
static inline unsigned sym(State*s,uint64_t j){uint64_t bit=j*s->bits;uint32_t x;memcpy(&x,s->data+(bit>>3),4);return (x>>(bit&7))&s->mask;}
static inline bool segment(State*s,uint32_t pos,uint32_t end,uint8_t*&o,uint8_t*e){
 bool fast=size_t(e-o)>=size_t(end-pos)*16;
 if(fast){
  while(end-pos>=4){unsigned a=sym(s,pos),b=sym(s,pos+1),c=sym(s,pos+2),d=sym(s,pos+3);if((a|b|c|d)>=s->h.nd && (a>=s->h.nd||b>=s->h.nd||c>=s->h.nd||d>=s->h.nd))return false;unsigned la=s->len[a],lb=s->len[b],lc=s->len[c],ld=s->len[d];memcpy(o,s->dict+a*16,16);memcpy(o+la,s->dict+b*16,16);memcpy(o+la+lb,s->dict+c*16,16);memcpy(o+la+lb+lc,s->dict+d*16,16);o+=la+lb+lc+ld;pos+=4;}
  while(pos<end){unsigned c=sym(s,pos++);if(c>=s->h.nd)return false;unsigned l=s->len[c];memcpy(o,s->dict+c*16,16);o+=l;}
 }else{
  while(pos<end){unsigned c=sym(s,pos++);if(c>=s->h.nd)return false;unsigned l=s->len[c];if(size_t(e-o)<l)return false;if(e-o>=16)memcpy(o,s->dict+c*16,16);else memcpy(o,s->dict+c*16,l);o+=l;}
 }return true;
}
static int64_t decode(State*s,uint8_t*out,size_t cap){
 if(!s||cap<s->h.raw)return -1;uint8_t*o=out,*e=out+s->h.raw;
 uint32_t i=0;
 for(;s->h.cs-i>=16 && e-o>=256;i+=16)if(!segment(s,i,i+16,o,e))return -1;
 if(!segment(s,i,s->h.cs,o,e))return -1;
 return o==e?s->h.raw:-1;
}
static int64_t rows(State*s,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets){
 if(!s||(!offsets)||(!ids&&count)||(!out&&cap))return -1;uint8_t*o=out,*e=out+cap;offsets[0]=0;
 for(size_t i=0;i<count;i++){if(ids[i]>=s->h.rows||!segment(s,s->ix[ids[i]],s->ix[ids[i]+1],o,e))return -1;offsets[i+1]=o-out;}return o-out;
}
}
