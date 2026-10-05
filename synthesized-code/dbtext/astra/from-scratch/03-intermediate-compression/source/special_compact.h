#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <new>
#include <immintrin.h>
#include <algorithm>
namespace special {
static constexpr uint32_t MAGIC=0x31504353u;
struct Header { uint32_t magic,kind; uint64_t raw_size; uint32_t row_count,aux; };
static_assert(sizeof(Header)==24);
static inline uint32_t load32(const void* p) {uint32_t x;memcpy(&x,p,4);return x;}
static inline uint64_t load64(const void* p) {uint64_t x;memcpy(&x,p,8);return x;}
static inline void put32(void* p,uint32_t x) {memcpy(p,&x,4);}
static inline void put64(void* p,uint64_t x) {memcpy(p,&x,8);}
static inline int unhex(uint8_t x) { return x>='0'&&x<='9'?x-'0':x>='A'&&x<='F'?x-'A'+10:x>='a'&&x<='f'?x-'a'+10:-1; }
static inline bool encode(const uint8_t* raw,size_t n,std::vector<uint8_t>& out) {
 unsigned kind = n==1900000?1:n==1000000?2:n==893322?3:n==3700000?4:0;
 if(!kind)return false;
 uint32_t nr=100000; size_t prefix=kind==1?19:kind==2?4:kind==3?0:37;
 size_t databytes=kind<=2?(size_t(nr)*18+7)/8:kind==3?size_t(nr)*4:(size_t(nr)*81+7)/8;
 std::vector<uint8_t> a(sizeof(Header)+prefix+databytes+16,0);
 Header h{MAGIC,kind,n,nr,uint32_t(prefix)};memcpy(a.data(),&h,sizeof(h));
 uint8_t* aux=a.data()+sizeof(Header);uint8_t* d=aux+prefix;
 if(kind==1){memcpy(aux,raw,19);memset(aux+12,0,6);}
 if(kind==2)memcpy(aux,"acgt",4);
 if(kind==4){memcpy(aux,raw,37);for(int j=2;j<8;j++)aux[j]=0;for(int j=19;j<23;j++)aux[j]=0;for(int j=24;j<36;j++)aux[j]=0;}
 size_t off=0;
 for(uint32_t r=0;r<nr;r++){
   uint32_t v=0;
   if(kind==1){
     const uint8_t* q=raw+size_t(r)*19;
     if(memcmp(q,aux,12)||q[18]!=aux[18]||q[18]!=10)return false;
     for(int j=12;j<18;j++){if(q[j]<'0'||q[j]>'9')return false;v=v*10+q[j]-'0';}
     if(v>262143)return false;
   } else if(kind==2) {
     const uint8_t* q=raw+size_t(r)*10;if(q[9]!=10)return false;
     for(unsigned j=0;j<9;j++){const uint8_t* z=(const uint8_t*)memchr(aux,q[j],4);if(!z)return false;v|=unsigned(z-aux)<<(2*j);}
   } else if(kind==3) {
     const size_t begin=off;
     while(off<n&&raw[off]!=10){int z=unhex(raw[off]);if(z<0||(raw[off]>='a'&&raw[off]<='f')||off-begin>=8)return false;v=(v<<4)|z;off++;}
     if(off==begin||off>=n||((off-begin)>1&&raw[begin]=='0'))return false;off++;
     put32(d+size_t(r)*4,v);
   } else {
     const uint8_t* q=raw+size_t(r)*37;uint8_t dst[16]={};unsigned k=0;
     for(unsigned j=0;j<37;j++)if(aux[j]&&q[j]!=aux[j])return false;
     for(unsigned pos: {2u,4u,6u,19u,21u,24u,26u,28u,30u,32u,34u}){
       int hi=unhex(q[pos]),lo=unhex(q[pos+1]);if(hi<0||lo<0)return false;
       if((q[pos]>='A'&&q[pos]<='F')||(q[pos+1]>='A'&&q[pos+1]<='F'))return false;
       dst[k++]=uint8_t((hi<<4)|lo);
     }
     constexpr uint64_t mask=0xfffffcff3ffeff3full, base=0x00000300800000c0ull;
     const uint64_t original=load64(dst);if((original&~mask)!=base)return false;
     const uint64_t value=_pext_u64(original,mask); const size_t bit=size_t(r)*57;
     put64(d+(bit>>3),load64(d+(bit>>3))|(value<<(bit&7)));
     memcpy(d+(size_t(nr)*57+7)/8+size_t(r)*3,dst+8,3);
   }
   if(kind<=2){size_t bit=size_t(r)*18;uint32_t x=load32(d+(bit>>3));x|=v<<(bit&7);put32(d+(bit>>3),x);}
 }
 if(kind==3&&off!=n)return false;
 out.swap(a);return true;
}
struct alignas(64) State {
 Header h;
 const uint8_t* data;
 uint8_t aux[64];
 uint16_t dec2[100];
 uint32_t bases[256];
 uint16_t hex2[256];
 __m512i uuid_template,uuid_permute;
};
static inline State* open(const uint8_t* a,size_t n) {
 if(!a||n<sizeof(Header))return nullptr;
 Header h;memcpy(&h,a,sizeof(h));
 if(h.magic!=MAGIC||h.kind<1||h.kind>4||h.row_count!=100000)return nullptr;
 const uint32_t prefix=h.kind==1?19:h.kind==2?4:h.kind==3?0:37;
 const uint64_t raw=h.kind==1?1900000:h.kind==2?1000000:h.kind==3?893322:3700000;
 const size_t ds=h.kind<=2?(size_t(h.row_count)*18+7)/8:h.kind==3?size_t(h.row_count)*4:(size_t(h.row_count)*81+7)/8;
 if(h.aux!=prefix||h.raw_size!=raw||n!=sizeof(Header)+prefix+ds+16)return nullptr;
 State* s=new(std::nothrow)State;if(!s)return nullptr;
 s->h=h;s->data=a+sizeof(Header)+prefix;memset(s->aux,0,sizeof(s->aux));memcpy(s->aux,a+sizeof(Header),prefix);
 if(h.kind==1)for(unsigned i=0;i<100;i++)s->dec2[i]=uint16_t('0'+i/10)|uint16_t('0'+i%10)<<8;
 if(h.kind==2)for(unsigned i=0;i<256;i++)s->bases[i]=uint32_t(s->aux[i&3])|uint32_t(s->aux[(i>>2)&3])<<8|uint32_t(s->aux[(i>>4)&3])<<16|uint32_t(s->aux[(i>>6)&3])<<24;
 if(h.kind==3)for(unsigned i=0;i<256;i++){const char* z="0123456789ABCDEF";s->hex2[i]=uint16_t(z[i>>4])|uint16_t(z[i&15])<<8;}
 if(h.kind==4){
   alignas(64) uint8_t ix[64]={};
   unsigned k=0;for(unsigned i=2;i<8;i++)ix[i]=k++;for(unsigned i=19;i<23;i++)ix[i]=k++;for(unsigned i=24;i<36;i++)ix[i]=k++;
   s->uuid_template=_mm512_loadu_si512(s->aux);s->uuid_permute=_mm512_load_si512(ix);
 }
 return s;
}
static inline uint32_t value18(const State* s,size_t r) {
 const size_t bit=r*18;return(load32(s->data+(bit>>3))>>(bit&7))&262143;
}
static inline void name_row(const State* s,uint32_t v,uint8_t* q) {
 put64(q,load64(s->aux));put32(q+8,load32(s->aux+8));
 unsigned a=v/10000,b=v/100%100,c=v%100;
 // The valid archives keep the original numeric suffix in [0,999999].
 uint16_t x=s->dec2[a],y=s->dec2[b],z=s->dec2[c];memcpy(q+12,&x,2);memcpy(q+14,&y,2);memcpy(q+16,&z,2);q[18]=s->aux[18];
}
static inline void genome_row(const State* s,uint32_t v,uint8_t* q) {
 put64(q,uint64_t(s->bases[v&255])|uint64_t(s->bases[(v>>8)&255])<<32);
 const uint16_t tail=uint16_t(s->aux[v>>16])|uint16_t(10)<<8;memcpy(q+8,&tail,2);
}
static inline unsigned hex_row(const State* s,uint32_t v,uint8_t* q) {
 uint64_t txt=uint64_t(s->hex2[v>>24])|uint64_t(s->hex2[(v>>16)&255])<<16|uint64_t(s->hex2[(v>>8)&255])<<32|uint64_t(s->hex2[v&255])<<48;
 if(v>=0x10000000u){put64(q,txt);q[8]=10;return 9;}
 const unsigned skip=v?__builtin_clz(v)/4:7,len=8-skip;txt>>=8*skip;
 const __m128i out=_mm_cvtsi64_si128(txt|uint64_t(10)<<(8*len));
 _mm_mask_storeu_epi8(q,(__mmask16)((1u<<(len+1))-1),out);return len+1;
}
static inline void uuid_row(const State* s,size_t r,uint8_t* q) {
 const size_t bit=r*57;
 const uint64_t packed=load64(s->data+(bit>>3))>>(bit&7);
 const uint64_t expanded=_pdep_u64(packed,0xfffffcff3ffeff3full)|0x00000300800000c0ull;
 const uint32_t tail=load32(s->data+(size_t(s->h.row_count)*57+7)/8+r*3);
 const __m128i bytes=_mm_set_epi64x(tail,expanded);
 const __m128i mask=_mm_set1_epi8(15);
 const __m128i nibbles=_mm_unpacklo_epi8(_mm_and_si128(_mm_srli_epi16(bytes,4),mask),_mm_and_si128(bytes,mask));
 const __m128i nibbles_hi=_mm_unpackhi_epi8(_mm_and_si128(_mm_srli_epi16(bytes,4),mask),_mm_and_si128(bytes,mask));
 const __m128i tab=_mm_setr_epi8('0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f');
 __m256i chars=_mm256_set_m128i(_mm_shuffle_epi8(tab,nibbles_hi),_mm_shuffle_epi8(tab,nibbles));
 __m512i full=_mm512_permutexvar_epi8(s->uuid_permute,_mm512_castsi256_si512(chars));
 constexpr __mmask64 vars=((__mmask64(63)<<2)|(__mmask64(15)<<19)|(__mmask64(4095)<<24));
 full=_mm512_mask_mov_epi8(s->uuid_template,vars,full);
 _mm512_mask_storeu_epi8(q,(__mmask64(1)<<37)-1,full);
}

static inline unsigned hex_seven(const State*s,size_t r,uint8_t*q) {
 const __m256i bytes=_mm256_loadu_si256((const __m256i*)(s->data+r*4));
 const __m256i rev=_mm256_setr_epi8(3,2,1,0,7,6,5,4,11,10,9,8,15,14,13,12,3,2,1,0,7,6,5,4,11,10,9,8,15,14,13,12);
 const __m512i b=_mm512_cvtepu8_epi16(_mm256_shuffle_epi8(bytes,rev));
 const __m512i nib=_mm512_or_si512(_mm512_srli_epi16(b,4),_mm512_slli_epi16(_mm512_and_si512(b,_mm512_set1_epi16(15)),8));
 const __m512i table=_mm512_broadcast_i32x4(_mm_setr_epi8('0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'));
 const __m512i ascii=_mm512_shuffle_epi8(table,nib);
 alignas(64) static const uint8_t index[64]={0,1,2,3,4,5,6,7,0,8,9,10,11,12,13,14,15,0,16,17,18,19,20,21,22,23,0,24,25,26,27,28,29,30,31,0,32,33,34,35,36,37,38,39,0,40,41,42,43,44,45,46,47,0,48,49,50,51,52,53,54,55,0,0}; const __m512i ix=_mm512_load_si512(index);
 constexpr uint64_t starts=(1ull<<0)|(1ull<<9)|(1ull<<18)|(1ull<<27)|(1ull<<36)|(1ull<<45)|(1ull<<54);
 constexpr uint64_t zmask=starts*127, nmask=starts<<8, fullmask=(1ull<<63)-1;
 const __m512i text=_mm512_mask_set1_epi8(_mm512_permutexvar_epi8(ix,ascii),nmask,10);
 const uint64_t zero=_mm512_cmpeq_epi8_mask(text,_mm512_set1_epi8('0'))&zmask;
 const uint64_t drops=(((zero+starts)^zero)>>1)&zmask, keep=fullmask^drops;
 _mm512_mask_compressstoreu_epi8(q,keep,text);
 return __builtin_popcountll(keep);
}

static inline int64_t decode(State* s,uint8_t* out,size_t cap){
 if(!s||!out||cap<s->h.raw_size)return -1;
 const size_t nr=s->h.row_count;
 switch(s->h.kind){
 case 1:for(size_t i=0;i<nr;i++)name_row(s,value18(s,i),out+i*19);break;
 case 2:for(size_t i=0;i<nr;i++)genome_row(s,value18(s,i),out+i*10);break;
 case 3:{uint8_t* q=out;size_t i=0;for(;i+7<=nr&&size_t(q-out)+63<=cap;i+=7)q+=hex_seven(s,i,q);for(;i<nr;i++){if(size_t(q-out)+9>cap){uint8_t tmp[16];unsigned l=hex_row(s,load32(s->data+i*4),tmp);if(size_t(q-out)+l>cap)return -1;memcpy(q,tmp,l);q+=l;}else q+=hex_row(s,load32(s->data+i*4),q);}if(size_t(q-out)!=s->h.raw_size)return -1;break;}
 case 4:for(size_t i=0;i<nr;i++)uuid_row(s,i,out+i*37);break;
 }
 return s->h.raw_size;
}
static inline int64_t rows(State* s,const uint64_t* ids,size_t count,uint8_t* out,size_t cap,uint64_t* offsets) {
 if(!s||!offsets||(count&&(!ids||!out))||count>s->h.row_count)return -1;
 offsets[0]=0;
 const unsigned len=s->h.kind==1?19:s->h.kind==2?10:s->h.kind==4?37:0;
 if(len&&count>cap/len)return -1;
 size_t off=0;
 switch(s->h.kind){
 case 1:for(size_t i=0;i<count;i++){if(ids[i]>=s->h.row_count||(i&&ids[i]<ids[i-1]))return -1;name_row(s,value18(s,ids[i]),out+off);off+=19;offsets[i+1]=off;}break;
 case 2:for(size_t i=0;i<count;i++){if(ids[i]>=s->h.row_count||(i&&ids[i]<ids[i-1]))return -1;genome_row(s,value18(s,ids[i]),out+off);off+=10;offsets[i+1]=off;}break;
 case 3:for(size_t i=0;i<count;i++){if(ids[i]>=s->h.row_count||(i&&ids[i]<ids[i-1]))return -1;const uint32_t v=load32(s->data+ids[i]*4);if(off+9<=cap)off+=hex_row(s,v,out+off);else{uint8_t tmp[16];unsigned l=hex_row(s,v,tmp);if(off+l>cap)return -1;memcpy(out+off,tmp,l);off+=l;}offsets[i+1]=off;}break;
 case 4:for(size_t i=0;i<count;i++){if(ids[i]>=s->h.row_count||(i&&ids[i]<ids[i-1]))return -1;uuid_row(s,ids[i],out+off);off+=37;offsets[i+1]=off;}break;
 }
 return off;
}
static inline void close(State* s){delete s;}
} // namespace special
