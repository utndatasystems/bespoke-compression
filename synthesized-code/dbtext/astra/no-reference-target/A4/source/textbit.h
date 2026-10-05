#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <array>
#include <algorithm>
#include <functional>
#include <immintrin.h>
#ifdef ENCODER
#include "train.h"
#endif
namespace tb {
static constexpr uint32_t MAGIC=0x34425844;
struct Header {uint32_t magic,rows,raw,ns,ntokens,bits,lenbytes,bytes;};
#ifdef ENCODER
static bool encode(const uint8_t*raw,size_t n,std::vector<uint8_t>&out){
 if(!n||n>UINT32_MAX)return false;auto t=train(raw,n,16384,32);size_t ns0=t.parents.size()+256;size_t best=SIZE_MAX;
 for(unsigned lim:{1024,2048,4096,8192,16384}){
  lim=std::min<size_t>(lim,ns0);std::vector<std::vector<uint16_t>> rows;rows.reserve(t.rows.size());std::vector<uint8_t>used(lim);std::vector<uint16_t>tokens;
  std::function<void(unsigned,std::vector<uint16_t>&)> split=[&](unsigned c,std::vector<uint16_t>&r){if(c<lim)r.push_back(c);else{split(t.parents[c-256][0],r);split(t.parents[c-256][1],r);}};
  size_t ntokens=0,maxrow=0;for(auto&r:t.rows){std::vector<uint16_t>v;for(auto c:r)split(c,v);ntokens+=v.size();maxrow=std::max(maxrow,v.size());for(auto c:v)used[c]=1;rows.push_back(std::move(v));}
  for(int c=lim-1;c>=256;c--)if(used[c]){used[t.parents[c-256][0]]=1;used[t.parents[c-256][1]]=1;}
  std::vector<uint16_t>order,map(lim);for(unsigned c=0;c<lim;c++)if(used[c]){map[c]=order.size();order.push_back(c);}
  unsigned ns=order.size(),bits=10;while((1u<<bits)<ns)++bits;if(maxrow>65535||ntokens>UINT32_MAX)return false;unsigned lb=1;while((1u<<lb)<=maxrow)++lb;
  unsigned rowmode=lb,smallbits=lb;size_t rowtotal=rows.size()*lb;for(unsigned q=1;q<lb&&q<=6;q++){size_t escapes=0;unsigned cut=(1u<<q)-1;for(auto&r:rows)escapes+=r.size()>=cut;size_t trial=rows.size()*q+escapes*lb;if(trial<rowtotal){rowtotal=trial;smallbits=q;rowmode=0x1000|(lb<<4)|q;}}
  size_t nodesbytes=(size_t(ns)*2*bits+7)/8+8,rowbytes=(rowtotal+7)/8+8;size_t bytes=(ntokens*bits+7)/8+16,size=sizeof(Header)+nodesbytes+rowbytes+bytes;if(size>=best)continue;best=size;
  Header h{MAGIC,(uint32_t)rows.size(),(uint32_t)n,ns,(uint32_t)ntokens,bits,rowmode,(uint32_t)bytes};out.assign(size,0);uint8_t*p=out.data();memcpy(p,&h,sizeof(h));p+=sizeof(h);
  for(size_t i=0;i<order.size();i++){auto c=order[i];uint16_t a=c<256?i:map[t.parents[c-256][0]],b=c<256?c:map[t.parents[c-256][1]];size_t bit=i*2*bits,pos=bit>>3;uint64_t v=(uint64_t(a)|(uint64_t(b)<<bits))<<(bit&7);for(int j=0;j<5;j++)p[pos+j]|=v>>(j*8);}p+=nodesbytes;
  size_t rowbit=0;auto putrow=[&](unsigned value,unsigned width){size_t pos=rowbit>>3;uint32_t v=value<<(rowbit&7);p[pos]|=v;p[pos+1]|=v>>8;p[pos+2]|=v>>16;rowbit+=width;};unsigned cut=(1u<<smallbits)-1;for(auto&r:rows){unsigned count=r.size();if(rowmode&0x1000){putrow(std::min(count,cut),smallbits);if(count>=cut)putrow(count,lb);}else putrow(count,lb);}p+=rowbytes;
  size_t bit=0;for(auto&r:rows)for(auto c:r){uint32_t v=uint32_t(map[c])<<(bit&7);size_t pos=bit>>3;p[pos]|=v;p[pos+1]|=v>>8;p[pos+2]|=v>>16;bit+=bits;}
 }
 return best!=SIZE_MAX;
}
#else
struct State {Header h;const uint8_t*data;std::vector<std::array<uint8_t,32>>dict;std::vector<uint8_t>len;std::vector<uint32_t>offset;};
static State*open(const uint8_t*a,size_t size){
 if(size<sizeof(Header))return nullptr;Header h;memcpy(&h,a,sizeof(h));
 if(h.magic!=MAGIC||!h.ns||h.bits<10||h.bits>14||h.ns>(1u<<h.bits)||!h.rows)return nullptr;
 unsigned small=h.lenbytes&15,wide=(h.lenbytes>>4)&31;bool escape=(h.lenbytes&0x1000)!=0;if(escape){if(!small||small>6||wide<=small||wide>16||h.lenbytes!=(0x1000|(wide<<4)|small))return nullptr;}else if(h.lenbytes<1||h.lenbytes>16)return nullptr;
 size_t nodesbytes=(size_t(h.ns)*2*h.bits+7)/8+8;
 if(h.bytes!=(uint64_t(h.ntokens)*h.bits+7)/8+16||uint64_t(sizeof(h))+nodesbytes+h.bytes+8>size)return nullptr;size_t rowbytes=size-sizeof(h)-nodesbytes-h.bytes;if(!escape&&rowbytes!=(size_t(h.rows)*h.lenbytes+7)/8+8)return nullptr;if(escape&&(rowbytes<(size_t(h.rows)*small+7)/8+8||rowbytes>(size_t(h.rows)*(small+wide)+7)/8+8))return nullptr;
 State*s=new State;s->h=h;s->dict.resize(h.ns);s->len.resize(h.ns);s->offset.resize(size_t(h.rows)+1);const uint8_t*p=a+sizeof(h);unsigned mask=(1u<<h.bits)-1;
 for(unsigned i=0;i<h.ns;i++){size_t bit=size_t(i)*2*h.bits;uint64_t v;memcpy(&v,p+(bit>>3),8);v>>=bit&7;unsigned l=v&mask,r=(v>>h.bits)&mask;if(l==i){if(r>255){delete s;return nullptr;}s->dict[i][0]=r;s->len[i]=1;}else{if(l>=i||r>=i||s->len[l]+s->len[r]>32){delete s;return nullptr;}unsigned n=s->len[l],m=s->len[r];_mm256_storeu_si256((__m256i*)s->dict[i].data(),_mm256_loadu_si256((const __m256i*)s->dict[l].data()));_mm256_mask_storeu_epi8(s->dict[i].data()+n,(__mmask32)((1ull<<m)-1),_mm256_loadu_si256((const __m256i*)s->dict[r].data()));s->len[i]=n+m;}}
 p+=nodesbytes;uint64_t pos=0;if(!escape){unsigned rowmask=(1u<<h.lenbytes)-1;for(size_t i=0;i<h.rows;i++){s->offset[i]=pos;size_t bit=i*h.lenbytes;uint32_t v;memcpy(&v,p+(bit>>3),4);pos+=(v>>(bit&7))&rowmask;if(pos>h.ntokens){delete s;return nullptr;}}}else{size_t bit=0;unsigned cut=(1u<<small)-1,mask=(1u<<wide)-1;for(size_t i=0;i<h.rows;i++){s->offset[i]=pos;if((bit>>3)+4>rowbytes){delete s;return nullptr;}uint32_t v;memcpy(&v,p+(bit>>3),4);unsigned count=(v>>(bit&7))&cut;bit+=small;if(count==cut){if((bit>>3)+4>rowbytes){delete s;return nullptr;}memcpy(&v,p+(bit>>3),4);count=(v>>(bit&7))&mask;bit+=wide;if(count<cut){delete s;return nullptr;}}pos+=count;if(pos>h.ntokens){delete s;return nullptr;}}if((bit+7)/8+8!=rowbytes){delete s;return nullptr;}}
 p+=rowbytes;s->offset[h.rows]=pos;s->data=p;if(pos!=h.ntokens){delete s;return nullptr;}return s;
}

template<unsigned B>__attribute__((noinline)) static uint8_t*block(const State*s,uint32_t first,uint32_t count,uint8_t*out,uint8_t*limit){
 size_t bit=size_t(first)*B;const uint8_t*p=s->data+(bit>>3);unsigned shift=bit&7;constexpr unsigned MASK=(1u<<B)-1;
 if(count&&count<=4&&limit-out>=128){uint64_t pack;memcpy(&pack,p,8);pack>>=shift;
  #define TB_SHORT() {unsigned v=unsigned(pack)&MASK;if(v>=s->h.ns)return nullptr;_mm256_storeu_si256((__m256i*)out,_mm256_loadu_si256((const __m256i*)s->dict[v].data()));out+=s->len[v];pack>>=B;}
  TB_SHORT() if(count>1){TB_SHORT() if(count>2){TB_SHORT() if(count>3){TB_SHORT()}}}
  #undef TB_SHORT
  return out;
 }
 while(count>=8&&limit-out>=256){
  __uint128_t pack;memcpy(&pack,p,16);pack>>=shift;
  #define TB_ONE(J) {unsigned v=unsigned(pack>>((J)*B))&MASK;if(v>=s->h.ns)return nullptr;_mm256_storeu_si256((__m256i*)out,_mm256_loadu_si256((const __m256i*)s->dict[v].data()));out+=s->len[v];}
  TB_ONE(0) TB_ONE(1) TB_ONE(2) TB_ONE(3) TB_ONE(4) TB_ONE(5) TB_ONE(6) TB_ONE(7)
  #undef TB_ONE
  p+=B;count-=8;
 }
 bit=(p-s->data)*8+shift;
 while(count--){uint32_t pack;memcpy(&pack,s->data+(bit>>3),4);unsigned v=(pack>>(bit&7))&MASK;if(v>=s->h.ns)return nullptr;unsigned n=s->len[v];if(size_t(limit-out)<n)return nullptr;if(limit-out>=32)_mm256_storeu_si256((__m256i*)out,_mm256_loadu_si256((const __m256i*)s->dict[v].data()));else memcpy(out,s->dict[v].data(),n);out+=n;bit+=B;}
 return out;
}
static inline uint8_t*dispatch(const State*s,uint32_t first,uint32_t count,uint8_t*out,uint8_t*limit){switch(s->h.bits){
#define TB_CASE(B) case B:return block<B>(s,first,count,out,limit);
 TB_CASE(10) TB_CASE(11) TB_CASE(12) TB_CASE(13) TB_CASE(14)
#undef TB_CASE
 default:return nullptr;}}
static int64_t decode(State*s,uint8_t*out,size_t cap){if(!s||cap<s->h.raw)return -1;uint8_t*end=dispatch(s,0,s->h.ntokens,out,out+s->h.raw);return end&&size_t(end-out)==s->h.raw?s->h.raw:-1;}
template<unsigned B>static int64_t rows_impl(State*s,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets){uint8_t*p=out;offsets[0]=0;for(size_t i=0;i<count;i++){uint64_t r=ids[i];if(r>=s->h.rows)return -1;p=block<B>(s,s->offset[r],s->offset[r+1]-s->offset[r],p,out+cap);if(!p)return -1;offsets[i+1]=p-out;}return p-out;}
static int64_t rows(State*s,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets){if(!s||!offsets)return -1;switch(s->h.bits){
#define TB_ROW(B) case B:return rows_impl<B>(s,ids,count,out,cap,offsets);
 TB_ROW(10) TB_ROW(11) TB_ROW(12) TB_ROW(13) TB_ROW(14)
#undef TB_ROW
 default:return -1;}}

#endif
}
