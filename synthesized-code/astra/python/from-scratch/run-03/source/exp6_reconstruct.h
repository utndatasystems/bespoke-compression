#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <immintrin.h>
namespace exp6rec {
struct RepeatMasks {alignas(32) uint8_t m[16][32]{};constexpr RepeatMasks(){for(unsigned off=1;off<16;off++)for(unsigned i=0;i<32;i++)m[off][i]=i%off;}};
alignas(32) static constexpr RepeatMasks repeat_masks{};
inline void repeat32(uint8_t*d,unsigned off){__m128i v=_mm_loadu_si128((const __m128i*)(d-off));__m256i p=_mm256_shuffle_epi8(_mm256_broadcastsi128_si256(v),_mm256_load_si256((const __m256i*)repeat_masks.m[off]));_mm256_storeu_si256((__m256i*)d,p);}
inline void copy32(uint8_t*d,const uint8_t*s){_mm256_storeu_si256((__m256i*)d,_mm256_loadu_si256((const __m256i*)s));}
inline void copy8(uint8_t*d,const uint8_t*s){uint64_t v;memcpy(&v,s,8);memcpy(d,&v,8);}
inline void copy16(uint8_t*d,const uint8_t*s){_mm_storeu_si128((__m128i*)d,_mm_loadu_si128((const __m128i*)s));}
// ctrl: bit5 flags a nonzero literal run; low5 bits hold match length minus5.
// Nonzero literal lengths arrive separately as length minus1 capped at255.
// Capped literal (256) and match (36) lengths use shared extension bytes, in
// sequence order. Each extension appends bytes until a byte smaller than255.
// logs are floor(log2(distance)); low distance bits are LSB-first. Bit input
// must provide at least8 readable padding bytes beyond ceil(valid_bits/8).
// Literal input needs no padding. seq_count excludes the trailing literals.
// PREDECODE separates bit unpacking into a small stack offset array.
// This codec instantiates State<5,1,true> for six-bit control tokens.
template<unsigned MBITS=6,unsigned LCAP=((1u<<(8-MBITS))-1),bool PREDECODE=false,unsigned MAXBLOCK=1024>
struct State {
 static_assert(MBITS>=1&&MBITS<=7,"invalid match bits");
 static constexpr unsigned MMASK=(1u<<MBITS)-1, MCAP=MMASK+5;
 const uint8_t *lens=nullptr,*lens_end=nullptr;
 const uint8_t *bits=nullptr,*literal=nullptr,*literal_end=nullptr,*extension=nullptr,*extension_end=nullptr;
 uint8_t *output=nullptr,*d=nullptr,*output_end=nullptr;
 size_t bitpos=0,maxbits=0;
 bool init(const uint8_t* bit_data,size_t padded_bit_bytes,size_t valid_bits,
           const uint8_t* literal_data,size_t literal_bytes,
           const uint8_t* extension_data,size_t extension_bytes,const uint8_t* lens_data,size_t lens_bytes,
           uint8_t* out,size_t raw_bytes,size_t capacity){
  if(capacity<raw_bytes||valid_bits>SIZE_MAX-7||padded_bit_bytes<(valid_bits+7)/8||padded_bit_bytes-(valid_bits+7)/8<8)return false;
  lens=lens_data;lens_end=lens_data+lens_bytes;bits=bit_data;maxbits=valid_bits;bitpos=0;literal=literal_data;literal_end=literal_data+literal_bytes;
  extension=extension_data;extension_end=extension_data+extension_bytes;
  output=out;d=out;output_end=out+raw_bytes;return true;
 }
 inline bool offset(unsigned log,uint32_t&off){
  if(log>30||bitpos>maxbits||log>maxbits-bitpos)return false;
  uint64_t word;memcpy(&word,bits+(bitpos>>3),8);word>>=(bitpos&7);off=_bzhi_u32((uint32_t)word,log)|(1u<<log);bitpos+=log;return true;
 }
 inline bool extend(size_t&n){unsigned x;do{if(extension==extension_end)return false;x=*extension++;if(n>SIZE_MAX-x)return false;n+=x;}while(x==255);return true;}
 template<bool CHECK_BIT_LIMIT> bool block_impl(const uint8_t*ctrl,const uint8_t*logs,size_t seq_count){
  uint8_t* __restrict__ dest=d;const uint8_t* __restrict__ lp=literal;const uint8_t* __restrict__ ep=extension;
  uint8_t* const outend=output_end;uint8_t* const outbegin=output;
  const uint8_t* const litend=literal_end;const uint8_t* const extendend=extension_end;const uint8_t* const bitdata=bits;
  const uint8_t* llp=lens;const uint8_t* lend=lens_end;
  size_t bp=bitpos;const size_t bitlimit=maxbits;
  auto offset=[&](unsigned log,uint32_t&off)->bool{
   if(log>30)return false;
   if constexpr(CHECK_BIT_LIMIT){if(bp>bitlimit||log>bitlimit-bp)return false;}
   uint64_t word;memcpy(&word,bitdata+(bp>>3),8);word>>=(bp&7);off=_bzhi_u32((uint32_t)word,log)|(1u<<log);bp+=log;return true;
  };
  auto extend=[&](size_t&n)->bool{unsigned x;do{if(ep==extendend)return false;x=*ep++;if(n>SIZE_MAX-x)return false;n+=x;}while(x==255);return true;};
  uint32_t offsets[PREDECODE?MAXBLOCK:1];
  if constexpr(PREDECODE){
   if(seq_count>MAXBLOCK)return false;size_t i=0;
   const __m512i index1=_mm512_setr_epi32(0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14);
   const __m512i index2=_mm512_setr_epi32(0,0,0,1,2,3,4,5,6,7,8,9,10,11,12,13);
   const __m512i index4=_mm512_setr_epi32(0,0,0,0,0,1,2,3,4,5,6,7,8,9,10,11);
   const __m512i index8=_mm512_setr_epi32(0,0,0,0,0,0,0,0,0,1,2,3,4,5,6,7);
   for(;i+16<=seq_count;i+=16){
    __m512i widths=_mm512_cvtepu8_epi32(_mm_loadu_si128((const __m128i*)(logs+i)));
    if(_mm512_cmp_epu32_mask(widths,_mm512_set1_epi32(30),_MM_CMPINT_GT))return false;
    __m512i scan=widths;
    scan=_mm512_mask_add_epi32(scan,0xfffe,scan,_mm512_permutexvar_epi32(index1,scan));
    scan=_mm512_mask_add_epi32(scan,0xfffc,scan,_mm512_permutexvar_epi32(index2,scan));
    scan=_mm512_mask_add_epi32(scan,0xfff0,scan,_mm512_permutexvar_epi32(index4,scan));
    scan=_mm512_mask_add_epi32(scan,0xff00,scan,_mm512_permutexvar_epi32(index8,scan));
    unsigned sum=(unsigned)_mm_extract_epi32(_mm512_extracti32x4_epi32(scan,3),3);
    if constexpr(CHECK_BIT_LIMIT){if(bp>bitlimit||sum>bitlimit-bp)return false;}
    __m512i positions=_mm512_add_epi32(_mm512_sub_epi32(scan,widths),_mm512_set1_epi32(bp&7));
    __m512i indexes=_mm512_srli_epi32(positions,3);
    __m512i shifts=_mm512_and_si512(positions,_mm512_set1_epi32(7));
    const uint8_t* src=bitdata+(bp>>3);
    __m512i words0,words1;
    if(bitlimit-bp>=512 && sum-(unsigned)logs[i+15]+(bp&7)<=448){
     const __m512i take0=_mm512_setr_epi64(0x0000000000000000LL,0x0404040404040404LL,0x0808080808080808LL,0x0c0c0c0c0c0c0c0cLL,0x1010101010101010LL,0x1414141414141414LL,0x1818181818181818LL,0x1c1c1c1c1c1c1c1cLL);
     const __m512i take1=_mm512_add_epi8(take0,_mm512_set1_epi8(32));
     const __m512i consecutive=_mm512_set1_epi64(0x0706050403020100LL);
     __m512i idx0=_mm512_add_epi8(_mm512_permutexvar_epi8(take0,indexes),consecutive);
     __m512i idx1=_mm512_add_epi8(_mm512_permutexvar_epi8(take1,indexes),consecutive);
     __m512i source=_mm512_loadu_si512(src);
     words0=_mm512_permutexvar_epi8(idx0,source);words1=_mm512_permutexvar_epi8(idx1,source);
    }else{
     words0=_mm512_i32gather_epi64(_mm512_castsi512_si256(indexes),src,1);
     words1=_mm512_i32gather_epi64(_mm512_extracti64x4_epi64(indexes,1),src,1);
    }
    words0=_mm512_srlv_epi64(words0,_mm512_cvtepu32_epi64(_mm512_castsi512_si256(shifts)));
    words1=_mm512_srlv_epi64(words1,_mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(shifts,1)));
    __m512i lowwords=_mm512_inserti64x4(_mm512_castsi256_si512(_mm512_cvtepi64_epi32(words0)),_mm512_cvtepi64_epi32(words1),1);
    __m512i bases=_mm512_sllv_epi32(_mm512_set1_epi32(1),widths);
    __m512i result=_mm512_or_si512(bases,_mm512_and_si512(lowwords,_mm512_sub_epi32(bases,_mm512_set1_epi32(1))));
    _mm512_storeu_si512(offsets+i,result);bp+=sum;
   }
   for(;i<seq_count;i++)if(!offset(logs[i],offsets[i]))return false;
  }
  for(size_t i=0;i<seq_count;i++){
   unsigned t=ctrl[i];size_t l=0,m=(t&MMASK)+5;
   if(t&32){if(llp==lend)return false;l=unsigned(*llp++)+1;}
   uint32_t off;
   if constexpr(PREDECODE)off=offsets[i];else if(!offset(logs[i],off))return false;
   // With these limits, all wide writes fit the supplied outbegin, and the
   // lp read fits the lp buffer. Logical bytes are still checked.
   if(l<=8 && m<MCAP && m<=32 && size_t(outend-dest)>=64 && size_t(litend-lp)>=8){
    copy8(dest,lp);dest+=l;lp+=l;
    if(off>size_t(dest-outbegin))return false;
    if(off>=32){_mm256_storeu_si256((__m256i*)dest,_mm256_loadu_si256((const __m256i*)(dest-off)));}
    else if(off>=16){copy16(dest,dest-off);copy16(dest+16,dest+16-off);}
    else {repeat32(dest,off);}
    dest+=m;continue;
   }
   if(l==256&&!extend(l))return false;
   if(l>size_t(litend-lp)||l>size_t(outend-dest))return false;
   if(l<=8&&size_t(outend-dest)>=8&&size_t(litend-lp)>=8)copy8(dest,lp);else if(l<=32&&size_t(outend-dest)>=32&&size_t(litend-lp)>=32)copy32(dest,lp);else memcpy(dest,lp,l);dest+=l;lp+=l;
   if(m==MCAP&&!extend(m))return false;
   if(off>size_t(dest-outbegin)||m>size_t(outend-dest))return false;
   if(m<=64&&off>=32&&size_t(outend-dest)>=64){copy32(dest,dest-off);if(m>32)copy32(dest+32,dest+32-off);}
   else if(m<=32&&off<16&&size_t(outend-dest)>=32){repeat32(dest,off);}
   else if(off>=16 && size_t(outend-dest)-m>=16){
    copy16(dest,dest-off);if(m>16){size_t j=16;do{copy16(dest+j,dest+j-off);j+=16;}while(j<m);}
   }else if(off>=m)memcpy(dest,dest-off,m);
   else {for(size_t j=0;j<m;j++)dest[j]=(dest-off)[j];}
   dest+=m;
  }
  d=dest;literal=lp;extension=ep;bitpos=bp;lens=llp;return true;
 }
 bool block(const uint8_t*ctrl,const uint8_t*logs,size_t seq_count){
  if(bitpos<=maxbits&&seq_count<=(maxbits-bitpos)/30)return block_impl<false>(ctrl,logs,seq_count);
  return block_impl<true>(ctrl,logs,seq_count);
 }
 bool finish(size_t trailing_literals){
  if(lens!=lens_end||trailing_literals!=size_t(output_end-d)||trailing_literals!=size_t(literal_end-literal)||extension!=extension_end||bitpos!=maxbits)return false;
  memcpy(d,literal,trailing_literals);d+=trailing_literals;literal+=trailing_literals;return true;
 }
};
}
