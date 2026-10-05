#include "codec.h"
#include "fsst.h"
#include <lz4.h>
#include <zstd.h>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

#ifndef LAB_ROW_DELIMITER
#define LAB_ROW_DELIMITER 10
#endif

#ifndef LAB_ZSTD_LEVEL
#define LAB_ZSTD_LEVEL 1
#endif

// Build METHOD=0 (LZ4 bulk), 1 (Zstd bulk), 2 (FSST indexed strings).
static void need(bool ok) { if (!ok) throw std::runtime_error("invalid codec input"); }
static uint64_t get64(const uint8_t* p) { uint64_t n; std::memcpy(&n,p,8); return n; }
static uint32_t get32(const uint8_t* p) { uint32_t n; std::memcpy(&n,p,4); return n; }
static void put64(uint8_t* p,uint64_t n) { std::memcpy(p,&n,8); }
static void put32(uint8_t* p,uint32_t n) { std::memcpy(p,&n,4); }
#ifndef DECODE_ONLY
extern "C" int64_t lab_encode(const uint8_t* raw,size_t n,uint8_t* out,size_t cap) try {
    need(n < 0x20000000 && cap >= 16); put64(out,n);
#if METHOD == 0
    auto written=LZ4_compress_default((const char*)raw,(char*)out+8,int(n),int(cap-8));
    need(written>0); return written+8;
#elif METHOD == 1
    auto written=ZSTD_compress(out+8,cap-8,raw,n,LAB_ZSTD_LEVEL); need(!ZSTD_isError(written)); return written+8;
#else
    if(n==0) {need(cap>=20);put64(out+8,0);put32(out+16,0);return 20;}
    std::vector<size_t> lengths; std::vector<const uint8_t*> pointers;
    size_t begin=0;
    for(size_t i=0;i<n;++i) if(raw[i]==LAB_ROW_DELIMITER) { lengths.push_back(i+1-begin);pointers.push_back(raw+begin);begin=i+1; }
    if(begin<n) {lengths.push_back(n-begin);pointers.push_back(raw+begin);}
    put64(out+8,lengths.size());
    size_t header=16+4*(lengths.size()+1); need(cap>header+FSST_MAXHEADER+2*n+32);
    auto enc=std::unique_ptr<fsst_encoder_t,decltype(&fsst_destroy)>(fsst_create(lengths.size(),lengths.data(),pointers.data(),0),fsst_destroy);
    need(bool(enc)); auto table=fsst_export(enc.get(),out+header);
    std::vector<size_t> sizes(lengths.size());std::vector<uint8_t*> strings(lengths.size());
    uint8_t* payload=out+header+table;
    auto count=fsst_compress(enc.get(),lengths.size(),lengths.data(),pointers.data(),cap-header-table,payload,sizes.data(),strings.data());
    need(count==lengths.size()); size_t end=0;
    for(size_t i=0;i<count;++i) { need(strings[i]==payload+end);put32(out+16+4*i,uint32_t(end));end+=sizes[i]; }
    put32(out+16+4*count,uint32_t(end));return header+table+end;
#endif
} catch(...) {return -1;}
#else
struct State {
    const uint8_t* data;size_t size;size_t raw;
#if METHOD == 2
    size_t rows;const uint8_t* index;const uint8_t* payload;size_t payload_size;fsst_decoder_t decoder;
#endif
};
extern "C" void* lab_open(const uint8_t* data,size_t size) try {
    need(size>=8);auto s=std::make_unique<State>();s->data=data;s->size=size;s->raw=get64(data);need(s->raw<0x20000000);
#if METHOD == 2
    need(size>=20);s->rows=get64(data+8);need(s->rows<=(size-20)/4);s->index=data+16;
    if(s->rows==0) {need(s->raw==0 && size==20 && get32(s->index)==0);s->payload=data+20;s->payload_size=0;return s.release();}
    size_t header=16+4*(s->rows+1);need(size-header>=17);
    // FSST import reads up to FSST_MAXHEADER bytes; padded copy bounds truncated headers.
    uint8_t table[FSST_MAXHEADER]={};std::memcpy(table,data+header,std::min(size-header,size_t(FSST_MAXHEADER)));
    auto used=fsst_import(&s->decoder,table);need(used>0 && used<=size-header);
    s->payload=data+header+used;s->payload_size=size-header-used;
    need(get32(s->index)==0 && get32(s->index+4*s->rows)==s->payload_size);
    for(size_t i=1;i<=s->rows;++i) need(get32(s->index+4*i)>=get32(s->index+4*(i-1)));
#endif
    return s.release();
} catch(...) {return nullptr;}
#if METHOD == 2
static size_t row(State& s,size_t id,uint8_t* out,size_t cap) {
    need(id<s.rows);size_t a=get32(s.index+4*id),b=get32(s.index+4*(id+1));
    auto n=fsst_decompress(&s.decoder,b-a,s.payload+a,cap,out);need(n<=cap);return n;
}
extern "C" int64_t lab_rows(void* state,const uint64_t* ids,size_t count,uint8_t* out,size_t cap,uint64_t* offsets) try {
    auto& s=*static_cast<State*>(state);size_t pos=0;offsets[0]=0;
    for(size_t i=0;i<count;++i) {pos+=row(s,ids[i],out+pos,cap-pos);offsets[i+1]=pos;}
    return pos;
} catch(...) {return -1;}
#endif
extern "C" int64_t lab_decode(void* state,uint8_t* out,size_t cap) try {
    auto& s=*static_cast<State*>(state);need(s.raw<=cap);
#if METHOD == 0
    auto n=LZ4_decompress_safe((const char*)s.data+8,(char*)out,int(s.size-8),int(cap));need(n>=0 && size_t(n)==s.raw);return n;
#elif METHOD == 1
    auto n=ZSTD_decompress(out,cap,s.data+8,s.size-8);need(!ZSTD_isError(n) && n==s.raw);return n;
#else
    // FSST strings share one symbol table and concatenate without extra state.
    // A full scan must use the native whole-stream kernel, just as OnPair+ does;
    // row-by-row dispatch is needed only for the selective workload.
    if(s.raw==0)return 0;
    auto n=fsst_decompress(&s.decoder,s.payload_size,s.payload,cap,out);need(n==s.raw);return n;
#endif
} catch(...) {return -1;}
extern "C" void lab_close(void* state) {delete static_cast<State*>(state);}
#endif
