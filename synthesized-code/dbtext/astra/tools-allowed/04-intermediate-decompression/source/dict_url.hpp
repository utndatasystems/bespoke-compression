#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <immintrin.h>
// Numeric-template engine. All templates are serialized in the charged archive.
// Row payload: LE uint16 template ID (1-based), then packed decimal nibbles,
// independently aligned per 64-byte template segment. Empty encoded vector means
// caller must use a fallback codec for that row.
namespace dicturl {
struct alignas(64) Template {uint8_t fixed[128]; uint64_t mask[2];uint16_t len;uint8_t packed0;uint8_t totalpacked;};
struct State {std::vector<Template> t;};
inline void put16(std::vector<uint8_t>&o,uint16_t v){o.push_back(v);o.push_back(v>>8);}
inline void put64(std::vector<uint8_t>&o,uint64_t v){for(int i=0;i<8;i++)o.push_back(v>>(8*i));}
inline uint16_t get16(const uint8_t*p){uint16_t v;memcpy(&v,p,2);return v;}
inline uint64_t get64(const uint8_t*p){uint64_t v;memcpy(&v,p,8);return v;}
struct Group {std::string base;uint64_t mask[2]={};uint32_t count=0;uint16_t id=0;};
inline void fit(const std::vector<const uint8_t*>&rows,const std::vector<size_t>&lens,
 std::vector<uint8_t>&table,std::vector<std::vector<uint8_t>>&codes){
 std::unordered_map<std::string,uint32_t> map;std::vector<Group>groups;std::vector<uint32_t> gid(rows.size(),~0u);
 for(size_t i=0;i<rows.size();i++){
  size_t n=lens[i];if(n>128)continue;std::string key((const char*)rows[i],n);bool has=false;
  for(auto &c:key)if(c>='0'&&c<='9'){c='0';has=true;}if(!has)continue;
  auto [it,ins]=map.emplace(key,groups.size());if(ins){Group g;g.base.assign((const char*)rows[i],n);groups.push_back(std::move(g));}
  auto &g=groups[it->second];g.count++;for(size_t j=0;j<n;j++)if(g.base[j]!=char(rows[i][j]))g.mask[j/64]|=1ull<<(j%64);gid[i]=it->second;
 }
 // A template must save data even when independently charged without fallback
 // compression benefit. At least four repeated shapes amortize metadata.
 uint16_t nt=0;for(auto&g:groups){int vars=__builtin_popcountll(g.mask[0])+__builtin_popcountll(g.mask[1]);if(g.count>=4 && __builtin_popcountll(g.mask[0])<=32 && __builtin_popcountll(g.mask[1])<=32 && g.base.size()>size_t(2+(vars+1)/2) && nt<65535)g.id=++nt;}
 table.clear();put16(table,nt);for(auto&g:groups)if(g.id){put16(table,g.base.size());put64(table,g.mask[0]);put64(table,g.mask[1]);table.insert(table.end(),g.base.begin(),g.base.end());}
 codes.clear();codes.resize(rows.size());for(size_t i=0;i<rows.size();i++)if(gid[i]!=~0u){auto&g=groups[gid[i]];if(!g.id)continue;auto &o=codes[i];put16(o,g.id);for(int seg=0;seg<2;seg++){uint64_t mask=g.mask[seg];bool low=true;while(mask){int b=__builtin_ctzll(mask);uint8_t v=rows[i][seg*64+b]-'0';if(low)o.push_back(v);else o.back()|=v<<4;low=!low;mask&=mask-1;}}}
}
inline size_t open(State&s,const uint8_t*p,size_t size){if(size<2)return 0;const uint8_t*begin=p;uint16_t n=get16(p);p+=2;s.t.resize(n);for(int i=0;i<n;i++){if(size_t(p-begin)+18>size)return 0;auto&t=s.t[i];memset(&t,0,sizeof(t));t.len=get16(p);t.mask[0]=get64(p+2);t.mask[1]=get64(p+10);p+=18;if(t.len>128||size_t(p-begin)+t.len>size)return 0;memcpy(t.fixed,p,t.len);p+=t.len;t.packed0=(__builtin_popcountll(t.mask[0])+1)/2;t.totalpacked=t.packed0+(__builtin_popcountll(t.mask[1])+1)/2;}return p-begin;}
// source points at >=32 accessible archive bytes; callers may pad archive tail.
#ifdef __AVX512VBMI2__
inline __m512i digits(const uint8_t*p){__m128i x=_mm_loadu_si128((const __m128i*)p);__m256i v=_mm256_cvtepu8_epi16(x);__m256i lo=_mm256_and_si256(v,_mm256_set1_epi16(15));__m256i hi=_mm256_slli_epi16(_mm256_and_si256(v,_mm256_set1_epi16(240)),4);return _mm512_zextsi256_si512(_mm256_add_epi8(_mm256_or_si256(lo,hi),_mm256_set1_epi8('0')));}
inline size_t decode(const State&s,const uint8_t*p,uint8_t*out){const auto&t=s.t[get16(p)-1];p+=2;__m512i a=_mm512_load_si512((const void*)t.fixed);a=_mm512_mask_expand_epi8(a,t.mask[0],digits(p));if(t.len>=64)_mm512_storeu_si512((void*)out,a);else _mm512_mask_storeu_epi8(out,(1ull<<t.len)-1,a);if(t.len>64){__m512i b=_mm512_load_si512((const void*)(t.fixed+64));b=_mm512_mask_expand_epi8(b,t.mask[1],digits(p+t.packed0));uint64_t mask=t.len==128?~0ull:(1ull<<(t.len-64))-1;_mm512_mask_storeu_epi8(out+64,mask,b);}return t.len;}
#else
inline size_t decode(const State&s,const uint8_t*p,uint8_t*out){const auto&t=s.t[get16(p)-1];p+=2;memcpy(out,t.fixed,t.len);for(int j=0;j<2;j++){uint64_t m=t.mask[j];int n=0;while(m){int b=__builtin_ctzll(m);out[j*64+b]='0'+((p[n/2]>>(4*(n%2)))&15);n++;m&=m-1;}p+=(n+1)/2;}return t.len;}
#endif
}
