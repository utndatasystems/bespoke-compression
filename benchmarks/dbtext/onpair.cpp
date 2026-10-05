#include "codec.h"
#include <array>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>
#ifndef DECODE_ONLY
#include "compressor/onpair_advanced/OnPairAdvancedCompressor.hpp"
#endif

// Row-directory adapter around pinned OnPair+ (onpairbyte16, cutoff=true,
// store_lengths=true). The dictionary and token stream are the author's format.
// Dictionary expansion and the 4-token decode kernel follow the author code;
// row offsets are added because the artifact concatenates encoded strings.
static void need(bool ok) {if(!ok)throw std::runtime_error("invalid OnPair archive");}
template<class T> static T get(const uint8_t* p) {T x;std::memcpy(&x,p,sizeof(x));return x;}
template<class T> static void put(uint8_t* p,T x) {std::memcpy(p,&x,sizeof(x));}
struct Dictionary {
    std::array<std::array<uint8_t,16>,65536> symbols{};
    std::array<uint8_t,65536> lengths{};
    size_t count=256;
    Dictionary(const uint8_t* data,size_t size) {
        need(size>=24);size_t offset=get<uint64_t>(data),end=get<uint64_t>(data+8);
        need(end>=16 && end-16<=65536-256 && offset==16+4*(end-16) && offset<=size-8);
        for(size_t i=0;i<256;++i) {symbols[i][0]=i;lengths[i]=1;}
        for(size_t i=0;i<end-16;++i) {
            auto a=get<uint16_t>(data+16+4*i),b=get<uint16_t>(data+18+4*i);
            need(a<count && b<count && lengths[a]+lengths[b]<=16);
            std::memcpy(symbols[count].data(),symbols[a].data(),lengths[a]);
            std::memcpy(symbols[count].data()+lengths[a],symbols[b].data(),lengths[b]);
            lengths[count]=lengths[a]+lengths[b];++count;
        }
    }
};
#ifndef DECODE_ONLY
extern "C" int64_t lab_encode(const uint8_t* raw,size_t n,uint8_t* out,size_t cap) try {
    std::vector<std::string_view> rows;size_t begin=0;
    for(size_t i=0;i<n;++i)if(raw[i]=='\n'){rows.emplace_back((const char*)raw+begin,i+1-begin);begin=i+1;}
    if(begin<n)rows.emplace_back((const char*)raw+begin,n-begin);
    using namespace sgtt::compressor;
    OnPairAdvancedCompressor<onpair::MaxSymbolLength::SIXTEEN> codec({true,true});
    std::vector<std::byte> encoded;codec.prepare(n);codec.compress(rows,encoded);
    size_t header=16+4*(rows.size()+1);need(cap>=header+encoded.size() && encoded.size()<UINT32_MAX);
    put<uint64_t>(out,n);put<uint64_t>(out+8,rows.size());
    auto data=reinterpret_cast<const uint8_t*>(encoded.data());
    Dictionary dictionary(data,encoded.size());size_t pos=get<uint64_t>(data);const size_t first=pos;
    for(size_t i=0;i<rows.size();++i) {
        put<uint32_t>(out+16+4*i,uint32_t(pos-first));size_t decoded=0;
        while(decoded<rows[i].size()) {need(pos+2<=encoded.size()-8);auto id=get<uint16_t>(data+pos);need(id<dictionary.count);decoded+=dictionary.lengths[id];pos+=2;}
        need(decoded==rows[i].size());
    }
    need(pos==encoded.size()-8);put<uint32_t>(out+16+4*rows.size(),uint32_t(pos-first));
    std::memcpy(out+header,data,encoded.size());return header+encoded.size();
}catch(...){return -1;}
#else
struct State {
    size_t raw,rows;const uint8_t* index;const uint8_t* tokens;size_t bytes;Dictionary dictionary;
    State(const uint8_t* data,size_t n,size_t header):raw(get<uint64_t>(data)),rows(get<uint64_t>(data+8)),index(data+16),
        tokens(data+header+get<uint64_t>(data+header)),bytes(n-header-get<uint64_t>(data+header)-8),dictionary(data+header,n-header) {}
};
extern "C" void* lab_open(const uint8_t* data,size_t n)try{
    need(n>=44);size_t rows=get<uint64_t>(data+8);need(rows<=(n-44)/4);size_t header=16+4*(rows+1);
    need(get<uint64_t>(data+header)<=n-header-8);
    auto s=std::make_unique<State>(data,n,header);need(s->raw==get<uint64_t>(data+n-8) && s->raw<0x20000000);
    need(s->bytes%2==0 && get<uint32_t>(s->index)==0 && get<uint32_t>(s->index+4*rows)==s->bytes);
    for(size_t i=1;i<=rows;++i)need(get<uint32_t>(s->index+4*i)>=get<uint32_t>(s->index+4*(i-1)) && get<uint32_t>(s->index+4*i)%2==0);
    return s.release();
}catch(...){return nullptr;}
static size_t range(State& s,size_t a,size_t b,uint8_t* out,size_t cap) {
    need(a<=b && b<=s.bytes && a%2==0 && b%2==0);size_t pos=0;
    auto token=[&](size_t at){auto id=get<uint16_t>(s.tokens+at);auto length=s.dictionary.lengths[id];
        need(length && length<=cap-pos);std::memcpy(out+pos,s.dictionary.symbols[id].data(),cap-pos>=16?16:length);pos+=length;};
    // The author expands four symbols per iteration. One capacity guard is
    // sufficient for four maximum-length (16-byte) stores; only the tail needs
    // individual capacity checks. Every uint16 index addresses the fixed table.
    for(;a+8<=b && cap-pos>=64;a+=8){
        auto t0=get<uint16_t>(s.tokens+a),t1=get<uint16_t>(s.tokens+a+2);
        auto t2=get<uint16_t>(s.tokens+a+4),t3=get<uint16_t>(s.tokens+a+6);
        std::memcpy(out+pos,s.dictionary.symbols[t0].data(),16);pos+=s.dictionary.lengths[t0];
        std::memcpy(out+pos,s.dictionary.symbols[t1].data(),16);pos+=s.dictionary.lengths[t1];
        std::memcpy(out+pos,s.dictionary.symbols[t2].data(),16);pos+=s.dictionary.lengths[t2];
        std::memcpy(out+pos,s.dictionary.symbols[t3].data(),16);pos+=s.dictionary.lengths[t3];
    }
    for(;a<b;a+=2)token(a);return pos;
}
extern "C" int64_t lab_decode(void* state,uint8_t* out,size_t cap)try{
    auto& s=*static_cast<State*>(state);need(s.raw<=cap);auto n=range(s,0,s.bytes,out,cap);need(n==s.raw);return n;
}catch(...){return -1;}
extern "C" int64_t lab_rows(void* state,const uint64_t* ids,size_t count,uint8_t* out,size_t cap,uint64_t* offsets)try{
    auto& s=*static_cast<State*>(state);size_t pos=0;offsets[0]=0;
    for(size_t i=0;i<count;++i){need(ids[i]<s.rows);size_t a=get<uint32_t>(s.index+4*ids[i]),b=get<uint32_t>(s.index+4*(ids[i]+1));pos+=range(s,a,b,out+pos,cap-pos);offsets[i+1]=pos;}
    return pos;
}catch(...){return -1;}
extern "C" void lab_close(void* state){delete static_cast<State*>(state);}
#endif
