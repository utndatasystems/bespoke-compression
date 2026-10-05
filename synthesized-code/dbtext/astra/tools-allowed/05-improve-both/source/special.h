#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#include <map>
#include <immintrin.h>
namespace spl {
static constexpr char digits[]="00010203040506070809101112131415161718192021222324252627282930313233343536373839404142434445464748495051525354555657585960616263646566676869707172737475767778798081828384858687888990919293949596979899";
static inline uint32_t rd32(const uint8_t*p){uint32_t x;memcpy(&x,p,4);return x;}
static inline uint64_t rd64(const uint8_t*p){uint64_t x;memcpy(&x,p,8);return x;}
static inline void wr32(uint8_t*p,uint32_t x){memcpy(p,&x,4);}
static inline void wr64(uint8_t*p,uint64_t x){memcpy(p,&x,8);}
static inline void put32(std::vector<uint8_t>&v,uint32_t x){size_t p=v.size();v.resize(p+4);wr32(v.data()+p,x);}
static inline uint32_t getbits(const uint8_t*p,uint64_t i,unsigned bits){uint64_t b=i*bits;return(rd32(p+(b>>3))>>(b&7))&((1u<<bits)-1);}
static inline void setbits(uint8_t*p,uint64_t i,unsigned bits,uint32_t v){uint64_t b=i*bits;uint32_t z=rd32(p+(b>>3));z|=v<<(b&7);wr32(p+(b>>3),z);}
static inline int hx(unsigned c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
static inline unsigned bw(uint32_t n){return n?32-__builtin_clz(n):1;}
struct State {uint32_t type,nrows,bits;uint64_t rawsize;const uint8_t*p,*data;size_t size;uint32_t genome4[256];uint8_t prefix[16];__m128i uuidfixed1,uuidfixed2;std::vector<uint64_t> times;};
}
static bool special_encode(const uint8_t*raw,size_t size,std::vector<uint8_t>&payload,uint32_t&type,uint32_t&nrows){
 using namespace spl;
 if(size&&size%19==0&&size/19<=UINT32_MAX&&memcmp(raw,"Customer#",9)==0){
  uint32_t nr=size/19,maxv=0;std::vector<uint32_t>v(nr);bool ok=true;
  for(uint32_t i=0;i<nr&&ok;i++){const uint8_t*s=raw+i*19;ok=memcmp(s,raw,12)==0&&s[18]=='\n';uint32_t x=0;for(int j=12;j<18;j++){ok&=s[j]>='0'&&s[j]<='9';x=x*10+s[j]-'0';}v[i]=x;maxv=std::max(maxv,x);}
  if(ok&&maxv<(1u<<18)){unsigned bits=bw(maxv);payload.assign(16+(uint64_t(nr)*bits+7)/8+8,0);memcpy(payload.data(),raw,12);payload[12]=bits;payload[13]=raw[18];for(uint32_t i=0;i<nr;i++)setbits(payload.data()+16,i,bits,v[i]);type=101;nrows=nr;return true;}
 }
 if(size&&size%10==0&&size/10<=UINT32_MAX){
  uint32_t nr=size/10;uint8_t alpha[4]={'a','c','g','t'};std::vector<uint32_t>v(nr);bool ok=true;
  for(uint32_t i=0;i<nr&&ok;i++){uint32_t x=0;const uint8_t*s=raw+i*10;ok=s[9]=='\n';for(int j=0;j<9;j++){unsigned k=0;while(k<4&&s[j]!=alpha[k])k++;if(k==4){ok=false;break;}x|=k<<(2*j);}v[i]=x;}
  if(ok){payload.assign(8+(uint64_t(nr)*18+7)/8+8,0);memcpy(payload.data(),alpha,4);payload[4]='\n';for(uint32_t i=0;i<nr;i++)setbits(payload.data()+8,i,18,v[i]);type=102;nrows=nr;return true;}
 }
 if(size&&size%37==0&&size/37<=UINT32_MAX&&raw[8]=='-'&&raw[13]=='-'&&raw[18]=='-'&&raw[23]=='-'){
  uint32_t nr=size/37;unsigned rb=bw(nr-1);if(rb<=18){
   struct U{uint32_t time,seq,idx;uint64_t node;};std::vector<U>r(nr);bool ok=true;
   for(uint32_t i=0;i<nr&&ok;i++){const uint8_t*s=raw+i*37;ok=memcmp(s+8,raw+8,11)==0&&s[23]=='-'&&s[36]=='\n';uint64_t t=0,q=0,node=0;for(int j=0;j<8;j++){int h=hx(s[j]);ok&=h>=0&&!(s[j]>='A'&&s[j]<='F');t=(t<<4)|unsigned(h);}for(int j=19;j<23;j++){int h=hx(s[j]);ok&=h>=0&&!(s[j]>='A'&&s[j]<='F');q=(q<<4)|unsigned(h);}for(int j=24;j<36;j++){int h=hx(s[j]);ok&=h>=0&&!(s[j]>='A'&&s[j]<='F');node=(node<<4)|unsigned(h);}ok&=((node>>40)&3)==3;r[i]={uint32_t(t),uint32_t(q),i,node};}
   if(ok){
    std::sort(r.begin(),r.end(),[](const U&a,const U&b){return a.time<b.time;});for(uint32_t i=1;i<nr;i++)if(r[i].time==r[i-1].time)ok=false;
    if(ok){std::map<uint32_t,uint32_t>counts;for(uint32_t i=1;i<nr;i++)counts[r[i].time-r[i-1].time]++;std::vector<std::pair<uint32_t,uint32_t>>f;for(auto kv:counts)f.push_back({kv.second,kv.first});std::sort(f.rbegin(),f.rend());uint32_t ds[3]={0,0,0};for(size_t j=0;j<3&&j<f.size();j++)ds[j]=f[j].second;
     std::vector<uint32_t>ex;std::vector<std::pair<uint32_t,uint32_t>>reset;for(uint32_t i=1;i<nr;i++)if(r[i].seq!=(((r[i-1].seq+1)&0x3fff)|0x8000))reset.push_back({i,r[i].seq});
     size_t nc=(nr+3)/4;payload.assign(80+nc+8,0);memcpy(payload.data(),raw,37);payload[37]=rb;wr32(payload.data()+40,r[0].time);wr32(payload.data()+44,r[0].seq);for(int j=0;j<3;j++)wr32(payload.data()+48+4*j,ds[j]);wr32(payload.data()+64,reset.size());
     for(uint32_t i=1;i<nr;i++){uint32_t d=r[i].time-r[i-1].time,k=0;while(k<3&&ds[k]!=d)k++;payload[80+i/4]|=k<<(2*(i%4));if(k==3)ex.push_back(d);}
     wr32(payload.data()+60,ex.size());for(auto x:ex)put32(payload,x);for(auto x:reset){put32(payload,x.first);put32(payload,x.second);}wr32(payload.data()+68,payload.size());size_t dat=payload.size();payload.resize(dat+uint64_t(nr)*8);for(uint32_t rank=0;rank<nr;rank++){uint64_t node=(r[rank].node&((1ull<<40)-1))|((r[rank].node>>42)<<40);wr64(payload.data()+dat+uint64_t(r[rank].idx)*8,(node<<rb)|rank);}type=104;nrows=nr;return true;
    }
   }
  }
 }
 if(size&&raw[size-1]=='\n'&&size<UINT32_MAX){std::vector<uint32_t>v;v.reserve(size/8);bool ok=true;size_t p=0;while(p<size&&ok){uint32_t x=0;unsigned len=0;size_t start=p;while(p<size&&raw[p]!='\n'){unsigned c=raw[p++];int h=hx(c);if(h<0||(c>='a'&&c<='f')||++len>8){ok=false;break;}x=(x<<4)|h;}if(!ok||p==size||!len||(len>1&&raw[start]=='0')){ok=false;break;}p++;v.push_back(x);}if(ok&&!v.empty()){payload.resize(v.size()*4);memcpy(payload.data(),v.data(),payload.size());type=103;nrows=v.size();return true;}}
 return false;
}
static void* special_open(uint32_t type,const uint8_t*p,size_t size,uint32_t nrows,uint64_t rawsize){
 using namespace spl;State*s=new State{};s->type=type;s->nrows=nrows;s->rawsize=rawsize;s->p=p;s->size=size;bool ok=nrows>0;
 if(type==101){ok&=size>=24&&rawsize==uint64_t(nrows)*19;if(ok){s->bits=p[12];ok&=s->bits>0&&s->bits<=18&&size==16+(uint64_t(nrows)*s->bits+7)/8+8;if(ok){memcpy(s->prefix,p,14);s->data=p+16;}}}
 else if(type==102){ok&=rawsize==uint64_t(nrows)*10&&size==8+(uint64_t(nrows)*18+7)/8+8;if(ok){s->data=p+8;memcpy(s->prefix,p,5);for(unsigned v=0;v<256;v++){uint32_t q=0;for(unsigned j=0;j<4;j++)q|=uint32_t(p[(v>>(j*2))&3])<<(j*8);s->genome4[v]=q;}}}
 else if(type==103){ok&=size==uint64_t(nrows)*4&&rawsize>=uint64_t(nrows)*2&&rawsize<=uint64_t(nrows)*9;s->data=p;}
 else if(type==104){ok&=size>=88&&rawsize==uint64_t(nrows)*37;if(ok){s->bits=p[37];uint32_t ne=rd32(p+60),nr=rd32(p+64),dat=rd32(p+68);size_t nc=(nrows+3)/4;ok&=s->bits>0&&s->bits<=18&&uint64_t(nrows)<=(1ull<<s->bits)&&dat==80+nc+8+uint64_t(ne)*4+uint64_t(nr)*8&&size==dat+uint64_t(nrows)*8;if(ok){const uint8_t*ep=p+80+nc+8;const uint8_t*rp=ep+ne*4;uint32_t ei=0,ri=0,time=rd32(p+40),seq=rd32(p+44);uint32_t ds[3]={rd32(p+48),rd32(p+52),rd32(p+56)};s->times.resize(nrows);s->times[0]=time|(uint64_t(seq)<<32);for(uint32_t i=1;i<nrows;i++){unsigned c=(p[80+i/4]>>(2*(i%4)))&3;uint32_t delta;if(c<3)delta=ds[c];else{if(ei>=ne){ok=false;break;}delta=rd32(ep+4*ei++);}time+=delta;seq=((seq+1)&0x3fff)|0x8000;if(ri<nr&&rd32(rp+8*ri)==i){seq=rd32(rp+8*ri+4);ri++;}s->times[i]=time|(uint64_t(seq)<<32);}ok&=ei==ne&&ri==nr;s->data=p+dat;s->uuidfixed1=_mm_loadl_epi64((const __m128i*)(p+8));alignas(16)uint8_t mid[16]={};mid[0]=p[16];mid[1]=p[17];mid[2]=p[18];mid[7]=p[23];s->uuidfixed2=_mm_load_si128((const __m128i*)mid);}}
 }
 else ok=false;
 if(!ok){delete s;return nullptr;}return s;
}
namespace spl {
static inline __m128i hex16(uint64_t n){const __m128i lut=_mm_setr_epi8('0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f');__m128i x=_mm_cvtsi64_si128(__builtin_bswap64(n));__m128i lo=_mm_and_si128(x,_mm_set1_epi8(15)),hi=_mm_and_si128(_mm_srli_epi16(x,4),_mm_set1_epi8(15));return _mm_unpacklo_epi8(_mm_shuffle_epi8(lut,hi),_mm_shuffle_epi8(lut,lo));}
static inline void cname(const State*s,uint32_t id,uint8_t*d){uint32_t x=getbits(s->data,id,s->bits);memcpy(d,s->prefix,12);unsigned a=x/10000,b=(x/100)%100,c=x%100;memcpy(d+12,digits+2*a,2);memcpy(d+14,digits+2*b,2);memcpy(d+16,digits+2*c,2);d[18]=s->prefix[13];}
static inline void genome(const State*s,uint32_t id,uint8_t*d){uint32_t x=getbits(s->data,id,18);wr32(d,s->genome4[x&255]);wr32(d+4,s->genome4[(x>>8)&255]);d[8]=s->prefix[x>>16];d[9]=s->prefix[4];}
static inline unsigned hex(const State*s,uint32_t id,uint8_t*d,size_t avail){uint32_t x=rd32(s->data+uint64_t(id)*4);unsigned len=x?(32-__builtin_clz(x)+3)/4:1;const __m128i lut=_mm_setr_epi8('0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F');__m128i v=_mm_cvtsi32_si128(__builtin_bswap32(x));__m128i a=_mm_and_si128(_mm_srli_epi16(v,4),_mm_set1_epi8(15)),b=_mm_and_si128(v,_mm_set1_epi8(15));uint64_t z=_mm_cvtsi128_si64(_mm_unpacklo_epi8(_mm_shuffle_epi8(lut,a),_mm_shuffle_epi8(lut,b)))>>(8*(8-len));if(avail>=8)wr64(d,z);else memcpy(d,&z,len);d[len]='\n';return len+1;}
static inline bool uuid(const State*s,uint32_t id,uint8_t*d){uint64_t v=rd64(s->data+uint64_t(id)*8);uint32_t rank=v&((1u<<s->bits)-1);if(rank>=s->nrows)return false;uint64_t ts=s->times[rank];uint64_t node=v>>s->bits;node=(node&((1ull<<40)-1))|((((node>>40)<<2)|3)<<40);__m128i h=hex16(((ts>>32)<<48)|node);__m128i first=hex16(uint64_t(uint32_t(ts))<<32);_mm_storeu_si128((__m128i*)d,_mm_unpacklo_epi64(first,s->uuidfixed1));const __m128i sh=_mm_setr_epi8(-1,-1,-1,0,1,2,3,-1,4,5,6,7,8,9,10,11);_mm_storeu_si128((__m128i*)(d+16),_mm_or_si128(_mm_shuffle_epi8(h,sh),s->uuidfixed2));wr32(d+32,_mm_extract_epi32(h,3));d[36]=s->p[36];return true;}
}
static int64_t special_decode(void*state,uint8_t*out,size_t cap){using namespace spl;State*s=(State*)state;if(!s||cap<s->rawsize)return -1;size_t pos=0;switch(s->type){case 101:for(uint32_t i=0;i<s->nrows;i++)cname(s,i,out+uint64_t(i)*19);return s->rawsize;case 102:for(uint32_t i=0;i<s->nrows;i++)genome(s,i,out+uint64_t(i)*10);return s->rawsize;case 103:for(uint32_t i=0;i<s->nrows;i++){if(cap-pos<2)return -1;uint32_t x=rd32(s->data+uint64_t(i)*4);unsigned len=(x?(32-__builtin_clz(x)+3)/4:1)+1;if(len>cap-pos)return -1;pos+=hex(s,i,out+pos,cap-pos);}return pos==s->rawsize?int64_t(pos):-1;case 104:for(uint32_t i=0;i<s->nrows;i++)if(!uuid(s,i,out+uint64_t(i)*37))return -1;return s->rawsize;default:return -1;}}
static int64_t special_rows(void*state,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets){using namespace spl;State*s=(State*)state;if(!s)return -1;offsets[0]=0;size_t pos=0;unsigned stride=s->type==101?19:s->type==102?10:s->type==104?37:0;if(stride&&count>cap/stride)return -1;switch(s->type){case 101:for(size_t i=0;i<count;i++){if(ids[i]>=s->nrows)return -1;cname(s,ids[i],out+pos);offsets[i+1]=pos+=19;}break;case 102:for(size_t i=0;i<count;i++){if(ids[i]>=s->nrows)return -1;genome(s,ids[i],out+pos);offsets[i+1]=pos+=10;}break;case 103:for(size_t i=0;i<count;i++){if(ids[i]>=s->nrows)return -1;uint32_t x=rd32(s->data+ids[i]*4);unsigned len=(x?(32-__builtin_clz(x)+3)/4:1)+1;if(len>cap-pos)return -1;pos+=hex(s,ids[i],out+pos,cap-pos);offsets[i+1]=pos;}break;case 104:for(size_t i=0;i<count;i++){if(ids[i]>=s->nrows||!uuid(s,ids[i],out+pos))return -1;offsets[i+1]=pos+=37;}break;default:return -1;}return pos;}
static void special_close(void*state){delete(spl::State*)state;}
