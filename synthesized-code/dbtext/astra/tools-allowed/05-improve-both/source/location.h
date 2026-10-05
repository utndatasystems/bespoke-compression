#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <immintrin.h>
struct LocState {const uint8_t*p=nullptr;uint32_t n=0;alignas(32) uint32_t decimal[10000];};
#ifdef ENCODER
static std::vector<uint8_t> loc_encode(const uint8_t*raw,size_t size){
 std::vector<uint8_t>out(4);uint32_t n=0;size_t pos=0;
 while(pos<size){size_t end=pos;while(end<size&&raw[end]!='\n')end++;if(end==size)return {};const uint8_t*r=raw+pos;size_t len=end-pos;__uint128_t v=0;
  if(len==4&&!memcmp(r,"NULL",4))v=__uint128_t(127)<<97;
  else{if(len<31||len>40||memcmp(r,"(40.",4)||r[len-1]!=')')return {};size_t sep=4;while(sep<len&&r[sep]!=',')sep++;unsigned l1=sep-4;if(l1<9||l1>15||sep+7>=len)return {};if(memcmp(r+sep,", -7",4)||(r[sep+4]!='3'&&r[sep+4]!='4')||r[sep+5]!='.')return {};unsigned l2=len-(sep+6)-1;if(l2<10||l2>14)return {};uint64_t a=0,b=0;for(unsigned j=0;j<l1;j++){unsigned d=r[4+j]-'0';if(d>9)return {};a=a*10+d;}for(unsigned j=0;j<l2;j++){unsigned d=r[sep+6+j]-'0';if(d>9)return {};b=b*10+d;}unsigned m=(l1-9)|((l2-10)<<3)|((r[sep+4]=='4')<<6);v=a|(__uint128_t(b)<<50)|(__uint128_t(m)<<97);}
  size_t k=out.size();out.resize(k+13);memcpy(out.data()+k,&v,13);n++;pos=end+1;
 }
 memcpy(out.data(),&n,4);return out;
}
#endif
static bool loc_open(LocState&st,const uint8_t*p,size_t size){if(size<4)return false;uint32_t n;memcpy(&n,p,4);if(size!=size_t(n)*13+4)return false;for(uint32_t i=0;i<n;i++){unsigned m=p[4+size_t(i)*13+12]>>1;if(m==127)continue;if((m&7)>6||((m>>3)&7)>4)return false;}st.p=p+4;st.n=n;for(unsigned i=0;i<10000;i++){uint32_t a='0'+i/1000,b='0'+i/100%10,c='0'+i/10%10,d='0'+i%10;st.decimal[i]=a|(b<<8)|(c<<16)|(d<<24);}return true;}
static inline unsigned loc_rowlen(const LocState&st,uint32_t id){unsigned m=st.p[size_t(id)*13+12]>>1;return m==127?5:31+(m&7)+((m>>3)&7);}
static inline __m128i loc_digits(const LocState&st,uint64_t x,unsigned len){uint64_t hi=x/100000000,lo=x%100000000;__m128i v=_mm_set_epi32(st.decimal[lo%10000],st.decimal[lo/10000],st.decimal[hi%10000],st.decimal[hi/10000]);__m128i idx=_mm_add_epi8(_mm_setr_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15),_mm_set1_epi8(16-len));return _mm_shuffle_epi8(v,idx);}
static inline unsigned loc_row(const LocState&st,uint32_t id,uint8_t*d){const uint8_t*p=st.p+size_t(id)*13;unsigned m=p[12]>>1;if(m==127){memcpy(d,"NULL\n",5);return 5;}unsigned l1=(m&7)+9,l2=((m>>3)&7)+10;uint64_t a,b;memcpy(&a,p,8);memcpy(&b,p+5,8);a&=(1ull<<50)-1;b=(b>>10)&((1ull<<47)-1);__m128i x=loc_digits(st,a,l1),y=loc_digits(st,b,l2);memcpy(d,"(40.",4);_mm_storeu_si128((__m128i*)(d+4),x);uint8_t*q=d+4+l1;memcpy(q,", -73.",6);if(m&64)q[4]='4';
#if defined(__AVX512BW__) && defined(__AVX512VL__)
 _mm_mask_storeu_epi8(q+6,(__mmask16)((1u<<l2)-1),y);
#else
 alignas(16) uint8_t temp[16];_mm_store_si128((__m128i*)temp,y);memcpy(q+6,temp,l2);
#endif
 q[6+l2]=')';q[7+l2]='\n';return 12+l1+l2;}
