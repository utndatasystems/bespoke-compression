#include "row_adapter.hpp"
#include <bit>
using namespace std;
// Format bridge only. The timed decoder is the unchanged upstream implementation.
void RowArchive::load(span<const byte> archive) {
    static_assert(endian::native == endian::little && sizeof(size_t) == 8);
    auto need = [](bool ok) { if (!ok) throw runtime_error("invalid OnPair+ row archive"); };
    auto read64 = [&](size_t off) { need(off <= archive.size() && archive.size()-off >= 8); uint64_t v; memcpy(&v, archive.data()+off, 8); return v; };
    need(archive.size() >= 32 && !memcmp(archive.data(), "OPROW001", 8));
    const uint64_t rows=read64(8), bytes=read64(16);
    need(rows < (archive.size()-24)/8);
    size_t offset=24+8*(rows+1);
    need(bytes==archive.size()-offset && bytes >= 24);
    const auto payload=archive.subspan(offset);
    auto get64 = [&](size_t off) { uint64_t v; memcpy(&v,payload.data()+off,8); return v; };
    size_t token_start=get64(0), merges=get64(8);
    need(token_start >= 16 && token_start <= payload.size()-8 && (token_start-16)%4==0);
    need(merges==16+(token_start-16)/4 && merges-16 <= 65536-256);
    need((payload.size()-8-token_start)%2==0);
    OnPair16 fresh;
    fresh.token_boundaries.push_back(0);
    for (unsigned i=0;i<256;++i) { fresh.dictionary.push_back(i); fresh.token_boundaries.push_back(i+1); }
    // Replay the exact parent pairs emitted by OnPair+ (same layout as the authors'
    // tools/onpairplus_train.cpp replayDictionary). memcpy avoids alignment assumptions.
    for(size_t i=16;i<token_start;i+=4) {
        uint16_t parents[2]; memcpy(parents,payload.data()+i,4);
        size_t count=fresh.token_boundaries.size()-1;
        need(parents[0]<count && parents[1]<count);
        vector<uint8_t> symbol;
        for(auto parent:parents) {
            auto begin=fresh.token_boundaries[parent], end=fresh.token_boundaries[parent+1];
            symbol.insert(symbol.end(),fresh.dictionary.begin()+begin,fresh.dictionary.begin()+end);
        }
        need(!symbol.empty() && symbol.size()<=16);
        fresh.dictionary.insert(fresh.dictionary.end(),symbol.begin(),symbol.end());
        fresh.token_boundaries.push_back(fresh.dictionary.size());
    }
    fresh.dictionary.resize(fresh.dictionary.size()+16,0); // Required by upstream fixed-width reads.
    size_t tokens=(payload.size()-8-token_start)/2;
    fresh.compressed_data.resize(tokens);
    if(tokens) memcpy(fresh.compressed_data.data(),payload.data()+token_start,2*tokens);
    uint64_t reconstructed=0;
    for(auto token:fresh.compressed_data) {
        need(size_t(token)+1<fresh.token_boundaries.size());
        reconstructed+=fresh.token_boundaries[token+1]-fresh.token_boundaries[token];
    }
    original_bytes=get64(payload.size()-8);
    need(reconstructed==original_bytes);
    fresh.string_boundaries.resize(rows+1);
    for(size_t i=0;i<=rows;++i) {
        auto v=read64(24+8*i);
        need(v<=tokens && (i==0 ? v==0 : v>=fresh.string_boundaries[i-1]));
        fresh.string_boundaries[i]=v;
    }
    need(fresh.string_boundaries.back()==tokens);
    decoder=std::move(fresh);
}
