#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <charconv>
#include <vector>
#include <algorithm>
#include <limits>
namespace loc {
static constexpr uint32_t MAGIC=0x4c4f4331;
static inline uint32_t u32(const uint8_t*p){uint32_t x;memcpy(&x,p,4);return x;}
static inline uint64_t u64(const uint8_t*p){uint64_t x;memcpy(&x,p,8);return x;}
static inline uint64_t bits(const uint8_t*p,uint64_t pos,unsigned n){return (u64(p+(pos>>3))>>(pos&7))&((uint64_t(1)<<n)-1);}
struct State {
 bool valid=false; uint64_t nrows=0,rawbytes=0;
 uint64_t base[2]{},maximum[2]{}; unsigned b[2]{},full[2]{}; uint32_t nullrow=~0u,exceptions=0;
 const uint8_t*data=nullptr,*flags=nullptr,*ranks=nullptr,*high=nullptr;
 State(const uint8_t*a,size_t n){
  if(n<80||u32(a)!=MAGIC)return;
  nrows=u32(a+4);rawbytes=u64(a+8);nullrow=u32(a+16);exceptions=u32(a+20);
  base[0]=u64(a+24);base[1]=u64(a+32);maximum[0]=u64(a+40);maximum[1]=u64(a+48);
  b[0]=a[56];b[1]=a[57];full[0]=a[58];full[1]=a[59];
  if(!nrows||nrows>10000000||nullrow!=~0u&&nullrow>=nrows||exceptions>nrows)return;
  for(unsigned k=0;k<2;k++)if(!b[k]||b[k]>full[k]||full[k]>=56||base[k]<0x3ff0000000000000ULL||base[k]>maximum[k]||maximum[k]<(uint64_t(1)<<full[k])||maximum[k]>0x41cdcd6500000000ULL||maximum[k]-(uint64_t(1)<<full[k])<0x3ff0000000000000ULL)return;
  if(full[0]+full[1]-b[0]-b[1]>56)return;
  size_t ds=((b[0]+b[1])*nrows+7)/8+8, fs=(nrows+7)/8+8, rs=((nrows+511)/512+1)*4, hs=((full[0]+full[1]-b[0]-b[1])*uint64_t(exceptions)+7)/8+8;
  if(64+ds+fs+rs+hs!=n)return;
  data=a+64;flags=data+ds;ranks=flags+fs;high=ranks+rs;
  if(u32(ranks)!=0||u32(ranks+rs-4)!=exceptions)return;
  if((nrows&7)&&(flags[nrows>>3]>>(nrows&7)))return;
  for(size_t i=(nrows+7)/8;i<fs;i++)if(flags[i])return;
  if(nullrow!=~0u&&(flags[nullrow>>3]>>(nullrow&7)&1))return;
  uint32_t counted=0;
  for(size_t start=0;start<nrows;start+=512){
   if(u32(ranks+(start>>9)*4)!=counted)return;
   size_t stop=std::min<size_t>(nrows,start+512);
   for(size_t j=start>>3;j<(stop+7)/8;j++)counted+=__builtin_popcount(unsigned(flags[j]));
  }
  if(counted!=exceptions)return;
  valid=true;
 }
 inline bool exceptional(uint64_t row)const{return flags[row>>3]>>(row&7)&1;}
 inline uint32_t rank(uint64_t row)const{
  uint32_t r=u32(ranks+(row>>9)*4);uint64_t word=(row>>9)*8, last=row>>6;
  for(;word<last;word++)r+=__builtin_popcountll(u64(flags+word*8));
  unsigned bit=row&63;if(bit)r+=__builtin_popcountll(u64(flags+last*8)&((uint64_t(1)<<bit)-1));
  return r;
 }
 inline uint64_t coord(uint64_t row,unsigned k,uint64_t hi)const{
  unsigned shift=k?full[0]-b[0]:0;uint64_t low=bits(data,row*(b[0]+b[1])+(k?b[0]:0),b[k]);
  uint64_t v=base[k]+low+(((hi>>shift)&((uint64_t(1)<<(full[k]-b[k]))-1))<<b[k]);
  if(v>maximum[k])v-=uint64_t(1)<<full[k];return v;
 }
 inline size_t render(uint64_t row,uint64_t hi,uint8_t*out)const{
  if(row==nullrow){memcpy(out,"NULL\n",5);return 5;}
  char*p=reinterpret_cast<char*>(out);*p++='(';
  uint64_t x=coord(row,0,hi);double d;memcpy(&d,&x,8);
  auto cv0=std::to_chars(p,p+30,d,std::chars_format::fixed);if(cv0.ec!=std::errc())return 0;p=cv0.ptr;*p++=',';*p++=' ';*p++='-';
  x=coord(row,1,hi);memcpy(&d,&x,8);
  auto cv1=std::to_chars(p,p+30,d,std::chars_format::fixed);if(cv1.ec!=std::errc())return 0;p=cv1.ptr;*p++=')';*p++='\n';
  return p-reinterpret_cast<char*>(out);
 }
};
static inline int64_t decode(State*s,uint8_t*out,size_t cap){
 if(!s||!s->valid||cap<s->rawbytes)return -1;size_t z=0;uint64_t j=0;unsigned eb=s->full[0]+s->full[1]-s->b[0]-s->b[1];
 for(uint64_t i=0;i<s->nrows;i++){uint64_t hi=0;if(s->exceptional(i))hi=bits(s->high,j++*eb,eb);uint8_t tmp[80];size_t n=s->render(i,hi,tmp);if(!n||n>cap-z)return -1;memcpy(out+z,tmp,n);z+=n;}
 return z==s->rawbytes?int64_t(z):-1;
}
static inline int64_t rows(State*s,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*off){
 if(!s||!s->valid||!off)return -1;size_t z=0;off[0]=0;unsigned eb=s->full[0]+s->full[1]-s->b[0]-s->b[1];
 for(size_t j=0;j<count;j++){uint64_t r=ids[j];if(r>=s->nrows||(j&&r<ids[j-1]))return -1;uint64_t hi=0;if(s->exceptional(r))hi=bits(s->high,uint64_t(s->rank(r))*eb,eb);uint8_t tmp[80];size_t n=s->render(r,hi,tmp);if(!n||n>cap-z)return -1;memcpy(out+z,tmp,n);z+=n;off[j+1]=z;}
 return z;
}
#ifdef LAB_ENCODER
static inline void w32(std::vector<uint8_t>&v,size_t p,uint32_t x){memcpy(v.data()+p,&x,4);}
static inline void w64(std::vector<uint8_t>&v,size_t p,uint64_t x){memcpy(v.data()+p,&x,8);}
static inline void put(uint8_t*p,uint64_t pos,unsigned n,uint64_t x){if(!n)return;x&=(uint64_t(1)<<n)-1;uint64_t w=u64(p+(pos>>3));w|=x<<(pos&7);memcpy(p+(pos>>3),&w,8);}
static inline std::vector<uint8_t> encode(const uint8_t*raw,size_t n){
 struct Pair{uint64_t v[2];};std::vector<Pair> vals;std::vector<uint64_t> sorted[2];uint32_t nullrow=~0u;
 const char*p=reinterpret_cast<const char*>(raw),*end=p+n;
 while(p<end){const char*q=static_cast<const char*>(memchr(p,'\n',end-p));if(!q)return {};Pair pair{};
  if(q-p==4&&!memcmp(p,"NULL",4)){if(nullrow!=~0u)return {};nullrow=vals.size();vals.push_back(pair);p=q+1;continue;}
  if(q-p<10||*p!='('||q[-1]!=')')return {};const char*comma=static_cast<const char*>(memchr(p,',',q-p));if(!comma||comma[1]!=' '||comma[2]!='-')return {};
  const char*start[2]={p+1,comma+3};const char*stop[2]={comma,q-1};
  for(unsigned k=0;k<2;k++){double d;auto r=std::from_chars(start[k],stop[k],d,std::chars_format::fixed);if(r.ec!=std::errc()||r.ptr!=stop[k]||!(d>0))return {};char buf[64];auto f=std::to_chars(buf,buf+64,d,std::chars_format::fixed);if(f.ec!=std::errc()||size_t(f.ptr-buf)!=size_t(stop[k]-start[k])||memcmp(buf,start[k],f.ptr-buf))return {};memcpy(&pair.v[k],&d,8);sorted[k].push_back(pair.v[k]);}
  vals.push_back(pair);p=q+1;
 }
 if(vals.empty()||vals.size()>10000000||sorted[0].empty())return {};
 uint64_t bases[2][56]{},maxv[2];unsigned full[2];
 for(unsigned k=0;k<2;k++){auto&v=sorted[k];std::sort(v.begin(),v.end());maxv[k]=v.back();uint64_t diff=v.back()-v.front();full[k]=diff?64-__builtin_clzll(diff):1;if(full[k]>=56)return {};
  for(unsigned b=std::max(1u,full[k]>8?full[k]-8:1u);b<=full[k];b++){size_t best=0,r=0;for(size_t l=0;l<v.size();l++){while(r<v.size()&&v[r]-v[l]<(uint64_t(1)<<b))r++;if(r-l>best){best=r-l;bases[k][b]=v[l];}}}
 }
 uint64_t bestsize=~uint64_t(0),bestbase[2]{};unsigned bestb[2]{};uint32_t bestex=0;
 for(unsigned a=std::max(1u,full[0]>8?full[0]-8:1u);a<=full[0];a++)for(unsigned b=std::max(1u,full[1]>8?full[1]-8:1u);b<=full[1];b++){
  uint32_t ex=0;for(size_t i=0;i<vals.size();i++)if(i!=nullrow){auto v=vals[i];ex+=(v.v[0]-bases[0][a]>=(uint64_t(1)<<a)||v.v[1]-bases[1][b]>=(uint64_t(1)<<b));}
  uint64_t sz=(a+b+1)*vals.size()+uint64_t(full[0]+full[1]-a-b)*ex;
  if(sz<bestsize){bestsize=sz;bestb[0]=a;bestb[1]=b;bestbase[0]=bases[0][a];bestbase[1]=bases[1][b];bestex=ex;}
 }
 size_t nr=vals.size();unsigned a=bestb[0],b=bestb[1],eb=full[0]+full[1]-a-b;
 size_t ds=((a+b)*nr+7)/8+8,fs=(nr+7)/8+8,rs=((nr+511)/512+1)*4,hs=(uint64_t(eb)*bestex+7)/8+8;
 std::vector<uint8_t> out(64+ds+fs+rs+hs,0);w32(out,0,MAGIC);w32(out,4,nr);w64(out,8,n);w32(out,16,nullrow);w32(out,20,bestex);w64(out,24,bestbase[0]);w64(out,32,bestbase[1]);w64(out,40,maxv[0]);w64(out,48,maxv[1]);out[56]=a;out[57]=b;out[58]=full[0];out[59]=full[1];
 uint8_t*data=out.data()+64,*flags=data+ds,*ranks=flags+fs,*high=ranks+rs;uint32_t ex=0;
 for(size_t i=0;i<nr;i++){if(!(i&511))memcpy(ranks+(i>>9)*4,&ex,4);uint64_t delta[2]={0,0};if(i!=nullrow)for(unsigned k=0;k<2;k++)delta[k]=(vals[i].v[k]-bestbase[k])&((uint64_t(1)<<full[k])-1);
  put(data,i*(a+b),a,delta[0]);put(data,i*(a+b)+a,b,delta[1]);uint64_t hi=(delta[0]>>a)|((delta[1]>>b)<<(full[0]-a));if(hi){flags[i>>3]|=1u<<(i&7);put(high,uint64_t(ex++)*eb,eb,hi);}
 }
 memcpy(ranks+rs-4,&ex,4);if(ex!=bestex)return {};return out;
}
#endif
}
