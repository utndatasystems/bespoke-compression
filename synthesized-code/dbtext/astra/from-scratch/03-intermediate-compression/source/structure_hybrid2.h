#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <string>
#include <new>
#include <immintrin.h>
namespace structure {
static constexpr uint64_t MAGIC=0x3548434f4c545344ULL;
struct Header {uint64_t magic,raw_size,count;};
struct State {const uint8_t*rec;uint64_t count,raw_size;};
static constexpr uint8_t lengths[]={14|(14<<4),15|(14<<4),14|(13<<4),13|(14<<4),15|(13<<4),14|(12<<4),13|(13<<4),12|(14<<4),15|(12<<4),12|(13<<4),13|(12<<4),14|(11<<4),11|(14<<4),15|(11<<4),14|(10<<4),10|(14<<4),13|(11<<4),11|(13<<4),12|(12<<4),15|(10<<4),12|(11<<4),9|(14<<4)};
static bool encode(const uint8_t*raw,size_t n,std::vector<uint8_t>&out){
 if(n<40||raw[0]!='('||raw[1]!='4'||raw[2]!='0'||raw[3]!='.')return false;
 std::vector<uint8_t>r;const uint8_t*p=raw,*end=raw+n;size_t count=0;
 while(p<end){uint64_t a=0,b=0,meta=0;std::string sa,sb;bool common=false;
  if(end-p>=5&&!memcmp(p,"NULL\n",5)){meta=31|64;p+=5;}
  else{
   if(end-p<20||memcmp(p,"(40.",4))return false;p+=4;unsigned al=0,bl=0;
   while(p<end&&*p>='0'&&*p<='9'){if(al>=15)return false;sa.push_back(*p++);++al;}
   if(al<9||end-p<6||memcmp(p,", -7",4)||(p[4]!='3'&&p[4]!='4')||p[5]!='.')return false;meta=uint64_t(p[4]=='4')<<6;p+=6;
   while(p<end&&*p>='0'&&*p<='9'){if(bl>=14)return false;sb.push_back(*p++);++bl;}
   if(bl<10||end-p<2||p[0]!=')'||p[1]!='\n')return false;p+=2;meta|=(al-9)|((bl-10)<<3);
   common=sa[0]=='8'&&(sb[0]=='8'||sb[0]=='9');
   if(common){meta|=uint64_t(sb[0]=='9')<<7;sa=sa.substr(1)+std::string(15-al,'0');sb=sb.substr(1)+std::string(14-bl,'0');for(unsigned j=0;j<7;++j)a|=uint64_t((sa[2*j]-'0')*10+sa[2*j+1]-'0')<<(7*j);for(unsigned j=0;j<6;++j)b|=uint64_t((sb[2*j]-'0')*10+sb[2*j+1]-'0')<<(7*j);b|=uint64_t(sb[12]-'0')<<42;}
   else{unsigned code=0;while(code<sizeof(lengths)&&lengths[code]!=(al|(bl<<4)))++code;if(code==sizeof(lengths))return false;meta=code|((meta&64)>>1)|64;for(char c:sa)a=a*10+c-'0';for(char c:sb)b=b*10+c-'0';}
  }
  ++count;
  uint64_t lo,hi;if(common){lo=a|(b<<49);hi=(a>>40)|(b<<9)|(meta<<55);}else{lo=a|(b<<50);hi=(a>>40)|(b<<10)|(meta<<57);}size_t k=r.size();r.resize(k+13);memcpy(r.data()+k,&lo,8);memcpy(r.data()+k+5,&hi,8);
 }
 Header h{MAGIC,n,count};out.resize(sizeof h+r.size());memcpy(out.data(),&h,sizeof h);memcpy(out.data()+sizeof h,r.data(),r.size());return true;
}
static State*open(const uint8_t*a,size_t n){if(n<sizeof(Header))return nullptr;Header h;memcpy(&h,a,sizeof h);if(h.magic!=MAGIC||h.count>(n-sizeof h)/13||sizeof h+h.count*13!=n)return nullptr;return new(std::nothrow)State{a+sizeof h,h.count,h.raw_size};}
static inline uint64_t load64(const uint8_t*p){uint64_t x;memcpy(&x,p,8);return x;}
static inline __m128i pair_digits(uint64_t n){uint64_t e=_pdep_u64(n,0x7f7f7f7f7f7f7f7fULL);__m128i p=_mm_cvtepu8_epi16(_mm_cvtsi64_si128(e));__m128i t=_mm_srli_epi16(_mm_mullo_epi16(p,_mm_set1_epi16(205)),11);__m128i u=_mm_sub_epi16(p,_mm_mullo_epi16(t,_mm_set1_epi16(10)));return _mm_add_epi8(_mm_or_si128(t,_mm_slli_epi16(u,8)),_mm_set1_epi8('0'));}
static inline __m128i binary_digits(uint64_t n,unsigned len){uint64_t hi=n/100000000,lo=n-hi*100000000;unsigned a=hi/10000,b=hi-a*10000,c=lo/10000,d=lo-c*10000;__m128i x=_mm_setr_epi16(a,b,c,d,0,0,0,0);__m128i hundreds=_mm_srli_epi16(_mm_mulhi_epu16(x,_mm_set1_epi16(5243)),3);__m128i pairs=_mm_unpacklo_epi16(hundreds,_mm_sub_epi16(x,_mm_mullo_epi16(hundreds,_mm_set1_epi16(100))));__m128i tens=_mm_srli_epi16(_mm_mullo_epi16(pairs,_mm_set1_epi16(205)),11);__m128i units=_mm_sub_epi16(pairs,_mm_mullo_epi16(tens,_mm_set1_epi16(10)));__m128i v=_mm_add_epi8(_mm_or_si128(tens,_mm_slli_epi16(units,8)),_mm_set1_epi8('0'));__m128i idx=_mm_add_epi8(_mm_setr_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15),_mm_set1_epi8(16-len));return _mm_shuffle_epi8(v,idx);}
static inline unsigned length(const uint8_t*r,bool binary){uint64_t hi=load64(r+5);unsigned m=hi>>(binary?57:55);if(binary){unsigned code=m&31;if(code==31)return 5;if(code>=sizeof(lengths))return 0;unsigned pair=lengths[code];return 12+(pair&15)+(pair>>4);}return 31+(m&7)+((m>>3)&7);}
static inline unsigned emit(const uint8_t*r,bool binary,uint8_t*out){uint64_t lo=load64(r),hi=load64(r+5);
 if(__builtin_expect(binary,0)){unsigned m=hi>>57;if(__builtin_expect((m&31)==31,0)){memcpy(out,"NULL\n",5);return 5;}unsigned pair=lengths[m&31],al=pair&15,bl=pair>>4;uint64_t a=lo&((1ULL<<50)-1),b=(hi>>10)&((1ULL<<47)-1);memcpy(out,"(40.",4);_mm_mask_storeu_epi8(out+4,(__mmask16)((1u<<al)-1),binary_digits(a,al));uint8_t*q=out+4+al;memcpy(q,", -73.",6);q[4]+=(m>>5)&1;q+=6;_mm_mask_storeu_epi8(q,(__mmask16)((1u<<bl)-1),binary_digits(b,bl));q[bl]=')';q[bl+1]='\n';return 12+al+bl;}
 unsigned m=hi>>55,al=(m&7)+9,bl=((m>>3)&7)+10;uint64_t a=lo&((1ULL<<49)-1),b=(hi>>9)&((1ULL<<46)-1);memcpy(out,"(40.8",5);_mm_mask_storeu_epi8(out+5,(__mmask16)((1u<<(al-1))-1),pair_digits(a));uint8_t*q=out+4+al;memcpy(q,", -73.8",7);q[4]+=(m>>6)&1;q[6]+=(m>>7)&1;q+=7;__m128i v=_mm_insert_epi8(pair_digits(b),((b>>42)&15)+'0',12);_mm_mask_storeu_epi8(q,(__mmask16)((1u<<(bl-1))-1),v);q[bl-1]=')';q[bl]='\n';return 12+al+bl;
}
static int64_t decode(State*s,uint8_t*out,size_t cap){if(!s||cap<s->raw_size)return -1;uint8_t*start=out;for(uint64_t i=0;i<s->count;++i){const uint8_t*r=s->rec+13*i;bool binary=r[12]&128;unsigned len=length(r,binary);if(!len||len>cap)return -1;out+=emit(r,binary,out);cap-=len;}return uint64_t(out-start)==s->raw_size?int64_t(out-start):-1;}
static int64_t rows(State*s,const uint64_t*ids,size_t n,uint8_t*out,size_t cap,uint64_t*offs){if(!s)return -1;uint8_t*start=out;offs[0]=0;for(size_t i=0;i<n;++i){uint64_t id=ids[i];if(id>=s->count)return -1;const uint8_t*r=s->rec+13*id;bool binary=r[12]&128;unsigned len=length(r,binary);if(!len||len>cap)return -1;out+=emit(r,binary,out);cap-=len;offs[i+1]=out-start;}return out-start;}
static void close(State*s){delete s;}
}
