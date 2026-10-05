#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>
#include <vector>
#include <array>
#include <immintrin.h>
#ifndef SMALL_SPECIAL
#define SMALL_SPECIAL 0
#endif
#ifndef UUID_PACKED
#define UUID_PACKED 1
#endif

namespace sp_internal {
constexpr std::array<uint16_t,256> make_pairs(){std::array<uint16_t,256>a{};for(unsigned i=0;i<256;i++)a[i]=uint16_t('0'+(i%100)/10)|uint16_t('0'+i%10)<<8;return a;}
constexpr std::array<uint32_t,256> make_genes(){std::array<uint32_t,256>a{};for(unsigned i=0;i<256;i++){uint32_t v=0;for(unsigned j=0;j<4;j++)v|=uint32_t("acgt"[(i>>(2*j))&3])<<(8*j);a[i]=v;}return a;}
inline constexpr auto pairs=make_pairs();
inline constexpr auto genes=make_genes();
inline int unhex(uint8_t c,bool lower){if(c>='0'&&c<='9')return c-'0';if(lower&&c>='a'&&c<='f')return c-'a'+10;if(!lower&&c>='A'&&c<='F')return c-'A'+10;return -1;}
inline __m128i hexchars(__m128i in,bool lower){const __m128i lut=lower?_mm_setr_epi8('0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'):_mm_setr_epi8('0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F');const __m128i m=_mm_set1_epi8(15);return _mm_shuffle_epi8(lut,_mm_unpacklo_epi8(_mm_and_si128(_mm_srli_epi16(in,4),m),_mm_and_si128(in,m)));}
inline void cname(const uint8_t*p,uint8_t*out){memcpy(out,"Customer#000",12);uint16_t a=pairs[p[0]],b=pairs[p[1]],c=pairs[p[2]];memcpy(out+12,&a,2);memcpy(out+14,&b,2);memcpy(out+16,&c,2);out[18]=10;}
inline void packed_cname(uint32_t word,uint8_t*out){memcpy(out,"Customer#000",12);uint16_t a=pairs[word&15],b=pairs[(word>>4)&127],c=pairs[(word>>11)&127];memcpy(out+12,&a,2);memcpy(out+14,&b,2);memcpy(out+16,&c,2);out[18]=10;}
inline void packed_genome(uint32_t word,uint8_t*out){uint32_t a=genes[word&255],b=genes[(word>>8)&255];memcpy(out,&a,4);memcpy(out+4,&b,4);uint16_t c=uint8_t("acgt"[(word>>16)&3])|uint16_t(10)<<8;memcpy(out+8,&c,2);}
inline uint32_t packed_word(const uint8_t*p,uint64_t id){size_t bit=id*18;uint32_t word;memcpy(&word,p+(bit>>3),4);return word>>(bit&7);}
inline std::vector<uint8_t> pack18(const std::vector<uint8_t>&v,bool cname){std::vector<uint8_t>r((v.size()/3*18+7)/8+4,0);for(size_t i=0;i<v.size()/3;i++){uint32_t w=cname?uint32_t(v[3*i])|(uint32_t(v[3*i+1])<<4)|(uint32_t(v[3*i+2])<<11):uint32_t(v[3*i])|(uint32_t(v[3*i+1])<<8)|(uint32_t(v[3*i+2])<<16);size_t bit=i*18,at=bit>>3;uint32_t old;memcpy(&old,r.data()+at,4);old|=w<<(bit&7);memcpy(r.data()+at,&old,4);}return r;}
inline void genome(const uint8_t*p,uint8_t*out){uint32_t a=genes[p[0]],b=genes[p[1]];memcpy(out,&a,4);memcpy(out+4,&b,4);uint16_t c=uint8_t("acgt"[p[2]&3])|uint16_t(10)<<8;memcpy(out+8,&c,2);}
inline unsigned hexlength(uint32_t v){return v?8-(__builtin_clz(v)>>2):1;}
inline void masked_store(uint8_t*out,__m128i value,unsigned len){
#ifdef __AVX512BW__
 _mm_mask_storeu_epi8(out,(__mmask16)((1u<<len)-1),value);
#else
 alignas(16) uint8_t tmp[16];_mm_store_si128((__m128i*)tmp,value);memcpy(out,tmp,len);
#endif
}
inline void hexrow(uint32_t v,unsigned len,uint8_t*out){__m128i in=_mm_cvtsi32_si128(__builtin_bswap32(v));__m128i chars=hexchars(in,false);__m128i shifts=_mm_add_epi8(_mm_setr_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15),_mm_set1_epi8(8-len));chars=_mm_shuffle_epi8(chars,shifts);masked_store(out,chars,len);out[len]=10;}
inline constexpr uint64_t uuid_mask=UINT64_C(0xfffffcff3ffeff3f);
inline constexpr uint64_t uuid_fixed=UINT64_C(0x00000300800000c0);
inline void uuid_words(uint64_t a,uint64_t b,uint8_t*out){
  __m128i first=hexchars(_mm_cvtsi64_si128(a),true);
  __m128i last=hexchars(_mm_cvtsi64_si128(b),true);
  uint64_t f=(uint64_t)_mm_cvtsi128_si64(first);uint64_t g=(uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(first,6));
  memcpy(out,"84",2);memcpy(out+2,&f,6);memcpy(out+8,"-2da5-11e8-",11);memcpy(out+19,&g,4);out[23]='-';
  __m128i node=_mm_srli_si128(last,4);masked_store(out+24,node,12);out[36]=10;
}
inline void uuid(const uint8_t*p,uint8_t*out){
  // Two exact 8-byte reads stay within this row's 11-byte record.
  uint64_t a,b;memcpy(&a,p,8);memcpy(&b,p+3,8);uuid_words(a,b,out);
}
inline void packed_uuid(const uint8_t*p,uint64_t id,uint8_t*out){
  size_t bit=id*81,at=bit>>3;uint64_t lo;uint32_t hi;memcpy(&lo,p+at,8);memcpy(&hi,p+at+8,4);
  unsigned __int128 w=((unsigned __int128)hi<<64)|lo;w>>=bit&7;
  uint64_t a=_pdep_u64(uint64_t(w),uuid_mask)|uuid_fixed;
  uint64_t tail=uint64_t(w>>57)&0xffffff;
  uuid_words(a,(a>>24)|(tail<<40),out);
}
inline bool pack_uuid81(const std::vector<uint8_t>&v,std::vector<uint8_t>&r){
  size_t nr=v.size()/11;r.assign((nr*81+7)/8+4,0);
  for(size_t i=0;i<nr;i++){
    uint64_t a;memcpy(&a,v.data()+i*11,8);if((a&~uuid_mask)!=uuid_fixed)return false;
    uint32_t tail=uint32_t(v[i*11+8])|(uint32_t(v[i*11+9])<<8)|(uint32_t(v[i*11+10])<<16);
    unsigned __int128 w=(unsigned __int128)_pext_u64(a,uuid_mask)|((unsigned __int128)tail<<57);
    size_t bit=i*81,at=bit>>3;w<<=bit&7;for(unsigned j=0;j<11;j++)r[at+j]|=uint8_t(w>>(j*8));
  }return true;
}
template<unsigned TYPE,bool ALL> inline int64_t fixed(const uint8_t*p,uint64_t nr,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets){
 constexpr size_t stride=TYPE==1?3:TYPE==2?3:11;constexpr size_t width=TYPE==1?19:TYPE==2?10:37;
 if(count>SIZE_MAX/width||cap<count*width)return -1;
 if constexpr(!ALL)offsets[0]=0;
 for(size_t i=0;i<count;i++){uint64_t id=ALL?i:ids[i];if constexpr(!ALL)if(id>=nr)return -1;uint8_t*d=out+i*width;
 if constexpr(TYPE==1){if constexpr(SMALL_SPECIAL)packed_cname(packed_word(p,id),d);else cname(p+id*stride,d);}
 else if constexpr(TYPE==2){if constexpr(SMALL_SPECIAL)packed_genome(packed_word(p,id),d);else genome(p+id*stride,d);}
 else {if constexpr(SMALL_SPECIAL&&UUID_PACKED)packed_uuid(p,id,d);else uuid(p+id*stride,d);}
 if constexpr(!ALL)offsets[i+1]=(i+1)*width;
 }return count*width;
}
template<bool ALL> inline int64_t hexrows(const uint8_t*p,uint64_t nr,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets){
 size_t pos=0;if constexpr(!ALL)offsets[0]=0;
 for(size_t i=0;i<count;i++){uint64_t id=ALL?i:ids[i];if constexpr(!ALL)if(id>=nr)return -1;uint32_t v;memcpy(&v,p+id*4,4);unsigned len=hexlength(v);if(cap-pos<len+1)return -1;hexrow(v,len,out+pos);pos+=len+1;if constexpr(!ALL)offsets[i+1]=pos;}
 return pos;
}
}

inline int encode_special(const uint8_t*raw,size_t size,std::vector<uint8_t>&payload,uint64_t&rows){
 using namespace sp_internal;
 if(size>=19&&size%19==0&&memcmp(raw,"Customer#000",12)==0){
  std::vector<uint8_t> v;v.reserve(size/19*3);bool ok=true;
  for(size_t at=0;at<size;at+=19){const uint8_t*p=raw+at;if(memcmp(p,"Customer#000",12)||p[18]!=10){ok=false;break;}for(int j=12;j<18;j+=2){if(p[j]<'0'||p[j]>'9'||p[j+1]<'0'||p[j+1]>'9'){ok=false;break;}v.push_back((p[j]-'0')*10+p[j+1]-'0');}if(!ok)break;}
  if(ok){if constexpr(SMALL_SPECIAL){for(size_t i=0;i<v.size();i+=3)if(v[i]>=16){ok=false;break;}if(ok){payload=pack18(v,true);rows=size/19;return 1;}}else{payload.swap(v);rows=size/19;return 1;}}
 }
 if(size>=10&&size%10==0&&(raw[0]=='a'||raw[0]=='c'||raw[0]=='g'||raw[0]=='t')){
  std::vector<uint8_t>v;v.reserve(size/10*3);bool ok=true;
  for(size_t at=0;at<size;at+=10){const uint8_t*p=raw+at;if(p[9]!=10){ok=false;break;}unsigned word=0;for(unsigned j=0;j<9;j++){unsigned c=p[j]=='a'?0:p[j]=='c'?1:p[j]=='g'?2:p[j]=='t'?3:4;if(c==4){ok=false;break;}word|=c<<(j*2);}if(!ok)break;v.push_back(word);v.push_back(word>>8);v.push_back(word>>16);}
  if(ok){if constexpr(SMALL_SPECIAL)payload=pack18(v,false);else payload.swap(v);rows=size/10;return 2;}
 }
 if(size>=37&&size%37==0&&raw[0]=='8'&&raw[1]=='4'&&memcmp(raw+8,"-2da5-11e8-",11)==0){
  std::vector<uint8_t>v;v.reserve(size/37*11);bool ok=true;constexpr int positions[11]={2,4,6,19,21,24,26,28,30,32,34};
  for(size_t at=0;at<size;at+=37){const uint8_t*p=raw+at;if(p[0]!='8'||p[1]!='4'||memcmp(p+8,"-2da5-11e8-",11)||p[23]!='-'||p[36]!=10){ok=false;break;}for(int j:positions){int a=unhex(p[j],true),b=unhex(p[j+1],true);if(a<0||b<0){ok=false;break;}v.push_back(a*16+b);}if(!ok)break;}
  if(ok){if constexpr(SMALL_SPECIAL&&UUID_PACKED){std::vector<uint8_t>q;if(pack_uuid81(v,q)){payload.swap(q);rows=size/37;return 4;}}else{payload.swap(v);rows=size/37;return 4;}}
 }
 if(size&&unhex(raw[0],false)>=0){
  std::vector<uint8_t>v;v.reserve(size/2);uint64_t nr=0;bool ok=true;size_t pos=0;
  while(pos<size){uint32_t value=0;unsigned len=0;size_t begin=pos;while(pos<size&&raw[pos]!=10){int c=unhex(raw[pos++],false);if(c<0||++len>8){ok=false;break;}value=(value<<4)|c;}if(!ok||pos==size||len==0||(len>1&&raw[begin]=='0')){ok=false;break;}++pos;++nr;size_t at=v.size();v.resize(at+4);memcpy(v.data()+at,&value,4);}
  if(ok){payload.swap(v);rows=nr;return 3;}
 }
 return 0;
}
inline bool special_validate(unsigned type,const uint8_t*p,size_t ps,uint64_t nr,uint64_t rawsize){
 (void)p;if(nr>SIZE_MAX/(SMALL_SPECIAL&&UUID_PACKED?81:37))return false;
 if(type==1)return ps==(SMALL_SPECIAL?(nr*18+7)/8+4:nr*3)&&rawsize==nr*19;
 if(type==2)return ps==(SMALL_SPECIAL?(nr*18+7)/8+4:nr*3)&&rawsize==nr*10;
 if(type==3)return ps==nr*4&&rawsize>=nr*2&&rawsize<=nr*9;
 if(type==4)return ps==((SMALL_SPECIAL&&UUID_PACKED)?(nr*81+7)/8+4:nr*11)&&rawsize==nr*37;
 return false;
}
inline int64_t special_decode(unsigned type,const uint8_t*p,size_t ps,uint64_t nr,uint8_t*out,size_t cap){
 using namespace sp_internal;(void)ps;
 switch(type){case 1:return fixed<1,true>(p,nr,nullptr,nr,out,cap,nullptr);case 2:return fixed<2,true>(p,nr,nullptr,nr,out,cap,nullptr);case 3:return hexrows<true>(p,nr,nullptr,nr,out,cap,nullptr);case 4:return fixed<4,true>(p,nr,nullptr,nr,out,cap,nullptr);default:return -1;}
}
inline int64_t special_rows(unsigned type,const uint8_t*p,size_t ps,uint64_t nr,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets){
 using namespace sp_internal;(void)ps;
 switch(type){case 1:return fixed<1,false>(p,nr,ids,count,out,cap,offsets);case 2:return fixed<2,false>(p,nr,ids,count,out,cap,offsets);case 3:return hexrows<false>(p,nr,ids,count,out,cap,offsets);case 4:return fixed<4,false>(p,nr,ids,count,out,cap,offsets);default:return -1;}
}
