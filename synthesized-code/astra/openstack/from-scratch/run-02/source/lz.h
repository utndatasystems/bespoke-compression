#ifndef LAB_LZ_H
#define LAB_LZ_H
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

// A self-contained LZ77 stream. A token holds literal length in its high
// nibble and match length minus four in its low nibble. Nibble 15 is extended
// with an unsigned LEB128 integer. Distances use 15 or 23 bits, in 2 or 3 bytes.
// A final literal-only sequence has a zero low nibble. The input length is the
// stream boundary; no external dictionary or process state is used.
namespace lz {
inline void put_var(std::vector<uint8_t>& v, size_t n) {
    while (n >= 128) { v.push_back(uint8_t(n) | 128); n >>= 7; }
    v.push_back(uint8_t(n));
}
inline bool get_var(const uint8_t*& p, const uint8_t* e, size_t& v) {
    v=0; unsigned shift=0;
    while (p<e) {
        uint8_t c=*p++;
        if (shift >= sizeof(size_t)*8 ||
            size_t(c&127) > (std::numeric_limits<size_t>::max() >> shift))
            return false;
        v |= size_t(c&127) << shift;
        if (!(c&128)) return true;
        shift += 7;
    }
    return false;
}
inline uint32_t load32(const uint8_t* p) {
    uint32_t a; std::memcpy(&a,p,4); return a;
}
inline unsigned hash4(const uint8_t* p) {
    return (load32(p)*2654435761u) >> 15;
}
inline size_t common(const uint8_t* a, const uint8_t* b, size_t lim) {
    size_t j=4;
    while (j+8<=lim) {
        uint64_t x,y; std::memcpy(&x,a+j,8);std::memcpy(&y,b+j,8);
        uint64_t d=x^y;
        if(d) return j+unsigned(__builtin_ctzll(d))/8;
        j+=8;
    }
    while (j<lim && a[j]==b[j]) ++j;
    return j;
}
inline std::vector<uint8_t> encode(const uint8_t* s, size_t n) {
    std::vector<uint8_t> dst;
    if (!n) return dst;
    dst.reserve(n/2+64);
    constexpr uint32_t NIL=0xffffffffu;
    constexpr size_t MAX_DIST=0x7fffff;
    // Streams this large are accepted with a literal-only representation.
    if (n>=NIL) {
        dst.push_back(0xf0); put_var(dst,n-15);
        dst.insert(dst.end(),s,s+n); return dst;
    }
    std::vector<uint32_t> head(1<<17,NIL), prev(n,NIL);
    auto insert=[&](size_t p) {
        if(p+4<=n) {
            unsigned h=hash4(s+p); prev[p]=head[h];head[h]=uint32_t(p);
        }
    };
    struct Match { size_t len=0, dist=0; };
    auto find=[&](size_t p) {
        Match m;
        if(p+4>n) return m;
        uint32_t q=head[hash4(s+p)]; unsigned depth=0;
        size_t lim=n-p;
        while(q!=NIL && size_t(q)<p && p-q<=MAX_DIST && depth++<1024) {
            if(load32(s+q)==load32(s+p) &&
               (!m.len || (m.len<lim && s[q+m.len]==s[p+m.len]))) {
                size_t len=common(s+p,s+q,lim);
                if(len>m.len || (len==m.len && p-q<m.dist)) {
                    m={len,p-q};
                    if(len==lim) break;
                }
            }
            q=prev[q];
        }
        return m;
    };
    size_t p=0,anchor=0;
    while(p+4<=n) {
        Match m=find(p); insert(p);
        if(m.len<4) { ++p; continue; }
        if(p+1+4<=n) {
            Match next=find(p+1);
            unsigned cost=m.dist>=32768 ? 3 : 2;
            unsigned nextcost=next.dist>=32768 ? 3 : 2;
            if(next.len && next.len+cost>m.len+nextcost+1) {
                ++p; continue;
            }
        }
        size_t lit=p-anchor,ml=m.len-4;
        dst.push_back(uint8_t((std::min<size_t>(lit,15)<<4)|
                             std::min<size_t>(ml,15)));
        if(lit>=15) put_var(dst,lit-15);
        dst.insert(dst.end(),s+anchor,s+p);
        size_t dist=m.dist;
        dst.push_back(uint8_t(dist));
        dst.push_back(uint8_t((dist>>8)&127)|(dist>=32768?128:0));
        if(dist>=32768) dst.push_back(uint8_t(dist>>15));
        if(ml>=15) put_var(dst,ml-15);
        size_t end=p+m.len;
        while(++p<end) insert(p);
        anchor=p;
    }
    if(anchor<n) {
        size_t lit=n-anchor;
        dst.push_back(uint8_t(std::min<size_t>(lit,15)<<4));
        if(lit>=15) put_var(dst,lit-15);
        dst.insert(dst.end(),s+anchor,s+n);
    }
    return dst;
}

namespace detail {
struct OptNode { uint32_t cost, pos; };
inline OptNode best(OptNode a, OptNode b) {
    return a.cost<b.cost || (a.cost==b.cost && a.pos>b.pos) ? a:b;
}
struct OptTree {
    size_t base=1;
    std::vector<OptNode> tree;
    explicit OptTree(size_t n) {
        while(base<n)base*=2;
        tree.assign(base*2,{0x3fffffffu,0});
    }
    void set(size_t p,uint32_t cost) {
        size_t j=p+base;tree[j]={cost,uint32_t(p)};
        while(j>1) {j>>=1;tree[j]=best(tree[j*2],tree[j*2+1]);}
    }
    OptNode query(size_t lo,size_t hi) const {
        OptNode ans={0x3fffffffu,0};
        for(lo+=base,hi+=base+1;lo<hi;lo>>=1,hi>>=1) {
            if(lo&1)ans=best(ans,tree[lo++]);
            if(hi&1)ans=best(ans,tree[--hi]);
        }
        return ans;
    }
};
struct OptMatch { uint32_t len=0,dist=0; };
}

// Optimal token parsing for bounded dictionaries. At every position a match
// finder records the longest match in each distance-price class. A backwards
// dynamic program uses range minima to consider every shorter match and every
// literal-run length without scanning those lengths one by one. Its output
// is compatible with decode and incurs no additional decoder or format cost.
inline std::vector<uint8_t> encode_optimal(const uint8_t* s,size_t n) {
    if(n<4 || n>100000000) return encode(s,n);
    constexpr uint32_t NIL=0xffffffffu;
    constexpr size_t MAX_DIST=0x7fffff;
    std::vector<uint32_t> head(1<<17,NIL),prev(n,NIL);
    std::vector<detail::OptMatch> near(n),far(n);
    detail::OptMatch a,b;
    for(size_t p=0;p+4<=n;++p) {
        if(a.len>=5)--a.len;else a={};
        if(b.len>=5)--b.len;else b={};
        size_t lim=n-p;
        unsigned h=hash4(s+p);uint32_t q=head[h];unsigned depth=0;
        while(q!=NIL && p-q<=MAX_DIST && depth++<1024) {
            size_t dist=p-q;
            if(a.len==lim)break;
            bool isnear=dist<32768;
            if(!isnear && b.len==lim)break;
            auto& m=isnear?a:b;
            if(load32(s+q)==load32(s+p) &&
               (!m.len || (m.len<lim && s[q+m.len]==s[p+m.len]))) {
                size_t len=common(s+p,s+q,lim);
                if(len>m.len)m={uint32_t(len),uint32_t(dist)};
            }
            q=prev[q];
        }
        near[p]=a;far[p]=b;prev[p]=head[h];head[h]=uint32_t(p);
    }
    // Match lengths: [4,18] need no extension; [19,146] need one
    // byte, [147,16402] need two, and so on. Literal extension
    // intervals have the same shape starting at 15.
    detail::OptTree dp(n+1),match(n+1);
    dp.set(n,0);
    std::vector<uint32_t> next(n,uint32_t(n)),mlen(n),mdist(n);
    for(size_t pos=n;pos--;) {
        detail::OptNode optmatch={0x3fffffffu,0};
        uint32_t dist=0;
        auto consider=[&](detail::OptMatch m,unsigned dcost,size_t minlen) {
            size_t low=4,high=18;unsigned ext=0;
            while(low<=m.len) {
                size_t l=std::max(low,minlen),r=std::min(high,size_t(m.len));
                if(l<=r) {
                    auto z=dp.query(pos+l,pos+r);
                    z.cost+=dcost+ext;
                    if(z.cost<optmatch.cost ||
                       (z.cost==optmatch.cost && z.pos>optmatch.pos)) {
                        optmatch=z;dist=m.dist;
                    }
                }
                low=high+1;
                high=ext==0?146:19+((high-18)*128)-1;
                ++ext;
            }
        };
        consider(near[pos],2,4);
        if(far[pos].len>near[pos].len)
            consider(far[pos],3,std::max<size_t>(4,near[pos].len+1));
        if(dist) {
            mlen[pos]=optmatch.pos-uint32_t(pos);mdist[pos]=dist;
            match.set(pos,optmatch.cost+uint32_t(pos));
        }
        size_t lit=n-pos;
        uint32_t ext=0;size_t x=lit;
        if(x>=15) {x-=15;do{++ext;x>>=7;}while(x);}
        uint32_t cost=uint32_t(1+lit+ext);
        size_t low=0,high=14;ext=0;
        while(low<n-pos) {
            size_t r=std::min(high,n-pos-1);
            auto z=match.query(pos+low,pos+r);
            uint32_t c=1+z.cost-uint32_t(pos)+ext;
            if(c<cost || (c==cost && z.pos<next[pos])) {
                cost=c;next[pos]=z.pos;
            }
            low=high+1;
            high=ext==0?142:15+((high-14)*128)-1;
            ++ext;
        }
        dp.set(pos,cost);
    }
    std::vector<uint8_t> dst;
    dst.reserve(dp.query(0,0).cost);
    size_t p=0;
    while(p<n) {
        size_t j=next[p],lit=j-p;
        if(j==n) {
            dst.push_back(uint8_t(std::min<size_t>(lit,15)<<4));
            if(lit>=15)put_var(dst,lit-15);
            dst.insert(dst.end(),s+p,s+n);break;
        }
        size_t len=mlen[j],d=mdist[j],ml=len-4;
        dst.push_back(uint8_t((std::min<size_t>(lit,15)<<4)|
                             std::min<size_t>(ml,15)));
        if(lit>=15)put_var(dst,lit-15);
        dst.insert(dst.end(),s+p,s+j);
        dst.push_back(uint8_t(d));
        dst.push_back(uint8_t((d>>8)&127)|(d>=32768?128:0));
        if(d>=32768)dst.push_back(uint8_t(d>>15));
        if(ml>=15)put_var(dst,ml-15);
        p=j+len;
    }
    return dst;
}
inline std::vector<uint8_t> encode_optimal(const std::vector<uint8_t>& s) {
    return encode_optimal(s.data(),s.size());
}
inline bool decode(const uint8_t* s, size_t n, std::vector<uint8_t>& out,
                   size_t max_output) {
    out.clear();
    if(!n) return true;
    const uint8_t* p=s;const uint8_t* e=s+n;
    // One allocation, followed by direct writes. max_output is an explicit
    // caller-supplied resource limit and may equal the expected output size.
    out.resize(max_output);
    uint8_t* b=out.data(); size_t used=0;
    auto fail=[&](){out.clear();return false;};
    while(p<e) {
        uint8_t token=*p++;size_t lit=token>>4;
        if(lit==15) {
            size_t x;if(!get_var(p,e,x) || x>max_output || lit>max_output-x)
                return fail();
            lit+=x;
        }
        if(lit>size_t(e-p) || lit>max_output-used) return fail();
        if(lit) {std::memcpy(b+used,p,lit);p+=lit;used+=lit;}
        if(p==e) {
            if(token&15) return fail();
            out.resize(used);return true;
        }
        if(e-p<2) return fail();
        size_t dist=size_t(p[0])|((size_t(p[1])&127)<<8);
        uint8_t hi=p[1];p+=2;
        if(hi&128) {
            if(p==e) return fail();
            dist|=size_t(*p++)<<15;
        }
        if(!dist || dist>used) return fail();
        size_t len=(token&15)+4;
        if((token&15)==15) {
            size_t x;if(!get_var(p,e,x) || x>max_output || len>max_output-x)
                return fail();
            len+=x;
        }
        if(len>max_output-used) return fail();
        uint8_t* d=b+used;const uint8_t* src=d-dist;
        if(dist>=len) std::memcpy(d,src,len);
        else if(dist==1) std::memset(d,*src,len);
        else {
            // The chunks never overlap, including the last partial chunk.
            // Doubling uses bytes already reconstructed by the prior copy.
            size_t done=std::min(dist,len);
            std::memcpy(d,src,done);
            while(done<len) {
                size_t chunk=std::min(done,len-done);
                std::memcpy(d+done,d,chunk);done+=chunk;
            }
        }
        used+=len;
    }
    out.resize(used);return true;
}
} // namespace lz
#endif
