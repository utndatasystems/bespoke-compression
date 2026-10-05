#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cstring>
#include <vector>
#include <new>
#include <immintrin.h>
namespace structured {
static constexpr uint64_t magic = 0x3152445453584454ULL;
struct Header {uint64_t magic,bytes,rows;uint32_t kind,reserved;};
struct State {const uint8_t* data;uint64_t n,bytes;uint32_t kind;uint16_t dec[128];};
static inline uint32_t rd32(const void*p){uint32_t v;memcpy(&v,p,4);return v;}
static inline uint64_t rd64(const void*p){uint64_t v;memcpy(&v,p,8);return v;}
static inline uint32_t value18(const uint8_t*p,uint64_t i){uint64_t b=i*18;return (rd32(p+(b>>3))>>(b&7))&262143;}
#ifdef ENCODER
static inline bool encode(const uint8_t*raw,size_t sz,std::vector<uint8_t>&out){
 uint32_t kind=0;size_t n=0;
 if(sz && sz%19==0 && !memcmp(raw,"Customer#000",12)) {kind=1;n=sz/19;for(size_t i=0;i<n;i++){const uint8_t*p=raw+i*19;if(memcmp(p,"Customer#000",12)||p[18]!=10)return false;uint32_t v=0;for(int j=12;j<18;j++){if(p[j]<'0'||p[j]>'9')return false;v=v*10+p[j]-'0';}if(v>159999)return false;}}
 else if(sz && sz%10==0 && (raw[0]=='a'||raw[0]=='c'||raw[0]=='g'||raw[0]=='t')) {kind=2;n=sz/10;for(size_t i=0;i<n;i++){const uint8_t*p=raw+i*10;if(p[9]!=10)return false;for(int j=0;j<9;j++)if(p[j]!='a'&&p[j]!='c'&&p[j]!='g'&&p[j]!='t')return false;}}
 else if(sz && sz%37==0 && !memcmp(raw,"84",2)&&!memcmp(raw+8,"-2da5-11e8-",11)){kind=4;n=sz/37;for(size_t i=0;i<n;i++){const uint8_t*p=raw+i*37;if(memcmp(p,"84",2)||memcmp(p+8,"-2da5-11e8-",11)||p[23]!='-'||p[36]!=10)return false;for(int j=2;j<36;j++){if(j==8||j==13||j==18||j==23)continue;if(!((p[j]>='0'&&p[j]<='9')||(p[j]>='a'&&p[j]<='f')))return false;}}}
 else if(sz && ((raw[0]>='0'&&raw[0]<='9')||(raw[0]>='A'&&raw[0]<='F'))){kind=3;size_t len=0;for(size_t i=0;i<sz;i++){uint8_t c=raw[i];if(c==10){if(!len||len>8)return false;n++;len=0;}else {if(!((c>='0'&&c<='9')||(c>='A'&&c<='F')))return false;if(!len&&c=='0'&&i+1<sz&&raw[i+1]!=10)return false;len++;}}if(len||!n)return false;}
 else return false;
 size_t payload=kind==1||kind==2?(n*18+7)/8:kind==3?n*4:n*11;
 out.assign(sizeof(Header)+payload+16,0);Header h{magic,sz,n,kind,0};memcpy(out.data(),&h,sizeof(h));uint8_t*d=out.data()+sizeof(h);
 if(kind==1||kind==2){for(size_t i=0;i<n;i++){uint32_t v=0;if(kind==1){for(int j=12;j<18;j++)v=v*10+raw[i*19+j]-'0';v=((v/10000)<<14)|((v/100%100)<<7)|(v%100);}else{for(int j=0;j<9;j++){uint8_t c=raw[i*10+j];uint32_t b=c=='a'?0:c=='c'?1:c=='g'?2:3;v|=b<<(j*2);}}size_t bit=i*18;uint32_t w=v<<(bit&7);size_t pos=bit>>3;d[pos]|=w;d[pos+1]|=w>>8;d[pos+2]|=w>>16;d[pos+3]|=w>>24;}}
 else if(kind==3){size_t row=0;uint32_t v=0;for(size_t i=0;i<sz;i++){uint8_t c=raw[i];if(c==10){memcpy(d+row*4,&v,4);row++;v=0;}else v=v*16+(c<='9'?c-'0':c-'A'+10);}}
 else{auto cv=[](uint8_t c){return c<='9'?c-'0':c-'a'+10;};for(size_t i=0;i<n;i++){const uint8_t*p=raw+i*37;uint8_t*q=d+i*11;int z=0;for(int j=2;j<8;j+=2)q[z++]=cv(p[j])*16+cv(p[j+1]);for(int j=19;j<23;j+=2)q[z++]=cv(p[j])*16+cv(p[j+1]);for(int j=24;j<36;j+=2)q[z++]=cv(p[j])*16+cv(p[j+1]);}}
 return true;
}
#endif
static inline void* open(const uint8_t*data,size_t size){
 if(!data||size<sizeof(Header))return nullptr;Header h;memcpy(&h,data,sizeof(h));if(h.magic!=magic||h.kind<1||h.kind>4||h.rows>UINT64_MAX/37)return nullptr;
 uint64_t payload=h.kind<=2?(h.rows*18+7)/8:h.kind==3?h.rows*4:h.rows*11;
 if(payload>size-sizeof(Header)||size-sizeof(Header)-payload<16)return nullptr;
 if(h.kind==1&&h.bytes!=h.rows*19)return nullptr;if(h.kind==2&&h.bytes!=h.rows*10)return nullptr;if(h.kind==4&&h.bytes!=h.rows*37)return nullptr;if(h.kind==3&&(h.bytes<h.rows*2||h.bytes>h.rows*9))return nullptr;
 State*s=new(std::nothrow) State;if(!s)return nullptr;s->data=data+sizeof(Header);s->n=h.rows;s->bytes=h.bytes;s->kind=h.kind;
 if(h.kind==1)for(int i=0;i<128;i++)s->dec[i]=uint16_t('0'+i/10)|(uint16_t('0'+i%10)<<8);
 return s;
}
static inline void cname(const State*s,uint32_t v,uint8_t*out){
 memcpy(out,"Customer#000",12);uint32_t a=v>>14,b=(v>>7)&127,c=v&127;memcpy(out+12,s->dec+a,2);memcpy(out+14,s->dec+b,2);memcpy(out+16,s->dec+c,2);out[18]=10;
}
static inline void genome(const State*s,uint32_t v,uint8_t*out){__m128i q=_mm_multishift_epi64_epi8(_mm_set1_epi64x(0x0e0c0a0806040200LL),_mm_set1_epi64x(v));q=_mm_and_si128(q,_mm_set1_epi8(3));q=_mm_shuffle_epi8(_mm_setr_epi8('a','c','g','t',0,0,0,0,0,0,0,0,0,0,0,0),q);_mm_storel_epi64((__m128i*)out,q);out[8]="acgt"[v>>16];out[9]=10;}
static inline uint64_t hex8(uint32_t v){
 __m128i x=_mm_cvtsi32_si128((int)__builtin_bswap32(v));const __m128i mask=_mm_set1_epi8(15);__m128i hi=_mm_and_si128(_mm_srli_epi16(x,4),mask),lo=_mm_and_si128(x,mask);__m128i nib=_mm_unpacklo_epi8(hi,lo);__m128i ascii=_mm_shuffle_epi8(_mm_setr_epi8('0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'),nib);return (uint64_t)_mm_cvtsi128_si64(ascii);
}
static inline unsigned hexlen(uint32_t v){return v?(32-__builtin_clz(v)+3)/4:1;}
static inline unsigned hexrow(uint32_t v,uint8_t*out,size_t capacity){unsigned len=hexlen(v);uint64_t bytes=hex8(v)>>((8-len)*8);if(capacity>=8)memcpy(out,&bytes,8);else memcpy(out,&bytes,len);out[len]=10;return len+1;}
static inline uint64_t hex4(__m128i x,uint8_t*out,const unsigned*len){
 const __m128i bswap=_mm_setr_epi8(3,2,1,0,7,6,5,4,11,10,9,8,15,14,13,12);x=_mm_shuffle_epi8(x,bswap);
 __m128i m=_mm_set1_epi8(15),hi=_mm_and_si128(_mm_srli_epi16(x,4),m),lo=_mm_and_si128(x,m);__m256i nib=_mm256_set_m128i(_mm_unpackhi_epi8(hi,lo),_mm_unpacklo_epi8(hi,lo));
 const __m256i lut=_mm256_broadcastsi128_si256(_mm_setr_epi8('0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'));__m256i chars=_mm256_shuffle_epi8(lut,nib);
 alignas(64) static const uint8_t ix[64]={0,1,2,3,4,5,6,7,0,8,9,10,11,12,13,14,15,0,16,17,18,19,20,21,22,23,0,24,25,26,27,28,29,30,31};
 constexpr uint64_t nl=(1ULL<<8)|(1ULL<<17)|(1ULL<<26)|(1ULL<<35);constexpr uint64_t all=(1ULL<<36)-1;
 __m512i q=_mm512_maskz_permutexvar_epi8(all^nl,_mm512_load_si512(ix),_mm512_broadcast_i64x4(chars));q=_mm512_mask_mov_epi8(q,nl,_mm512_set1_epi8(10));
 uint64_t mask=all;mask^=((1ULL<<(8-len[0]))-1);mask^=((1ULL<<(8-len[1]))-1)<<9;mask^=((1ULL<<(8-len[2]))-1)<<18;mask^=((1ULL<<(8-len[3]))-1)<<27;
 _mm512_mask_compressstoreu_epi8(out,mask,q);return len[0]+len[1]+len[2]+len[3]+4;
}
static inline void uuid(const uint8_t*p,uint8_t*out){
 __m128i x=_mm_loadu_si128((const __m128i*)p),m=_mm_set1_epi8(15);__m128i hi=_mm_and_si128(_mm_srli_epi16(x,4),m),lo=_mm_and_si128(x,m);__m256i nib=_mm256_set_m128i(_mm_unpackhi_epi8(hi,lo),_mm_unpacklo_epi8(hi,lo));
 const __m256i lut=_mm256_broadcastsi128_si256(_mm_setr_epi8('0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'));__m256i chars=_mm256_shuffle_epi8(lut,nib);
 alignas(64) static const uint8_t ix[64]={0,0,0,1,2,3,4,5,0,0,0,0,0,0,0,0,0,0,0,6,7,8,9,0,10,11,12,13,14,15,16,17,18,19,20,21};
 alignas(64) static const uint8_t fixed[64]={'8','4',0,0,0,0,0,0,'-','2','d','a','5','-','1','1','e','8','-',0,0,0,0,'-',0,0,0,0,0,0,0,0,0,0,0,0,10};
 constexpr uint64_t mask=((uint64_t(63)<<2)|(uint64_t(15)<<19)|(uint64_t(4095)<<24));
 __m512i q=_mm512_maskz_permutexvar_epi8(mask,_mm512_load_si512(ix),_mm512_broadcast_i64x4(chars));q=_mm512_or_si512(q,_mm512_load_si512(fixed));_mm512_mask_storeu_epi8(out,(uint64_t(1)<<37)-1,q);
}
static inline int64_t decode(void*state,uint8_t*out,size_t capacity){
 State*s=(State*)state;if(!s||capacity<s->bytes||(!out&&s->bytes))return -1;const uint8_t*p=s->data;uint64_t n=s->n;
 if(s->kind==1){for(uint64_t i=0;i<n;i++)cname(s,value18(p,i),out+i*19);}
 else if(s->kind==2){for(uint64_t i=0;i<n;i++)genome(s,value18(p,i),out+i*10);}
 else if(s->kind==3){size_t off=0;uint64_t i=0;for(;i+4<=n;i+=4){uint32_t a=rd32(p+i*4),b=rd32(p+i*4+4),c=rd32(p+i*4+8),d=rd32(p+i*4+12);unsigned lens[4]={hexlen(a),hexlen(b),hexlen(c),hexlen(d)};unsigned sum=lens[0]+lens[1]+lens[2]+lens[3]+4;if(capacity-off<sum)return -1;off+=hex4(_mm_loadu_si128((const __m128i*)(p+i*4)),out+off,lens);}for(;i<n;i++){uint32_t v=rd32(p+i*4);unsigned len=hexlen(v)+1;if(capacity-off<len)return -1;off+=hexrow(v,out+off,capacity-off);}if(off!=s->bytes)return -1;}
 else{for(uint64_t i=0;i<n;i++)uuid(p+i*11,out+i*37);}
 return (int64_t)s->bytes;
}
static inline int64_t rows(void*state,const uint64_t*ids,size_t count,uint8_t*out,size_t capacity,uint64_t*offsets){
 State*s=(State*)state;if(!s||!offsets||(!ids&&count)||(!out&&count))return -1;offsets[0]=0;if(!count)return 0;
 uint64_t stride=s->kind==1?19:s->kind==2?10:s->kind==4?37:0;if(stride&&count>capacity/stride)return -1;
 const uint8_t*p=s->data;uint64_t off=0;
 if(s->kind==1)for(size_t i=0;i<count;i++){if(ids[i]>=s->n)return -1;cname(s,value18(p,ids[i]),out+off);off+=19;offsets[i+1]=off;}
 else if(s->kind==2)for(size_t i=0;i<count;i++){if(ids[i]>=s->n)return -1;genome(s,value18(p,ids[i]),out+off);off+=10;offsets[i+1]=off;}
 else if(s->kind==3){size_t i=0;for(;i+4<=count;i+=4){if(ids[i]>=s->n||ids[i+1]>=s->n||ids[i+2]>=s->n||ids[i+3]>=s->n)return -1;uint32_t a=rd32(p+ids[i]*4),b=rd32(p+ids[i+1]*4),c=rd32(p+ids[i+2]*4),d=rd32(p+ids[i+3]*4);unsigned lens[4]={hexlen(a),hexlen(b),hexlen(c),hexlen(d)};unsigned sum=lens[0]+lens[1]+lens[2]+lens[3]+4;if(capacity-off<sum)return -1;hex4(_mm_setr_epi32(a,b,c,d),out+off,lens);for(int j=0;j<4;j++){off+=lens[j]+1;offsets[i+j+1]=off;}}for(;i<count;i++){if(ids[i]>=s->n)return -1;uint32_t v=rd32(p+ids[i]*4);unsigned len=hexlen(v)+1;if(capacity-off<len)return -1;off+=hexrow(v,out+off,capacity-off);offsets[i+1]=off;}}
 else for(size_t i=0;i<count;i++){if(ids[i]>=s->n)return -1;uuid(p+ids[i]*11,out+off);off+=37;offsets[i+1]=off;}
 return (int64_t)off;
}
static inline void close(void*state){delete (State*)state;}
}
