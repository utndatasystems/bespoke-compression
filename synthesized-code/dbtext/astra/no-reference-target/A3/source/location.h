#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <immintrin.h>
#ifdef ENCODER
#include <vector>
#endif

namespace location {
static constexpr uint64_t MAGIC = 0x3135434f4c544244ULL;
struct Header { uint64_t magic, count, bytes; };
struct State { const uint8_t* records; uint64_t count, bytes; };

#ifdef ENCODER
static bool encode(const uint8_t* raw, size_t size, std::vector<uint8_t>& out) {
    if (size < 16 || std::memcmp(raw, "(40.", 4)) return false;
    std::vector<uint8_t> data;
    data.reserve(size / 2);
    size_t pos = 0; uint64_t count = 0;
    while (pos < size) {
        size_t end = pos;
        while (end < size && raw[end] != '\n') ++end;
        if (end == size) return false;
        const uint8_t* r = raw + pos;
        size_t len = end - pos;
        uint8_t rec[15] = {};
        if (len == 4 && !std::memcmp(r, "NULL", 4)) {
            rec[0] = 127;
        } else {
            if (len < 31 || std::memcmp(r, "(40.", 4) || r[len-1] != ')') return false;
            size_t split = 4;
            while (split < len && r[split] >= '0' && r[split] <= '9') ++split;
            size_t alen = split - 4;
            if (alen < 9 || alen > 15 || split + 7 >= len) return false;
            if (std::memcmp(r+split, ", -7", 4) || (r[split+4] != '3' && r[split+4] != '4') || r[split+5] != '.') return false;
            unsigned flag = r[split+4] == '4';
            size_t bstart = split+6, blen = len-1-bstart;
            if (blen < 10 || blen > 14) return false;
            for (size_t k = 0; k < blen; ++k) if (r[bstart+k] < '0' || r[bstart+k] > '9') return false;
            uint8_t digits[29];
            for (size_t k=0;k<alen;++k) digits[k]=r[4+k]-'0';
            for (size_t k=0;k<blen;++k) digits[alen+k]=r[bstart+k]-'0';
            if (alen+blen == 29) {
                rec[0] = 128 | (flag<<4) | digits[0];
                for (size_t k=1;k<29;++k) rec[(k+1)/2] |= digits[k] << ((k&1) ? 4 : 0);
            } else {
                rec[0] = (alen-9) | ((blen-10)<<3) | (flag<<6);
                for (size_t k=0;k<alen+blen;++k) rec[1+k/2] |= digits[k] << ((k&1) ? 0 : 4);
            }
        }
        data.insert(data.end(),rec,rec+15); ++count; pos=end+1;
    }
    Header hdr{MAGIC,count,size};
    out.resize(sizeof(hdr) + data.size()+16);
    std::memcpy(out.data(), &hdr, sizeof(hdr));
    std::memcpy(out.data()+sizeof(hdr), data.data(),data.size());
    std::memset(out.data()+sizeof(hdr)+data.size(),0,16);
    return true;
}
#endif

static void* open(const uint8_t* archive, size_t size) {
    if (size < sizeof(Header)+16) return nullptr;
    Header h; std::memcpy(&h,archive,sizeof(h));
    if (h.magic != MAGIC || h.count > (size-sizeof(Header)-16)/15 || sizeof(Header)+h.count*15+16 != size) return nullptr;
    State* s = static_cast<State*>(std::malloc(sizeof(State)));
    if (!s) return nullptr;
    *s = {archive+sizeof(Header),h.count,h.bytes}; return s;
}
static inline unsigned length(const uint8_t* r) {
    unsigned m = r[0];
    if (m == 127) return 5;
    if (m >= 128) return 41;
    return 31 + (m&7) + ((m>>3)&7);
}
static inline unsigned unpack(const uint8_t* r,uint8_t* out) {
    const unsigned m=r[0];
    if (m == 127) { std::memcpy(out,"NULL\n",5); return 5; }
    unsigned a,b,start,flag;
    if (m>=128) { a=15; b=14; start=1; flag=(m>>4)&1; }
    else { a=9+(m&7); b=10+((m>>3)&7); start=2; flag=(m>>6)&1; }
    const __m128i v=_mm_loadu_si128(reinterpret_cast<const __m128i*>(r));
    const __m128i mask=_mm_set1_epi8(15), zero=_mm_set1_epi8('0');
    const __m128i lo=_mm_and_si128(v,mask), hi=_mm_and_si128(_mm_srli_epi16(v,4),mask);
    const __m128i first=_mm_add_epi8(_mm_unpacklo_epi8(hi,lo),zero);
    const __m128i second=_mm_add_epi8(_mm_unpackhi_epi8(hi,lo),zero);
    const __m256i digits=_mm256_inserti128_si256(_mm256_castsi128_si256(first),second,1);
    const __m128i indexes=_mm_add_epi8(_mm_setr_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15),_mm_set1_epi8(start+a));
    const __m128i longitude=_mm256_castsi256_si128(_mm256_permutexvar_epi8(_mm256_castsi128_si256(indexes),digits));
    const __m128i latitude=(m>=128)?_mm_alignr_epi8(second,first,1):_mm_alignr_epi8(second,first,2);
    std::memcpy(out,"(40.",4);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(out+4),latitude);
    const uint64_t sep=0x00002e33372d202cULL+(uint64_t(flag)<<32);
    std::memcpy(out+4+a,&sep,8);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(out+10+a),longitude);
    std::memcpy(out+10+a+b,")\n",2);
    return 12+a+b;
}
static int64_t decode(void* state,uint8_t* out,size_t capacity) {
    auto* s=static_cast<State*>(state);
    if (!s || capacity<s->bytes || (!out && s->bytes)) return -1;
    uint8_t* p=out; const uint8_t* r=s->records;
    for (uint64_t i=0;i<s->count;++i,r+=15) {
        unsigned n=length(r);
        if (size_t(p-out)>capacity || capacity-size_t(p-out)<n) return -1;
        if (size_t(out+capacity-p)>=n+16) unpack(r,p);
        else { uint8_t tmp[64]; unpack(r,tmp); std::memcpy(p,tmp,n); }
        p+=n;
    }
    return uint64_t(p-out)==s->bytes ? p-out : -1;
}
static int64_t rows(void* state,const uint64_t* ids,size_t count,uint8_t* out,size_t capacity,uint64_t* offsets) {
    auto* s=static_cast<State*>(state); if(!s||!offsets||(!ids&&count)||(!out&&count))return -1; uint8_t* p=out; offsets[0]=0;
    for(size_t k=0;k<count;++k) {
        if(ids[k]>=s->count) return -1;
        const uint8_t* r=s->records+ids[k]*15; unsigned n=length(r);
        const size_t remain=capacity-size_t(p-out);
        if(remain<n) return -1;
        if(remain>=n+16) unpack(r,p);
        else { uint8_t tmp[64]; unpack(r,tmp); std::memcpy(p,tmp,n); }
        p+=n; offsets[k+1]=p-out;
    }
    return p-out;
}
static void close(void* state) { std::free(state); }
}
