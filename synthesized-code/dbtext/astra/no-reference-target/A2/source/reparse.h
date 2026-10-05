#ifndef DBTEXT_REPARSE_H
#define DBTEXT_REPARSE_H
#ifdef ENCODER
#include "grammar.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

// Encoder-only optimal parsing of the unchanged grammar. Byte position fixes
// preceding-byte context, so the cost callback can also use a context model.
class ReparseDictionary {
    struct Node {uint32_t first,term;uint16_t count;};
    struct Edge {uint32_t target;uint8_t byte;};
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    std::vector<uint32_t> next_term;
    std::vector<uint16_t> lengths;
    uint32_t roots[256];
    static constexpr uint32_t NONE=UINT32_MAX;
    uint32_t follow(uint32_t node,uint8_t byte)const {
        const Node& n=nodes[node];const Edge* e=edges.data()+n.first;
        // Small fan-outs dominate trie nodes; binary search the larger ones.
        if(n.count<=6) {for(unsigned i=0;i<n.count;++i)if(e[i].byte==byte)return e[i].target;return NONE;}
        unsigned lo=0,hi=n.count;
        while(lo<hi){unsigned m=(lo+hi)/2;if(e[m].byte<byte)lo=m+1;else hi=m;}
        return lo<n.count&&e[lo].byte==byte?e[lo].target:NONE;
    }
public:
    template<class Pair>
    ReparseDictionary(uint32_t base,uint32_t ns,const Pair* pairs) {
        if(base<256||ns<base||ns>65536)throw std::runtime_error("Invalid reparse dictionary");
        struct BuildNode {uint32_t child=NONE,next=NONE,term=NONE;uint8_t byte=0;};
        std::vector<BuildNode> build(1);std::vector<std::string> dict(ns);
        next_term.assign(ns,NONE);lengths.resize(ns);
        for(uint32_t i=0;i<256;++i)dict[i]=std::string(1,char(i));
        for(uint32_t i=base;i<ns;++i) {
            const auto& p=pairs[i-base];
            if(p.a>=i||p.b>=i)throw std::runtime_error("Invalid reparse reference");
            dict[i]=dict[p.a]+dict[p.b];
            if(dict[i].empty()||dict[i].size()>65535)throw std::runtime_error("Invalid reparse token");
        }
        for(uint32_t id=0;id<ns;++id) {
            const auto& word=dict[id];if(word.empty())continue;lengths[id]=word.size();
            uint32_t node=0;
            for(uint8_t b:word) {
                uint32_t next=build[node].child;
                while(next!=NONE&&build[next].byte!=b)next=build[next].next;
                if(next==NONE) {
                    next=build.size();BuildNode v;v.byte=b;v.next=build[node].child;
                    build.push_back(v);build[node].child=next;
                }
                node=next;
            }
            next_term[id]=build[node].term;build[node].term=id;
        }
        nodes.resize(build.size());edges.reserve(build.size()-1);
        std::vector<Edge> children;
        for(uint32_t i=0;i<build.size();++i) {
            children.clear();
            for(uint32_t j=build[i].child;j!=NONE;j=build[j].next)children.push_back({j,build[j].byte});
            std::sort(children.begin(),children.end(),[](const Edge& a,const Edge& b){return a.byte<b.byte;});
            nodes[i]={uint32_t(edges.size()),build[i].term,uint16_t(children.size())};
            edges.insert(edges.end(),children.begin(),children.end());
        }
        std::fill(roots,roots+256,NONE);
        for(unsigned i=0;i<nodes[0].count;++i){auto e=edges[nodes[0].first+i];roots[e.byte]=e.target;}
    }
    // Cost is an integer in caller-selected units. UINT32_MAX disables a token.
    // Ties prefer longer tokens. Each parsed token is an existing dictionary
    // entry: dictionary row-boundary restrictions therefore remain unchanged.
    template<class Cost>
    std::vector<uint32_t> parse(const uint8_t* raw,size_t size,Cost cost)const {
        std::vector<uint64_t> dp(size+1);std::vector<uint16_t> choice(size);
        dp[size]=0;
        for(size_t p=size;p-->0;) {
            uint64_t best=UINT64_MAX;uint32_t selected=NONE;size_t chosen_len=0;
            const uint8_t prev=p?raw[p-1]:10;
            uint32_t node=roots[raw[p]];size_t q=p+1;
            while(node!=NONE) {
                for(uint32_t id=nodes[node].term;id!=NONE;id=next_term[id]) {
                    const uint32_t c=cost(id,prev);if(c==UINT32_MAX||dp[q]==UINT64_MAX)continue;
                    const uint64_t v=dp[q]+c;
                    if(v<best||(v==best&&(q-p>chosen_len||(q-p==chosen_len&&id<selected)))) {
                        best=v;selected=id;chosen_len=q-p;
                    }
                }
                if(q==size)break;node=follow(node,raw[q++]);
            }
            if(selected==NONE)throw std::runtime_error("Unparseable input");
            dp[p]=best;choice[p]=uint16_t(selected);
        }
        std::vector<uint32_t> result;result.reserve(size/3+1);
        for(size_t p=0;p<size;){uint32_t id=choice[p];result.push_back(id);p+=lengths[id];}
        return result;
    }
};

// Plain-Huffman convenience optimizer. Every iteration fits a new code, parses
// to those lengths, and retains the smallest actual stream plus metadata. An
// unused symbol gets a finite one-observation cost so parsing may activate it.
template<class Pair>
static std::vector<uint32_t> refine_tokens(const uint8_t* raw,size_t size,uint32_t base,const Pair* pairs,
                                         const std::vector<uint32_t>& initial,uint32_t ns,unsigned iterations=3) {
    if(base!=256||initial.empty()||!iterations)return initial;
    ReparseDictionary dictionary(base,ns,pairs);
    auto evaluate=[&](const std::vector<uint32_t>& tokens,HEnc& model)->uint64_t {
        std::vector<uint64_t> freq(ns);for(uint32_t x:tokens)++freq[x];model.build(freq);
        uint64_t bits=0;for(uint32_t i=0;i<ns;++i)bits+=freq[i]*model.lengths[i];
        return (bits+7)/8+grammar_pack(base,model.lengths,pairs).size();
    };
    HEnc model;uint64_t best_size=evaluate(initial,model);std::vector<uint32_t> best=initial,current=initial;
    for(unsigned it=0;it<iterations;++it) {
        unsigned inactive=1;while((uint64_t(1)<<inactive)<current.size()+ns)++inactive;
        inactive=std::min(inactive,24u);
        auto parsed=dictionary.parse(raw,size,[&](uint32_t id,uint8_t)->uint32_t {
            return model.lengths[id]?model.lengths[id]:inactive;
        });
        if(parsed==current)break;
        current.swap(parsed);const uint64_t bytes=evaluate(current,model);
        if(bytes<best_size){best_size=bytes;best=current;}
    }
    return best;
}
// Remove grammar rules unreachable from the emitted token stream. The remaining
// IDs retain their relative order, so canonical Huffman tie-breaking is stable.
// Base symbols are always retained. Returns old ID -> new ID, or UINT32_MAX
// for removed rules. Mutates both pairs and tokens; all work is encoder-only.
template<class Pair>
static std::vector<uint32_t> prune_grammar(uint32_t base,std::vector<Pair>& pairs,std::vector<uint32_t>& tokens) {
    if(base>65536||pairs.size()>65536-base)throw std::runtime_error("Invalid prune dictionary");
    const uint32_t ns=base+pairs.size();std::vector<uint8_t> live(ns);
    for(uint32_t id:tokens){if(id>=ns)throw std::runtime_error("Invalid prune token");live[id]=1;}
    for(uint32_t id=ns;id-->base;)if(live[id]) {
        const auto& p=pairs[id-base];
        if(p.a>=id||p.b>=id)throw std::runtime_error("Invalid prune reference");
        live[p.a]=1;live[p.b]=1;
    }
    std::vector<uint32_t> remap(ns,UINT32_MAX);
    for(uint32_t id=0;id<base;++id)remap[id]=id;
    uint32_t next=base;
    for(uint32_t id=base;id<ns;++id)if(live[id])remap[id]=next++;
    size_t out=0;
    for(uint32_t id=base;id<ns;++id)if(live[id]) {
        auto pair=pairs[id-base];pair.a=remap[pair.a];pair.b=remap[pair.b];pairs[out++]=pair;
    }
    pairs.resize(out);
    for(uint32_t& id:tokens)id=remap[id];
    return remap;
}
// Compact per-symbol attributes (first/last byte, length, terminator flag, ...)
// using the result from prune_grammar. Extra attributes beyond remap are dropped.
template<class T>
static void prune_attributes(std::vector<T>& values,const std::vector<uint32_t>& remap) {
    if(values.size()<remap.size())throw std::runtime_error("Invalid prune attributes");
    size_t next=0;
    for(size_t old=0;old<remap.size();++old)if(remap[old]!=UINT32_MAX)values[next++]=values[old];
    values.resize(next);
}
#endif
#endif
