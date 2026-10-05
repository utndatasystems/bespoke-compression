#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cstring>
#include <vector>
#include <algorithm>
namespace loc2 {
static constexpr uint64_t LATMASK=(1ull<<46)-1, LONMASK=(1ull<<46)-1;
struct Header {uint64_t magic,nrows,rawsize,latmin,lonmin;};
static constexpr uint64_t MAGIC=0x0032434F4C584244ull;
#ifdef ENCODER
static bool encode(const uint8_t*raw,size_t n,std::vector<uint8_t>&out){
 std::vector<uint64_t> lat,lon; const uint8_t*p=raw,*end=raw+n; uint64_t mnlat=~0ull,mxlat=0,mnlon=~0ull,mxlon=0;
 while(p<end){
  if(end-p>=5 && !memcmp(p,"NULL\n",5)){lat.push_back(~0ull);lon.push_back(0);p+=5;continue;}
  if(end-p<15||memcmp(p,"(40.",4))return false;p+=4;
  uint64_t a=0,b=0;int al=0,bl=0;
  while(p<end&&*p>='0'&&*p<='9'){a=a*10+*p++-'0';if(++al>15)return false;}
  if(!al||end-p<7||memcmp(p,", -7",4))return false;p+=4;
  int lonint=*p++-'0';if((lonint!=3&&lonint!=4)||*p++!='.')return false;
  while(p<end&&*p>='0'&&*p<='9'){b=b*10+*p++-'0';if(++bl>14)return false;}
  if(!bl||end-p<2||*p++!=')'||*p++!='\n'||a%10==0||b%10==0)return false;
  for(int i=al;i<15;i++)a*=10;for(int i=bl;i<14;i++)b*=10;b+=uint64_t(lonint-3)*100000000000000ull;
  a=(__uint128_t(a)*(1ull<<47)+500000000000000ull)/1000000000000000ull;b=(__uint128_t(b)*(1ull<<46)+50000000000000ull)/100000000000000ull;lat.push_back(a);lon.push_back(b);mnlat=std::min(mnlat,a);mxlat=std::max(mxlat,a);mnlon=std::min(mnlon,b);mxlon=std::max(mxlon,b);
 }
 if(lat.empty()||mnlat==~0ull||mxlat-mnlat>=LATMASK||mxlon-mnlon>LONMASK)return false;
 Header h{MAGIC,lat.size(),n,mnlat,mnlon};out.assign(sizeof(h)+(lat.size()*92+7)/8+16,0);memcpy(out.data(),&h,sizeof(h));
 for(size_t i=0;i<lat.size();i++){uint64_t a=lat[i]==~0ull?LATMASK:lat[i]-mnlat,b=lat[i]==~0ull?0:lon[i]-mnlon;__uint128_t v=(__uint128_t(b)<<46)|a;size_t bit=i*92,byt=bit/8;v<<=bit%8;for(int j=0;j<13;j++){out[sizeof(h)+byt+j]|=uint8_t(v);v>>=8;}}
 return true;
}
#else
struct State {Header h;const uint8_t*data;};
static State* open(const uint8_t*p,size_t n){if(n<sizeof(Header)+16)return nullptr;Header h;memcpy(&h,p,sizeof(h));if(h.magic!=MAGIC||h.nrows>(SIZE_MAX-sizeof(Header)-16)/92||n!=sizeof(h)+(h.nrows*92+7)/8+16||h.rawsize<h.nrows*5||h.rawsize>h.nrows*41||h.latmin>=(1ull<<47)||h.lonmin>=(1ull<<47))return nullptr;return new State{h,p+sizeof(h)};}
static const char pairs[]="00010203040506070809101112131415161718192021222324252627282930313233343536373839404142434445464748495051525354555657585960616263646566676869707172737475767778798081828384858687888990919293949596979899";
static inline void digits4(char*p,uint32_t v){uint32_t q=v/100;memcpy(p,pairs+q*2,2);memcpy(p+2,pairs+(v-q*100)*2,2);}
static inline char* frac15(char*p,uint64_t v){uint64_t q=v/100000000;uint32_t lo=v-q*100000000;uint32_t hi=q;uint32_t p3=hi/10000;uint32_t p4=hi-p3*10000;p[0]='0'+p3/100;memcpy(p+1,pairs+(p3%100)*2,2);digits4(p+3,p4);digits4(p+7,lo/10000);digits4(p+11,lo%10000);char*e=p+15;while(e>p+1&&e[-1]=='0')--e;return e;}
static inline char* frac14(char*p,uint64_t v){uint64_t q=v/100000000;uint32_t lo=v-q*100000000;uint32_t hi=q;memcpy(p,pairs+(hi/10000)*2,2);digits4(p+2,hi%10000);digits4(p+6,lo/10000);digits4(p+10,lo%10000);char*e=p+14;while(e>p+1&&e[-1]=='0')--e;return e;}
static inline uint8_t* one(const State*s,size_t i,uint8_t*out){size_t bit=i*92;__uint128_t packed;memcpy(&packed,s->data+bit/8,16);packed>>=bit%8;uint64_t a=uint64_t(packed)&LATMASK;if(a==LATMASK){memcpy(out,"NULL\n",5);return out+5;}uint64_t b=uint64_t(packed>>46)&LONMASK;a+=s->h.latmin;b+=s->h.lonmin;if(a>=(1ull<<47)||b>=(1ull<<47))return nullptr;__uint128_t z=__uint128_t(a)*6103515625ull;uint64_t q=(z+(1ull<<32))>>33;bool ok=((uint64_t(z)+3051757812ull)&((1ull<<33)-1))<=6103515624ull;a=ok?q*10:uint64_t((z*5+(1ull<<31))>>32);z=__uint128_t(b)*1220703125ull;q=(z+(1ull<<32))>>33;ok=((uint64_t(z)+610351562ull)&((1ull<<33)-1))<=1220703124ull;b=ok?q*10:uint64_t((z*5+(1ull<<31))>>32);char*p=(char*)out;memcpy(p,"(40.",4);p=frac15(p+4,a);memcpy(p,", -73.",6);if(b>=100000000000000ull){p[4]='4';b-=100000000000000ull;}p=frac14(p+6,b);memcpy(p,")\n",2);return (uint8_t*)p+2;}
static int64_t decode(State*s,uint8_t*out,size_t cap){if(!s||cap<s->h.rawsize)return -1;size_t pos=0;for(size_t i=0;i<s->h.nrows;i++){if(cap-pos>=41){uint8_t*q=one(s,i,out+pos);if(!q)return -1;pos=q-out;}else{uint8_t tmp[48];uint8_t*q=one(s,i,tmp);if(!q)return -1;size_t len=q-tmp;if(len>cap-pos)return -1;memcpy(out+pos,tmp,len);pos+=len;}}return pos==s->h.rawsize?int64_t(pos):-1;}
static int64_t rows(State*s,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets){if(!s||!offsets)return -1;size_t pos=0;offsets[0]=0;for(size_t i=0;i<count;i++){if(ids[i]>=s->h.nrows)return -1;if(cap-pos>=41){uint8_t*q=one(s,ids[i],out+pos);if(!q)return -1;pos=q-out;}else{uint8_t tmp[48];uint8_t*q=one(s,ids[i],tmp);if(!q)return -1;size_t len=q-tmp;if(len>cap-pos)return -1;memcpy(out+pos,tmp,len);pos+=len;}offsets[i+1]=pos;}return pos;}
#endif
}
