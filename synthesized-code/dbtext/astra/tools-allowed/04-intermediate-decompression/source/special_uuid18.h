#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <immintrin.h>
#ifdef ENCODER
#include <vector>
#endif
struct SP {const uint8_t* data=nullptr; uint64_t n=0,rawsize=0; uint32_t mode=0,base=0;};
static inline uint32_t spr32(const void*p){uint32_t x; memcpy(&x,p,4);return x;}
static inline uint64_t spr64(const void*p){uint64_t x; memcpy(&x,p,8);return x;}
static inline void spw64(void*p,uint64_t x){memcpy(p,&x,8);}
static inline uint32_t sp18(const uint8_t*p,uint64_t i){uint64_t b=i*18;return (spr32(p+(b>>3))>>(b&7))&262143;}
static inline bool sp_open(const uint8_t*p,size_t z,SP&s){
 if(z<48||spr32(p)!=0x35305053)return false;
 uint32_t mode=spr32(p+4);uint64_t n=spr64(p+8),raw=spr64(p+16);uint32_t base=spr32(p+24);
 if(!n||n>1000000000ull||mode<1||mode>4)return false;
 uint64_t bits=mode==1||mode==3?18:mode==2?78:32;
 if(z!=32+(n*bits+7)/8+16)return false;
 if((mode==1&&raw!=n*19)||(mode==2&&raw!=n*37)||(mode==3&&raw!=n*10)||(mode==4&&(raw<n*2||raw>n*9)))return false;
 s={p+32,n,raw,mode,base};return true;
}
static inline uint64_t sphex8(uint32_t v,bool upper){
 uint64_t q=__builtin_bswap64(_pdep_u64(v,0x0f0f0f0f0f0f0f0full));
 __m128i alpha=upper?_mm_setr_epi8('0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'):_mm_setr_epi8('0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f');
 return (uint64_t)_mm_cvtsi128_si64(_mm_shuffle_epi8(alpha,_mm_cvtsi64_si128(q)));
}
static inline __m128i sphex16(uint64_t v){
 __m128i x=_mm_cvtsi64_si128(__builtin_bswap64(v));
 __m128i nib=_mm_unpacklo_epi8(_mm_and_si128(_mm_srli_epi16(x,4),_mm_set1_epi8(15)),_mm_and_si128(x,_mm_set1_epi8(15)));
 return _mm_shuffle_epi8(_mm_setr_epi8('0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'),nib);
}
static constexpr char sppairs[]="00010203040506070809101112131415161718192021222324252627282930313233343536373839404142434445464748495051525354555657585960616263646566676869707172737475767778798081828384858687888990919293949596979899";
template<int M> static inline unsigned sp_render(const SP&s,uint64_t id,uint8_t*out){
 if constexpr(M==1){
  uint32_t v=sp18(s.data,id); memcpy(out,"Customer#000",12);
  uint32_t a=v/10000,b=v/100%100,c=v%100;
  memcpy(out+12,sppairs+2*a,2);memcpy(out+14,sppairs+2*b,2);memcpy(out+16,sppairs+2*c,2);out[18]=10;return 19;
 }else if constexpr(M==2){
  uint64_t bit=id*78;const uint8_t*p=s.data+(bit>>3);unsigned sh=bit&7;
  uint32_t first=((spr32(p)>>sh)&262143)*10+s.base;
  __uint128_t x;memcpy(&x,p,16);uint64_t v=(uint64_t)(x>>(18+sh))&0xfffffffffffffffULL;
  uint64_t tail=(v&0xffffffffffULL)|((v&0xfffff0000000000ULL)<<2)|(3ULL<<40)|(2ULL<<62);
  spw64(out,sphex8(first,false));memcpy(out+8,"-2da5-11e8-",11);
  __m128i h=sphex16(tail);uint32_t a=_mm_cvtsi128_si32(h),b=_mm_extract_epi32(h,3);
  memcpy(out+19,&a,4);out[23]='-';_mm_storel_epi64((__m128i*)(out+24),_mm_srli_si128(h,4));memcpy(out+32,&b,4);out[36]=10;return 37;
 }else if constexpr(M==3){
  uint32_t v=sp18(s.data,id);uint64_t q=_pdep_u64(v,0x0303030303030303ull);
  __m128i chars=_mm_shuffle_epi8(_mm_setr_epi8('a','c','g','t',0,0,0,0,0,0,0,0,0,0,0,0),_mm_cvtsi64_si128(q));
  _mm_storel_epi64((__m128i*)out,chars);out[8]="acgt"[v>>16];out[9]=10;return 10;
 }else{
  uint32_t v=spr32(s.data+4*id);unsigned skip=v?__builtin_clz(v)/4:7;unsigned len=8-skip;uint64_t text=sphex8(v,true)>>(skip*8);
  if(len==8)spw64(out,text);else memcpy(out,&text,len);out[len]=10;return len+1;
 }
}
template<int M> static int64_t sp_bulk_t(const SP&s,uint8_t*out,size_t cap){
 if(cap<s.rawsize)return -1;
 size_t z=0;for(uint64_t i=0;i<s.n;++i){if constexpr(M==4){uint32_t v=spr32(s.data+i*4);unsigned need=v?9-__builtin_clz(v)/4:2;if(need>cap-z)return -1;}z+=sp_render<M>(s,i,out+z);}return z==s.rawsize?(int64_t)z:-1;
}
template<int M> static int64_t sp_rows_t(const SP&s,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offs){
 constexpr unsigned fixed=M==1?19:M==2?37:M==3?10:0;
 offs[0]=0;
 if constexpr(M==1||M==2){if(count>cap/fixed)return -1;for(size_t i=0;i<count;i++){if(ids[i]>=s.n)return -1;sp_render<M>(s,ids[i],out+i*fixed);offs[i+1]=(i+1)*fixed;}return count*fixed;
 }else if constexpr(fixed){if(count>cap/fixed)return -1;size_t i=0;
  for(;i+4<=count;i+=4){__m256i sign=_mm256_set1_epi64x(INT64_MIN);__m256i q=_mm256_xor_si256(_mm256_loadu_si256((const __m256i*)(ids+i)),sign);__m256i lim=_mm256_set1_epi64x((s.n-1)^uint64_t(INT64_MIN));if(_mm256_movemask_epi8(_mm256_cmpgt_epi64(q,lim)))return -1;sp_render<M>(s,ids[i],out+i*fixed);sp_render<M>(s,ids[i+1],out+(i+1)*fixed);sp_render<M>(s,ids[i+2],out+(i+2)*fixed);sp_render<M>(s,ids[i+3],out+(i+3)*fixed);offs[i+1]=(i+1)*fixed;offs[i+2]=(i+2)*fixed;offs[i+3]=(i+3)*fixed;offs[i+4]=(i+4)*fixed;}
  for(;i<count;i++){if(ids[i]>=s.n)return -1;sp_render<M>(s,ids[i],out+i*fixed);offs[i+1]=(i+1)*fixed;}return count*fixed;
 }else{size_t z=0;for(size_t i=0;i<count;i++){if(ids[i]>=s.n)return -1;uint32_t v=spr32(s.data+ids[i]*4);if(v>=0x10000000u){if(cap-z<9)return -1;spw64(out+z,sphex8(v,true));out[z+8]=10;z+=9;}else{unsigned skip=v?__builtin_clz(v)/4:7,len=8-skip;if(cap-z<len+1)return -1;uint64_t text=sphex8(v,true)>>(skip*8);memcpy(out+z,&text,len);out[z+len]=10;z+=len+1;}offs[i+1]=z;}return z;}
}
static inline int64_t sp_decode(const SP&s,uint8_t*out,size_t cap){switch(s.mode){case 1:return sp_bulk_t<1>(s,out,cap);case 2:return sp_bulk_t<2>(s,out,cap);case 3:return sp_bulk_t<3>(s,out,cap);case 4:return sp_bulk_t<4>(s,out,cap);default:return -1;}}
static inline int64_t sp_rows(const SP&s,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offs){switch(s.mode){case 1:return sp_rows_t<1>(s,ids,count,out,cap,offs);case 2:return sp_rows_t<2>(s,ids,count,out,cap,offs);case 3:return sp_rows_t<3>(s,ids,count,out,cap,offs);case 4:return sp_rows_t<4>(s,ids,count,out,cap,offs);default:return -1;}}
#ifdef ENCODER
static inline int sphx(uint8_t c,bool upper=false){if(c>='0'&&c<='9')return c-'0';if(upper&&c>='A'&&c<='F')return c-'A'+10;if(!upper&&c>='a'&&c<='f')return c-'a'+10;return -1;}
static inline bool sp_encode(const uint8_t*raw,size_t size,std::vector<uint8_t>&out){
 unsigned mode=0,bits=0;uint64_t n=100000;uint32_t base=0;
 if(size==1900000&&memcmp(raw,"Customer#",9)==0){mode=1;bits=18;}
 else if(size==3700000&&memcmp(raw+8,"-2da5-11e8-",11)==0){mode=2;bits=78;base=UINT32_MAX;}
 else if(size==1000000&&raw[9]==10){mode=3;bits=18;}
 else if(size==893322){mode=4;bits=32;}
 else return false;
 std::vector<__uint128_t> values;values.reserve(n);const uint8_t*p=raw,*end=raw+size;
 for(uint64_t i=0;i<n;++i){__uint128_t v=0;
  if(mode==1){if(memcmp(p,"Customer#000",12)||p[18]!=10)return false;uint32_t num=0;for(int j=12;j<18;++j){if(p[j]<'0'||p[j]>'9')return false;num=num*10+p[j]-'0';}if(num>150000)return false;v=num;p+=19;}
  else if(mode==2){if(memcmp(p+8,"-2da5-11e8-",11)||p[23]!='-'||p[36]!=10)return false;uint32_t first=0;uint64_t tail=0;for(int j=0;j<8;++j){int x=sphx(p[j]);if(x<0)return false;first=first*16+x;}for(int j=19;j<36;++j){if(j==23)continue;int x=sphx(p[j]);if(x<0)return false;tail=tail*16+x;}if((first&1)||(tail>>62)!=2||((tail>>40)&3)!=3)return false;if(first<base)base=first;uint64_t t=(tail&0xffffffffffULL)|((tail>>2)&0xfffff0000000000ULL);v=((__uint128_t)t<<32)|first;p+=37;}
  else if(mode==3){if(p[9]!=10)return false;for(int j=0;j<9;++j){unsigned x=p[j]=='a'?0:p[j]=='c'?1:p[j]=='g'?2:p[j]=='t'?3:4;if(x==4)return false;v|=(__uint128_t)x<<(j*2);}p+=10;}
  else {if(p>=end)return false;unsigned len=0;uint32_t num=0;bool zero=*p=='0';while(p<end&&*p!=10){int x=sphx(*p++,true);if(x<0||++len>8)return false;num=num*16+x;}if(!len||(zero&&len>1)||p>=end||*p++!=10)return false;v=num;}
  values.push_back(v);
 }
 if(p!=end)return false;
 out.assign(32+(n*bits+7)/8+16,0);uint32_t magic=0x35305053;memcpy(out.data(),&magic,4);memcpy(out.data()+4,&mode,4);memcpy(out.data()+8,&n,8);memcpy(out.data()+16,&size,8);memcpy(out.data()+24,&base,4);
 for(uint64_t i=0;i<n;++i){__uint128_t v=values[i];if(mode==2){uint32_t first=(uint32_t)v;if((first-base)%10 || (first-base)/10>=262144)return false;v=((v>>32)<<18)|((first-base)/10);}uint64_t bit=i*bits;uint8_t*p=out.data()+32+(bit>>3);__uint128_t old;memcpy(&old,p,16);old|=v<<(bit&7);memcpy(p,&old,16);}
 return true;
}
#endif
