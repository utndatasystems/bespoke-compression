#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <immintrin.h>
struct ByteHeader {uint32_t dictbytes,codes,wide,groups;};
#ifdef ENCODER
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <cmath>
struct ByteTrie {int child[256];int id;ByteTrie(){std::fill(child,child+256,-1);id=-1;}};
static void byte_build_trie(const std::vector<std::string>&d,std::vector<ByteTrie>&tr) {
 tr.clear();tr.emplace_back();for(int i=0;i<(int)d.size();++i){int t=0;for(unsigned char c:d[i]){if(tr[t].child[c]<0){tr[t].child[c]=tr.size();tr.emplace_back();}t=tr[t].child[c];}tr[t].id=i;}
}
static inline int byte_match(const uint8_t *r,size_t n,size_t i,const std::vector<ByteTrie>&tr) {
 int best=-1,t=0;for(size_t j=i;j<n&&j<i+32;j++){int nt=tr[t].child[r[j]];if(nt<0)break;t=nt;if(tr[t].id>=0)best=tr[t].id;}return best;
}
static bool byte_encode(const uint8_t*raw,size_t size,std::vector<uint8_t>&out,uint32_t&type,uint32_t&nrows) {
 std::vector<uint32_t> ri(1,0);for(size_t i=0;i<size;i++)if(raw[i]==10)ri.push_back(i+1);if(ri.back()!=size)ri.push_back(size);nrows=ri.size()-1;
 uint32_t freq[256]={};for(size_t i=0;i<size;i++)freq[raw[i]]++;
 std::vector<std::string> singles;for(int c=0;c<256;c++)if(freq[c]>=100)singles.emplace_back(1,c);
 if(singles.size()>240){singles.clear();for(int c=0;c<256;c++)if(freq[c]>=1000)singles.emplace_back(1,c);}
 std::vector<std::string>d=singles,bestd;std::vector<ByteTrie>tr;size_t bestcost=SIZE_MAX;
 for(int round=0;round<24;round++){
  byte_build_trie(d,tr);std::unordered_map<std::string,uint32_t>cnt;size_t nc=0;
  for(uint32_t r=0;r<nrows;r++){
   const uint8_t*s=raw+ri[r];size_t n=ri[r+1]-ri[r];std::vector<std::string>tokens;tokens.reserve(n);
   for(size_t i=0;i<n;){int best=byte_match(s,n,i,tr);if(best<0){tokens.emplace_back(1,s[i]);i++;nc++;}else{tokens.push_back(d[best]);i+=d[best].size();}}
   nc+=tokens.size();
   for(size_t i=0;i<tokens.size();i++){cnt[tokens[i]]+=2;if(i+1<tokens.size()&&tokens[i].size()+tokens[i+1].size()<=32)cnt[tokens[i]+tokens[i+1]]++;}
  }
  if(nc<bestcost){bestcost=nc;bestd=d;}
  std::vector<std::pair<double,std::string>>v;v.reserve(cnt.size());for(auto &x:cnt)if(x.first.size()>1)v.emplace_back(double(x.second)*(x.first.size()-1)/sqrt(double(x.first.size())),x.first);
  size_t k=std::min(v.size(),size_t(255-singles.size()));std::partial_sort(v.begin(),v.begin()+k,v.end(),std::greater<std::pair<double,std::string>>());
  std::vector<std::string>next=singles;for(size_t i=0;i<k;i++)next.push_back(std::move(v[i].second));
  if(next==d)break;d.swap(next);
 }
 d.swap(bestd);byte_build_trie(d,tr);
 std::vector<uint8_t>codes;codes.reserve(bestcost);std::vector<uint32_t>begins(nrows+1);uint32_t maxclen=0;
 for(uint32_t r=0;r<nrows;r++){
  begins[r]=codes.size();const uint8_t*s=raw+ri[r];size_t n=ri[r+1]-ri[r];
  for(size_t i=0;i<n;){int best=byte_match(s,n,i,tr);if(best<0){codes.push_back(255);codes.push_back(s[i++]);}else{codes.push_back(best);i+=d[best].size();}}
  maxclen=std::max(maxclen,uint32_t(codes.size()-begins[r]));
 }
 begins[nrows]=codes.size();if(maxclen>65535)return false;
 uint32_t db=0;for(auto&s:d)db+=s.size();ByteHeader h{db,uint32_t(codes.size()),maxclen>255?2u:1u,(nrows+15)/16};
 out.assign(sizeof(h)+256+db+(h.groups+1)*4+(size_t(h.groups)*16+16)*h.wide+codes.size()+32,0);memcpy(out.data(),&h,sizeof(h));
 uint8_t*p=out.data()+sizeof(h),*chars=p+256;uint32_t dp=0;for(size_t i=0;i<d.size();i++){p[i]=d[i].size();memcpy(chars+dp,d[i].data(),d[i].size());dp+=d[i].size();}
 p=chars+db;for(uint32_t g=0;g<h.groups;g++){uint32_t q=begins[g*16];memcpy(p+g*4,&q,4);}memcpy(p+h.groups*4,&h.codes,4);p+=(h.groups+1)*4;
 for(uint32_t r=0;r<nrows;r++){uint16_t q=begins[r+1]-begins[r];if(h.wide==1)p[r]=q;else memcpy(p+r*2,&q,2);}
 p+=(size_t(h.groups)*16+16)*h.wide;memcpy(p,codes.data(),codes.size());type=20;return true;
}
#else
struct ByteState {
 const uint8_t*codes;const uint8_t*lens;const uint8_t*index;
 uint32_t nrows,rawsize,ncodes,wide;
 alignas(32) uint8_t dict[256][32];uint8_t sizes[256]; uint64_t d8[256];uint32_t maxlen;
};
static void* byte_open(uint32_t type,const uint8_t*p,size_t n,uint32_t rows,uint64_t rawsize) {
 if(type!=20||n<sizeof(ByteHeader)+256)return nullptr;ByteHeader h;memcpy(&h,p,sizeof(h));
 if(h.wide<1||h.wide>2||h.groups!=(uint64_t(rows)+15)/16||h.dictbytes>8160)return nullptr;
 size_t need=sizeof(h)+256+h.dictbytes+(h.groups+1)*4+(size_t(h.groups)*16+16)*h.wide+h.codes+32;if(need!=n)return nullptr;
 ByteState*s=(ByteState*)aligned_alloc(32,(sizeof(ByteState)+31)&~size_t(31));if(!s)return nullptr;
 s->nrows=rows;s->rawsize=rawsize;s->ncodes=h.codes;s->wide=h.wide;const uint8_t*l=p+sizeof(h),*chars=l+256;uint32_t d=0;
 s->maxlen=0;memset(s->dict,0,sizeof(s->dict));for(int i=0;i<256;i++){if(l[i]>32||d+l[i]>h.dictbytes){free(s);return nullptr;}s->sizes[i]=l[i];memcpy(s->dict[i],chars+d,l[i]);d+=l[i];s->maxlen=s->maxlen>l[i]?s->maxlen:l[i];memcpy(s->d8+i,s->dict[i],8);}
 if(d!=h.dictbytes){free(s);return nullptr;}
 s->index=chars+d;s->lens=s->index+(h.groups+1)*4;s->codes=s->lens+(size_t(h.groups)*16+16)*h.wide;return s;
}
static inline uint32_t byte_index(const ByteState*s,uint32_t row) {
 uint32_t pos;memcpy(&pos,s->index+(row/16)*4,4);uint32_t k=row&15;
 if(s->wide==1) {
  __m128i a=_mm_loadu_si128((const __m128i*)(s->lens+(row&~15u)));
  __m128i mask=_mm_cmpgt_epi8(_mm_set1_epi8(k),_mm_setr_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15));
  __m128i sums=_mm_sad_epu8(_mm_and_si128(a,mask),_mm_setzero_si128());pos+=_mm_cvtsi128_si64(sums)+_mm_extract_epi64(sums,1);
 } else {const uint8_t*p=s->lens+(row&~15u)*2;for(uint32_t j=0;j<k;j++){uint16_t x;memcpy(&x,p+j*2,2);pos+=x;}}
 return pos;
}
static inline uint32_t byte_length(const ByteState*s,uint32_t row){if(s->wide==1)return s->lens[row];uint16_t x;memcpy(&x,s->lens+row*2,2);return x;}
static inline bool byte_has_escape(uint64_t x){uint64_t y=~x;return ((y-0x0101010101010101ULL)&~y&0x8080808080808080ULL)!=0;}
static inline uint8_t* byte_expand(const ByteState*s,const uint8_t*p,const uint8_t*end,uint8_t*out,uint8_t*limit) {
 if(s->maxlen<=8) {
 while(end-p>=8 && limit-out>=64) {
  uint64_t w;memcpy(&w,p,8);if(byte_has_escape(w)){unsigned c=*p++;if(c==255){if(p==end||out==limit)return nullptr;*out++=*p++;}else{memcpy(out,s->dict[c],32);out+=s->sizes[c];}continue;}
  for(int j=0;j<8;j++){unsigned c=uint8_t(w>>(j*8));memcpy(out,s->d8+c,8);out+=s->sizes[c];}p+=8;

 }
}
while(end-p>=8 && limit-out>=256) {
  uint64_t w;memcpy(&w,p,8);if(byte_has_escape(w)){unsigned c=*p++;if(c==255){if(p==end||out==limit)return nullptr;*out++=*p++;}else{memcpy(out,s->dict[c],32);out+=s->sizes[c];}continue;}
  uint8_t a=w,b=w>>8,c=w>>16,d=w>>24,e=w>>32,f=w>>40,g=w>>48,h=w>>56;
  memcpy(out,s->dict[a],32);out+=s->sizes[a];memcpy(out,s->dict[b],32);out+=s->sizes[b];memcpy(out,s->dict[c],32);out+=s->sizes[c];memcpy(out,s->dict[d],32);out+=s->sizes[d];
  memcpy(out,s->dict[e],32);out+=s->sizes[e];memcpy(out,s->dict[f],32);out+=s->sizes[f];memcpy(out,s->dict[g],32);out+=s->sizes[g];memcpy(out,s->dict[h],32);out+=s->sizes[h];p+=8;
 }
 for(;p<end;p++){
  unsigned c=*p;if(c==255){if(++p>=end||out>=limit)return nullptr;*out++=*p;}
  else{size_t z=s->sizes[c];if(size_t(limit-out)<z)return nullptr;if(limit-out>=32)memcpy(out,s->dict[c],32);else memcpy(out,s->dict[c],z);out+=z;}
 }
 return out;
}
static int64_t byte_decode_state(void*st,uint8_t*out,size_t capacity) {
 ByteState*s=(ByteState*)st;if(capacity<s->rawsize)return -1;
 uint8_t*p=byte_expand(s,s->codes,s->codes+s->ncodes,out,out+s->rawsize);return p&&size_t(p-out)==s->rawsize?p-out:-1;
}
static int64_t byte_rows_state(void*st,const uint64_t*ids,size_t count,uint8_t*out,size_t capacity,uint64_t*offs) {
 ByteState*s=(ByteState*)st;uint8_t*p=out;offs[0]=0;if(!count)return 0;uint64_t previous=UINT64_MAX;uint32_t next=0;
 for(size_t i=0;i<count;i++){if(ids[i]>=s->nrows)return -1;uint32_t row=ids[i],start=(previous!=UINT64_MAX && ids[i]==previous+1)?next:byte_index(s,row),len=byte_length(s,row);previous=ids[i];next=start+len;if(start>s->ncodes||len>s->ncodes-start)return -1;
  p=byte_expand(s,s->codes+start,s->codes+start+len,p,out+capacity);if(!p)return -1;offs[i+1]=p-out;
 }return p-out;
}
static void byte_close(void*st){free(st);}
#endif
