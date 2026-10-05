#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <immintrin.h>
#ifdef ENCODER
#include <vector>
#include <string>
static bool location_encode(const uint8_t* raw,size_t size,std::vector<uint8_t>& p,uint32_t& type,uint32_t& nr) {
 std::vector<std::string> rows; size_t a=0;
 for(size_t i=0;i<size;i++) if(raw[i]=='\n'){rows.emplace_back((const char*)raw+a,i+1-a);a=i+1;}
 if(a!=size || rows.empty())return false;
 p.assign(48+rows.size()*16,0); 
 const char fmt[]="(40., -73)\nNULL\n0123456789"; memcpy(p.data(),fmt,sizeof(fmt));
 for(size_t i=0;i<rows.size();i++) {
  const auto&s=rows[i]; uint8_t *q=p.data()+48+i*16;
  if(s=="NULL\n") continue;
  if(s.size()<10 || s.compare(0,4,"(40.")!=0 || s.substr(s.size()-2)!=")\n")return false;
  size_t c=s.find(", -7");if(c==std::string::npos)return false;
  if(c<13||c>19 || c+6>=s.size() || (s[c+4]!='3'&&s[c+4]!='4') ||s[c+5]!='.')return false;
  size_t na=c-4,nb=s.size()-c-8;if(na>15||nb>14||nb<10)return false;
  uint8_t nib[32]={};
  for(size_t j=0;j<na;j++){if(s[4+j]<'0'||s[4+j]>'9')return false;nib[j]=s[4+j]-'0';}
  nib[15]=na;
  for(size_t j=0;j<nb;j++){if(s[c+6+j]<'0'||s[c+6+j]>'9')return false;nib[16+j]=s[c+6+j]-'0';}
  nib[30]=nb; nib[31]=s[c+4]-'3';
  for(int j=0;j<16;j++)q[j]=nib[j*2]|(nib[j*2+1]<<4);
 }
 nr=rows.size();type=5;return true;
}
#endif
static bool location_validate(const uint8_t* p,size_t size,uint32_t n,size_t rawsize) {
 return size==48+(size_t)n*16 && rawsize<=(size_t)n*41 && rawsize>=(size_t)n*5;
}
static inline size_t location_one(const uint8_t* p,const uint8_t*q,uint8_t*out) {
 unsigned na=q[7]>>4,nb=q[15]&15;
 if(!na){memcpy(out,p+11,5);return 5;}
 if(na>15||nb>14)return 0;
 __m128i b=_mm_loadu_si128((const __m128i*)q), m=_mm_set1_epi8(15);
 __m128i l=_mm_and_si128(b,m),h=_mm_and_si128(_mm_srli_epi16(b,4),m);
 __m128i x=_mm_add_epi8(_mm_unpacklo_epi8(l,h),_mm_set1_epi8('0'));
 __m128i y=_mm_add_epi8(_mm_unpackhi_epi8(l,h),_mm_set1_epi8('0'));
 memcpy(out,p,4);
 _mm_storeu_si128((__m128i*)(out+4),x);
 memcpy(out+4+na,p+4,5);out[8+na]+=q[15]>>4;out[9+na]=p[3];
 _mm_storeu_si128((__m128i*)(out+10+na),y);
 memcpy(out+10+na+nb,p+9,2);
 return na+nb+12;
}
static inline size_t location_len(const uint8_t*q){unsigned a=q[7]>>4,b=q[15]&15;return a?a+b+12:5;}
static int64_t location_decode(const uint8_t*p,size_t size,uint32_t n,uint8_t*out,size_t cap,size_t rawsize){
 if(cap<rawsize)return -1;size_t pos=0;
 for(uint32_t i=0;i<n;i++){
  const uint8_t*q=p+48+(size_t)i*16;size_t len=location_len(q); if(len>cap-pos)return -1;
  if(cap-pos>=42){size_t z=location_one(p,q,out+pos);if(z!=len)return -1;}
  else{uint8_t tmp[64];size_t z=location_one(p,q,tmp);if(z!=len)return -1;memcpy(out+pos,tmp,len);}
  pos+=len;
 }return pos==rawsize?pos:-1;
}
static int64_t location_rows(const uint8_t*p,size_t size,uint32_t n,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offs){
 size_t pos=0;offs[0]=0;
 for(size_t i=0;i<count;i++){
  if(ids[i]>=n)return -1;const uint8_t*q=p+48+ids[i]*16;size_t len=location_len(q);if(len>cap-pos)return -1;
  if(cap-pos>=42){if(location_one(p,q,out+pos)!=len)return -1;}else{uint8_t tmp[64];if(location_one(p,q,tmp)!=len)return -1;memcpy(out+pos,tmp,len);}
  pos+=len;offs[i+1]=pos;
 }return pos;
}
