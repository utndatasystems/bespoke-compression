
#pragma once
#include <cstdint>
#include <vector>
#include <cstring>
#include <algorithm>
#include <limits>
#include "entropy.h"
#include "lz.h"
using Bytes=std::vector<uint8_t>;
inline void put(Bytes& b,uint64_t v){while(v>=128){b.push_back(uint8_t(v)|128);v>>=7;}b.push_back(v);}
struct Reader {
 const uint8_t* p; const uint8_t* end;
 Reader(const uint8_t* a,size_t n):p(a),end(a+n){}
 Reader(const Bytes&b):Reader(b.data(),b.size()){}
 uint64_t get(){uint64_t v=0;for(unsigned s=0;s<64;s+=7){if(p==end)throw 1;uint8_t b=*p++;if(s==63&&(b&254))throw 1;v|=uint64_t(b&127)<<s;if(!(b&128))return v;}throw 1;}
 uint8_t byte(){if(p==end)throw 1;return *p++;}
 const uint8_t* take(size_t n){if(n>size_t(end-p))throw 1;auto a=p;p+=n;return a;}
 bool done(){return p==end;}
};
inline void block(Bytes& dst,const Bytes& src){put(dst,src.size());dst.insert(dst.end(),src.begin(),src.end());}
inline Bytes unblock(Reader& r,size_t limit){size_t n=r.get();auto p=r.take(n);Bytes b;if(!entropy::decode(p,n,b,limit))throw 1;return b;}
inline uint64_t zz(uint64_t v){return (v<<1)^uint64_t(int64_t(v)>>63);}
inline uint64_t unzz(uint64_t v){return (v>>1)^uint64_t(-int64_t(v&1));}
inline unsigned bits(uint64_t v){return v?64-__builtin_clzll(v):0;}
inline void packbits(Bytes& b,const std::vector<uint64_t>& a,unsigned w){
 uint64_t buf=0;unsigned used=0;
 for(auto v:a){if(w==64){for(unsigned i=0;i<8;++i)b.push_back(v>>(8*i));continue;}
  unsigned rem=w;
  while(rem){unsigned take=std::min(64-used,rem);uint64_t m=take==64?~0ull:((1ull<<take)-1);buf|=(v&m)<<used;used+=take;v>>=take;rem-=take;
   while(used>=8){b.push_back(buf);buf>>=8;used-=8;}
  }
 }
 if(used)b.push_back(buf);
}
inline std::vector<uint64_t> unpackbits(Reader&r,size_t n,unsigned w){
 if(w>63)throw 1;
 if(n>300000)throw 1;
 size_t len=(n*w+7)/8;const uint8_t*p=r.take(len);const uint8_t*e=p+len;
 std::vector<uint64_t>a(n);uint64_t buf=0;unsigned used=0;
 for(auto &v:a){v=0;unsigned done=0;while(done<w){if(!used){if(p==e)throw 1;buf=*p++;used=8;}unsigned take=std::min(used,w-done);v|=(buf&((1u<<take)-1))<<done;buf>>=take;used-=take;done+=take;}}
 return a;
}
// Integer column: choose among bit packing, delta coding, run lengths,
// and independently entropy-coded little-endian byte planes.
inline Bytes ints_encode(const std::vector<uint64_t>& a,bool optimal=false){
 if(a.empty())return Bytes{0};
 uint64_t lo=*std::min_element(a.begin(),a.end()),hi=*std::max_element(a.begin(),a.end());
 Bytes best;
 auto tryit=[&](Bytes b){
 Bytes e=entropy::encode(b);e.insert(e.begin(),0);
 if(best.empty()||e.size()<best.size())best=std::move(e);
 if(b.size()>=64){
  Bytes z=lz::encode(b.data(),b.size());Bytes c{1};put(c,b.size());Bytes ze=entropy::encode(z);c.insert(c.end(),ze.begin(),ze.end());
  if(c.size()<best.size())best=std::move(c);
  if(optimal){z=lz::encode_optimal(b.data(),b.size());Bytes c2{1};put(c2,b.size());ze=entropy::encode(z);c2.insert(c2.end(),ze.begin(),ze.end());if(c2.size()<best.size())best=std::move(c2);}
 }
};
 for(unsigned mode=0;mode<5;++mode){
  Bytes b{uint8_t(mode)};std::vector<uint64_t>v(a);
  if(mode==0){put(b,lo);unsigned w=bits(hi-lo);b.push_back(w);for(auto&x:v)x-=lo;packbits(b,v,w);}
  if(mode==1){uint64_t prev=0;for(auto&x:v){uint64_t c=x;x=zz(c-prev);prev=c;}unsigned w=bits(*std::max_element(v.begin(),v.end()));if(w>=63)continue;b.push_back(w);packbits(b,v,w);}
  if(mode==2){for(auto x:v)put(b,x);}
  if(mode==3){uint64_t prev=0;for(auto x:v){put(b,zz(x-prev));prev=x;}}
  if(mode==4){uint64_t prev=0;for(size_t i=0;i<v.size();){size_t j=i+1;while(j<v.size()&&v[j]==v[i])++j;put(b,j-i);put(b,zz(v[i]-prev));prev=v[i];i=j;}}
  tryit(std::move(b));
 }
 for(unsigned mode=5;mode<7;++mode){
  Bytes b{uint8_t(mode)};std::vector<uint64_t>v(a);uint64_t prev=0,mx=0;
  if(mode==5){put(b,lo);for(auto&x:v){x-=lo;mx|=x;}}
  else {for(auto&x:v){auto y=x;x=zz(x-prev);prev=y;mx|=x;}}
  unsigned nb=(bits(mx)+7)/8;b.push_back(nb);
  for(unsigned k=0;k<nb;++k){Bytes p;for(auto x:v)p.push_back(x>>(8*k));block(b,entropy::encode(p));}
  // Outer entropy wrapper raw, to avoid decoding nested bytes twice.
  Bytes e{0};put(e,b.size());e.insert(e.end(),b.begin(),b.end());
  e.insert(e.begin(),0);if(e.size()<best.size())best=std::move(e);
 }
 return best;
}
inline std::vector<uint64_t> ints_decode(const Bytes& b,size_t n){
 Reader r(b);unsigned mode=r.byte();std::vector<uint64_t>a;
 if(mode==0){uint64_t lo=r.get();unsigned w=r.byte();a=unpackbits(r,n,w);for(auto&x:a)x+=lo;}
 else if(mode==1){unsigned w=r.byte();a=unpackbits(r,n,w);uint64_t prev=0;for(auto&x:a){x=prev+unzz(x);prev=x;}}
 else if(mode==2||mode==3){a.resize(n);uint64_t prev=0;for(auto&x:a){x=r.get();if(mode==3){x=prev+unzz(x);prev=x;}}}
 else if(mode==4){a.reserve(n);uint64_t prev=0;while(a.size()<n){uint64_t count=r.get(),v=prev+unzz(r.get());if(!count||count>n-a.size())throw 1;a.insert(a.end(),count,v);prev=v;}}
 else if(mode==5||mode==6){uint64_t lo=mode==5?r.get():0;unsigned nb=r.byte();if(nb>8)throw 1;a.resize(n);
  for(unsigned k=0;k<nb;++k){Bytes p=unblock(r,n);if(p.size()!=n)throw 1;for(size_t i=0;i<n;++i)a[i]|=uint64_t(p[i])<<(8*k);}
  uint64_t prev=0;for(auto&x:a){if(mode==5)x+=lo;else {x=prev+unzz(x);prev=x;}}
 }else throw 1;
 if(!r.done())throw 1;return a;
}
inline std::vector<uint64_t> readints(Reader&r,size_t n){
 size_t sz=r.get();Reader c(r.take(sz),sz);uint8_t mode=c.byte();Bytes b;
 size_t limit=n*20+10000;
 if(mode==0){if(!entropy::decode(c.p,c.end-c.p,b,limit))throw 1;}
 else if(mode==1){size_t raw=c.get();if(raw>limit)throw 1;Bytes z;if(!entropy::decode(c.p,c.end-c.p,z,limit*2+100)||!lz::decode(z.data(),z.size(),b,raw)||b.size()!=raw)throw 1;}
 else throw 1;return ints_decode(b,n);
}
inline bool ishex(uint8_t c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}
inline unsigned hx(uint8_t c){return c<='9'?c-'0':c-'a'+10;}
inline void decimal(uint8_t*p,unsigned n,uint64_t v){
 while(n>=2){unsigned x=v%100;v/=100;n-=2;p[n]='0'+x/10;p[n+1]='0'+x%10;}
 if(n)p[0]='0'+v;
}
inline void timeformat(uint8_t*p,uint32_t v){
 unsigned ms=v%1000;unsigned sec=v/1000;unsigned ss=sec%60;unsigned mm=(sec/60)%60;unsigned hh=sec/3600;
 decimal(p,2,hh);p[2]=':';decimal(p+3,2,mm);p[5]=':';decimal(p+6,2,ss);p[8]='.';decimal(p+9,3,ms);
}
