#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <immintrin.h>
struct Text16State { const uint8_t *payload,*counts,*stream; const uint32_t *blocks; uint8_t *dict; uint32_t *lengths; uint32_t nd,nt,nrows,width,bits,mask; size_t raw_size; };
static void text16_close(void *vp){ auto*s=(Text16State*)vp;if(s){free(s->dict);free(s->lengths);free(s);} }
static void* text16_open(uint32_t type,const uint8_t*p,size_t n,uint32_t nr,size_t raw_size){
 if(type!=11||n<16)return nullptr;uint32_t h[4];memcpy(h,p,16); if(h[0]==0||h[0]>65536||(h[2]&255)<1||(h[2]&255)>2||(h[2]>>8)>16||(h[2]>>8)<1||h[3]!=nr)return nullptr;
 auto*s=(Text16State*)calloc(1,sizeof(Text16State));if(!s)return nullptr;s->nd=h[0];s->nt=h[1];s->width=h[2]&255;s->bits=h[2]>>8;s->mask=(1u<<s->bits)-1;s->nrows=nr;s->raw_size=raw_size;s->payload=p;
 s->dict=(uint8_t*)aligned_alloc(16,(size_t)s->nd*16);s->lengths=(uint32_t*)malloc((size_t)s->nd*4);if(!s->dict||!s->lengths){text16_close(s);return nullptr;}
 size_t pos=16;for(uint32_t i=0;i<s->nd;i++){if(pos>=n){text16_close(s);return nullptr;}uint8_t z=p[pos++];if(!z||z>16||z>n-pos){text16_close(s);return nullptr;}s->lengths[i]=z;if(n-pos>=16)_mm_store_si128((__m128i*)(s->dict+(size_t)i*16),_mm_loadu_si128((const __m128i*)(p+pos)));else{memset(s->dict+(size_t)i*16,0,16);memcpy(s->dict+(size_t)i*16,p+pos,z);}pos+=z;}
 size_t bs=((size_t)nr+31)/32+1; if(bs*4+(size_t)nr*s->width+((size_t)s->nt*s->bits+7)/8+8!=n-pos){text16_close(s);return nullptr;}
 s->blocks=(const uint32_t*)(p+pos);pos+=bs*4;s->counts=p+pos;pos+=(size_t)nr*s->width;s->stream=p+pos;return s;
}
static uint32_t text16_count(const Text16State*s,uint32_t r){if(s->width==1)return s->counts[r];uint16_t z;memcpy(&z,s->counts+(size_t)r*2,2);return z;}
static uint32_t text16_start(const Text16State*s,uint32_t r){uint32_t z;memcpy(&z,(const uint8_t*)s->blocks+(r>>5)*4,4);uint32_t q=r&31;if(s->width==1){__m256i v=_mm256_maskz_loadu_epi8(((__mmask32)1<<q)-1,s->counts+(r&~31));__m256i sums=_mm256_sad_epu8(v,_mm256_setzero_si256());__m128i a=_mm_add_epi64(_mm256_castsi256_si128(sums),_mm256_extracti128_si256(sums,1));z+=(uint32_t)(_mm_cvtsi128_si64(a)+_mm_extract_epi64(a,1));}else for(uint32_t i=r&~31;i<r;i++)z+=text16_count(s,i);return z;}
static uint32_t text16_token(const Text16State*s,uint32_t i){size_t b=(size_t)i*s->bits;uint32_t v;memcpy(&v,s->stream+(b>>3),4);return (v>>(b&7))&s->mask;}
static int64_t text16_tokens(const Text16State*s,uint32_t start,uint32_t nt,uint8_t*out,size_t cap){
 if(start>s->nt||nt>s->nt-start)return -1;uint8_t*d=out;uint8_t*safeend=cap>=16?out+cap-16:out;
 uint32_t i=0;for(;nt-i>=4;i+=4){uint32_t a=text16_token(s,start+i),b=text16_token(s,start+i+1),c=text16_token(s,start+i+2),e=text16_token(s,start+i+3);if(a>=s->nd||b>=s->nd||c>=s->nd||e>=s->nd)return -1;uint32_t la=s->lengths[a],lb=s->lengths[b],lc=s->lengths[c],le=s->lengths[e];uint32_t z=la+lb+lc+le;if(z>cap-(size_t)(d-out))return -1;if(cap>=16&&d+la+lb+lc<=safeend){_mm_storeu_si128((__m128i*)d,_mm_load_si128((const __m128i*)(s->dict+(size_t)a*16)));d+=la;_mm_storeu_si128((__m128i*)d,_mm_load_si128((const __m128i*)(s->dict+(size_t)b*16)));d+=lb;_mm_storeu_si128((__m128i*)d,_mm_load_si128((const __m128i*)(s->dict+(size_t)c*16)));d+=lc;_mm_storeu_si128((__m128i*)d,_mm_load_si128((const __m128i*)(s->dict+(size_t)e*16)));d+=le;}else{memcpy(d,s->dict+(size_t)a*16,la);d+=la;memcpy(d,s->dict+(size_t)b*16,lb);d+=lb;memcpy(d,s->dict+(size_t)c*16,lc);d+=lc;memcpy(d,s->dict+(size_t)e*16,le);d+=le;}}
 for(;i<nt;i++){uint32_t a=text16_token(s,start+i);if(a>=s->nd)return -1;uint32_t z=s->lengths[a];if(z>cap-(size_t)(d-out))return -1;if(d<=safeend&&cap>=16)_mm_storeu_si128((__m128i*)d,_mm_load_si128((const __m128i*)(s->dict+(size_t)a*16)));else memcpy(d,s->dict+(size_t)a*16,z);d+=z;}return d-out;
}
template<unsigned B,unsigned K> static inline uint32_t text16_unpack(uint64_t lo,uint64_t hi){constexpr unsigned sh=B*K;constexpr uint32_t mask=(1u<<B)-1;if constexpr(sh+B<=64)return (lo>>sh)&mask;else if constexpr(sh>=64)return (hi>>(sh-64))&mask;else return ((lo>>sh)|(hi<<(64-sh)))&mask;}
template<unsigned B> static int64_t text16_bulk(const Text16State*s,uint8_t*out,size_t cap){
 uint8_t*d=out;uint32_t i=0;for(;s->nt-i>=8;i+=8){const uint8_t*p=s->stream+(size_t)i*B/8;uint64_t lo,hi;memcpy(&lo,p,8);memcpy(&hi,p+8,8);
 uint32_t a[8]={text16_unpack<B,0>(lo,hi),text16_unpack<B,1>(lo,hi),text16_unpack<B,2>(lo,hi),text16_unpack<B,3>(lo,hi),text16_unpack<B,4>(lo,hi),text16_unpack<B,5>(lo,hi),text16_unpack<B,6>(lo,hi),text16_unpack<B,7>(lo,hi)};uint32_t l[8],sum=0;
 #pragma GCC unroll 8
 for(int j=0;j<8;j++){if(a[j]>=s->nd)return -1;l[j]=s->lengths[a[j]];sum+=l[j];}
 if(cap-(size_t)(d-out)<sum+16){int64_t z=text16_tokens(s,i,s->nt-i,d,cap-(d-out));return z<0?-1:(d-out)+z;}
 #pragma GCC unroll 8
 for(int j=0;j<8;j++){_mm_storeu_si128((__m128i*)d,_mm_load_si128((const __m128i*)(s->dict+(size_t)a[j]*16)));d+=l[j];}
 }
 int64_t z=text16_tokens(s,i,s->nt-i,d,cap-(d-out));return z<0?-1:(d-out)+z;
}
static int64_t text16_decode_state(void*vp,uint8_t*out,size_t cap){auto*s=(Text16State*)vp;if(!s||cap<s->raw_size)return -1;switch(s->bits){case 11:return text16_bulk<11>(s,out,cap);case 12:return text16_bulk<12>(s,out,cap);case 13:return text16_bulk<13>(s,out,cap);case 14:return text16_bulk<14>(s,out,cap);case 15:return text16_bulk<15>(s,out,cap);case 16:return text16_bulk<16>(s,out,cap);default:return text16_tokens(s,0,s->nt,out,cap);}}
template<unsigned B> static inline uint32_t text16_row_token(const Text16State*s,uint32_t i){size_t b=(size_t)i*B;uint32_t v;memcpy(&v,s->stream+(b>>3),4);return (v>>(b&7))&((1u<<B)-1);}
template<unsigned B> static int64_t text16_fast_row(const Text16State*s,uint32_t start,uint32_t nt,uint8_t*out,size_t cap){
 if(start>s->nt||nt>s->nt-start)return -1;if((uint64_t)nt*16>cap)return text16_tokens(s,start,nt,out,cap);uint8_t*d=out;uint32_t i=0;
 for(;nt-i>=4;i+=4){uint32_t a=text16_row_token<B>(s,start+i),b=text16_row_token<B>(s,start+i+1),c=text16_row_token<B>(s,start+i+2),e=text16_row_token<B>(s,start+i+3);if(a>=s->nd||b>=s->nd||c>=s->nd||e>=s->nd)return -1;uint32_t la=s->lengths[a],lb=s->lengths[b],lc=s->lengths[c],le=s->lengths[e];_mm_storeu_si128((__m128i*)d,_mm_load_si128((const __m128i*)(s->dict+(size_t)a*16)));d+=la;_mm_storeu_si128((__m128i*)d,_mm_load_si128((const __m128i*)(s->dict+(size_t)b*16)));d+=lb;_mm_storeu_si128((__m128i*)d,_mm_load_si128((const __m128i*)(s->dict+(size_t)c*16)));d+=lc;_mm_storeu_si128((__m128i*)d,_mm_load_si128((const __m128i*)(s->dict+(size_t)e*16)));d+=le;}
 for(;i<nt;i++){uint32_t a=text16_row_token<B>(s,start+i);if(a>=s->nd)return -1;_mm_storeu_si128((__m128i*)d,_mm_load_si128((const __m128i*)(s->dict+(size_t)a*16)));d+=s->lengths[a];}return d-out;
}
template<unsigned B> static int64_t text16_rows_impl(Text16State*s,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offs){
 size_t pos=0;offs[0]=0;uint32_t nextrow=~0u,nexttoken=0;for(size_t i=0;i<count;i++){if(ids[i]>=s->nrows)return -1;uint32_t r=(uint32_t)ids[i];uint32_t st=r==nextrow?nexttoken:text16_start(s,r);uint32_t cnt=text16_count(s,r);int64_t z=text16_fast_row<B>(s,st,cnt,out+pos,cap-pos);if(z<0)return -1;pos+=(size_t)z;offs[i+1]=pos;nextrow=r+1;nexttoken=st+cnt;}return pos;
}
static int64_t text16_rows_state(void*vp,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offs){auto*s=(Text16State*)vp;if(!s)return -1;switch(s->bits){case 11:return text16_rows_impl<11>(s,ids,count,out,cap,offs);case 12:return text16_rows_impl<12>(s,ids,count,out,cap,offs);case 13:return text16_rows_impl<13>(s,ids,count,out,cap,offs);case 14:return text16_rows_impl<14>(s,ids,count,out,cap,offs);case 15:return text16_rows_impl<15>(s,ids,count,out,cap,offs);case 16:return text16_rows_impl<16>(s,ids,count,out,cap,offs);default:return -1;}}
#ifdef ENCODER
#include <unordered_map>
#include <string>
#include <cstdio>
#include <array>
static bool text16_encode(const uint8_t*raw,size_t size,std::vector<uint8_t>&payload,uint32_t&type,uint32_t&nrows){
 std::vector<std::string> dict;dict.reserve(65536);for(int i=0;i<256;i++)dict.emplace_back(1,(char)i);
 std::vector<uint16_t> stream;stream.reserve(size);for(size_t i=0;i<size;i++)stream.push_back(raw[i]);nrows=0;for(size_t i=0;i<size;i++)if(raw[i]=='\n')nrows++;if(size&&raw[size-1]!='\n')nrows++;
 std::vector<uint16_t> tmp;tmp.reserve(size);uint32_t prevsize=stream.size();
 for(int round=0;round<48&&dict.size()<65536;round++){
  std::unordered_map<uint32_t,uint32_t> counts;counts.reserve(std::min((size_t)1000000,stream.size()/2));
  for(size_t i=0;i+1<stream.size();i++){uint16_t a=stream[i],b=stream[i+1];if(dict[a].back()!='\n'&&dict[a].size()+dict[b].size()<=16)counts[((uint32_t)a<<16)|b]++;}
  std::vector<std::pair<int64_t,uint32_t>> cand;cand.reserve(counts.size());for(auto &v:counts){uint32_t a=v.first>>16,b=v.first&65535;int64_t score=(int64_t)v.second*2-(dict[a].size()+dict[b].size()+1);if(v.second>=4&&score>4)cand.emplace_back(score,v.first);}
  size_t take=std::min((size_t)(round<4?512:2048),std::min(cand.size(),(size_t)65536-dict.size()));if(!take)break;if(take<cand.size())std::nth_element(cand.begin(),cand.begin()+take,cand.end(),std::greater<>());cand.resize(take);std::sort(cand.begin(),cand.end(),std::greater<>());
  std::unordered_map<uint32_t,uint16_t> rules;rules.reserve(take*2);for(auto &v:cand){uint32_t a=v.second>>16,b=v.second&65535;rules.emplace(v.second,(uint16_t)dict.size());dict.push_back(dict[a]+dict[b]);}
  tmp.clear();for(size_t i=0;i<stream.size();i++){if(i+1<stream.size()){auto it=rules.find(((uint32_t)stream[i]<<16)|stream[i+1]);if(it!=rules.end()){tmp.push_back(it->second);i++;continue;}}tmp.push_back(stream[i]);}stream.swap(tmp);
  if(prevsize-stream.size()<16)break;prevsize=stream.size();
 }

 {std::vector<uint32_t> preliminary(dict.size());for(auto x:stream)preliminary[x]++;
 struct Node {std::array<int,256> next;int sym;Node():sym(-1){next.fill(-1);}};
 std::vector<Node> nodes(1);for(size_t k=0;k<dict.size();k++)if(k<256||preliminary[k]){int v=0;for(unsigned char c:dict[k]){int n=nodes[v].next[c];if(n<0){n=nodes.size();nodes[v].next[c]=n;nodes.emplace_back();}v=n;}nodes[v].sym=k;}
 std::vector<uint32_t> best(size+1,~0u);std::vector<uint16_t> choice(size);best[size]=0;
 for(size_t at=size;at-->0;){int v=0;for(size_t j=at;j<size&&j<at+16;j++){v=nodes[v].next[raw[j]];if(v<0)break;int sym=nodes[v].sym;if(sym>=0&&best[j+1]!=~0u&&best[j+1]+1<=best[at]){best[at]=best[j+1]+1;choice[at]=sym;}}}
 stream.clear();for(size_t at=0;at<size;){uint16_t sym=choice[at];stream.push_back(sym);at+=dict[sym].size();}
 }
 std::vector<uint32_t> uses(dict.size());for(auto x:stream)uses[x]++;std::vector<uint16_t> renum(dict.size()),order;for(size_t i=0;i<dict.size();i++)if(uses[i])order.push_back(i);std::sort(order.begin(),order.end(),[&](uint16_t a,uint16_t b){return uses[a]!=uses[b]?uses[a]>uses[b]:a<b;});uint32_t nd=0;size_t dictsize=0;for(auto i:order){renum[i]=nd++;dictsize+=dict[i].size()+1;}
 std::vector<uint32_t> rc;rc.reserve(nrows);uint32_t x=0,maxcnt=0;for(auto tok:stream){x++;if(dict[tok].back()=='\n'){rc.push_back(x);maxcnt=std::max(maxcnt,x);x=0;}}if(x){rc.push_back(x);maxcnt=std::max(maxcnt,x);}if(rc.size()!=nrows||maxcnt>65535)return false;
 uint32_t width=maxcnt>255?2:1,nt=stream.size();payload.clear();payload.reserve(16+dictsize+((nrows+31)/32+1)*4+nrows*width+nt*2);auto append=[&](const void*p,size_t n){auto*b=(const uint8_t*)p;payload.insert(payload.end(),b,b+n);};uint32_t bits=1;while((1u<<bits)<nd)bits++;uint32_t hdr[]={nd,nt,width|(bits<<8),nrows};append(hdr,16);
 for(auto i:order){uint8_t l=dict[i].size();append(&l,1);append(dict[i].data(),l);}
 uint32_t sum=0;for(size_t i=0;i<rc.size();i++){if((i&31)==0)append(&sum,4);sum+=rc[i];}append(&sum,4);
 for(auto c:rc){if(width==1){uint8_t b=c;append(&b,1);}else{uint16_t b=c;append(&b,2);}}
 size_t at=payload.size();payload.resize(at+((size_t)nt*bits+7)/8+8);size_t bit=0;for(auto c:stream){uint32_t r=(uint32_t)renum[c]<<(bit&7);uint32_t v;memcpy(&v,payload.data()+at+(bit>>3),4);v|=r;memcpy(payload.data()+at+(bit>>3),&v,4);bit+=bits;}type=11;fprintf(stderr,"text: %zu -> %zu; nd=%u tokens=%u rows=%u\n",size,payload.size(),nd,nt,nrows);return true;
}
#endif
