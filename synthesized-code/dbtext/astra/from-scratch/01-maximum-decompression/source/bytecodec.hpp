#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <immintrin.h>
#ifdef ENCODER
#include <vector>
#include <array>
#include <unordered_map>
#include <algorithm>
#include <string>
#endif
namespace bc {
struct __attribute__((packed)) H {uint32_t mode,rows;uint64_t rawsize,total,bytes;uint32_t flags,index16;uint64_t val[256];};
static_assert(sizeof(H)==2088);
inline void w64(void*p,uint64_t v){memcpy(p,&v,8);}
inline uint64_t r64(const void*p){uint64_t v;memcpy(&v,p,8);return v;}
inline uint32_t r32(const void*p){uint32_t v;memcpy(&v,p,4);return v;}
#ifdef ENCODER
struct Key {uint64_t v;uint8_t n;bool operator==(const Key&o)const{return v==o.v&&n==o.n;}};
struct Hash {size_t operator()(const Key&k)const {uint64_t v=k.v^(uint64_t(k.n)*0x9e3779b97f4a7c15ULL);v^=v>>27;v*=0x3c79ac492ba7b653ULL;v^=v>>33;return v;}};
struct Node {std::array<int,256> next;int code;Node():code(-1){next.fill(-1);}};
struct Trie {
 std::vector<Node>t;
 Trie(const std::vector<Key>&keys){t.emplace_back();for(size_t c=0;c<keys.size();c++){int p=0;for(int i=0;i<keys[c].n;i++){unsigned b=(keys[c].v>>(8*i))&255;int v=t[p].next[b];if(v<0){v=t.size();t[p].next[b]=v;t.emplace_back();}p=v;}t[p].code=c;}}
 inline int match(const uint8_t*p,const uint8_t*end,unsigned&n)const {int v=0,c=-1;n=1;unsigned at=0;while(p+at<end&&at<7){v=t[v].next[p[at]];if(v<0)break;at++;if(t[v].code>=0){c=t[v].code;n=at;}}return c;}
};
inline std::vector<uint8_t> encode(const uint8_t*p,size_t n){
 if(n>UINT32_MAX)return {};
 std::vector<size_t>rows{0};std::array<uint64_t,256>counts{};for(size_t i=0;i<n;i++){counts[p[i]]++;if(p[i]=='\n')rows.push_back(i+1);}if(rows.back()!=n)rows.push_back(n);
 size_t nr=rows.size()-1;std::vector<Key>alphabet;for(unsigned b=0;b<256;b++)if(counts[b])alphabet.push_back({b,1});
 bool noescape=alphabet.size()<=255; std::vector<Key>keys=alphabet;if(keys.size()>255)keys.resize(255);
 // An evenly spaced row sample gives names and randomly ordered columns representative coverage.
 std::vector<uint8_t>sample;std::vector<size_t>sr{0};size_t step=std::max<size_t>(1,n/350000);for(size_t r=0;r<nr;r+=step){sample.insert(sample.end(),p+rows[r],p+rows[r+1]);sr.push_back(sample.size());}
 double best=1e100;std::vector<Key>bestkeys;
 for(int iter=0;iter<9;iter++){
  Trie trie(keys);std::unordered_map<Key,uint64_t,Hash>freq;freq.reserve(160000);uint64_t coded=0;
  for(size_t row=0;row+1<sr.size();row++){
   const uint8_t*q=sample.data()+sr[row],*end=sample.data()+sr[row+1];Key prev{};bool hasprev=false;
   while(q<end){unsigned len;int c=trie.match(q,end,len);Key k=c>=0?keys[c]:Key{*q,1};coded+=c>=0?1:2;freq[k]+=4;
    if(hasprev&&prev.n+k.n<=7){Key pair{prev.v|(k.v<<(prev.n*8)),uint8_t(prev.n+k.n)};freq[pair]+=5;}
    prev=k;hasprev=true;q+=len;
   }
  }
  if(coded<best){best=coded;bestkeys=keys;}
  if(iter==8)break;
  struct Candidate {Key k;uint64_t score;};std::vector<Candidate>candidates;candidates.reserve(freq.size());
  for(auto&kv:freq){if(noescape&&kv.first.n==1)continue;uint64_t sc=kv.second*(kv.first.n+1);candidates.push_back({kv.first,sc});}
  std::sort(candidates.begin(),candidates.end(),[](const Candidate&a,const Candidate&b){if(a.score!=b.score)return a.score>b.score;if(a.k.n!=b.k.n)return a.k.n>b.k.n;return a.k.v<b.k.v;});
  keys.clear();if(noescape)keys=alphabet;for(size_t i=0;i<candidates.size()&&keys.size()<255;i++)keys.push_back(candidates[i].k);
 }
 keys=bestkeys;Trie trie(keys);std::vector<uint8_t>data;data.reserve(n/2);std::vector<uint32_t>bases;std::vector<uint16_t>lens;uint16_t maxlen=0;bool escaped=false;
 for(size_t r=0;r<nr;r++){if(r%16==0)bases.push_back(data.size());size_t start=data.size();const uint8_t*q=p+rows[r],*end=p+rows[r+1];while(q<end){unsigned len;int c=trie.match(q,end,len);if(c<0){data.push_back(255);data.push_back(*q);escaped=true;}else data.push_back(c);q+=len;}size_t len=data.size()-start;if(len>65535)return {};lens.push_back(len);maxlen=std::max<uint16_t>(maxlen,len);}
 bases.push_back(data.size());H h{};h.mode=2;h.rows=nr;h.rawsize=n;h.bytes=data.size();h.flags=escaped?1:0;h.index16=maxlen<=15?2:maxlen>255?1:0;
 for(size_t i=0;i<keys.size();i++){h.val[i]=keys[i].v|(uint64_t(keys[i].n)<<56);}
 h.total=sizeof(H)+bases.size()*4+(h.index16==2?(nr+1)/2:nr*(h.index16?2:1))+data.size()+32;
 std::vector<uint8_t>out(h.total);memcpy(out.data(),&h,sizeof h);uint8_t*d=out.data()+sizeof h;memcpy(d,bases.data(),bases.size()*4);d+=bases.size()*4;if(h.index16==2){for(size_t i=0;i<nr;i+=2)*d++=lens[i]|(i+1<nr?lens[i+1]<<4:0);}else for(auto len:lens){*d++=len;if(h.index16)*d++=len>>8;}memcpy(d,data.data(),data.size());return out;
}
#endif
#ifdef DECODER
inline const uint8_t* lens(const H*h){return (const uint8_t*)h+sizeof(H)+((uint64_t(h->rows)+15)/16+1)*4;}
inline size_t indexbytes(const H*h){return h->index16==2?(uint64_t(h->rows)+1)/2:uint64_t(h->rows)*(h->index16?2:1);}
inline const uint8_t* data(const H*h){return lens(h)+indexbytes(h);}
inline void* open(const uint8_t*p,size_t n){
 if(n<sizeof(H))return nullptr;const H*h=(const H*)p;if(h->mode!=2||h->index16>2||h->flags>1||h->bytes>n||h->bytes>UINT32_MAX||h->rows>n)return nullptr;
 if(h->total!=sizeof(H)+uint64_t((uint64_t(h->rows)+15)/16+1)*4+uint64_t(indexbytes(h))+h->bytes+32||h->total>n)return nullptr;
 for(int i=0;i<256;i++)if((h->val[i]>>56)>7)return nullptr;
 if(h->rows>h->rawsize)return nullptr;
 const uint8_t*idx=(const uint8_t*)h+sizeof(H),*l=lens(h);uint64_t total=0;
 for(uint64_t b=0;b<(uint64_t(h->rows)+15)/16;b++){
  if(r32(idx+b*4)!=total)return nullptr;unsigned k=unsigned((h->rows-b*16)>16?16:(h->rows-b*16));
  if(!h->index16&&k==16){__m128i v=_mm_loadu_si128((const __m128i*)(l+b*16));if(_mm_cmpeq_epi8_mask(v,_mm_setzero_si128()))return nullptr;v=_mm_sad_epu8(v,_mm_setzero_si128());total+=_mm_cvtsi128_si64(v)+_mm_extract_epi64(v,1);}
  else if(h->index16==2&&k==16){uint64_t x=r64(l+b*8);if((x-0x1111111111111111ULL)&~x&0x8888888888888888ULL)return nullptr;x=(x&0x0f0f0f0f0f0f0f0fULL)+((x>>4)&0x0f0f0f0f0f0f0f0fULL);total+=(x*0x0101010101010101ULL)>>56;}
  else for(unsigned j=0;j<k;j++){unsigned len;if(h->index16==2)len=(l[b*8+j/2]>>(4*(j&1)))&15;else if(h->index16)len=uint16_t(l[(b*16+j)*2])|(uint16_t(l[(b*16+j)*2+1])<<8);else len=l[b*16+j];if(!len)return nullptr;total+=len;}
  if(total>h->bytes)return nullptr;
 }
 if(total!=h->bytes||r32(idx+((uint64_t(h->rows)+15)/16)*4)!=total)return nullptr;
 return (void*)p;
}
template<bool Escape>inline bool one(const H*h,const uint8_t*&s,uint8_t*&d,const uint8_t*end){unsigned c=*s++;if constexpr(Escape){if(__builtin_expect(c==255,0)){if(s>=end)return false;*d++=*s++;return true;}}uint64_t v=h->val[c];w64(d,v);d+=v>>56;return true;}
inline void four(const H*__restrict h,const uint8_t*&s,uint8_t*&d){
 unsigned a=s[0],b=s[1],c=s[2],e=s[3];s+=4;
 uint64_t va=h->val[a],vb=h->val[b],vc=h->val[c],ve=h->val[e];
 size_t la=va>>56,lb=vb>>56,lc=vc>>56,le=ve>>56;
 w64(d,va);w64(d+la,vb);w64(d+la+lb,vc);w64(d+la+lb+lc,ve);d+=la+lb+lc+le;
}
template<bool Escape>inline bool run(const H*h,const uint8_t*s,const uint8_t*end,uint8_t*&d,uint8_t*limit){
 // Up to eight bytes per code, including the final wide store.
 if(size_t(limit-d)>=size_t(end-s)*8+8){
  if constexpr(!Escape){while(s+4<=end){four(h,s,d);}}
  while(s<end)if(!one<Escape>(h,s,d,end))return false;
 }else{
  while(s<end){unsigned c=*s++;if constexpr(Escape){if(c==255){if(s>=end||d==limit)return false;*d++=*s++;continue;}}
   unsigned z=h->val[c]>>56;if(!z||size_t(limit-d)<z)return false;if(size_t(limit-d)>=8)w64(d,h->val[c]);else memcpy(d,h->val+c,z);d+=z;
  }
 }
 return true;
}
inline int64_t decode(void*__restrict st,uint8_t*__restrict out,size_t cap){
 const H*h=(const H*)st;if(!h||cap<h->rawsize)return -1;const uint8_t*s=data(h),*end=s+h->bytes;uint8_t*d=out,*limit=out+cap;
 if(!h->flags){
  while(size_t(end-s)>=128&&size_t(limit-d)>=1032){const uint8_t*stop=s+128;while(s<stop){four(h,s,d);}}
  if(!run<false>(h,s,end,d,limit))return -1;
 }else{
  while(size_t(end-s)>=128&&size_t(limit-d)>=1032){const uint8_t*stop=s+128;while(s<stop)if(!one<true>(h,s,d,end))return -1;}
  if(!run<true>(h,s,end,d,limit))return -1;
 }
 return uint64_t(d-out)==h->rawsize?int64_t(d-out):-1;
}
inline uint32_t rowstart(const H*h,uint64_t id,unsigned&len){
 unsigned block=id>>4,k=id&15;uint32_t offset=r32((const uint8_t*)h+sizeof(H)+block*4);const uint8_t*l=lens(h);
 if(h->index16==2){uint64_t x=r64(l+block*8);len=(x>>(k*4))&15;uint64_t v=_bzhi_u64(x,k*4);v=(v&0x0f0f0f0f0f0f0f0fULL)+((v>>4)&0x0f0f0f0f0f0f0f0fULL);offset+=(v*0x0101010101010101ULL)>>56;}
 else if(!h->index16){l+=block*16;__m128i v=_mm_maskz_loadu_epi8((__mmask16)((1u<<k)-1),l);v=_mm_sad_epu8(v,_mm_setzero_si128());offset+=_mm_cvtsi128_si64(v)+_mm_extract_epi64(v,1);len=l[k];}
 else {l+=block*32;for(unsigned j=0;j<k;j++)offset+=uint16_t(l[2*j])|(uint16_t(l[2*j+1])<<8);len=uint16_t(l[2*k])|(uint16_t(l[2*k+1])<<8);}
 return offset;
}
inline int64_t rows(void*__restrict st,const uint64_t*__restrict ids,size_t count,uint8_t*__restrict out,size_t cap,uint64_t*__restrict offs){
 const H*h=(const H*)st;if(!h||!offs||(count&&ids[count-1]>=h->rows))return -1;const uint8_t*base=data(h);uint8_t*d=out,*limit=out+cap;offs[0]=0;
 if(!h->flags){for(size_t i=0;i<count;i++){unsigned n;uint32_t p=rowstart(h,ids[i],n);if(!run<false>(h,base+p,base+p+n,d,limit))return -1;offs[i+1]=d-out;}}
 else {for(size_t i=0;i<count;i++){unsigned n;uint32_t p=rowstart(h,ids[i],n);if(!run<true>(h,base+p,base+p+n,d,limit))return -1;offs[i+1]=d-out;}}
 return d-out;
}
#endif
}
#ifdef ENCODER
inline std::vector<uint8_t> byte_encode(const uint8_t*p,size_t n){return bc::encode(p,n);}
#endif
#ifdef DECODER
inline void* byte_open(const uint8_t*p,size_t n){return bc::open(p,n);}
inline int64_t byte_decode(void*p,uint8_t*out,size_t cap){return bc::decode(p,out,cap);}
inline int64_t byte_rows(void*p,const uint64_t*ids,size_t n,uint8_t*out,size_t cap,uint64_t*off){return bc::rows(p,ids,n,out,cap,off);}
inline void byte_close(void*){}
#endif
