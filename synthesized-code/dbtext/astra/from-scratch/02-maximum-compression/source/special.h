#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>
#include <algorithm>
#include <immintrin.h>
struct SpecialHeader { uint64_t nrows,raw_size,p0,p1,p2,p3; uint32_t b0,b1; uint8_t alphabet[16],prefix[16]; };
struct SpecialState { SpecialHeader h{}; const uint8_t* data=nullptr; size_t nrows=0,raw_size=0; unsigned mode=0; bool valid=false; };
static unsigned special_bits(uint64_t x) { return x?64-__builtin_clzll(x):0; }
static uint64_t special_gcd(uint64_t a,uint64_t b) { while(b){uint64_t c=a%b;a=b;b=c;}return a; }
static void special_putbits(std::vector<uint8_t>&d,size_t bit,__uint128_t x,unsigned width) {for(unsigned j=0;j<width;){unsigned k=std::min(8u-unsigned(bit&7),width-j);d[bit>>3]|=uint8_t((x>>j)&((1u<<k)-1))<<(bit&7);bit+=k;j+=k;}}
static __uint128_t special_getbits(const uint8_t*d,size_t bit,unsigned width) {__uint128_t x;memcpy(&x,d+(bit>>3),16);x>>=bit&7;return width==128?x:x&(((__uint128_t)1<<width)-1);}
static int special_hexdigit(uint8_t c) {return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
static void special_append(std::vector<uint8_t>&out,const SpecialHeader&h,const std::vector<uint8_t>&d) {out.resize(sizeof(h)+d.size());memcpy(out.data(),&h,sizeof(h));memcpy(out.data()+sizeof(h),d.data(),d.size());}
#if defined(SPECIAL_ENCODER) || defined(LAB_ENCODER)
static int special_encode(const uint8_t*raw,size_t n,std::vector<uint8_t>&out) {
 if(!n||raw[n-1]!='\n')return 0;SpecialHeader h{};h.raw_size=n;
 if(n%19==0&&n>=19&&!memcmp(raw,"Customer#",9)) {
  h.nrows=n/19;uint64_t lo=UINT64_MAX,hi=0;std::vector<uint32_t>v;v.reserve(h.nrows);
  for(size_t i=0;i<h.nrows;++i){const uint8_t*p=raw+19*i;if(memcmp(p,raw,9)||p[18]!='\n')return 0;uint32_t x=0;for(unsigned j=9;j<18;++j){if(p[j]<'0'||p[j]>'9')return 0;x=x*10+p[j]-'0';}v.push_back(x);lo=std::min(lo,(uint64_t)x);hi=std::max(hi,(uint64_t)x);}
  memcpy(h.prefix,raw,9);h.p0=lo;h.p1=hi-lo+1;h.b0=5;__uint128_t max=1;for(unsigned j=0;j<5;++j)max*=h.p1;unsigned bits=0;--max;while(max){++bits;max>>=1;}if(bits>120)return 0;h.b1=bits;
  std::vector<uint8_t>d((((h.nrows+4)/5)*bits+7)/8+16);for(size_t i=0;i<h.nrows;i+=5){__uint128_t x=0;for(int j=4;j>=0;--j)x=x*h.p1+(i+j<h.nrows?v[i+j]-lo:0);special_putbits(d,(i/5)*bits,x,bits);}special_append(out,h,d);return 1;
 }
 if(n%10==0&&n>=10){bool ok=true;for(size_t i=0;i<n;++i)if(i%10==9?raw[i]!='\n':(raw[i]!='a'&&raw[i]!='c'&&raw[i]!='g'&&raw[i]!='t')){ok=false;break;}if(ok){h.nrows=n/10;memcpy(h.alphabet,"acgt",4);h.b0=18;std::vector<uint8_t>d((h.nrows*18+7)/8+16);for(size_t i=0;i<h.nrows;++i){uint32_t v=0;for(unsigned j=0;j<9;++j){uint8_t c=raw[10*i+j];v|=(c=='a'?0:c=='c'?1:c=='g'?2:3)<<(j*2);}special_putbits(d,i*18,v,18);}special_append(out,h,d);return 2;}}
 if(n%37==0&&n>=37&&raw[8]=='-'&&raw[13]=='-'&&raw[18]=='-'&&raw[23]=='-'){
  h.nrows=n/37;std::vector<uint32_t>ts;std::vector<uint64_t>suffix;uint64_t lo=UINT64_MAX,hi=0,bor=0,band=UINT64_MAX,middle=0;bool ok=true;
  for(size_t i=0;i<h.nrows&&ok;++i){const uint8_t*p=raw+i*37;uint64_t t=0,m=0,v=0;if(p[36]!='\n'){ok=false;break;}for(unsigned j=0;j<36;++j){if(j==8||j==13||j==18||j==23){if(p[j]!='-')ok=false;continue;}int x=special_hexdigit(p[j]);if(x<0||(p[j]>='A'&&p[j]<='F')){ok=false;break;}if(j<8)t=(t<<4)|x;else if(j<18)m=(m<<4)|x;else v=(v<<4)|x;}if(i&&m!=middle)ok=false;middle=m;ts.push_back(t);suffix.push_back(v);lo=std::min(lo,t);hi=std::max(hi,t);bor|=v;band&=v;}
  if(ok){uint64_t step=0;for(auto t:ts)step=special_gcd(step,t-lo);if(!step)step=1;h.p0=lo;h.p1=step;h.p2=bor^band;h.p3=band;h.b0=special_bits((hi-lo)/step);h.b1=__builtin_popcountll(h.p2);memcpy(h.prefix,&middle,8);memcpy(h.alphabet,"0123456789abcdef",16);unsigned bits=h.b0+h.b1;if(bits>120)return 0;std::vector<uint8_t>d((h.nrows*bits+7)/8+16);for(size_t i=0;i<h.nrows;++i){__uint128_t v=(ts[i]-lo)/step;v|=(__uint128_t)_pext_u64(suffix[i],h.p2)<<h.b0;special_putbits(d,i*bits,v,bits);}special_append(out,h,d);return 4;}
 }
 {std::vector<uint32_t>v;uint32_t x=0;unsigned len=0;bool ok=true;for(size_t i=0;i<n;++i){uint8_t c=raw[i];if(c=='\n'){if(!len){ok=false;break;}v.push_back(x);x=0;len=0;}else{int digit=special_hexdigit(c);if(digit<0||(c>='a'&&c<='f')||len==8||(!len&&c=='0'&&i+1<n&&raw[i+1]!='\n')){ok=false;break;}x=x*16+digit;++len;}}if(ok&&!len&&!v.empty()){h.nrows=v.size();memcpy(h.alphabet,"0123456789ABCDEF",16);std::vector<uint8_t>d(v.size()*4+16);memcpy(d.data(),v.data(),v.size()*4);special_append(out,h,d);return 3;}}
 return 0;
}
#endif
static bool special_open(SpecialState&s,const uint8_t*p,size_t n,unsigned mode) {
 if(mode<1||mode>4||n<sizeof(SpecialHeader)+16)return false;memcpy(&s.h,p,sizeof(s.h));const auto&h=s.h;if(h.nrows>UINT32_MAX||!h.nrows||h.raw_size>SIZE_MAX)return false;size_t len=0;
 if(mode==1){if(h.b0!=5||h.b1>120||h.p1<1||h.p1>1000000000||h.p0>999999999||h.p0+h.p1>1000000000||h.raw_size!=19*h.nrows)return false;len=(((h.nrows+4)/5)*h.b1+7)/8;}
 if(mode==2){if(h.b0!=18||h.raw_size!=10*h.nrows)return false;len=(h.nrows*18+7)/8;}
 if(mode==3){if(h.raw_size>9*h.nrows||h.raw_size<2*h.nrows)return false;len=h.nrows*4;}
 if(mode==4){if(h.b0>32||h.b1!=__builtin_popcountll(h.p2)||h.b0+h.b1>120||h.p1<1||h.raw_size!=37*h.nrows)return false;len=(h.nrows*(h.b0+h.b1)+7)/8;}
 if(sizeof(h)+len+16!=n)return false;if(mode==3){uint64_t total=0;const uint8_t*d=p+sizeof(h);for(size_t i=0;i<h.nrows;++i){uint32_t x;memcpy(&x,d+4*i,4);total+=(x?(special_bits(x)+3)/4:1)+1;}if(total!=h.raw_size)return false;}s.mode=mode;s.nrows=h.nrows;s.raw_size=h.raw_size;s.data=p+sizeof(h);s.valid=true;return true;
}
static size_t special_row_size(const SpecialState&s,size_t id) {if(s.mode==1)return 19;if(s.mode==2)return 10;if(s.mode==4)return 37;uint32_t x;memcpy(&x,s.data+id*4,4);return (x?(special_bits(x)+3)/4:1)+1;}
static inline void special_hex_write(uint8_t*out,uint64_t x,unsigned digits,const uint8_t*alphabet) {for(unsigned j=0;j<digits;++j)out[digits-1-j]=alphabet[(x>>(4*j))&15];}
static inline size_t special_row(const SpecialState&s,size_t id,uint8_t*out) {
 const auto&h=s.h;
 if(s.mode==1){__uint128_t x=special_getbits(s.data,(id/5)*h.b1,h.b1);for(unsigned j=0;j<id%5;++j)x/=h.p1;uint32_t v=uint64_t(x%h.p1)+h.p0;memcpy(out,h.prefix,9);for(int j=17;j>=9;--j){out[j]='0'+v%10;v/=10;}out[18]='\n';return 19;}
 if(s.mode==2){uint32_t x=special_getbits(s.data,id*18,18);for(unsigned j=0;j<9;++j)out[j]=h.alphabet[(x>>(2*j))&3];out[9]='\n';return 10;}
 if(s.mode==3){uint32_t x;memcpy(&x,s.data+id*4,4);unsigned digits=x?(special_bits(x)+3)/4:1;special_hex_write(out,x,digits,h.alphabet);out[digits]='\n';return digits+1;}
 __uint128_t x=special_getbits(s.data,id*(h.b0+h.b1),h.b0+h.b1);uint32_t ts=h.p0+uint64_t(x&(((__uint128_t)1<<h.b0)-1))*h.p1;uint64_t tail=_pdep_u64(uint64_t(x>>h.b0),h.p2)|h.p3;uint64_t middle;memcpy(&middle,h.prefix,8);special_hex_write(out,ts,8,h.alphabet);out[8]='-';special_hex_write(out+9,middle>>16,4,h.alphabet);out[13]='-';special_hex_write(out+14,middle&65535,4,h.alphabet);out[18]='-';special_hex_write(out+19,tail>>48,4,h.alphabet);out[23]='-';special_hex_write(out+24,tail&0xffffffffffffull,12,h.alphabet);out[36]='\n';return 37;
}
static size_t special_decode(const SpecialState&s,uint8_t*out) {size_t pos=0;for(size_t i=0;i<s.nrows;++i)pos+=special_row(s,i,out+pos);return pos;}
