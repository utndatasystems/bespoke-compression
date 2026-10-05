
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <immintrin.h>
#ifdef ENCODER
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#endif
namespace gdp {
struct H{uint32_t mode,rows;uint64_t raw,total;uint32_t nd,nt;uint64_t reserved[4];};
#ifdef ENCODER
inline std::vector<uint8_t> encode(const uint8_t*p,size_t n) {
 std::vector<std::string> dict; for(int c=0;c<256;c++)dict.push_back(std::string(1,char(c)));
 std::vector<uint16_t>s; s.reserve(n+n/10); uint32_t nr=0;
 for(size_t i=0;i<n;i++){s.push_back(p[i]);if(p[i]==10){s.push_back(65535);nr++;}}
 if(n&&p[n-1]!=10){s.push_back(65535);nr++;}
 // Batched adjacent phrase substitution. No external dictionary or model.
 for(int round=0;dict.size()<16384;round++){
   std::unordered_map<uint32_t,uint32_t>counts;counts.reserve(s.size()/4);
   for(size_t i=1;i<s.size();i++)if(s[i]!=65535&&s[i-1]!=65535&&dict[s[i]].size()+dict[s[i-1]].size()<=16)counts[uint32_t(s[i-1])<<16|s[i]]++;
   std::vector<std::pair<uint32_t,uint32_t>> cand; cand.reserve(counts.size());
   for(auto x:counts)if(x.second>=6)cand.emplace_back(x.second,x.first);
   if(cand.empty())break;
   size_t k=std::min(size_t(256),std::min(size_t(16384-dict.size()),cand.size()));
   std::partial_sort(cand.begin(),cand.begin()+k,cand.end(),std::greater<std::pair<uint32_t,uint32_t>>());
   std::unordered_map<uint32_t,uint16_t> merge;
   for(size_t j=0;j<k;j++){uint32_t pair=cand[j].second;merge[pair]=dict.size();dict.push_back(dict[pair>>16]+dict[pair&65535]);}
   size_t wr=0;
   for(size_t i=0;i<s.size();i++){
     if(i+1<s.size()&&s[i]!=65535&&s[i+1]!=65535){
       auto f=merge.find(uint32_t(s[i])<<16|s[i+1]);
       if(f!=merge.end()){s[wr++]=f->second;i++;continue;}
     }
     s[wr++]=s[i];
   }
   s.resize(wr);
 }

 // Reparse the original bytes optimally over the learned phrase vocabulary.
 struct TN { std::unordered_map<uint8_t,uint32_t> child;int symbol=-1; };
 std::vector<TN> trie(1);
 for(size_t j=0;j<dict.size();j++){uint32_t node=0;for(uint8_t c:dict[j]){auto f=trie[node].child.find(c);if(f==trie[node].child.end()){uint32_t v=trie.size();trie[node].child[c]=v;trie.emplace_back();node=v;}else node=f->second;}trie[node].symbol=j;}
 std::vector<uint32_t>cost(n+1);std::vector<uint16_t>choice(n);
 for(size_t i=n;i--;){uint32_t node=0,best=UINT32_MAX;unsigned sym=p[i];
  for(size_t j=i;j<n&&j<i+16;j++){auto f=trie[node].child.find(p[j]);if(f==trie[node].child.end())break;node=f->second;if(trie[node].symbol>=0&&cost[j+1]+1<=best){best=cost[j+1]+1;sym=trie[node].symbol;}}
  cost[i]=best;choice[i]=sym;
 }
 s.clear();
 for(size_t i=0;i<n;){unsigned sym=choice[i];s.push_back(sym);i+=dict[sym].size();if(i==n||p[i-1]==10)s.push_back(65535);}
 std::vector<uint32_t> use(dict.size());for(auto x:s)if(x!=65535)use[x]++;
 std::vector<uint16_t>remap(dict.size());std::vector<std::string>d;
 for(size_t j=0;j<dict.size();j++)if(use[j]){remap[j]=d.size();d.push_back(dict[j]);}
 uint32_t nd=d.size(),nt=s.size()-nr,blocks=(nr+15)/16;uint64_t db=0;for(auto&x:d)db+=x.size();
 H h{};h.mode=6;h.rows=nr;h.raw=n;h.nd=nd;h.nt=nt;
 h.reserved[0]=db;h.total=sizeof(H)+db+nd+uint64_t(blocks)*4+nr+16+(uint64_t(nt)*7+3)/4+32;
 std::vector<uint8_t>a(h.total);memcpy(a.data(),&h,sizeof h);
 uint8_t*dictp=a.data()+sizeof h;uint8_t*lens=dictp+db;
 for(size_t j=0,pos=0;j<nd;j++){memcpy(dictp+pos,d[j].data(),d[j].size());lens[j]=d[j].size();pos+=d[j].size();}
 uint8_t* bp=lens+nd;uint8_t*rl=bp+blocks*4;uint8_t*tok=rl+nr+16;
 uint32_t r=0,t=0,len=0;
 for(auto x:s){if(x==65535){if(len>255)return {};rl[r]=len;if(!(r&15)){uint32_t z=t-len;memcpy(bp+(r/16)*4,&z,4);}r++;len=0;}else{uint32_t z=uint32_t(remap[x])<<((t*6)&7);size_t pos=t*7/4;tok[pos]|=uint8_t(z);tok[pos+1]|=uint8_t(z>>8);tok[pos+2]|=uint8_t(z>>16);t++;len++;}}
 return a;
}
#endif
#ifdef DECODER
struct State{H h;const uint8_t*dict,*lens,*bp,*rl,*tok;};
inline uint32_t load32(const void*p){uint32_t v;memcpy(&v,p,4);return v;}
inline uint16_t load16(const void*p){uint16_t v;memcpy(&v,p,2);return v;}
inline void* open(const uint8_t*p,size_t n){
 if(n<sizeof(H))return nullptr;H h;memcpy(&h,p,sizeof h);
 uint64_t blocks=(uint64_t(h.rows)+15)/16;
 if(h.mode!=6||h.nd>16384||!h.nd||h.raw>UINT32_MAX||h.reserved[0]>uint64_t(h.nd)*16||h.reserved[0]<h.nd||h.total!=sizeof(H)+h.reserved[0]+h.nd+blocks*4+h.rows+16+(uint64_t(h.nt)*7+3)/4+32||h.total>n)return nullptr;
 State*s=(State*)malloc(sizeof(State)+16384*17);if(!s)return nullptr;
 s->h=h;s->dict=(const uint8_t*)(s+1);s->lens=s->dict+16384*16;
 const uint8_t*archive_lens=p+sizeof(H)+h.reserved[0];
 s->bp=archive_lens+h.nd;s->rl=s->bp+blocks*4;s->tok=s->rl+h.rows+16;
 uint8_t* __restrict expanded=(uint8_t*)s->dict;
 uint8_t* __restrict lengths=(uint8_t*)s->lens;
 memset(expanded+h.nd*16,0,(16384-h.nd)*16);
 memcpy(lengths,archive_lens,h.nd);
 memset(lengths+h.nd,0,16384-h.nd);
 uint64_t pos=0;
 for(uint32_t i=0;i<h.nd;i++){
  unsigned len=archive_lens[i];
  if(len<1||len>16||pos+len>h.reserved[0]){free(s);return nullptr;}
  memcpy(expanded+i*16,p+sizeof(H)+pos,16);pos+=len;
 }
 if(pos!=h.reserved[0]){free(s);return nullptr;}
 return s;
}

inline uint32_t token(const uint8_t*p,uint32_t i){return (load32(p+uint64_t(i)*7/4)>>((i*6)&7))&16383;}
inline int64_t decode(void*v,uint8_t* __restrict out,size_t cap){
 State*s=(State*)v;if(!s||cap<s->h.raw)return -1;
 const uint8_t* __restrict dict=s->dict;const uint8_t* __restrict lens=s->lens;const uint8_t* __restrict tok=s->tok;
 const uint32_t nd=s->h.nd;const size_t nt=s->h.nt;const uint64_t raw=s->h.raw;
 uint8_t*d=out;size_t i=0;
 // Worst-case16-byte token size proves every speculative store in this chunk
 // stays within capacity, so the inner loop needs no per-iteration cap branch.
 while(nt-i>=4 && cap-size_t(d-out)>=64){
  size_t block=(nt-i)&~size_t(3),budget=((cap-size_t(d-out))/16)&~size_t(3);
  if(block>budget)block=budget;
  size_t end=i+block;
  for(;i<end;i+=4){
   uint64_t z;memcpy(&z,tok+i*7/4,8);unsigned a=z&16383,b=(z>>14)&16383,c=(z>>28)&16383,e=(z>>42)&16383;
   unsigned la=lens[a],lb=lens[b],lc=lens[c],le=lens[e];
   _mm_storeu_si128((__m128i*)d,_mm_loadu_si128((const __m128i*)(dict+a*16)));
   _mm_storeu_si128((__m128i*)(d+la),_mm_loadu_si128((const __m128i*)(dict+b*16)));
   _mm_storeu_si128((__m128i*)(d+la+lb),_mm_loadu_si128((const __m128i*)(dict+c*16)));
   _mm_storeu_si128((__m128i*)(d+la+lb+lc),_mm_loadu_si128((const __m128i*)(dict+e*16)));
   d+=la+lb+lc+le;
  }
 }
 for(;i<nt;i++){
  unsigned x=token(tok,i);
  unsigned l=lens[x];if(cap-size_t(d-out)<l)return -1;
  memcpy(d,dict+x*16,l);d+=l;
 }
 return size_t(d-out)==raw?d-out:-1;
}
inline uint32_t start(State*s,uint32_t r){
 uint32_t t=load32(s->bp+(r/16)*4),k=r&15;
 __m128i lens=_mm_loadu_si128((const __m128i*)(s->rl+(r&~15u)));
 __m128i mask=_mm_cmpgt_epi8(_mm_set1_epi8(k),_mm_setr_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15));
 __m128i sum=_mm_sad_epu8(_mm_and_si128(lens,mask),_mm_setzero_si128());
 return t+_mm_cvtsi128_si64(sum)+_mm_extract_epi64(sum,1);
}
inline int64_t rows_accumulator(void*v,const uint64_t* __restrict ids,size_t count,uint8_t* __restrict out,size_t cap,uint64_t* __restrict offs){
 State*s=(State*)v;if(!s||!offs)return -1;
 offs[0]=0;uint8_t*d=out;
 const uint8_t* __restrict dict=s->dict;const uint8_t* __restrict lens=s->lens;const uint8_t* __restrict tok=s->tok;
 const uint8_t* __restrict rl=s->rl;const uint32_t nr=s->h.rows,nt=s->h.nt;
 unsigned bad=0;
 for(size_t r=0;r<count;r++){
  if(ids[r]>=nr)return -1;uint32_t row=ids[r],t=start(s,row),num=rl[row];if(uint64_t(t)+num>nt)return -1;
  if(cap-size_t(d-out)>=size_t(num)*16){
   for(unsigned j=0;j<num;j++){
    unsigned x=token(tok,t+j),l=lens[x];bad|=l-1;
    _mm_storeu_si128((__m128i*)d,_mm_loadu_si128((const __m128i*)(dict+x*16)));d+=l;
   }
  }else for(unsigned j=0;j<num;j++){
   unsigned x=token(tok,t+j),l=lens[x];bad|=l-1;
   if(cap-size_t(d-out)<l)return -1;memcpy(d,dict+x*16,l);d+=l;
  }
  offs[r+1]=d-out;
 }
 return bad&0x80000000u?-1:d-out;
}
inline int64_t rows(void*v,const uint64_t* __restrict ids,size_t count,uint8_t* __restrict out,size_t cap,uint64_t* __restrict offs){
 State*s=(State*)v;if(!s||!offs)return -1;
 offs[0]=0;uint8_t*d=out;
 const uint8_t* __restrict dict=s->dict;const uint8_t* __restrict lens=s->lens;const uint8_t* __restrict tok=s->tok;
 const uint8_t* __restrict rl=s->rl;const uint32_t nr=s->h.rows,nt=s->h.nt;
 unsigned bad=0;
 for(size_t r=0;r<count;r++){
  if(ids[r]>=nr)return -1;uint32_t row=ids[r],t=start(s,row),num=rl[row];if(uint64_t(t)+num>nt)return -1;
  if(cap-size_t(d-out)>=size_t(num)*16){
   for(unsigned j=0;j<num;j++){
    unsigned x=token(tok,t+j),l=lens[x];if(!l)return -1;
    _mm_storeu_si128((__m128i*)d,_mm_loadu_si128((const __m128i*)(dict+x*16)));d+=l;
   }
  }else for(unsigned j=0;j<num;j++){
   unsigned x=token(tok,t+j),l=lens[x];if(!l)return -1;
   if(cap-size_t(d-out)<l)return -1;memcpy(d,dict+x*16,l);d+=l;
  }
  offs[r+1]=d-out;
 }
 return bad&0x80000000u?-1:d-out;
}
inline void close(void*v){free(v);}
#endif
}
