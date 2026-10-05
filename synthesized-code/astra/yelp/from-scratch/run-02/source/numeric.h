#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#ifdef ENCODER
#include <vector>
#include <string>
#include <stdexcept>
#endif
namespace numcodec {
static const char dp[201]="00010203040506070809101112131415161718192021222324252627282930313233343536373839404142434445464748495051525354555657585960616263646566676869707172737475767778798081828384858687888990919293949596979899";
static inline uint32_t rd32(const void*p){uint32_t x;memcpy(&x,p,4);return x;}
static inline uint64_t rd64(const void*p){uint64_t x;memcpy(&x,p,8);return x;}
static inline uint8_t* digits(uint8_t*p,uint32_t x){
 if(x<10){*p++='0'+x;return p;}
 if(x<100){memcpy(p,dp+x*2,2);return p+2;}
 if(x<1000){*p++='0'+x/100;memcpy(p,dp+(x%100)*2,2);return p+2;}
 if(x<10000){memcpy(p,dp+(x/100)*2,2);memcpy(p+2,dp+(x%100)*2,2);return p+4;}
 return p;
}
static inline uint8_t* coordinate(uint8_t*p,uint32_t v,uint32_t tail,bool neg){
 if(neg)*p++='-';
 const uint32_t integer=v/10000000, frac=v%10000000;
 if(integer>=100){*p++='1';memcpy(p,dp+(integer-100)*2,2);p+=2;}else{memcpy(p,dp+integer*2,2);p+=2;}
 *p++='.';
 // Seven fractional digits, emitted as one digit and three table pairs.
 *p++='0'+frac/1000000;
 const uint32_t rest=frac%1000000;
 memcpy(p,dp+(rest/10000)*2,2);
 memcpy(p+2,dp+((rest/100)%100)*2,2);
 memcpy(p+4,dp+(rest%100)*2,2);p+=6;
 if(tail){*p++='0'+tail/100;memcpy(p,dp+(tail%100)*2,2);p+=2;}
 while(p[-1]=='0')--p;
 return p;
}
struct State {
 const uint8_t *bases=nullptr,*lat_tail=nullptr,*lon_tail=nullptr,*reviews=nullptr,*review_cur=nullptr;
 uint32_t lat_idx=0,lon_idx=0,n=0;
 bool open(const uint8_t*p,size_t len,size_t rows){
  if(len<24)return false;
  n=rd32(p);const uint32_t nl=rd32(p+4),no=rd32(p+8),nr=rd32(p+12);
  if(n!=rows)return false;
  const size_t ll=((size_t)nl*10+7)/8+8,lo=((size_t)no*10+7)/8+8;
  if(16+(size_t)n*8+ll+lo+nr>len)return false;
  bases=p+16;lat_tail=bases+(size_t)n*8;lon_tail=lat_tail+ll;reviews=lon_tail+lo;reset();return true;
 }
 void reset(){lat_idx=lon_idx=0;review_cur=reviews;}
 static inline uint32_t get_tail(const uint8_t*p,uint32_t idx){const uint32_t bit=idx*10;return (rd32(p+(bit>>3))>>(bit&7))&1023;}
 inline uint8_t* emit_lat(size_t row,uint8_t*p){
  if(row==0)lat_idx=0;
  uint64_t v=rd64(bases+row*8);uint32_t tail=0;
  if(v&(uint64_t(1)<<57))tail=get_tail(lat_tail,lat_idx++);
  return coordinate(p,(v&0xfffffff)+270000000,tail,false);
 }
 inline uint8_t* emit_lon(size_t row,uint8_t*p){
  if(row==0)lon_idx=0;
  uint64_t v=rd64(bases+row*8);uint32_t tail=0;
  if(v&(uint64_t(1)<<58))tail=get_tail(lon_tail,lon_idx++);
  return coordinate(p,((v>>28)&0x1fffffff)+740000000,tail,true);
 }
 inline uint8_t* emit_stars(size_t row,uint8_t*p){
  uint32_t m=(rd64(bases+row*8)>>59)>>1;
  p[0]='1'+m/2;p[1]='.';p[2]=(m&1)?'5':'0';return p+3;
 }
 inline uint8_t* emit_review(size_t row,uint8_t*p){
  if(row==0)review_cur=reviews;
  uint32_t x=*review_cur++;
  if(x==255){x=uint32_t(review_cur[0])+(uint32_t(review_cur[1])<<8);review_cur+=2;}else x+=5;
  return digits(p,x);
 }
 inline uint8_t* emit_open(size_t row,uint8_t*p){*p++='0'+((rd64(bases+row*8)>>59)&1);return p;}
};
#ifdef ENCODER
static inline uint64_t decimal(const std::string&s){
 uint64_t x=0;unsigned frac=0;bool dot=false;
 for(char c:s){if(c=='-')continue;if(c=='.'){dot=true;continue;}if(c<'0'||c>'9')throw std::runtime_error("numeric format");x=x*10+(c-'0');if(dot)++frac;}
 if(!dot||frac>10||s.back()=='0')throw std::runtime_error("numeric precision");
 while(frac++<10)x*=10;return x;
}
static inline void append32(std::vector<uint8_t>&v,uint32_t x){for(unsigned i=0;i<4;++i)v.push_back(x>>(i*8));}
static inline void append64(std::vector<uint8_t>&v,uint64_t x){for(unsigned i=0;i<8;++i)v.push_back(x>>(i*8));}
static inline std::vector<uint8_t> packed_tails(const std::vector<uint16_t>&in){
 std::vector<uint8_t>v((in.size()*10+7)/8+8,0);
 for(size_t i=0;i<in.size();++i){size_t bit=i*10;uint32_t val=uint32_t(in[i])<<(bit&7);v[bit>>3]|=val;v[(bit>>3)+1]|=val>>8;v[(bit>>3)+2]|=val>>16;}return v;
}
static inline std::vector<uint8_t> encode(const std::vector<std::string>&lat,const std::vector<std::string>&lon,const std::vector<std::string>&stars,const std::vector<std::string>&review,const std::vector<std::string>&isopen){
 const size_t n=lat.size();std::vector<uint64_t>bases;std::vector<uint16_t>lt,lo;std::vector<uint8_t>rv;
 bases.reserve(n);
 for(size_t i=0;i<n;++i){
  uint64_t a=decimal(lat[i]),b=decimal(lon[i]);uint32_t at=a%1000,bt=b%1000;
  uint64_t aa=a/1000-270000000,bb=b/1000-740000000;
  if(aa>0xfffffff||bb>0x1fffffff||lat[i][0]=='-'||lon[i][0]!='-')throw std::runtime_error("numeric range");
  if(at)lt.push_back(at);if(bt)lo.push_back(bt);
  uint32_t st=(stars[i][0]-'1')*2+(stars[i][2]=='5');uint32_t op=isopen[i][0]-'0';
  if(st>8||op>1)throw std::runtime_error("metadata range");
  bases.push_back(aa|(bb<<28)|(uint64_t(at!=0)<<57)|(uint64_t(bt!=0)<<58)|(uint64_t((st<<1)|op)<<59));
  unsigned r=std::stoul(review[i]);if(r<5||r>9999)throw std::runtime_error("review range");
  if(r<260)rv.push_back(r-5);else{rv.push_back(255);rv.push_back(r);rv.push_back(r>>8);}
 }
 auto ltp=packed_tails(lt),lop=packed_tails(lo);std::vector<uint8_t>out;out.reserve(16+n*8+ltp.size()+lop.size()+rv.size());
 append32(out,n);append32(out,lt.size());append32(out,lo.size());append32(out,rv.size());
 for(auto b:bases)append64(out,b);
 out.insert(out.end(),ltp.begin(),ltp.end());out.insert(out.end(),lop.begin(),lop.end());out.insert(out.end(),rv.begin(),rv.end());return out;
}
#endif
}
