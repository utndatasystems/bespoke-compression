#ifndef DBTEXT_HUFFMAN_H
#define DBTEXT_HUFFMAN_H
// Independently implemented canonical Huffman coding. MSB-first wire format.
// Save HEnc::lengths as uint8_t values. Save HBitWriter::bytes including padding.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <queue>
#include <stdexcept>
#include <utility>
#include <vector>

struct HEnc {
    static constexpr unsigned MAX_BITS = 24;
    std::vector<uint8_t> lengths;
    std::vector<uint32_t> codes;
    HEnc() = default;
    explicit HEnc(const std::vector<uint64_t>& frequencies) { build(frequencies); }
    void build(const std::vector<uint64_t>& frequencies) {
        const size_t n=frequencies.size();
        if(n>65536) throw std::runtime_error("Huffman alphabet too large");
        lengths.assign(n,0); codes.assign(n,0);
        std::vector<uint64_t> weights=frequencies;
        for(;;) {
            struct Node { int left,right; };
            std::vector<Node> nodes(n,{-1,-1});
            using Item=std::pair<uint64_t,int>;
            std::priority_queue<Item,std::vector<Item>,std::greater<Item>> heap;
            for(size_t s=0;s<n;++s) if(weights[s]) heap.emplace(weights[s],int(s));
            if(heap.empty()) return;
            if(heap.size()==1) { lengths[heap.top().second]=1; break; }
            while(heap.size()>1) {
                const Item a=heap.top(); heap.pop();
                const Item b=heap.top(); heap.pop();
                const int id=int(nodes.size());
                nodes.push_back({a.second,b.second});
                heap.emplace(a.first+b.first,id);
            }
            unsigned longest=0;
            std::vector<std::pair<int,unsigned>> stack;
            stack.emplace_back(heap.top().second,0);
            while(!stack.empty()) {
                const auto v=stack.back(); stack.pop_back();
                if(size_t(v.first)<n) {
                    // A Huffman tree over <=65536 leaves has depth <=65535.
                    // Do not truncate into the uint8_t length array on failure.
                    if(v.second>longest) longest=v.second;
                    if(v.second<=MAX_BITS) lengths[v.first]=uint8_t(v.second);
                } else {
                    stack.emplace_back(nodes[v.first].left,v.second+1);
                    stack.emplace_back(nodes[v.first].right,v.second+1);
                }
            }
            if(longest<=MAX_BITS) break;
            // Limit depth without requiring a second coding format. Iteratively
            // flatten only the fitting weights, retaining the original symbols.
            // All nonzero weights remain nonzero and eventually become one.
            std::fill(lengths.begin(),lengths.end(),0);
            for(auto& w:weights) if(w) w=(w>>1)+(w&1);
        }
        uint32_t counts[MAX_BITS+1]={},next[MAX_BITS+1]={};
        for(uint8_t l:lengths) if(l) ++counts[l];
        uint32_t code=0;
        for(unsigned l=1;l<=MAX_BITS;++l) { code=(code+counts[l-1])<<1; next[l]=code; }
        for(size_t s=0;s<n;++s) if(lengths[s]) codes[s]=next[lengths[s]]++;
    }
};

struct HBitWriter {
    std::vector<uint8_t> bytes;
    uint64_t bit_size=0;
    uint64_t pending=0;
    unsigned count=0;
    void put(uint32_t code,unsigned length) {
        pending=(pending<<length)|code;
        count+=length; bit_size+=length;
        while(count>=8) { count-=8; bytes.push_back(uint8_t(pending>>count)); }
        pending &= count ? ((uint64_t(1)<<count)-1) : 0;
    }
    void put(const HEnc& h,uint32_t symbol) { put(h.codes[symbol],h.lengths[symbol]); }
    // Call exactly once. Eight zero guard bytes permit unaligned word reads
    // at any valid code position, including a last partial byte.
    void finish() {
        if(count) bytes.push_back(uint8_t(pending<<(8-count)));
        count=0; pending=0;
        bytes.insert(bytes.end(),8,0);
    }
};

struct HDec {
    static constexpr unsigned MAX_BITS=24;
    unsigned look_bits=11;
    std::vector<uint32_t> lookup;
    uint32_t first[MAX_BITS+1]={},count[MAX_BITS+1]={},base[MAX_BITS+1]={};
    std::vector<uint16_t> ordered;
    unsigned max_bits=0;
    HDec()=default;
    explicit HDec(const std::vector<uint8_t>& lengths) { build(lengths.data(),lengths.size()); }
    HDec(const uint8_t* lengths,size_t n) { build(lengths,n); }
    void build(const uint8_t* lengths,size_t n) {
        if(n>65536) throw std::runtime_error("Huffman alphabet too large");
        look_bits=n>16384?16:n>3500?14:n>1536?13:n>256?12:11;
        lookup.assign(1u<<look_bits,0);
        std::fill(first,first+MAX_BITS+1,0);
        std::fill(count,count+MAX_BITS+1,0);
        std::fill(base,base+MAX_BITS+1,0);
        max_bits=0;
        for(size_t s=0;s<n;++s) {
            const unsigned l=lengths[s];
            if(l>MAX_BITS) throw std::runtime_error("Invalid Huffman length");
            if(l) { ++count[l]; max_bits=std::max(max_bits,l); }
        }
        unsigned total=0;
        for(unsigned l=1;l<=MAX_BITS;++l) {
            first[l]=(first[l-1]+count[l-1])<<1;
            base[l]=total; total+=count[l];
            if(first[l]+count[l]>(1u<<l)) throw std::runtime_error("Invalid Huffman tree");
        }
        ordered.resize(total);
        uint32_t used[MAX_BITS+1]={};
        for(size_t s=0;s<n;++s) {
            const unsigned l=lengths[s];
            if(!l) continue;
            const uint32_t rank=used[l]++;
            ordered[base[l]+rank]=uint16_t(s);
            if(l<=look_bits) {
                const uint32_t start=(first[l]+rank)<<(look_bits-l);
                const uint32_t entry=(uint32_t(s)<<5)|l;
                std::fill(lookup.begin()+start,lookup.begin()+start+(1u<<(look_bits-l)),entry);
            }
        }
    }
    // data must include the eight guard bytes written by HBitWriter::finish.
    // bit_position is an absolute bit offset within data and advances in place.
    inline uint32_t decode(const uint8_t* data,uint64_t& bit_position) const {
        uint64_t word;
        std::memcpy(&word,data+(bit_position>>3),8);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#else
        word=__builtin_bswap64(word);
#endif
        word <<= (bit_position&7);
        const uint32_t entry=lookup[word>>(64-look_bits)];
        if(entry) { bit_position+=entry&31; return entry>>5; }
        for(unsigned l=look_bits+1;l<=max_bits;++l) {
            const uint32_t rank=uint32_t(word>>(64-l))-first[l];
            if(rank<count[l]) { bit_position+=l; return ordered[base[l]+rank]; }
        }
        return UINT32_MAX;
    }
    // Convenience for callers without padded storage; the common interior path
    // is identical, with a checked copy only within the last eight bytes.
    inline uint32_t decode_safe(const uint8_t* data,size_t bytes,uint64_t& bit_position) const {
        const uint64_t offset=bit_position>>3;
        if(offset+8<=bytes) return decode(data,bit_position);
        if(offset>=bytes) return UINT32_MAX;
        uint8_t tail[8]={};
        std::memcpy(tail,data+offset,bytes-size_t(offset));
        uint64_t local=bit_position&7;
        const uint32_t symbol=decode(tail,local);
        bit_position=(offset<<3)+local;
        return symbol;
    }
};
#endif
