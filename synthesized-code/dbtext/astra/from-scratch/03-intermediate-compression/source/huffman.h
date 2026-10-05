#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <queue>
#include <algorithm>
#include <new>
#include <utility>
#include <immintrin.h>
namespace huffman {
#ifndef HUFF_DIRECT_BITS
#define HUFF_DIRECT_BITS 16
#endif
static constexpr unsigned DIRECT_BITS=HUFF_DIRECT_BITS, DIRECT_SIZE=1u<<DIRECT_BITS;
inline bool canonical(const uint8_t* lengths,uint32_t symbols,std::vector<uint32_t>& codes) {
 if(!symbols||symbols>32768)return false;
 uint32_t counts[32]={},next[32]={},active=0;uint64_t code=0;
 for(uint32_t s=0;s<symbols;s++){unsigned l=lengths[s];if(l>31)return false;if(l){counts[l]++;active++;}}
 if(!active)return false;
 for(unsigned l=1;l<=31;l++){code=(code+counts[l-1])*2;if(code+counts[l]>(uint64_t(1)<<l))return false;next[l]=uint32_t(code);}
 codes.resize(symbols);for(uint32_t s=0;s<symbols;s++)codes[s]=lengths[s]?next[lengths[s]]++:0;
 return true;
}
inline bool encode(const std::vector<uint16_t>&tokens,const std::vector<uint16_t>&remap,uint32_t symbols,std::vector<uint8_t>&stream,std::vector<uint32_t>&offsets,std::vector<uint8_t>&lengths) {
 if(!symbols||symbols>32768||tokens.empty()||tokens.back()!=65535)return false;
 std::vector<uint64_t> freq(symbols);
 for(auto t:tokens)if(t!=65535){if(t>=remap.size()||remap[t]>=symbols)return false;freq[remap[t]]++;}
 struct Node{uint64_t weight;int parent;};std::vector<Node>nodes;nodes.reserve(symbols*2);
 using Item=std::pair<uint64_t,uint32_t>;std::priority_queue<Item,std::vector<Item>,std::greater<Item>>q;
 for(uint32_t i=0;i<symbols;i++){nodes.push_back({freq[i],-1});if(freq[i])q.push({freq[i],i});}
 if(q.empty())return false;
 while(q.size()>1){auto a=q.top();q.pop();auto b=q.top();q.pop();uint32_t id=nodes.size();nodes[a.second].parent=id;nodes[b.second].parent=id;nodes.push_back({a.first+b.first,-1});q.push({a.first+b.first,id});}
 lengths.assign(symbols,0);for(uint32_t i=0;i<symbols;i++){if(!freq[i])continue;unsigned l=0;for(int j=i;nodes[j].parent>=0;j=nodes[j].parent)l++;if(!l)l=1;if(l>31)return false;lengths[i]=l;}
 std::vector<uint32_t>codes;if(!canonical(lengths.data(),symbols,codes))return false;
 stream.clear();offsets.clear();offsets.push_back(0);uint64_t bits=0,acc=0;unsigned used=0;
 for(auto t:tokens){if(t==65535){if(bits>UINT32_MAX)return false;offsets.push_back(uint32_t(bits));continue;}
   unsigned id=remap[t],l=lengths[id];acc=(acc<<l)|codes[id];used+=l;bits+=l;
   while(used>=8){used-=8;stream.push_back(uint8_t(acc>>used));}
 }
 if(used)stream.push_back(uint8_t(acc<<(8-used)));
 return true;
}

struct State {
 const uint8_t *dict,*lens;
 uint32_t symbols;
 std::vector<uint32_t> direct,secondary;
 std::vector<uint8_t> compact;
};
inline State* open(const uint8_t*lengths,uint32_t symbols,const uint8_t*dict,const uint8_t*lens) {
 if(!lengths||!dict||!lens)return nullptr;std::vector<uint32_t>codes;if(!canonical(lengths,symbols,codes))return nullptr;
 for(uint32_t i=0;i<symbols;i++)if(!lens[i]||lens[i]>32)return nullptr;
 State*s=new(std::nothrow)State;if(!s)return nullptr;s->lens=lens;s->symbols=symbols;s->direct.assign(DIRECT_SIZE,0);
 size_t total=0;for(uint32_t id=0;id<symbols;id++)if(lengths[id])total+=lens[id];
 if(total>(size_t(1)<<20)){delete s;return nullptr;}s->compact.resize(total+32);size_t cursor=0;

 s->dict=s->compact.data();
 std::vector<uint8_t>extra(DIRECT_SIZE,0);
 for(uint32_t id=0;id<symbols;id++){unsigned l=lengths[id];if(l>DIRECT_BITS){unsigned prefix=codes[id]>>(l-DIRECT_BITS);extra[prefix]=std::max<unsigned>(extra[prefix],l-DIRECT_BITS);}}
 for(unsigned i=0;i<DIRECT_SIZE;i++)if(extra[i]){unsigned n=extra[i];size_t base=s->secondary.size(),slots=size_t(1)<<n;if(base+slots>(size_t(1)<<22)){delete s;return nullptr;}s->direct[i]=uint32_t(base<<10)|(n<<5);s->secondary.resize(base+slots,0);}
 for(uint32_t id=0;id<symbols;id++){
   unsigned l=lengths[id];if(!l)continue;
   uint32_t packed_offset=cursor;_mm256_storeu_si256((__m256i*)(s->compact.data()+cursor),_mm256_loadu_si256((const __m256i*)(dict+size_t(id)*32)));cursor+=lens[id];
   uint32_t m=l|(unsigned(lens[id])<<5)|(packed_offset<<11)|(uint32_t(dict[size_t(id)*32+lens[id]-1]==10)<<31);
   if(l<=DIRECT_BITS){uint32_t first=codes[id]<<(DIRECT_BITS-l),count=1u<<(DIRECT_BITS-l);if(count==1)s->direct[first]=m;else if(count==2){uint64_t pair=uint64_t(m)*0x100000001ULL;memcpy(s->direct.data()+first,&pair,8);}else if(count==4)_mm_storeu_si128((__m128i*)(s->direct.data()+first),_mm_set1_epi32(m));else for(unsigned j=first;j<first+count;j+=8)_mm256_storeu_si256((__m256i*)(s->direct.data()+j),_mm256_set1_epi32(m));}
   else{unsigned prefix=codes[id]>>(l-DIRECT_BITS),n=extra[prefix],suffix=codes[id]&((1u<<(l-DIRECT_BITS))-1),base=s->direct[prefix]>>10;unsigned first=suffix<<(n-l+DIRECT_BITS),count=1u<<(n-l+DIRECT_BITS);std::fill(s->secondary.begin()+base+first,s->secondary.begin()+base+first+count,m);}
 }
 return s;
}
inline uint64_t load_bits(const uint8_t*p,unsigned shift){uint64_t x;memcpy(&x,p,8);return __builtin_bswap64(x)<<shift;}

inline bool resolve(const State*s,uint64_t word,unsigned&m) {
 m=s->direct[word>>(64-DIRECT_BITS)];if(m&31)return true;if(!m)return false;
 unsigned extra=(m>>5)&31,slot=(word>>(64-DIRECT_BITS-extra))&((1u<<extra)-1);
 m=s->secondary[(m>>10)+slot];return (m&31)!=0;
}
inline uint8_t* decode_span(const State*s,const uint8_t*base,uint32_t begin_bit,uint32_t end_bit,uint8_t*out,uint8_t*limit,bool stop_lf=false) {
 if(!s||!base||!out||!limit||out>limit||begin_bit>end_bit)return nullptr;
 uint64_t bit=begin_bit;const uint32_t*table=s->direct.data();
 if(!stop_lf)while(bit+124<=end_bit&&size_t(limit-out)>=128){
   for(unsigned k=0;k<4;k++){
     uint64_t word=load_bits(base+(bit>>3),bit&7);unsigned m=table[word>>(64-DIRECT_BITS)];
     if(__builtin_expect(!(m&31),0)&&!resolve(s,word,m))return nullptr;
     unsigned consumed=m&31,len=(m>>5)&63,id=(m>>11)&1048575;
     _mm256_storeu_si256((__m256i*)out,_mm256_loadu_si256((const __m256i*)(s->dict+id)));
     out+=len;bit+=consumed;
   }
 }
 while(bit<end_bit){
   uint64_t word=load_bits(base+(bit>>3),bit&7);unsigned m=table[word>>(64-DIRECT_BITS)];
   if(__builtin_expect(!(m&31),0)&&!resolve(s,word,m))return nullptr;
   unsigned consumed=m&31,produced=(m>>5)&63,id=(m>>11)&1048575;const uint8_t*src=s->dict+id;
   if(bit+consumed>end_bit||size_t(limit-out)<produced)return nullptr;bit+=consumed;
   if(size_t(limit-out)>=32)_mm256_storeu_si256((__m256i*)out,_mm256_loadu_si256((const __m256i*)src));else memcpy(out,src,produced);
   out+=produced;
   if(stop_lf&&(m&(1u<<31)))return bit==end_bit?out:nullptr;
 }
 return stop_lf?nullptr:out;
}

struct Span {uint32_t bit,end;uint8_t*out,*limit;};
template<unsigned N> inline bool batch(const State*s,const uint8_t*base,Span*sp){
 uint64_t words[N];uint32_t meta[N];unsigned active;
 while(sp[0].end-sp[0].bit>=248&&size_t(sp[0].limit-sp[0].out)>=256){
   bool ready=true;
   #pragma GCC unroll 8
   for(unsigned j=1;j<N;j++)if(sp[j].end-sp[j].bit<248||size_t(sp[j].limit-sp[j].out)<256)ready=false;
   if(!ready)break;
   for(unsigned iter=0;iter<8;iter++){
     #pragma GCC unroll 8
     for(unsigned j=0;j<N;j++){words[j]=load_bits(base+(sp[j].bit>>3),sp[j].bit&7);meta[j]=s->direct[words[j]>>(64-DIRECT_BITS)];}
     #pragma GCC unroll 8
     for(unsigned j=0;j<N;j++){
       unsigned m=meta[j];if(__builtin_expect(!(m&31),0)&&!resolve(s,words[j],m))return false;
       unsigned consumed=m&31,len=(m>>5)&63,id=(m>>11)&1048575;
       _mm256_storeu_si256((__m256i*)sp[j].out,_mm256_loadu_si256((const __m256i*)(s->dict+id)));
       sp[j].bit+=consumed;sp[j].out+=len;
     }
   }
 }
 do {
   active=0;
   #pragma GCC unroll 8
   for(unsigned j=0;j<N;j++){
     if(sp[j].bit<sp[j].end){active|=1u<<j;words[j]=load_bits(base+(sp[j].bit>>3),sp[j].bit&7);meta[j]=s->direct[words[j]>>(64-DIRECT_BITS)];}
   }
   #pragma GCC unroll 8
   for(unsigned j=0;j<N;j++)if(active&(1u<<j)){
     unsigned m=meta[j];if(__builtin_expect(!(m&31),0)&&!resolve(s,words[j],m))return false;
     unsigned consumed=m&31,len=(m>>5)&63,id=(m>>11)&1048575;
     if(__builtin_expect(consumed>sp[j].end-sp[j].bit||size_t(sp[j].limit-sp[j].out)<len,0))return false;
     __m256i value=_mm256_loadu_si256((const __m256i*)(s->dict+id));
     if(size_t(sp[j].limit-sp[j].out)>=32)_mm256_storeu_si256((__m256i*)sp[j].out,value);
     else _mm256_mask_storeu_epi8(sp[j].out,(__mmask32)((uint64_t(1)<<len)-1),value);
     sp[j].bit+=consumed;sp[j].out+=len;
   }
 }while(active);
 for(unsigned j=0;j<N;j++)if(sp[j].bit!=sp[j].end||sp[j].out!=sp[j].limit)return false;
 return true;
}
inline bool decode_batch(const State*s,const uint8_t*base,Span*sp,unsigned n){
 if(!s||!base||!sp||!n||n>8)return false;
 for(unsigned j=0;j<n;j++)if(sp[j].bit>sp[j].end||!sp[j].out||!sp[j].limit||sp[j].out>sp[j].limit)return false;
 if(n==8)return batch<8>(s,base,sp);
 if(n==4)return batch<4>(s,base,sp);
 for(unsigned j=0;j<n;j++){auto*p=decode_span(s,base,sp[j].bit,sp[j].end,sp[j].out,sp[j].limit,false);if(p!=sp[j].limit)return false;sp[j].out=p;sp[j].bit=sp[j].end;}return true;
}
inline uint32_t row_length(const uint32_t*p,size_t id){return p[id+1]-p[id];}
inline uint32_t row_length(const uint16_t*p,size_t id){return p[id];}
inline uint32_t row_length(const uint8_t*p,size_t id){return p[id];}
template<unsigned N,class Raw> inline int64_t dynamic_rows(const State*s,const uint8_t*base,const uint32_t*bits,const Raw*raw,const uint64_t*ids,size_t count,uint32_t nrows,uint8_t*out,size_t cap,uint64_t*offs){
 if(!s||!base||!bits||!raw||!offs||(count&&(!ids||!out)))return -1;
 size_t bytes=0,next=0;offs[0]=0;Span sp[N];uint64_t words[N];uint32_t meta[N];unsigned live=0;
 auto assign=[&](unsigned j)->bool{uint64_t id=ids[next];if(id>=nrows||(next&&id<ids[next-1]))return false;unsigned len=row_length(raw,id);if(len>cap-bytes)return false;sp[j]={bits[id],bits[id+1],out+bytes,out+bytes+len};bytes+=len;offs[++next]=bytes;return true;};
 #pragma GCC unroll 8
 for(unsigned j=0;j<N;j++)if(next<count){if(!assign(j))return -1;live|=1u<<j;}
 #pragma GCC unroll 8
 for(unsigned j=0;j<N;j++)if(live&(1u<<j)){words[j]=load_bits(base+(sp[j].bit>>3),sp[j].bit&7);meta[j]=s->direct[words[j]>>(64-DIRECT_BITS)];}
 while(next+N<=count){
   #pragma GCC unroll 8
   for(unsigned j=0;j<N;j++){
     unsigned m=meta[j];if(__builtin_expect(!(m&31),0)&&!resolve(s,words[j],m))return -1;
     unsigned consumed=m&31,len=(m>>5)&63,id=(m>>11)&1048575;
     if(__builtin_expect(consumed>sp[j].end-sp[j].bit||size_t(sp[j].limit-sp[j].out)<len,0))return -1;
     __m256i v=_mm256_loadu_si256((const __m256i*)(s->dict+id));
     _mm256_mask_storeu_epi8(sp[j].out,(__mmask32)((uint64_t(1)<<len)-1),v);
     sp[j].bit+=consumed;sp[j].out+=len;
     if(sp[j].bit==sp[j].end){
       if(sp[j].out!=sp[j].limit)return -1;
       {if(!assign(j))return -1;}
     }
     words[j]=load_bits(base+(sp[j].bit>>3),sp[j].bit&7);meta[j]=s->direct[words[j]>>(64-DIRECT_BITS)];
   }
 }
 while(live){
   #pragma GCC unroll 8
   for(unsigned j=0;j<N;j++)if(live&(1u<<j)){
     unsigned m=meta[j];if(__builtin_expect(!(m&31),0)&&!resolve(s,words[j],m))return -1;
     unsigned consumed=m&31,len=(m>>5)&63,id=(m>>11)&1048575;
     if(__builtin_expect(consumed>sp[j].end-sp[j].bit||size_t(sp[j].limit-sp[j].out)<len,0))return -1;
     __m256i v=_mm256_loadu_si256((const __m256i*)(s->dict+id));
     _mm256_mask_storeu_epi8(sp[j].out,(__mmask32)((uint64_t(1)<<len)-1),v);
     sp[j].bit+=consumed;sp[j].out+=len;
     if(sp[j].bit==sp[j].end){
       if(sp[j].out!=sp[j].limit)return -1;
       if(next<count){if(!assign(j))return -1;}
       else {live&=~(1u<<j);continue;}
     }
     words[j]=load_bits(base+(sp[j].bit>>3),sp[j].bit&7);meta[j]=s->direct[words[j]>>(64-DIRECT_BITS)];
   }
 }
 return bytes;
}
inline void close(State*s){delete s;}
}
