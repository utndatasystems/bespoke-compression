#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <immintrin.h>

// Fixed-width structured values: templates, nibbles, or two-bit symbols.
// All input-dependent state is stored in this header and its payload.
struct __attribute__((packed)) FixedHeader {
 uint64_t magic; uint32_t type, rows; uint64_t rawsize, datasize;
 uint8_t templ[64]; uint8_t reserved[32];
};
static_assert(sizeof(FixedHeader)==128, "header");
static constexpr uint64_t FIXED_MAGIC=0x3178444946454e44ull;
static inline bool fixed_detect(const uint8_t* p) { uint64_t x; memcpy(&x,p,8);return x==FIXED_MAGIC; }
static inline bool fixed_valid(const uint8_t* p,size_t n) {
 if(n<sizeof(FixedHeader)) return false;
 const auto*h=(const FixedHeader*)p;
 if(h->magic!=FIXED_MAGIC || !h->rows || h->type<1 || h->type>4) return false;
 size_t stride=h->type==3?4:h->type==4?11:3;
 size_t ds=h->type==2?(size_t(h->rows)*18+7)/8:size_t(h->rows)*stride;
 if(h->datasize!=ds || h->datasize+sizeof(FixedHeader)+16!=n) return false;
 if(h->type!=3 && h->rawsize!=size_t(h->rows)*(h->type==1?19:h->type==2?10:37))return false;
 if(h->type==3 && (h->rawsize<size_t(h->rows)*2 || h->rawsize>size_t(h->rows)*9))return false;
 return true;
}
#ifdef FIXED_ENCODER
static inline int fixed_hex(uint8_t c) {return c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:-1;}
static bool fixed_encode(const uint8_t* raw,size_t size,std::vector<uint8_t>& out) {
 unsigned t=0;size_t rows=0;
 if(size%19==0 && size>=19 && !memcmp(raw,"Customer#000",12))t=1,rows=size/19;
 else if(size%10==0 && size>=10 && raw[9]=='\n') {
   bool ok=true;for(size_t i=0;i<9;i++)if(raw[i]!='a'&&raw[i]!='c'&&raw[i]!='g'&&raw[i]!='t')ok=false;
   if(ok)t=2,rows=size/10;
 }
 if(!t && size%37==0 && size>=37 && raw[8]=='-' && raw[36]=='\n')t=4,rows=size/37;
 if(!t){ bool ok=size>0 && raw[size-1]=='\n';size_t len=0;
   for(size_t i=0;i<size&&ok;i++) { if(raw[i]=='\n'){if(len<1||len>8)ok=false;len=0;rows++;}else {if(!((raw[i]>='0'&&raw[i]<='9')||(raw[i]>='A'&&raw[i]<='F')))ok=false;len++;} }
   if(ok)t=3;
 }
 if(!t)return false;
 size_t stride=t==3?4:t==4?11:3;
 size_t datasize=t==2?(rows*18+7)/8:rows*stride;
 out.assign(sizeof(FixedHeader)+datasize+16,0);
 auto*h=(FixedHeader*)out.data();h->magic=FIXED_MAGIC;h->type=t;h->rows=rows;h->rawsize=size;h->datasize=datasize;
 uint8_t*q=out.data()+sizeof(FixedHeader);
 if(t==1){memcpy(h->templ,raw,19); memset(h->templ+12,0,6);
   for(size_t r=0;r<rows;r++){const uint8_t*p=raw+r*19;if(memcmp(p,h->templ,12)||p[18]!='\n')return false;
     for(unsigned j=0;j<3;j++){unsigned a=p[12+2*j]-'0',b=p[13+2*j]-'0';if(a>9||b>9)return false;q[3*r+j]=(a<<4)|b;}
   }
 }else if(t==2){
   for(size_t r=0;r<rows;r++){const uint8_t*p=raw+r*10;if(p[9]!='\n')return false;uint32_t v=0;
    for(unsigned j=0;j<9;j++){unsigned b=p[j]=='a'?0:p[j]=='c'?1:p[j]=='g'?2:p[j]=='t'?3:4;if(b==4)return false;v|=b<<(2*j);}size_t off=(r*18)>>3;uint32_t w;memcpy(&w,q+off,4);w|=v<<((r*18)&7);memcpy(q+off,&w,4);
   }
 }else if(t==3){size_t r=0;uint32_t v=0;unsigned digits=0;
   for(size_t i=0;i<size;i++){if(raw[i]=='\n'){if(digits>1 && v<(1u<<(4*(digits-1))))return false;memcpy(q+4*r++,&v,4);v=digits=0;}else {v=v*16+fixed_hex(raw[i]);digits++;}}
 }else{
   static constexpr unsigned at[22]={2,3,4,5,6,7,19,20,21,22,24,25,26,27,28,29,30,31,32,33,34,35};
   memcpy(h->templ,raw,37);for(unsigned j:at)h->templ[j]=0;
   for(size_t r=0;r<rows;r++){const uint8_t*p=raw+r*37;
    for(unsigned j=0;j<37;j++)if(h->templ[j]&&p[j]!=h->templ[j])return false;
    for(unsigned j=0;j<11;j++){uint8_t a=p[at[2*j]],b=p[at[2*j+1]];int x=fixed_hex(a),y=fixed_hex(b);if(x<0||y<0||(a>='A'&&a<='F')||(b>='A'&&b<='F'))return false;q[r*11+j]=(x<<4)|y;}
   }
 }
 return true;
}
#endif

static inline __m128i fixed_unpack(__m128i x,__m128i chars) {
 __m128i mask=_mm_set1_epi8(15);return _mm_shuffle_epi8(chars,_mm_unpacklo_epi8(_mm_and_si128(_mm_srli_epi16(x,4),mask),_mm_and_si128(x,mask)));
}
static inline void fixed_name(const uint8_t*p,uint8_t*q,const FixedHeader*h){
 uint32_t x;memcpy(&x,p,4);
 __m128i v=_mm_cvtsi32_si128(x);__m128i d=fixed_unpack(v,_mm_setr_epi8('0','1','2','3','4','5','6','7','8','9',0,0,0,0,0,0));
 memcpy(q,h->templ,12);
 uint64_t z=(uint64_t)_mm_cvtsi128_si64(d);memcpy(q+12,&z,4);memcpy(q+16,((uint8_t*)&z)+4,2);q[18]=h->templ[18];
}
static inline void fixed_genome_code(uint32_t x,uint8_t*q){
 // BMI2 expands eight 2-bit symbols into byte lanes; the ninth is scalar.
 uint64_t d=_pdep_u64(x,0x0303030303030303ull);
 __m128i v=_mm_shuffle_epi8(_mm_setr_epi8('a','c','g','t',0,0,0,0,0,0,0,0,0,0,0,0),_mm_cvtsi64_si128(d));
 _mm_storel_epi64((__m128i*)q,v);q[8]="acgt"[(x>>16)&3];q[9]='\n';
}
static inline void fixed_genome_at(const uint8_t*p,size_t r,uint8_t*q){uint32_t x;size_t bits=r*18;memcpy(&x,p+(bits>>3),4);fixed_genome_code(x>>(bits&7),q);}
static inline unsigned fixed_hexrow(const uint8_t*p,uint8_t*q){
 uint32_t x;memcpy(&x,p,4);unsigned digits=(32-__builtin_clz(x|1)+3)/4;
 __m128i v=fixed_unpack(_mm_cvtsi32_si128(__builtin_bswap32(x)),_mm_setr_epi8('0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'));
 __m128i shift=_mm_add_epi8(_mm_setr_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15),_mm_set1_epi8(8-digits));v=_mm_shuffle_epi8(v,shift);

#ifdef __AVX512BW__
 _mm_mask_storeu_epi8(q,(__mmask16)((1u<<digits)-1),v);
#else
 uint64_t temp=_mm_cvtsi128_si64(v);memcpy(q,&temp,digits);
#endif
 q[digits]='\n';return digits+1;
}
static inline void fixed_uuid(const uint8_t*p,uint8_t*q,const FixedHeader*h){
 __m128i x=_mm_loadu_si128((const __m128i*)p),mask=_mm_set1_epi8(15),chars=_mm_setr_epi8('0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f');
 __m128i a=_mm_and_si128(_mm_srli_epi16(x,4),mask),b=_mm_and_si128(x,mask);
 __m128i lo=_mm_shuffle_epi8(chars,_mm_unpacklo_epi8(a,b)),hi=_mm_shuffle_epi8(chars,_mm_unpackhi_epi8(a,b));
 __m128i first=_mm_shuffle_epi8(lo,_mm_setr_epi8(-1,-1,0,1,2,3,4,5,-1,-1,-1,-1,-1,-1,-1,-1));
 __m128i tail=_mm_alignr_epi8(hi,lo,6);
 __m128i second=_mm_shuffle_epi8(tail,_mm_setr_epi8(-1,-1,-1,0,1,2,3,-1,4,5,6,7,8,9,10,11));
 _mm_storeu_si128((__m128i*)q,_mm_or_si128(first,_mm_loadu_si128((const __m128i*)h->templ)));
 _mm_storeu_si128((__m128i*)(q+16),_mm_or_si128(second,_mm_loadu_si128((const __m128i*)(h->templ+16))));
 uint32_t last=_mm_cvtsi128_si32(_mm_srli_si128(hi,2));memcpy(q+32,&last,4);q[36]=h->templ[36];
}
static inline int64_t fixed_decode(const uint8_t*p,uint8_t*out,size_t cap){
 auto*h=(const FixedHeader*)p;if(cap<h->rawsize)return -1;p+=sizeof(FixedHeader);
 switch(h->type){
 case 1:for(size_t i=0;i<h->rows;i++)fixed_name(p+3*i,out+19*i,h);break;
 case 2:{size_t i=0;for(;i+4<=h->rows;i+=4){uint32_t a,b,c,d;const uint8_t*s=p+(i*18)/8;memcpy(&a,s,4);memcpy(&b,s+2,4);memcpy(&c,s+4,4);memcpy(&d,s+6,4);fixed_genome_code(a,out+10*i);fixed_genome_code(b>>2,out+10*i+10);fixed_genome_code(c>>4,out+10*i+20);fixed_genome_code(d>>6,out+10*i+30);}for(;i<h->rows;i++)fixed_genome_at(p,i,out+10*i);break;}
 case 3:{uint8_t*q=out;size_t i=0;
 while(i<h->rows){
  size_t chunk=(cap-size_t(q-out))/9;if(chunk>h->rows-i)chunk=h->rows-i;
  if(chunk){size_t end=i+chunk;for(;i<end;i++)q+=fixed_hexrow(p+4*i,q);}
  else {uint32_t x;memcpy(&x,p+4*i,4);unsigned n=(32-__builtin_clz(x|1)+3)/4+1;if(size_t(q-out)+n>cap)return -1;q+=fixed_hexrow(p+4*i,q);i++;}
 }
 return size_t(q-out)==h->rawsize?q-out:-1;}
 case 4:for(size_t i=0;i<h->rows;i++)fixed_uuid(p+11*i,out+37*i,h);break;
 default:return -1;
 }return h->rawsize;
}
static inline int64_t fixed_rows(const uint8_t*p,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets){
 auto*h=(const FixedHeader*)p;p+=sizeof(FixedHeader);offsets[0]=0;
 size_t len=h->type==1?19:h->type==2?10:h->type==4?37:0;
 if(count && ids[count-1]>=h->rows)return -1;
 if(len && count>cap/len)return -1;
 switch(h->type){
 case 1:for(size_t i=0;i<count;i++){fixed_name(p+3*ids[i],out+19*i,h);offsets[i+1]=19*(i+1);}break;
 case 2:for(size_t i=0;i<count;i++){fixed_genome_at(p,ids[i],out+10*i);offsets[i+1]=10*(i+1);}break;
 case 3:{size_t pos=0;for(size_t i=0;i<count;i++){uint32_t x;memcpy(&x,p+4*ids[i],4);unsigned n=(32-__builtin_clz(x|1)+3)/4+1;if(pos+n>cap)return -1;pos+=fixed_hexrow(p+4*ids[i],out+pos);offsets[i+1]=pos;}return pos;}
 case 4:for(size_t i=0;i<count;i++){fixed_uuid(p+11*ids[i],out+37*i,h);offsets[i+1]=37*(i+1);}break;
 default:return -1;
 }return len*count;
}
