#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>
#include <immintrin.h>

// LOCATION v1: one 16-byte record per original LF-delimited row.
// Bytes 0..7: up to 15 latitude fractional digits in high/low nibbles.
// Bytes 8..14: up to 14 longitude fractional digits in high/low nibbles.
// Byte 15: latitude length minus 9, longitude length minus 10, longitude
// integer 73/74. Bit 7 marks the original NULL line. Constants reconstruct
// the dataset's literal punctuation; they are part of the charged decoder.
#if SMALL_SPECIAL
static constexpr size_t LOCATION_STRIDE=15;
#else
static constexpr size_t LOCATION_STRIDE=16;
#endif
static int encode_location(const uint8_t* raw,size_t size,std::vector<uint8_t>&payload,uint64_t&rows) {
    size_t pos=0; rows=0; payload.clear();
    while(pos<size) {
        const uint8_t* s=raw+pos;
        const uint8_t* nl=(const uint8_t*)memchr(s,'\n',size-pos);
        if(!nl) return 0;
        size_t n=nl-s+1;
        uint8_t rec[16]={};
        if(n==5 && memcmp(s,"NULL\n",5)==0) rec[15]=128;
        else {
            if(n<31 || n>41 || memcmp(s,"(40.",4)!=0 || s[n-2]!=')') return 0;
            const uint8_t* comma=(const uint8_t*)memchr(s,',',n);
            if(!comma) return 0;
            size_t a=comma-(s+4);
            if(a<9 || a>15 || memcmp(comma,", -7",4)!=0 || (comma[4]!='3' && comma[4]!='4') || comma[5]!='.') return 0;
            const uint8_t* bstart=comma+6;
            size_t b=(s+n-2)-bstart;
            if(b<10 || b>14) return 0;
            for(size_t i=0;i<a;i++) {
                if(s[4+i]<'0'||s[4+i]>'9') return 0;
                rec[i/2]|=(s[4+i]-'0')<<(i%2?0:4);
            }
            for(size_t i=0;i<b;i++) {
                if(bstart[i]<'0'||bstart[i]>'9') return 0;
                rec[8+i/2]|=(bstart[i]-'0')<<(i%2?0:4);
            }
            rec[15]=(a-9)|((b-10)<<3)|((comma[4]=='4')<<6);
        }
#if SMALL_SPECIAL
        if(rec[15]&128) rec[7]=0xf7;
        else {
            unsigned la=(rec[15]&7)+9;
            unsigned last=la==15?rec[7]>>4:la+1;
            rec[7]=(last<<4)|((rec[15]>>3)&7)|((rec[15]>>3)&8);
        }
#endif
        payload.insert(payload.end(),rec,rec+LOCATION_STRIDE); rows++; pos+=n;
    }
#if SMALL_SPECIAL
    payload.push_back(0);
#endif
    return rows?5:0;
}

static inline unsigned location_len(const uint8_t* rec) {
#if SMALL_SPECIAL
    unsigned m=rec[7];
    unsigned a=m>>4;
    return m==0xf7?5:(a<10?15:a-1)+10+(m&7)+12;
#else
    unsigned m=rec[15];
    return m&128?5:31+(m&7)+((m>>3)&7);
#endif
}
static inline unsigned location_one(const uint8_t* rec,uint8_t* out) {
#if SMALL_SPECIAL
    unsigned m=rec[7];
    if(__builtin_expect(m==0xf7,0)) {memcpy(out,"NULL\n",5);return 5;}
    const unsigned a0=m>>4,la=a0<10?15:a0-1,lb=10+(m&7),lon74=(m>>3)&1;
#else
    unsigned m=rec[15];
    if(__builtin_expect(m&128,0)) {memcpy(out,"NULL\n",5);return 5;}
    const unsigned la=9+(m&7),lb=10+((m>>3)&7),lon74=(m>>6)&1;
#endif
    __m128i x=_mm_loadu_si128((const __m128i*)rec);
    __m128i mask=_mm_set1_epi8(15);
    __m128i low=_mm_and_si128(x,mask),high=_mm_and_si128(_mm_srli_epi16(x,4),mask);
    __m128i a=_mm_add_epi8(_mm_unpacklo_epi8(high,low),_mm_set1_epi8('0'));
    __m128i b=_mm_add_epi8(_mm_unpackhi_epi8(high,low),_mm_set1_epi8('0'));
    memcpy(out,"(40.",4);
    _mm_storeu_si128((__m128i*)(out+4),a);
    uint64_t mid=0x30302e33372d202cULL+((uint64_t)lon74<<32);
    memcpy(out+4+la,&mid,8);
#if defined(__AVX512BW__) && defined(__AVX512VL__)
    _mm_mask_storeu_epi8(out+10+la,(__mmask16)((1u<<lb)-1),b);
#else
    uint8_t digits[16]; _mm_storeu_si128((__m128i*)digits,b);
    memcpy(out+10+la,digits,lb);
#endif
    memcpy(out+10+la+lb,")\n",2);
    return la+lb+12;
}
static bool location_validate(const uint8_t*p,size_t payload_size,uint64_t nrows,uint64_t raw_size) {
    if(!nrows || nrows>SIZE_MAX/41 || payload_size!=nrows*LOCATION_STRIDE+(LOCATION_STRIDE==15) || raw_size<nrows*5 || raw_size>nrows*41) return false;
    size_t total=0;
    for(uint64_t i=0;i<nrows;i++) {
        const uint8_t* rec=p+i*LOCATION_STRIDE;
#if SMALL_SPECIAL
        unsigned m=rec[7];
        if(m!=0xf7 && (m&7)>4) return false;
#else
        unsigned m=rec[15];
        if(!(m&128) && ((m&7)>6 || ((m>>3)&7)>4)) return false;
#endif
        total+=location_len(rec);
    }
    return total==raw_size;
}
static int64_t location_decode(const uint8_t*p,size_t payload_size,uint64_t nrows,uint8_t*out,size_t cap) {
    if(nrows>SIZE_MAX/41 || payload_size!=nrows*LOCATION_STRIDE+(LOCATION_STRIDE==15)) return -1;
    size_t o=0;
    for(uint64_t i=0;i<nrows;i++) {
        const uint8_t* r=p+i*LOCATION_STRIDE;
        unsigned len=location_len(r);
        if(len>cap-o) return -1;
        o+=location_one(r,out+o);
    }
    return o;
}
static int64_t location_rows(const uint8_t*p,size_t payload_size,uint64_t nrows,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets) {
    if(nrows>SIZE_MAX/41 || payload_size!=nrows*LOCATION_STRIDE+(LOCATION_STRIDE==15)) return -1;
    size_t o=0; offsets[0]=0;
    for(size_t i=0;i<count;i++) {
        if(ids[i]>=nrows) return -1;
        const uint8_t* r=p+ids[i]*LOCATION_STRIDE;
        unsigned len=location_len(r);
        if(len>cap-o) return -1;
        o+=location_one(r,out+o); offsets[i+1]=o;
    }
    return o;
}
