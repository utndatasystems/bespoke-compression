#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>
#include <algorithm>
#include <cmath>
using std::vector;
static inline void put32(vector<uint8_t>&o,uint32_t x){size_t p=o.size();o.resize(p+4);memcpy(o.data()+p,&x,4);}
static inline uint32_t get32(const uint8_t*p){uint32_t x;memcpy(&x,p,4);return x;}
struct BitsOut{vector<uint8_t> v;uint64_t b=0;int n=0;void put(uint32_t x,int k){b|=(uint64_t)x<<n;n+=k;while(n>=8){v.push_back(b);b>>=8;n-=8;}}void finish(){if(n)v.push_back(b);n=0;b=0;}};
struct BitsIn{const uint8_t*p,*e;uint64_t bitpos=0;bool ok=true;uint32_t get(int k){size_t offset=bitpos>>3;if(offset+8>size_t(e-p)){ok=false;return 0;}uint64_t x;memcpy(&x,p+offset,8);x>>=bitpos&7;bitpos+=k;return x&((1u<<k)-1);}};
static vector<uint8_t> ans16_encode(const vector<uint16_t>&s,unsigned scale){unsigned ns=1;for(auto x:s)ns=std::max(ns,unsigned(x+1));uint32_t total=1u<<scale;vector<uint32_t>cnt(ns),f(ns),c(ns);for(auto x:s)cnt[x]++;uint32_t sum=0;for(unsigned i=0;i<ns;i++){if(cnt[i])f[i]=std::max(1u,(uint32_t)((uint64_t)cnt[i]*total/std::max((size_t)1,s.size())));sum+=f[i];}if(s.empty()){f[0]=total;sum=total;}
while(sum<total){unsigned k=0;double best=-1;for(unsigned i=0;i<ns;i++)if(cnt[i]){double a=(double)cnt[i]/(f[i]+0.5);if(a>best)best=a,k=i;}f[k]++;sum++;}
while(sum>total){unsigned k=0;double best=1e100;for(unsigned i=0;i<ns;i++)if(f[i]>1){double a=(double)cnt[i]/(f[i]-0.5);if(a<best)best=a,k=i;}f[k]--;sum--;}
sum=0;for(unsigned i=0;i<ns;i++)c[i]=sum,sum+=f[i];vector<uint8_t>rev;uint32_t st[4]={1u<<23,1u<<23,1u<<23,1u<<23};for(size_t i=s.size();i-->0;){uint32_t x=st[i&3],ff=f[s[i]];while(x>=(ff<<(31-scale))){rev.push_back(x);x>>=8;}st[i&3]=(x/ff)*total+x%ff+c[s[i]];}vector<uint8_t>o;put32(o,s.size());put32(o,ns);put32(o,scale);BitsOut freq;for(auto x:f){unsigned v=x+1,k=31-__builtin_clz(v);freq.put(0,k);freq.put(1,1);freq.put(v-(1u<<k),k);}freq.finish();freq.v.resize(freq.v.size()+8);put32(o,freq.v.size());o.insert(o.end(),freq.v.begin(),freq.v.end());for(int i=0;i<4;i++)put32(o,st[i]);o.insert(o.end(),rev.rbegin(),rev.rend());return o;}
struct AnsE{uint16_t f,c,s,pad;};
static bool ans16_decode(const uint8_t*a,size_t bytes,vector<uint16_t>&out){if(bytes<40)return false;uint32_t n=get32(a),ns=get32(a+4),scale=get32(a+8);if(scale>15||scale<8||ns>(1u<<scale)||bytes<40||n>20000000)return false;uint32_t total=1u<<scale,mask=total-1;vector<AnsE>tab(total);uint32_t fsize=get32(a+12);if(fsize>bytes-32||fsize<8)return false;BitsIn freq{a+16,a+16+fsize};const uint8_t*p=a+16+fsize,*end=a+bytes;uint32_t c=0;for(unsigned s=0;s<ns;s++){unsigned k=0;while(!freq.get(1)){if(++k>16||!freq.ok)return false;}uint32_t f=(1u<<k)+freq.get(k)-1;if(!freq.ok)return false;if(c+f>total)return false;for(unsigned j=c;j<c+f;j++)tab[j]={(uint16_t)f,(uint16_t)c,(uint16_t)s,0};c+=f;}if(c!=total)return false;uint32_t st[4];for(int i=0;i<4;i++){st[i]=get32(p);p+=4;if(st[i]<(1u<<23))return false;}out.resize(n);for(unsigned i=0;i<n;i++){uint32_t&x=st[i&3];auto z=tab[x&mask];out[i]=z.s;x=z.f*(x>>scale)+(x&mask)-z.c;if(x<(1u<<23)){if(p==end)return false;x=(x<<8)|*p++;if(x<(1u<<23)){if(p==end)return false;x=(x<<8)|*p++;}}}return p==end;}
