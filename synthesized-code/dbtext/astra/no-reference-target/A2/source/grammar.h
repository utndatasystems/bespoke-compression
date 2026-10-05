#ifndef DBTEXT_GRAMMAR_H
#define DBTEXT_GRAMMAR_H
#include "huffman.h"
// Compact self-delimiting Re-Pair grammar metadata. All integers are little
// endian. Pair references use Huffman-coded magnitude categories plus literal
// low bits. Huffman code lengths use a second 25-symbol canonical code.
// Layout: u32 total_bytes, u32 payload_bits, 25 length-model code lengths,
// 16 magnitude-model code lengths, payload, eight zero guard bytes.
// Payload contains pairs, then ns code lengths.
struct GrammarPair { uint16_t a,b; };
static inline uint32_t grammar_u32(const uint8_t* p) {
    uint32_t x; std::memcpy(&x,p,4); return x;
}
static inline unsigned grammar_width(uint32_t current_id) {
    return 32u-unsigned(__builtin_clz(current_id-1));
}
static inline uint32_t grammar_get(const uint8_t* p,uint64_t& bit,unsigned n) {
    uint64_t x;std::memcpy(&x,p+(bit>>3),8);
    x=__builtin_bswap64(x);x<<=bit&7;bit+=n;
    return uint32_t(x>>(64-n));
}
#ifdef ENCODER
// Pair may be any structure exposing integer members a and b. Exactly
// lengths.size()-base pairs are read. Returned bytes include their own size.
template<class Pair>
static std::vector<uint8_t> grammar_pack(uint32_t base,const std::vector<uint8_t>& lengths,const Pair* pairs) {
    const uint32_t ns=lengths.size();
    if(base<2||ns<base||ns>65536)throw std::runtime_error("Invalid grammar size");
    std::vector<uint64_t> frequencies(25);
    for(uint8_t l:lengths) {if(l>24)throw std::runtime_error("Invalid code length");++frequencies[l];}
    std::vector<uint64_t> magnitudes(16);
    for(uint32_t i=base;i<ns;++i) {
        const auto& pair=pairs[i-base];
        if(pair.a>=i||pair.b>=i)throw std::runtime_error("Invalid grammar reference");
        ++magnitudes[31u-unsigned(__builtin_clz(uint32_t(pair.a)+1))];
        ++magnitudes[31u-unsigned(__builtin_clz(uint32_t(pair.b)+1))];
    }
    HEnc model(frequencies),magnitude(magnitudes);HBitWriter bits;
    auto put=[&](uint32_t v) {
        const unsigned category=31u-unsigned(__builtin_clz(v+1));
        bits.put(magnitude,category);
        if(category)bits.put(v+1-(1u<<category),category);
    };
    for(uint32_t i=base;i<ns;++i) {put(pairs[i-base].a);put(pairs[i-base].b);}
    for(uint8_t l:lengths)bits.put(model,l);
    uint32_t nbits=bits.bit_size;bits.finish();
    std::vector<uint8_t> result(49);
    const uint32_t bytes=uint32_t(result.size()+bits.bytes.size());
    std::memcpy(result.data(),&bytes,4);std::memcpy(result.data()+4,&nbits,4);
    std::memcpy(result.data()+8,model.lengths.data(),25);
    std::memcpy(result.data()+33,magnitude.lengths.data(),16);
    result.insert(result.end(),bits.bytes.begin(),bits.bytes.end());
    return result;
}
#endif
// Returns consumed bytes, or zero on malformed input. The caller validates
// semantic constraints such as no embedded row terminators on a left branch.
static size_t grammar_unpack(const uint8_t* data,size_t available,uint32_t base,uint32_t ns,
                             std::vector<GrammarPair>& pairs,std::vector<uint8_t>& lengths) {
    if(available<57||base<2||ns<base||ns>65536)return 0;
    const uint32_t bytes=grammar_u32(data),nbits=grammar_u32(data+4);
    if(bytes>available||bytes!=uint64_t(57)+(uint64_t(nbits)+7)/8)return 0;
    if(uint64_t(nbits)<uint64_t(ns)+2*(ns-base))return 0;
    HDec model,magnitude;
    try {model.build(data+8,25);magnitude.build(data+33,16);}catch(...) {return 0;}
    const uint8_t* stream=data+49;uint64_t bit=0;
    pairs.resize(ns-base);lengths.resize(ns);
    auto get=[&]()->uint32_t {
        if(bit>=nbits)return UINT32_MAX;
        const uint32_t category=magnitude.decode(stream,bit);
        if(category>15||bit+category>nbits)return UINT32_MAX;
        return (1u<<category)-1+(category?grammar_get(stream,bit,category):0);
    };
    for(uint32_t i=base;i<ns;++i) {
        uint32_t a=get(),b=get();
        if(a>=i||b>=i)return 0;pairs[i-base]={uint16_t(a),uint16_t(b)};
    }
    for(uint32_t i=0;i<ns;++i) {
        if(bit>=nbits)return 0;
        const uint32_t l=model.decode(stream,bit);
        if(l>24||bit>nbits)return 0;lengths[i]=uint8_t(l);
    }
    return bit==nbits?bytes:0;
}
#endif
