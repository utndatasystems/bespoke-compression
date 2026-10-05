#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <immintrin.h>
#ifdef ENCODER
#include <vector>
#endif

namespace sp {
struct __attribute__((packed)) Header {
  uint32_t mode, rows;
  uint64_t rawsize;
  uint64_t total;
  uint8_t tmpl[64];
};
static_assert(sizeof(Header)==88);
inline uint32_t load32(const void*p) {uint32_t x; memcpy(&x,p,4);return x;}
inline void put32(void*p,uint32_t x) {memcpy(p,&x,4);}
inline void put64(void*p,uint64_t x) {memcpy(p,&x,8);}
inline uint16_t load16(const void*p){uint16_t x;memcpy(&x,p,2);return x;}
#ifdef ENCODER
inline int nib(uint8_t c) { if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1; }
inline std::vector<uint8_t> encode(const uint8_t *p,size_t n){
 std::vector<uint8_t>a; Header h{};uint32_t mode=0;size_t stride=0,rows=0;
 if(n%19==0&&n>=19&&!memcmp(p,"Customer#",9)) {mode=100;stride=3;rows=n/19;}
 else if(n%10==0&&n>=10&&p[9]=='\n'){bool good=true;for(size_t i=0;i<9;i++)if(p[i]!='a'&&p[i]!='c'&&p[i]!='g'&&p[i]!='t')good=false;if(good){mode=101;stride=3;rows=n/10;}}
 if(!mode&&n%37==0&&n>=37&&p[8]=='-'&&p[13]=='-'&&p[18]=='-'&&p[23]=='-'&&p[36]=='\n'){mode=103;stride=11;rows=n/37;}
 if(!mode){bool good=n&&p[n-1]=='\n';size_t len=0;for(size_t i=0;good&&i<n;i++){if(p[i]=='\n'){if(!len||len>8)good=false;rows++;len=0;}else{if(nib(p[i])<0||(p[i]>='a'&&p[i]<='f')||(!len&&p[i]=='0'&&i+1<n&&p[i+1]!='\n'))good=false;len++;}}if(good){mode=102;stride=4;}}
 if(!mode||rows>UINT32_MAX)return {};
 h.mode=mode;h.rows=rows;h.rawsize=n;h.total=sizeof(Header)+stride*rows+32;
 a.resize(h.total);uint8_t *d=a.data()+sizeof(Header);
 if(mode==100){
   memcpy(h.tmpl,p,12);
   for(size_t r=0;r<rows;r++){const uint8_t*s=p+r*19;if(memcmp(s,h.tmpl,12)||s[18]!='\n')return {};unsigned x=0;for(int i=12;i<18;i++){if(s[i]<'0'||s[i]>'9')return {};x=x*10+s[i]-'0';}if(x>=256000)return {};*d++=x/1000;uint16_t lo=x%1000;memcpy(d,&lo,2);d+=2;}
 } else if(mode==101){
   for(size_t r=0;r<rows;r++){const uint8_t*s=p+r*10;unsigned v=0;if(s[9]!='\n')return {};for(int i=0;i<9;i++){unsigned q;if(s[i]=='a')q=0;else if(s[i]=='c')q=1;else if(s[i]=='g')q=2;else if(s[i]=='t')q=3;else return {};v|=q<<(2*i);}*d++=v;*d++=v>>8;*d++=v>>16;}
 } else if(mode==102){
   const uint8_t*s=p;for(size_t r=0;r<rows;r++){uint32_t v=0;while(*s!='\n')v=(v<<4)|nib(*s++);s++;memcpy(d,&v,4);d+=4;}
 } else {
   memcpy(h.tmpl,p,37);static const uint8_t pos[11]={2,4,6,19,21,24,26,28,30,32,34};for(int j=0;j<11;j++){h.tmpl[pos[j]]=0;h.tmpl[pos[j]+1]=0;}
   for(size_t r=0;r<rows;r++){const uint8_t*s=p+r*37;for(int j=0;j<37;j++)if(h.tmpl[j]&&s[j]!=h.tmpl[j])return {};for(int j=0;j<11;j++){int x=nib(s[pos[j]]),y=nib(s[pos[j]+1]);if(x<0||y<0||(s[pos[j]]>='A'&&s[pos[j]]<='F')||(s[pos[j]+1]>='A'&&s[pos[j]+1]<='F'))return {};*d++=x*16+y;}}
 }
 memcpy(a.data(),&h,sizeof h);return a;
}
#endif
#ifdef DECODER
struct Tables{
 uint32_t dec[1024]; uint32_t dna[256];uint16_t hex[256];
 constexpr Tables():dec{},dna{},hex{} {
  for(unsigned i=0;i<1024;i++)dec[i]=('0'+i/100)|(uint32_t('0'+i/10%10)<<8)|(uint32_t('0'+i%10)<<16);
  for(unsigned i=0;i<256;i++){uint32_t v=0;for(unsigned j=0;j<4;j++){unsigned x=(i>>(j*2))&3;v|=uint32_t(x==0?'a':x==1?'c':x==2?'g':'t')<<(j*8);}dna[i]=v;unsigned hi=i>>4,lo=i&15;hex[i]=(hi<10?'0'+hi:'A'+hi-10)|(uint16_t(lo<10?'0'+lo:'A'+lo-10)<<8);}
 }
};
static constexpr Tables tab{};
inline void* open(const uint8_t*p,size_t n){
 if(n<sizeof(Header))return nullptr;const Header*h=(const Header*)p;size_t stride;
 switch(h->mode){case 100:stride=3;if(h->rawsize!=uint64_t(h->rows)*19)return nullptr;break;case 101:stride=3;if(h->rawsize!=uint64_t(h->rows)*10)return nullptr;break;case 102:stride=4;if(h->rawsize<uint64_t(h->rows)*2||h->rawsize>uint64_t(h->rows)*9)return nullptr;break;case 103:stride=11;if(h->rawsize!=uint64_t(h->rows)*37)return nullptr;break;default:return nullptr;}
 if(h->total!=sizeof(Header)+uint64_t(h->rows)*stride+32||h->total>n)return nullptr;
 return (void*)p;
}
inline void cname(const uint8_t*s,uint8_t*d,const Header*h){
 // Copy the archive's twelve constant prefix bytes, then two decimal triples.
 uint64_t prefix;memcpy(&prefix,h->tmpl,8);put64(d,prefix);put32(d+8,load32(h->tmpl+8));
 uint32_t a=tab.dec[s[0]],b=tab.dec[load16(s+1)&1023];
 put32(d+12,a);put32(d+15,b|(uint32_t('\n')<<24));
}
inline void genome(const uint8_t*s,uint8_t*d){
 put32(d,tab.dna[s[0]]);put32(d+4,tab.dna[s[1]]);
 uint16_t z=uint8_t(tab.dna[s[2]&3])|(uint16_t('\n')<<8);memcpy(d+8,&z,2);
}
inline void genome4(const uint8_t*s,uint8_t*d){
 __m128i packed=_mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)s),_mm_set_epi64x(0x800b0a0980080706ULL,0x8005040380020100ULL));
 __m256i q=_mm256_cvtepu32_epi64(packed);
 __m512i x=_mm512_permutexvar_epi64(_mm512_set_epi64(3,3,2,2,1,1,0,0),_mm512_castsi256_si512(q));
 x=_mm512_multishift_epi64_epi8(_mm512_set_epi64(16,0x0e0c0a0806040200ULL,16,0x0e0c0a0806040200ULL,16,0x0e0c0a0806040200ULL,16,0x0e0c0a0806040200ULL),x);
 x=_mm512_and_si512(x,_mm512_set1_epi8(3));
 x=_mm512_shuffle_epi8(_mm512_set1_epi64(0x7467636174676361ULL),x);
 x=_mm512_mask_mov_epi8(x,(uint64_t(1)<<9)|(uint64_t(1)<<25)|(uint64_t(1)<<41)|(uint64_t(1)<<57),_mm512_set1_epi8('\n'));
 _mm512_mask_compressstoreu_epi8(d,0x03ff03ff03ff03ffULL,x);
}
inline unsigned hexrow(const uint8_t*s,uint8_t*d){
 uint32_t x=load32(s);unsigned len=x?(32-__builtin_clz(x)+3)/4:1,drop=8-len;
 __m128i v=_mm_cvtepu8_epi16(_mm_cvtsi32_si128(__builtin_bswap32(x)));
 v=_mm_and_si128(_mm_or_si128(_mm_srli_epi16(v,4),_mm_slli_epi16(v,8)),_mm_set1_epi16(0x0f0f));
 v=_mm_shuffle_epi8(_mm_set_epi64x(0x4645444342413938ULL,0x3736353433323130ULL),v);
 uint64_t z=_mm_cvtsi128_si64(v);
 if(len==8){put64(d,z);d[8]='\n';}
 else {z>>=drop*8;z|=uint64_t('\n')<<(len*8);_mm_mask_storeu_epi8(d,(__mmask16)((1u<<(len+1))-1),_mm_cvtsi64_si128(z));}
 return len+1;
}
inline unsigned hex4(const uint8_t*s,uint8_t*d){
 __m128i raw=_mm_loadu_si128((const __m128i*)s);
 __m128i dropped=_mm_min_epu32(_mm_srli_epi32(_mm_lzcnt_epi32(raw),2),_mm_set1_epi32(7));
 raw=_mm_shuffle_epi8(raw,_mm_set_epi64x(0x0c0d0e0f08090a0bULL,0x0405060700010203ULL));
 __m256i v=_mm256_cvtepu8_epi16(raw);
 v=_mm256_and_si256(_mm256_or_si256(_mm256_srli_epi16(v,4),_mm256_slli_epi16(v,8)),_mm256_set1_epi16(0x0f0f));
 v=_mm256_shuffle_epi8(_mm256_broadcastsi128_si256(_mm_set_epi64x(0x4645444342413938ULL,0x3736353433323130ULL)),v);
 __m512i full=_mm512_maskz_expand_epi64(0x55,_mm512_castsi256_si512(v));
 full=_mm512_mask_mov_epi8(full,0x0100010001000100ULL,_mm512_set1_epi8('\n'));
 __m512i drop=_mm512_broadcast_i32x4(dropped);
 drop=_mm512_shuffle_epi8(drop,_mm512_set_epi64(0x0c0c0c0c0c0c0c0cULL,0x0c0c0c0c0c0c0c0cULL,0x0808080808080808ULL,0x0808080808080808ULL,0x0404040404040404ULL,0x0404040404040404ULL,0,0));
 __m512i positions=_mm512_broadcast_i32x4(_mm_set_epi64x(0x0f0e0d0c0b0a0908ULL,0x0706050403020100ULL));
 __mmask64 keep=_mm512_cmpge_epi8_mask(positions,drop)&0x01ff01ff01ff01ffULL;
 _mm512_mask_compressstoreu_epi8(d,keep,full);
 return _mm_popcnt_u64(keep);
}
inline __m512i uuid_indices(){
 return _mm512_set_epi64(
  0x0000000000000000ULL,0x0000000000000000ULL,0x0000000000000000ULL,0x0000000015141312ULL,
  0x11100f0e0d0c0b0aULL,0x0009080706000000ULL,0x0000000000000000ULL,0x0504030201000000ULL);
}
inline __m512i uuid_expanded(const uint8_t*s,__m512i templ){
 __m256i x=_mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i*)s));
 x=_mm256_and_si256(_mm256_or_si256(_mm256_srli_epi16(x,4),_mm256_slli_epi16(x,8)),_mm256_set1_epi16(0x0f0f));
 const __m256i alphabet=_mm256_broadcastsi128_si256(_mm_set_epi64x(0x6665646362613938ULL,0x3736353433323130ULL));
 __m256i chars=_mm256_shuffle_epi8(alphabet,x);
 __m512i v=_mm512_permutexvar_epi8(uuid_indices(),_mm512_castsi256_si512(chars));
 constexpr uint64_t variable=((uint64_t(0x3f)<<2)|(uint64_t(0xf)<<19)|(uint64_t(0xfff)<<24));
 return _mm512_mask_mov_epi8(templ,variable,v);
}
inline void uuid(const uint8_t*s,uint8_t*d,__m512i templ){_mm512_mask_storeu_epi8(d,(__mmask64)((uint64_t(1)<<37)-1),uuid_expanded(s,templ));}
inline int64_t decode(void*state,uint8_t*out,size_t cap){
 const Header*h=(const Header*)state;if(!h||cap<h->rawsize)return -1;const uint8_t*s=(const uint8_t*)h+sizeof(Header);uint8_t*d=out;
 switch(h->mode){
 case 100:for(size_t i=0;i<h->rows;i++,s+=3,d+=19)cname(s,d,h);break;
 case 101:{size_t i=0;for(;i+4<=h->rows;i+=4,s+=12,d+=40)genome4(s,d);for(;i<h->rows;i++,s+=3,d+=10)genome(s,d);break;}
 // A batch can write at most 36 bytes; never use rawsize to bypass this bound.
 case 102:{size_t i=0;for(;i+4<=h->rows&&cap-size_t(d-out)>=36;i+=4,s+=16){d+=hex4(s,d);}for(;i<h->rows;i++,s+=4){if(__builtin_expect(cap-size_t(d-out)<9,0)){uint32_t x=load32(s);unsigned n=x?(32-__builtin_clz(x)+3)/4+1:2;if(n>cap-size_t(d-out))return -1;}d+=hexrow(s,d);}break;}
 case 103:{__m512i t=_mm512_loadu_si512(h->tmpl);for(size_t i=0;i<h->rows;i++,s+=11,d+=37)uuid(s,d,t);break;}
 default:return -1;
 }
 return uint64_t(d-out)==h->rawsize?int64_t(d-out):-1;
}
inline int64_t rows(void*state,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offs){
 const Header*h=(const Header*)state;if(!h||!offs)return -1;const uint8_t*base=(const uint8_t*)h+sizeof(Header);offs[0]=0;
 unsigned len=h->mode==100?19:h->mode==101?10:h->mode==103?37:0;
 if(len){
  if(count>cap/len)return -1;
  // The interface guarantees valid sorted IDs. Keep the upper bound check outside the row loop.
  if(count&&ids[count-1]>=h->rows)return -1;
  if(h->mode==100){for(size_t i=0;i<count;i++){cname(base+ids[i]*3,out+i*19,h);offs[i+1]=(i+1)*19;}}
  else if(h->mode==101){for(size_t i=0;i<count;i++){genome(base+ids[i]*3,out+i*10);offs[i+1]=(i+1)*10;}}
  else {__m512i t=_mm512_loadu_si512(h->tmpl);for(size_t i=0;i<count;i++){uuid(base+ids[i]*11,out+i*37,t);offs[i+1]=(i+1)*37;}}
  return count*len;
 }
 if(h->mode!=102)return -1;
 size_t total=0;if(count&&ids[count-1]>=h->rows)return -1;
 for(size_t i=0;i<count;i++){const uint8_t*s=base+ids[i]*4;uint32_t x=load32(s);unsigned n=x?(32-__builtin_clz(x)+3)/4+1:2;if(n>cap-total)return -1;total+=hexrow(s,out+total);offs[i+1]=total;}
 return total;
}
#endif
}
#ifdef ENCODER
inline std::vector<uint8_t> special_encode(const uint8_t*p,size_t n){return sp::encode(p,n);}
#endif
#ifdef DECODER
inline void* special_open(const uint8_t*p,size_t n){return sp::open(p,n);}
inline int64_t special_decode(void*p,uint8_t*out,size_t cap){return sp::decode(p,out,cap);}
inline int64_t special_rows(void*p,const uint64_t*ids,size_t n,uint8_t*out,size_t cap,uint64_t*off){return sp::rows(p,ids,n,out,cap,off);}
inline void special_close(void*){}
#endif
