#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <algorithm>
#include <immintrin.h>
namespace sp {
static constexpr uint32_t magic=0x31504353u;
static inline uint32_t u32(const void*p){uint32_t v;memcpy(&v,p,4);return v;}
static inline uint64_t u64(const void*p){uint64_t v;memcpy(&v,p,8);return v;}
struct Header{uint32_t magic,kind,rows,bits;uint64_t raw,base,param;};
static_assert(sizeof(Header)==40,"header");
#ifdef ENCODER
static inline unsigned hexval(uint8_t c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:99;}
static inline uint64_t hexread(const uint8_t*p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v=v*16+hexval(p[i]);return v;}
static inline void putbits(uint8_t*p,size_t bit,uint64_t v,unsigned n){for(unsigned i=0;i<n;i++){p[(bit+i)>>3]|=((v>>i)&1)<<((bit+i)&7);}}
static bool encode(const uint8_t*raw,size_t n,std::vector<uint8_t>&out){
 Header h={magic,0,0,0,n,0,0};
 if(n&&n%19==0&&n/19<=UINT32_MAX&&!memcmp(raw,"Customer#",9)){
   h.kind=1;h.rows=n/19;h.bits=0;
   for(size_t i=0;i<h.rows;i++){const uint8_t*p=raw+i*19;if(memcmp(p,"Customer#000",12)||p[18]!=10)return false;uint32_t v=0;for(int j=12;j<18;j++){if(p[j]<'0'||p[j]>'9')return false;v=v*10+p[j]-'0';}if(v>=3u<<16)return false;}
 }else if(n&&n%10==0&&n/10<=UINT32_MAX&&(raw[0]=='a'||raw[0]=='c'||raw[0]=='g'||raw[0]=='t')){
   h.kind=2;h.rows=n/10;h.bits=18;
   for(size_t i=0;i<h.rows;i++){for(int j=0;j<9;j++){uint8_t c=raw[i*10+j];if(c!='a'&&c!='c'&&c!='g'&&c!='t')return false;}if(raw[i*10+9]!=10)return false;}
 }else if(n&&n%37==0&&n/37<=UINT32_MAX&&n>=37&&!memcmp(raw+8,"-2da5-11e8-",11)){
   h.kind=4;h.rows=n/37;h.bits=78;h.base=UINT64_MAX;
   for(size_t i=0;i<h.rows;i++){const uint8_t*p=raw+i*37;if(memcmp(p+8,"-2da5-11e8-",11)||p[23]!='-'||p[36]!=10)return false;for(int j=0;j<36;j++){if(j==8||j==13||j==18||j==23)continue;if(!((p[j]>='0'&&p[j]<='9')||(p[j]>='a'&&p[j]<='f')))return false;}uint64_t first=hexread(p,8);h.base=std::min(h.base,first);if((hexread(p+19,4)&0xc000)!=0x8000||(hexread(p+24,2)&3)!=3)return false;}
   for(size_t i=0;i<h.rows;i++){uint64_t d=hexread(raw+i*37,8)-h.base;if(d%10||d/10>=(1u<<18))return false;}
 }else{
   h.kind=3;h.bits=32;size_t pos=0;
   while(pos<n){size_t start=pos;while(pos<n&&raw[pos]!=10){if(!((raw[pos]>='0'&&raw[pos]<='9')||(raw[pos]>='A'&&raw[pos]<='F')))return false;++pos;}if(pos==n||pos-start<1||pos-start>8||(raw[start]=='0'&&pos-start>1))return false;++pos;if(h.rows==UINT32_MAX)return false;++h.rows;}
   if(!h.rows)return false;
 }
 size_t payload=h.kind==1?size_t(h.rows)*2+(h.rows+4ull)/5:(size_t(h.rows)*h.bits+7)/8;out.assign(sizeof(h)+payload+8,0);memcpy(out.data(),&h,sizeof(h));uint8_t*p=out.data()+sizeof(h);
 if(h.kind==1){const unsigned pow3[]={1,3,9,27,81};for(size_t i=0;i<h.rows;i++){uint32_t v=0;for(int j=12;j<18;j++)v=v*10+raw[i*19+j]-'0';uint16_t lo=v;memcpy(p+i*2,&lo,2);p[size_t(h.rows)*2+i/5]+=(v>>16)*pow3[i%5];}}
 if(h.kind==2){for(size_t i=0;i<h.rows;i++){uint32_t v=0;for(int j=0;j<9;j++){uint8_t c=raw[i*10+j];unsigned q=c=='a'?0:c=='c'?1:c=='g'?2:3;v|=q<<(j*2);}putbits(p,i*18,v,18);}}
 if(h.kind==3){size_t pos=0;for(size_t i=0;i<h.rows;i++){uint32_t v=0;while(raw[pos]!=10)v=v*16+hexval(raw[pos++]);++pos;memcpy(p+4*i,&v,4);}}
 if(h.kind==4){for(size_t i=0;i<h.rows;i++){const uint8_t*q=raw+i*37;uint64_t t=(hexread(q,8)-h.base)/10,s=hexread(q+19,4)&0x3fff,node=hexread(q+24,12);node=(node&0xffffffffffull)|((node>>2)&0x3f0000000000ull);putbits(p,i*78,t|(s<<18),32);putbits(p,i*78+32,node,46);}}
 return true;
}
#else
struct State{Header h;const uint8_t*p;};
static State*open(const uint8_t*a,size_t n){
 if(n<sizeof(Header)+8)return nullptr;Header h;memcpy(&h,a,sizeof(h));
 if(h.magic!=magic||h.kind<1||h.kind>4)return nullptr;
 unsigned bits=h.kind==1?0:h.kind==3?32:h.kind==4?78:18;
 size_t payload=h.kind==1?size_t(h.rows)*2+(h.rows+4ull)/5:(size_t(h.rows)*bits+7)/8;
 if(h.bits!=bits||sizeof(Header)+payload+8!=n)return nullptr;
 if((h.kind==1&&h.raw!=size_t(h.rows)*19)||(h.kind==2&&h.raw!=size_t(h.rows)*10)||(h.kind==4&&(h.raw!=size_t(h.rows)*37||h.base>UINT32_MAX))||(h.kind==3&&(h.raw<size_t(h.rows)*2||h.raw>size_t(h.rows)*9)))return nullptr;
 return new State{h,a+sizeof(h)};
}
static inline uint64_t bits(const uint8_t*p,size_t b){return u64(p+(b>>3))>>(b&7);}
struct Decimal{uint16_t p[100];constexpr Decimal():p{}{for(int i=0;i<100;i++)p[i]=('0'+i/10)|(('0'+i%10)<<8);}};
static constexpr Decimal decimal{};
struct Ternary{uint16_t p[256];constexpr Ternary():p{}{for(unsigned i=0;i<256;i++){unsigned v=i;for(int j=0;j<5;j++){p[i]|=(v%3)<<(j*2);v/=3;}}}};
static constexpr Ternary ternary{};
static inline uint32_t nameval(const State*s,size_t i){uint16_t low;memcpy(&low,s->p+i*2,2);return low|(((ternary.p[s->p[size_t(s->h.rows)*2+i/5]]>>(2*(i%5)))&3u)<<16);}
static inline void name(uint32_t v,uint8_t*o){memcpy(o,"Customer#000",12);uint32_t a=v/10000,b=v/100%100,c=v%100;memcpy(o+12,decimal.p+a,2);memcpy(o+14,decimal.p+b,2);memcpy(o+16,decimal.p+c,2);o[18]=10;}
static inline void genome(uint32_t v,uint8_t*o){
 uint64_t q=_pdep_u64(v,0x0303030303030303ull);__m128i lut=_mm_setr_epi8('a','c','g','t',0,0,0,0,0,0,0,0,0,0,0,0);q=uint64_t(_mm_cvtsi128_si64(_mm_shuffle_epi8(lut,_mm_cvtsi64_si128(q))));memcpy(o,&q,8);o[8]="acgt"[(v>>16)&3];o[9]=10;
}
static inline __m128i tohex(__m128i in,__m128i lut){__m128i m=_mm_set1_epi8(15);return _mm_unpacklo_epi8(_mm_shuffle_epi8(lut,_mm_and_si128(_mm_srli_epi16(in,4),m)),_mm_shuffle_epi8(lut,_mm_and_si128(in,m)));}
static inline unsigned hex(uint32_t v,uint8_t*o){unsigned len=v?(32-__builtin_clz(v)+3)/4:1;__m128i lut=_mm_setr_epi8('0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F');uint64_t text=uint64_t(_mm_cvtsi128_si64(tohex(_mm_cvtsi32_si128(__builtin_bswap32(v)),lut)));text>>=8*(8-len);_mm_mask_storeu_epi8(o,(__mmask16)((1u<<len)-1),_mm_cvtsi64_si128(text));o[len]=10;return len+1;}
static inline void uuid(const State*s,size_t r,uint8_t*o){
 size_t b=r*78;uint32_t low=uint32_t(bits(s->p,b));uint64_t node=bits(s->p,b+32)&0x3fffffffffffull;node=(node&0xffffffffffull)|((node>>40)<<42)|(3ull<<40);uint32_t first=uint32_t(s->h.base)+10*(low&0x3ffff);uint32_t seq=(low>>18)|0x8000;
 uint64_t a=__builtin_bswap64((uint64_t(first)<<32)|(uint64_t(seq)<<16)|(node>>32));uint64_t z=__builtin_bswap64(node<<32);__m128i in=_mm_set_epi64x(z,a),lut=_mm_setr_epi8('0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'),m=_mm_set1_epi8(15);__m128i hi=_mm_shuffle_epi8(lut,_mm_and_si128(_mm_srli_epi16(in,4),m)),lo=_mm_shuffle_epi8(lut,_mm_and_si128(in,m));__m128i x=_mm_unpacklo_epi8(hi,lo),y=_mm_unpackhi_epi8(hi,lo);uint64_t v=uint64_t(_mm_cvtsi128_si64(x));memcpy(o,&v,8);memcpy(o+8,"-2da5-11e8-",11);uint32_t q=uint32_t(_mm_extract_epi32(x,2));memcpy(o+19,&q,4);o[23]='-';q=uint32_t(_mm_extract_epi32(x,3));memcpy(o+24,&q,4);v=uint64_t(_mm_cvtsi128_si64(y));memcpy(o+28,&v,8);o[36]=10;
}
static int64_t decode(State*s,uint8_t*out,size_t cap){if(!s||s->h.raw>cap)return -1;size_t n=s->h.rows;
 switch(s->h.kind){
 case 1:for(size_t i=0;i<n;i++){uint32_t v=nameval(s,i);name(v,out+i*19);}break;
 case 2:for(size_t i=0;i<n;i++)genome(bits(s->p,i*18)&0x3ffff,out+i*10);break;
 case 3:{size_t pos=0;for(size_t i=0;i<n;i++){uint32_t v=u32(s->p+i*4);unsigned l=v?(32-__builtin_clz(v)+3)/4+1:2;if(l>cap-pos)return -1;pos+=hex(v,out+pos);}if(pos!=s->h.raw)return -1;break;}
 case 4:for(size_t i=0;i<n;i++)uuid(s,i,out+i*37);break;
 }return s->h.raw;}
static int64_t rows(State*s,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets){if(!s||!offsets)return -1;offsets[0]=0;size_t pos=0;unsigned fixed=s->h.kind==1?19:s->h.kind==2?10:s->h.kind==4?37:0;if(fixed&&count>cap/fixed)return -1;
 switch(s->h.kind){
 case 1:for(size_t j=0;j<count;j++){size_t i=ids[j];if(i>=s->h.rows)return -1;name(nameval(s,i),out+j*19);offsets[j+1]=(j+1)*19;}return count*19;
 case 2:for(size_t j=0;j<count;j++){size_t i=ids[j];if(i>=s->h.rows)return -1;genome(bits(s->p,i*18)&0x3ffff,out+j*10);offsets[j+1]=(j+1)*10;}return count*10;
 case 4:for(size_t j=0;j<count;j++){size_t i=ids[j];if(i>=s->h.rows)return -1;uuid(s,i,out+j*37);offsets[j+1]=(j+1)*37;}return count*37;
 case 3:for(size_t j=0;j<count;j++){size_t i=ids[j];if(i>=s->h.rows)return -1;uint32_t v=u32(s->p+i*4);unsigned l=v?(32-__builtin_clz(v)+3)/4+1:2;if(l>cap-pos)return -1;pos+=hex(v,out+pos);offsets[j+1]=pos;}return pos;
 }return -1;}

#endif
}
