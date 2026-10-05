#ifndef DBTEXT_CONTEXT_H
#define DBTEXT_CONTEXT_H
#include "huffman.h"
#include <memory>
#include <array>
// Two-stage canonical Huffman: next token's first byte conditioned on previous
// token's last byte, followed by a token code within that first-byte category.
// Category 256 is reserved for sorted-front-coding prefix symbols.
static inline uint32_t cc_r32(const uint8_t*p){uint32_t v;memcpy(&v,p,4);return v;}
static inline void cc_w32(std::vector<uint8_t>&v,uint32_t x){size_t n=v.size();v.resize(n+4);memcpy(v.data()+n,&x,4);}
struct CHTable {
 static constexpr unsigned MAX_BITS=24;
 std::vector<uint32_t> lookup;
 std::vector<uint16_t> ordered;
 uint32_t first[25]={},count[25]={},base[25]={};
 uint32_t single=UINT32_MAX;unsigned look=0,max_bits=0;
 void build(const uint8_t*lengths,size_t n,bool implicit_single=true){
  if(n>65536)throw std::runtime_error("Context alphabet too large");lookup.clear();ordered.clear();single=UINT32_MAX;look=max_bits=0;std::fill(first,first+25,0);std::fill(count,count+25,0);std::fill(base,base+25,0);
  uint32_t total=0,last=0;for(size_t i=0;i<n;i++){unsigned l=lengths[i];if(l>24)throw std::runtime_error("Context code length");if(l){count[l]++;total++;last=i;max_bits=std::max(max_bits,l);}}
  if(!total)return;if(total==1&&implicit_single){if(lengths[last]!=1)throw std::runtime_error("Context singleton length");single=last;return;}
  uint32_t used[25]={};total=0;for(unsigned l=1;l<=24;l++){first[l]=(first[l-1]+count[l-1])<<1;base[l]=total;total+=count[l];if(first[l]+count[l]>(1u<<l))throw std::runtime_error("Context Huffman tree");}
  ordered.resize(total);look=std::min(n>16384?16u:n>3500?14u:n>1536?13u:n>256?12u:10u,max_bits);lookup.assign(1u<<look,0);
  for(size_t i=0;i<n;i++){unsigned l=lengths[i];if(!l)continue;uint32_t rank=used[l]++;ordered[base[l]+rank]=i;if(l<=look){uint32_t begin=(first[l]+rank)<<(look-l),entry=(uint32_t(i)<<5)|l;std::fill(lookup.begin()+begin,lookup.begin()+begin+(1u<<(look-l)),entry);}}
 }
 inline uint32_t decode(const uint8_t*data,uint64_t&bit)const{
  if(single!=UINT32_MAX)return single;if(lookup.empty())return UINT32_MAX;uint64_t word;memcpy(&word,data+(bit>>3),8);word=__builtin_bswap64(word);word<<=bit&7;uint32_t e=lookup[word>>(64-look)];if(e){bit+=e&31;return e>>5;}for(unsigned l=look+1;l<=max_bits;l++){uint32_t rank=uint32_t(word>>(64-l))-first[l];if(rank<count[l]){bit+=l;return ordered[base[l]+rank];}}return UINT32_MAX;
 }
};
#ifdef ENCODER
struct CCPacked {std::vector<uint8_t> lens,stream;uint32_t bits;};
static CCPacked cc_pack_lengths(const std::vector<uint8_t>&model){
 std::vector<uint64_t>mf(64);for(size_t i=0;i<model.size();){if(model[i]){mf[model[i]]++;i++;}else{size_t j=i+1;while(j<model.size()&&!model[j])j++;unsigned lg=63-__builtin_clzll(uint64_t(j-i));mf[25+lg]++;i=j;}}
 HEnc mh(mf);HBitWriter w;for(size_t i=0;i<model.size();){if(model[i]){w.put(mh,model[i]);i++;}else{size_t j=i+1;while(j<model.size()&&!model[j])j++;uint64_t run=j-i;unsigned lg=63-__builtin_clzll(run);w.put(mh,25+lg);if(lg)w.put(uint32_t(run-(1ull<<lg)),lg);i=j;}}
 if(w.bit_size>0x7fffffffu)throw std::runtime_error("Context metadata size");uint32_t nbits=w.bit_size;w.finish();return {std::move(mh.lengths),std::move(w.bytes),nbits};
}
struct CEnc {
 std::vector<uint8_t> lengths,blob;
 std::vector<uint16_t> first,last;
 std::vector<uint32_t> codes;
 std::array<HEnc,257> firstcodes;
 std::array<std::unique_ptr<HEnc>,257>directcodes;
 std::array<uint32_t,257> firstUsed{},tokenUsed{},directUsed{};
 unsigned mode=0;uint32_t fallbackUsed=0;
 CEnc()=default;
 CEnc(const std::vector<uint32_t>&tokens,const std::vector<uint16_t>&f,const std::vector<uint16_t>&l,unsigned kind=0){build(tokens,f,l,kind);}
 void build(const std::vector<uint32_t>&sequence,const std::vector<uint16_t>&f,const std::vector<uint16_t>&l,unsigned kind=0){
  if(f.empty()||f.size()>65536||f.size()!=l.size()||kind>1)throw std::runtime_error("Context symbol maps");mode=kind;first=f;last=l;size_t ns=f.size();lengths.assign(ns,0);codes.assign(ns,0);firstUsed.fill(0);tokenUsed.fill(0);directUsed.fill(0);fallbackUsed=0;for(auto&p:directcodes)p.reset();
  std::array<std::vector<uint16_t>,257>groups;for(size_t s=0;s<ns;s++){if(f[s]>256||l[s]>256)throw std::runtime_error("Context category");groups[f[s]].push_back(s);}
  std::vector<uint64_t>freq(ns);for(uint32_t s:sequence){if(s>=ns)throw std::runtime_error("Context token");freq[s]++;}
  std::vector<uint8_t>model,bitmap(33);
  if(!mode){
   std::array<std::vector<uint64_t>,257>fc;for(auto&v:fc)v.resize(257);uint16_t prev=10;for(uint32_t s:sequence){fc[prev][f[s]]++;prev=l[s];}
   for(unsigned c=0;c<257;c++){std::vector<uint64_t>gf;gf.reserve(groups[c].size());for(uint16_t s:groups[c])gf.push_back(freq[s]);HEnc h(gf);for(size_t i=0;i<gf.size();i++){uint16_t s=groups[c][i];lengths[s]=h.lengths[i];codes[s]=h.codes[i];tokenUsed[c]+=gf[i]!=0;}firstcodes[c].build(fc[c]);for(auto z:fc[c])firstUsed[c]+=z!=0;}
   model.reserve(257*257);for(auto&h:firstcodes)model.insert(model.end(),h.lengths.begin(),h.lengths.end());
  }else{
   std::array<std::vector<uint64_t>,257>ct;uint16_t prev=10;for(uint32_t s:sequence){if(ct[prev].empty())ct[prev].resize(ns);ct[prev][s]++;prev=l[s];}HEnc global(freq);std::vector<uint64_t>fallback=freq;
   for(unsigned c=0;c<257;c++)if(!ct[c].empty()){
    auto h=std::make_unique<HEnc>(ct[c]);uint64_t oldbits=0,newbits=0;uint32_t used=0;for(size_t i=0;i<ns;i++)if(ct[c][i]){oldbits+=ct[c][i]*global.lengths[i];newbits+=ct[c][i]*h->lengths[i];used++;}if(used==1)newbits=0;
    auto packed=cc_pack_lengths(h->lengths);uint64_t cost=uint64_t(packed.stream.size()+packed.lens.size()+8)*8+32;
    if(oldbits>newbits+cost){directUsed[c]=used;directcodes[c]=std::move(h);bitmap[c>>3]|=1u<<(c&7);for(size_t i=0;i<ns;i++)fallback[i]-=ct[c][i];}
   }
   HEnc h(fallback);lengths=h.lengths;codes=h.codes;for(auto z:fallback)fallbackUsed+=z!=0;
   for(unsigned c=0;c<257;c++)if(directcodes[c])model.insert(model.end(),directcodes[c]->lengths.begin(),directcodes[c]->lengths.end());
  }
  auto packed=cc_pack_lengths(model);blob.clear();cc_w32(blob,8+(mode?33:0)+64+packed.stream.size());cc_w32(blob,packed.bits|(mode?0x80000000u:0));if(mode)blob.insert(blob.end(),bitmap.begin(),bitmap.end());blob.insert(blob.end(),packed.lens.begin(),packed.lens.end());blob.insert(blob.end(),packed.stream.begin(),packed.stream.end());
 }
 inline uint32_t cost(uint32_t symbol,uint16_t prev)const{
  if(symbol>=lengths.size()||prev>256)return UINT32_MAX;
  if(mode){if(directcodes[prev]){uint32_t l=directcodes[prev]->lengths[symbol];return l?(directUsed[prev]>1?l:0):UINT32_MAX;}uint32_t l=lengths[symbol];return l?(fallbackUsed>1?l:0):UINT32_MAX;}
  uint16_t f=first[symbol];uint32_t a=firstcodes[prev].lengths[f],b=lengths[symbol];if(!a||!b)return UINT32_MAX;return (firstUsed[prev]>1?a:0)+(tokenUsed[f]>1?b:0);
 }
 uint64_t estimate_bits(const std::vector<uint32_t>&sequence,uint16_t initial=10)const{uint64_t bits=0;for(uint32_t s:sequence){uint32_t c=cost(s,initial);if(c==UINT32_MAX)return UINT64_MAX;bits+=c;initial=last[s];}return bits;}
 inline void put(HBitWriter&w,uint32_t symbol,uint16_t&prev)const{
  if(cost(symbol,prev)==UINT32_MAX)throw std::runtime_error("Unavailable context symbol");
  if(mode){if(directcodes[prev]){if(directUsed[prev]>1)w.put(*directcodes[prev],symbol);}else if(fallbackUsed>1)w.put(codes[symbol],lengths[symbol]);}
  else{uint16_t f=first[symbol];if(firstUsed[prev]>1)w.put(firstcodes[prev],f);if(tokenUsed[f]>1)w.put(codes[symbol],lengths[symbol]);}prev=last[symbol];
 }
};
#endif
static std::vector<uint8_t>cc_unpack_lengths(const uint8_t*lens,const uint8_t*data,uint32_t nbits,size_t expected){
 CHTable meta;meta.build(lens,64,false);uint64_t bit=0;std::vector<uint8_t>model;model.reserve(expected);
 while(model.size()<expected){if(bit>=nbits)throw std::runtime_error("Context metadata truncated");uint32_t x=meta.decode(data,bit);if(bit>nbits||!x||x>=64)throw std::runtime_error("Context metadata symbol");if(x<=24)model.push_back(x);else{unsigned lg=x-25;if(lg>24||bit+lg>nbits)throw std::runtime_error("Context metadata run");uint64_t word;memcpy(&word,data+(bit>>3),8);word=__builtin_bswap64(word);word<<=bit&7;uint64_t run=(1ull<<lg)+(lg?(word>>(64-lg)):0);bit+=lg;if(run>expected-model.size())throw std::runtime_error("Context metadata run length");model.resize(model.size()+run);}}
 if(bit!=nbits)throw std::runtime_error("Context metadata trailing bits");return model;
}
struct CDec {
 static constexpr unsigned FUSED_BITS=14;
 std::array<std::vector<uint32_t>,257> fused;
 struct TokenGroup {CHTable h;std::vector<uint16_t>symbols;};
 std::array<std::unique_ptr<CHTable>,257>firstmodels;
 std::array<std::unique_ptr<TokenGroup>,257>tokens;
 std::vector<uint16_t>last;CHTable fallback;unsigned mode=0;
 size_t build(const uint8_t*blob,size_t size,const std::vector<uint16_t>&first,const std::vector<uint16_t>&lst,const std::vector<uint8_t>&lengths){
  if(size<80||first.empty()||first.size()>65536||first.size()!=lst.size()||first.size()!=lengths.size())throw std::runtime_error("Context model header");uint32_t bytes=cc_r32(blob),rawbits=cc_r32(blob+4);mode=rawbits>>31;uint32_t nbits=rawbits&0x7fffffffu;size_t head=72+(mode?33:0);if(bytes<head+8||bytes>size||uint64_t(head)+(uint64_t(nbits)+7)/8+8!=bytes)throw std::runtime_error("Context metadata bounds");
  for(size_t s=0;s<first.size();s++)if(first[s]>256||lst[s]>256||lengths[s]>24)throw std::runtime_error("Context symbol category");
  size_t expected=257*257;const uint8_t*bitmap=blob+8;if(mode){if(bitmap[32]&254)throw std::runtime_error("Context bitmap");unsigned selected=0;for(unsigned c=0;c<257;c++)selected+=(bitmap[c>>3]>>(c&7))&1;expected=first.size()*selected;}
  auto model=cc_unpack_lengths(blob+head-64,blob+head,nbits,expected);for(auto&p:firstmodels)p.reset();for(auto&p:tokens)p.reset();last=lst;
  if(mode){fallback.build(lengths.data(),lengths.size());size_t pos=0;for(unsigned c=0;c<257;c++)if((bitmap[c>>3]>>(c&7))&1){firstmodels[c]=std::make_unique<CHTable>();firstmodels[c]->build(model.data()+pos,first.size());pos+=first.size();}return bytes;}
  std::array<std::vector<uint16_t>,257>groups;for(size_t s=0;s<first.size();s++)groups[first[s]].push_back(s);
  for(unsigned c=0;c<257;c++){
   bool present=false;for(unsigned j=0;j<257;j++)present|=model[c*257+j]!=0;if(present){firstmodels[c]=std::make_unique<CHTable>();firstmodels[c]->build(model.data()+c*257,257);}
   std::vector<uint8_t>ls;ls.reserve(groups[c].size());bool used=false;for(uint16_t s:groups[c]){ls.push_back(lengths[s]);used|=lengths[s]!=0;}if(used){tokens[c]=std::make_unique<TokenGroup>();tokens[c]->symbols=std::move(groups[c]);tokens[c]->h.build(ls.data(),ls.size());}
  }
  for(unsigned c=0;c<257;++c)if(firstmodels[c]) {
   auto& dest=fused[c];dest.assign(1u<<FUSED_BITS,0);
   const CHTable& a=*firstmodels[c];
   auto fill_group=[&](uint32_t f,unsigned fl,uint32_t fc) {
    if(f>256||!tokens[f])return;
    const TokenGroup& t=*tokens[f];const CHTable& b=t.h;
    auto fill_one=[&](uint32_t local,unsigned tl,uint32_t tc) {
     const unsigned total=fl+tl;if(total>FUSED_BITS)return;
     uint16_t symbol=t.symbols[local];
     const uint32_t begin=uint32_t((uint64_t(fc)<<tl)|tc)<<(FUSED_BITS-total);
     const uint32_t entry=0x80000000u|uint32_t(symbol)|(total<<16)|(uint32_t(last[symbol])<<21);
     std::fill(dest.begin()+begin,dest.begin()+begin+(1u<<(FUSED_BITS-total)),entry);
    };
    if(b.single!=UINT32_MAX)fill_one(b.single,0,0);
    else for(unsigned l=1;l<=b.max_bits;++l)for(uint32_t rank=0;rank<b.count[l];++rank)
       fill_one(b.ordered[b.base[l]+rank],l,b.first[l]+rank);
   };
   if(a.single!=UINT32_MAX)fill_group(a.single,0,0);
   else for(unsigned l=1;l<=a.max_bits;++l)for(uint32_t rank=0;rank<a.count[l];++rank)
      fill_group(a.ordered[a.base[l]+rank],l,a.first[l]+rank);
  }
  return bytes;
 }
 inline uint32_t decode(const uint8_t*data,uint64_t&bit,uint16_t&prev,uint64_t limit=UINT64_MAX)const{
  if(prev>256||bit>limit)return UINT32_MAX;if(mode){uint32_t s=firstmodels[prev]?firstmodels[prev]->decode(data,bit):fallback.decode(data,bit);if(s>=last.size())return UINT32_MAX;prev=last[s];return s;}
  if(!fused[prev].empty()) {
   uint64_t word;memcpy(&word,data+(bit>>3),8);word=__builtin_bswap64(word);word<<=bit&7;
   const uint32_t entry=fused[prev][word>>(64-FUSED_BITS)];
   if(entry) {bit+=(entry>>16)&31;prev=(entry>>21)&511;return uint16_t(entry);}
  }
  if(!firstmodels[prev])return UINT32_MAX;uint32_t f=firstmodels[prev]->decode(data,bit);if(bit>limit||f>256||!tokens[f])return UINT32_MAX;const TokenGroup*t=tokens[f].get();uint32_t local=t->h.decode(data,bit);if(local>=t->symbols.size())return UINT32_MAX;uint16_t s=t->symbols[local];prev=last[s];return s;
 }
};
#endif
