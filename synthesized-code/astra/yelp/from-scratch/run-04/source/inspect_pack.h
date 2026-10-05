#pragma once
#include <immintrin.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Dataset coordinates use only digits, dot and minus. These functions preserve
// the literal ASCII representation, including all zeros and variable precision.
// pack_coord writes exactly ceil(length/2) bytes. length must be in [1,16].
static inline size_t inspect_pack_coord(const uint8_t *src, unsigned length, uint8_t *dst) {
    for (unsigned i=0;i<length;i+=2) {
        unsigned a=src[i]>='0'&&src[i]<='9'?src[i]-'0':src[i]=='.'?10:11;
        unsigned b=0;
        if (i+1<length) b=src[i+1]>='0'&&src[i+1]<='9'?src[i+1]-'0':src[i+1]=='.'?10:11;
        dst[i/2]=(uint8_t)(a|(b<<4));
    }
    return (length+1)/2;
}
static inline __m128i inspect_coord_vector(uint64_t packed) {
    const __m128i nibbles=_mm_set1_epi8(15);
    __m128i v=_mm_cvtsi64_si128((long long)packed);
    __m128i lo=_mm_and_si128(v,nibbles);
    __m128i hi=_mm_and_si128(_mm_srli_epi16(v,4),nibbles);
    __m128i indexes=_mm_unpacklo_epi8(lo,hi);
    return _mm_shuffle_epi8(_mm_setr_epi8('0','1','2','3','4','5','6','7','8','9','.','-',0,0,0,0),indexes);
}
// Fast form: caller provides at least 8 readable source bytes and 16 writable
// output bytes, with length actual output bytes. Those trailing output bytes
// are overwritten by the next field. It is safe inside every dataset row.
static inline void inspect_unpack_coord_wide(const uint8_t *src, uint8_t *dst) {
    uint64_t v; memcpy(&v,src,8);
    _mm_storeu_si128((__m128i*)dst,inspect_coord_vector(v));
}
// Exact-capacity variant, useful for standalone round-trip tests.
static inline void inspect_unpack_coord_exact(const uint8_t *src, unsigned length, uint8_t *dst) {
    uint64_t v=0; memcpy(&v,src,(length+1)/2);
    uint8_t tmp[16]; _mm_storeu_si128((__m128i*)tmp,inspect_coord_vector(v));
    memcpy(dst,tmp,length);
}

static inline int inspect_id_value(unsigned c) {
    if(c>='A'&&c<='Z') return c-'A';
    if(c>='a'&&c<='z') return c-'a'+26;
    if(c>='0'&&c<='9') return c-'0'+52;
    if(c=='-') return 62;
    if(c=='_') return 63;
    return -1;
}
// All 126509 business IDs have 22 URL-base64 characters and canonical unused
// low bits in the last character, hence exactly 16 bytes are sufficient.
// No dataset-derived constants are embedded here.
static inline bool inspect_pack_id(const uint8_t *src, uint8_t *dst) {
    unsigned vals[22];
    for(unsigned i=0;i<22;i++){int v=inspect_id_value(src[i]);if(v<0)return false;vals[i]=(unsigned)v;}
    if(vals[21]&15)return false;
    for(unsigned i=0;i<5;i++){
        uint32_t v=(vals[4*i]<<18)|(vals[4*i+1]<<12)|(vals[4*i+2]<<6)|vals[4*i+3];
        dst[3*i]=(uint8_t)(v>>16);dst[3*i+1]=(uint8_t)(v>>8);dst[3*i+2]=(uint8_t)v;
    }
    dst[15]=(uint8_t)((vals[20]<<2)|(vals[21]>>4));
    return true;
}
// Read exactly 16 bytes; write exactly 22 bytes. Requires AVX2.
static inline void inspect_unpack_id(const uint8_t *src, uint8_t *dst) {
    __m128i input=_mm_loadu_si128((const __m128i*)src);
    __m256i v=_mm256_castsi128_si256(input);
    v=_mm256_inserti128_si256(v,_mm_srli_si128(input,12),1);
    const __m128i shuffle=_mm_setr_epi8(1,0,2,1,4,3,5,4,7,6,8,7,10,9,11,10);
    v=_mm256_shuffle_epi8(v,_mm256_broadcastsi128_si256(shuffle));
    __m256i a=_mm256_mulhi_epu16(_mm256_and_si256(v,_mm256_set1_epi32(0x0fc0fc00)),_mm256_set1_epi32(0x04000040));
    __m256i b=_mm256_mullo_epi16(_mm256_and_si256(v,_mm256_set1_epi32(0x003f03f0)),_mm256_set1_epi32(0x01000010));
    v=_mm256_or_si256(a,b);
    __m256i index=_mm256_subs_epu8(v,_mm256_set1_epi8(51));
    index=_mm256_sub_epi8(index,_mm256_cmpgt_epi8(v,_mm256_set1_epi8(25)));
    const __m128i offsets=_mm_setr_epi8(65,71,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-17,32,0,0);
    __m256i result=_mm256_add_epi8(v,_mm256_shuffle_epi8(_mm256_broadcastsi128_si256(offsets),index));
    _mm_storeu_si128((__m128i*)dst,_mm256_castsi256_si128(result));
    uint64_t tail=(uint64_t)_mm_cvtsi128_si64(_mm256_extracti128_si256(result,1));
    memcpy(dst+16,&tail,6);
}

// Dataset-fitted 64-byte alphabet: these bytes are reconstruction information
// and must remain in the archive or charged decoder (as this constant does).
static constexpr uint8_t inspect_text_alphabet[65]=" eatrniolsS1d0uh2CcyvBA35RgmPp4kM6W7wND98THEbLf,FG'z&OI-VKJUx.qY";
// length_flag contains original length 0..127, plus bit7 for verbatim fallback.
static inline size_t inspect_pack_text(const uint8_t*src,unsigned length,uint8_t*dst,uint8_t*length_flag){
    int16_t reverse[256];for(unsigned i=0;i<256;i++)reverse[i]=-1;
    for(unsigned i=0;i<64;i++)reverse[inspect_text_alphabet[i]]=i;
    for(unsigned i=0;i<length;i++) if(reverse[src[i]]<0){*length_flag=(uint8_t)(length|128);memcpy(dst,src,length);return length;}
    *length_flag=(uint8_t)length;
    uint32_t acc=0;unsigned bits=0;size_t size=0;
    for(unsigned i=0;i<length;i++){
        acc=(acc<<6)|(unsigned)reverse[src[i]];bits+=6;
        if(bits>=8){bits-=8;dst[size++]=(uint8_t)(acc>>bits);}
    }
    if(bits)dst[size++]=(uint8_t)(acc<<(8-bits));
    return size;
}
#ifdef __AVX512VBMI__
static inline __m256i inspect_unpack_text_indices32(const uint8_t*src){
    __m256i v=_mm256_castsi128_si256(_mm_loadu_si128((const __m128i*)src));
    v=_mm256_inserti128_si256(v,_mm_loadu_si128((const __m128i*)(src+12)),1);
    const __m128i shuffle=_mm_setr_epi8(1,0,2,1,4,3,5,4,7,6,8,7,10,9,11,10);
    v=_mm256_shuffle_epi8(v,_mm256_broadcastsi128_si256(shuffle));
    __m256i a=_mm256_mulhi_epu16(_mm256_and_si256(v,_mm256_set1_epi32(0x0fc0fc00)),_mm256_set1_epi32(0x04000040));
    __m256i b=_mm256_mullo_epi16(_mm256_and_si256(v,_mm256_set1_epi32(0x003f03f0)),_mm256_set1_epi32(0x01000010));
    return _mm256_or_si256(a,b);
}
// Wide packed branch may read through next 27 bytes and write through next31
// output bytes. Caller supplies source padding and writes subsequent fields.
static inline size_t inspect_unpack_text_wide(const uint8_t*src,uint8_t flag,uint8_t*dst){
    unsigned length=flag&127;
    if(flag&128){memcpy(dst,src,length);return length;}
    const __m256i lo=_mm256_loadu_si256((const __m256i*)inspect_text_alphabet);
    const __m256i hi=_mm256_loadu_si256((const __m256i*)(inspect_text_alphabet+32));
    for(unsigned i=0;i<length;i+=32){
        __m256i indexes=inspect_unpack_text_indices32(src+(i/4)*3);
        __m256i result=_mm256_permutex2var_epi8(lo,indexes,hi);
        _mm256_storeu_si256((__m256i*)(dst+i),result);
    }
    return (length*6+7)/8;
}
#endif
