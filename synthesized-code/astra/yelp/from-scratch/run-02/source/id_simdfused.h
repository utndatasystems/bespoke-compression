#pragma once
#include <stdint.h>
#include <immintrin.h>
namespace idsimdfused {
static inline uint8_t* emit(const uint8_t*b,uint8_t*p){
 const __m256i index=_mm256_setr_epi8(0,1,2,0,3,4,5,0,6,7,8,0,9,10,11,0,12,13,14,0,0,0,15,0,0,0,0,0,0,0,0,0);
 const __m256i shifts=_mm256_setr_epi8(18,12,6,0,50,44,38,32,18,12,6,0,50,44,38,32,18,12,6,0,50,44,38,32,18,12,6,0,50,44,38,32);
 alignas(64) static const uint8_t alphabet[64]={'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z','a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v','w','x','y','z','0','1','2','3','4','5','6','7','8','9','-','_'};
 __m256i in=_mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i*)b));
 __m256i words=_mm256_maskz_permutexvar_epi8(0x00477777,index,in);
 __m256i codes=_mm256_and_si256(_mm256_multishift_epi64_epi8(shifts,words),_mm256_set1_epi8(63));
 __m256i chars=_mm256_permutex2var_epi8(_mm256_load_si256((const __m256i*)alphabet),codes,_mm256_load_si256((const __m256i*)(alphabet+32)));
 const __m256i label=_mm256_setr_epi8(0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'"',',','"','n','a','m','e','"',':','"');
 chars=_mm256_mask_mov_epi8(chars,0xffc00000,label);
 _mm256_storeu_si256((__m256i*)p,chars);
 return p+32;
}
}
