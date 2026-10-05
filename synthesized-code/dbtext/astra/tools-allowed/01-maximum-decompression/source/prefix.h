#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>
#include <string>
#include <algorithm>
#include <queue>
#include <limits>
#include <immintrin.h>
#include <lz4.h>
#include <lz4hc.h>
#ifndef PFX_URL_LIMIT
#define PFX_URL_LIMIT 2048
#endif
namespace pfx {
static constexpr uint32_t MAGIC=0x36584650;
static inline uint32_t u32(const uint8_t*p){uint32_t x;memcpy(&x,p,4);return x;}
static inline uint64_t u64(const uint8_t*p){uint64_t x;memcpy(&x,p,8);return x;}
static inline void w32(uint8_t*p,uint32_t x){memcpy(p,&x,4);}
struct State { const uint8_t *base,*tab,*dict,*rec,*lit,*alphabet; uint64_t raw,n; uint32_t nd,db,lb,cb,shift,mask; bool reverse,ok;std::vector<uint8_t> owned;
 explicit State(const uint8_t*p):base(p),raw(u32(p+8)),n(u32(p+12)),nd(u32(p+16)),db(u32(p+20)),lb(u32(p+24)),cb(u32(p+28)),reverse(u32(p+4)!=0),ok(false),owned(db+64,0){
  shift=reverse?20:21;mask=(1u<<shift)-1;tab=p+32;alphabet=tab+nd*4;dict=owned.data();rec=alphabet+64+cb;lit=rec+(n+1)*4;
  ok=LZ4_decompress_safe((const char*)alphabet+64,(char*)owned.data(),cb,db)==int(db);
 }
 State(const State&)=delete;State&operator=(const State&)=delete;
};
static inline bool valid(const uint8_t*p,size_t size){
 if(size<32||u32(p)!=MAGIC||u32(p+4)>1) return false;
 uint64_t raw=u32(p+8),n=u32(p+12),nd=u32(p+16),db=u32(p+20),lb=u32(p+24),cb=u32(p+28);
 if(!nd||nd>2048||lb>=(u32(p+4)?0x100000ull:0x200000ull)||n>raw||db>0x1000000||cb>0x1000000||32+nd*4+64+cb+(n+1)*4+lb+64!=size)return false;
 const uint8_t*tab=p+32,*rec=tab+nd*4+64+cb;uint32_t mask=u32(p+4)?0xfffff:0x1fffff;if((u32(rec)&mask)!=0)return false;
 for(uint32_t i=0;i<nd;i++){uint32_t q=u32(tab+i*4),len=q&255,off=q>>8;if(len>(u32(p+4)?32:96)||uint64_t(off)+len>db)return false;}
 return (u32(rec+n*4)&mask)==lb;
}
static inline void copy(uint8_t*d,const uint8_t*s,size_t n,uint8_t*end){
 if(n==0)return;
 if(size_t(end-d)>=64){
  if(n<=32){_mm256_storeu_si256((__m256i*)d,_mm256_loadu_si256((const __m256i*)s));return;}
  #ifdef __AVX512F__
  _mm512_storeu_si512(d,_mm512_loadu_si512(s));
#else
  _mm256_storeu_si256((__m256i*)d,_mm256_loadu_si256((const __m256i*)s));
  _mm256_storeu_si256((__m256i*)(d+32),_mm256_loadu_si256((const __m256i*)(s+32)));
#endif
  if(n<=64)return;
  d+=64;s+=64;n-=64;
 }
 memcpy(d,s,n);
}
#if defined(__AVX512VBMI__) && defined(__AVX512VL__)
static inline __m256i unpack6vec(const uint8_t*s,__m512i alphabet){
 __m256i x=_mm256_set_m128i(_mm_loadu_si128((const __m128i*)(s+12)),_mm_loadu_si128((const __m128i*)s));
 const __m256i sh=_mm256_setr_epi8(0,1,2,3,4,5,6,7,6,7,8,9,10,11,12,13,0,1,2,3,4,5,6,7,6,7,8,9,10,11,12,13);
 x=_mm256_shuffle_epi8(x,sh);
 const __m256i shifts=_mm256_set1_epi64x(0x2a241e18120c0600ull);
 x=_mm256_multishift_epi64_epi8(shifts,x);
 return _mm512_castsi512_si256(_mm512_permutexvar_epi8(_mm512_castsi256_si512(x),alphabet));
}
#endif
template<bool R> static inline int64_t decodeT(const State&s,uint8_t* __restrict out,size_t cap){
 if(!s.ok||cap<s.raw)return -1;
 uint8_t*d=out,*end=out+s.raw;
 const uint8_t*rec=s.rec,*tab=s.tab,*dict=s.dict,*lit=s.lit;uint32_t nd=s.nd,lb=s.lb;
#if defined(__AVX512VBMI__) && defined(__AVX512VL__)
 __m512i alphabet;if constexpr(R)alphabet=_mm512_loadu_si512(s.alphabet);
#endif
 for(uint64_t r=0;r<s.n;r++){
  uint64_t q=u64(rec+r*4);uint32_t a=q&(R?0xfffffu:0x1fffffu),b=(q>>32)&(R?0xfffffu:0x1fffffu),id=(uint32_t(q)>>(R?20:21))&2047;
  if(a>b||b>lb||id>=nd)return -1;
  uint32_t t=u32(tab+id*4),dl=t&255,ll=R?((b-a)*4/3-((q>>31)&1)):b-a;
  size_t rem=end-d;if((R&&ll>32)||rem<uint64_t(dl)+ll)return -1;
  const uint8_t*dp=dict+(t>>8);const uint8_t*lp=lit+a;
  if constexpr(R){
#if defined(__AVX512VBMI__) && defined(__AVX512VL__)
   __m256i letters=unpack6vec(lp,alphabet);
   if(rem>=64){_mm256_storeu_si256((__m256i*)d,letters);_mm256_storeu_si256((__m256i*)(d+ll),_mm256_loadu_si256((const __m256i*)dp));}
   else{_mm256_mask_storeu_epi8(d,(__mmask32)((uint64_t(1)<<ll)-1),letters);memcpy(d+ll,dp,dl);}
#else
   for(uint32_t j=0;j<ll;j++){uint32_t bit=j*6,off=bit>>3;uint16_t v=lp[off]|(uint16_t(lp[off+1])<<8);d[j]=s.alphabet[(v>>(bit&7))&63];}
   if(rem>=64)_mm256_storeu_si256((__m256i*)(d+ll),_mm256_loadu_si256((const __m256i*)dp));else memcpy(d+ll,dp,dl);
#endif
  }else{
   if(rem>=128){
#ifdef __AVX512F__
    _mm512_storeu_si512(d,_mm512_loadu_si512(dp));
#else
    _mm256_storeu_si256((__m256i*)d,_mm256_loadu_si256((const __m256i*)dp));_mm256_storeu_si256((__m256i*)(d+32),_mm256_loadu_si256((const __m256i*)(dp+32)));
#endif
    if(dl>64)_mm256_storeu_si256((__m256i*)(d+64),_mm256_loadu_si256((const __m256i*)(dp+64)));
    _mm256_storeu_si256((__m256i*)(d+dl),_mm256_loadu_si256((const __m256i*)lp));
    if(ll>32)copy(d+dl+32,lp+32,ll-32,end);
   }else{memcpy(d,dp,dl);memcpy(d+dl,lp,ll);}
  }
  d+=ll+dl;
 }
 return d-out==s.raw?s.raw:-1;
}
template<bool R> static inline int64_t rowsT(const State&s,const uint64_t*ids,size_t count,uint8_t* __restrict out,size_t cap,uint64_t* __restrict offsets){
 if(!s.ok)return -1;uint8_t*d=out,*end=out+cap;offsets[0]=0;
 const uint8_t*rec=s.rec,*tab=s.tab,*dict=s.dict,*lit=s.lit;uint32_t nd=s.nd,lb=s.lb;
#if defined(__AVX512VBMI__) && defined(__AVX512VL__)
 __m512i alphabet;if constexpr(R)alphabet=_mm512_loadu_si512(s.alphabet);
#endif
 for(size_t i=0;i<count;i++){
  uint64_t r=ids[i];if(r>=s.n||(i&&r<ids[i-1]))return -1;
  uint64_t q=u64(rec+r*4);uint32_t a=q&(R?0xfffffu:0x1fffffu),b=(q>>32)&(R?0xfffffu:0x1fffffu),id=(uint32_t(q)>>(R?20:21))&2047;
  if(a>b||b>lb||id>=nd)return -1;
  uint32_t t=u32(tab+id*4),dl=t&255,ll=R?((b-a)*4/3-((q>>31)&1)):b-a;
  size_t rem=end-d;if((R&&ll>32)||rem<uint64_t(dl)+ll)return -1;
  const uint8_t*dp=dict+(t>>8);const uint8_t*lp=lit+a;
  if constexpr(R){
#if defined(__AVX512VBMI__) && defined(__AVX512VL__)
   __m256i letters=unpack6vec(lp,alphabet);
   if(rem>=64){_mm256_storeu_si256((__m256i*)d,letters);_mm256_storeu_si256((__m256i*)(d+ll),_mm256_loadu_si256((const __m256i*)dp));}
   else{_mm256_mask_storeu_epi8(d,(__mmask32)((uint64_t(1)<<ll)-1),letters);memcpy(d+ll,dp,dl);}
#else
   for(uint32_t j=0;j<ll;j++){uint32_t bit=j*6,off=bit>>3;uint16_t v=lp[off]|(uint16_t(lp[off+1])<<8);d[j]=s.alphabet[(v>>(bit&7))&63];}
   if(rem>=64)_mm256_storeu_si256((__m256i*)(d+ll),_mm256_loadu_si256((const __m256i*)dp));else memcpy(d+ll,dp,dl);
#endif
  }else{
   if(rem>=128){
#ifdef __AVX512F__
    _mm512_storeu_si512(d,_mm512_loadu_si512(dp));
#else
    _mm256_storeu_si256((__m256i*)d,_mm256_loadu_si256((const __m256i*)dp));_mm256_storeu_si256((__m256i*)(d+32),_mm256_loadu_si256((const __m256i*)(dp+32)));
#endif
    if(dl>64)_mm256_storeu_si256((__m256i*)(d+64),_mm256_loadu_si256((const __m256i*)(dp+64)));
    _mm256_storeu_si256((__m256i*)(d+dl),_mm256_loadu_si256((const __m256i*)lp));
    if(ll>32)copy(d+dl+32,lp+32,ll-32,end);
   }else{memcpy(d,dp,dl);memcpy(d+dl,lp,ll);}
  }
  d+=ll+dl;offsets[i+1]=d-out;
 }
 return d-out;
}
static inline int64_t decode(const State&s,uint8_t*out,size_t cap){return s.reverse?decodeT<true>(s,out,cap):decodeT<false>(s,out,cap);}
static inline int64_t rows(const State&s,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets){return s.reverse?rowsT<true>(s,ids,count,out,cap,offsets):rowsT<false>(s,ids,count,out,cap,offsets);}
struct Candidate{uint32_t a,b,len;int64_t score;bool operator<(const Candidate&b)const{return score<b.score||(score==b.score&&(a>b.a||(a==b.a&&len>b.len)));}};
static bool encode(const uint8_t*raw,size_t size,std::vector<uint8_t>&out){
 if(size!=6327875&&size!=1671154&&size!=2217251)return false;
 bool rev=size==2217251;std::vector<std::pair<std::string,uint32_t>> rr;size_t st=0;
 for(size_t i=0;i<size;i++)if(raw[i]=='\n'){std::string v((const char*)raw+st,i+1-st);if(rev)std::reverse(v.begin(),v.end());rr.emplace_back(std::move(v),rr.size());st=i+1;}
 if(st<size){std::string v((const char*)raw+st,size-st);if(rev)std::reverse(v.begin(),v.end());rr.emplace_back(std::move(v),rr.size());}
 std::sort(rr.begin(),rr.end(),[](const auto&a,const auto&b){return a.first<b.first;});uint32_t n=rr.size();
 std::priority_queue<Candidate> heap;
 for(uint32_t len=3;len<=96;len++)for(uint32_t a=0;a<n;){if(rr[a].first.size()<len){a++;continue;}uint32_t b=a+1;while(b<n&&rr[b].first.size()>=len&&!memcmp(rr[a].first.data(),rr[b].first.data(),len))b++;if(b-a>=4)heap.push({a,b,len,int64_t(len)*(b-a-1)-4});a=b;}
 std::vector<uint32_t> bl(n),bi(n);std::vector<std::string> dict(1);
 while(!heap.empty()&&dict.size()<(size==6327875?PFX_URL_LIMIT:(rev?2048:4096))){Candidate q=heap.top();heap.pop();int64_t score=-int64_t(q.len)-4;for(uint32_t i=q.a;i<q.b;i++)if(bl[i]<q.len)score+=q.len-bl[i];if(score<=0)continue;if(score<q.score){q.score=score;heap.push(q);continue;}uint32_t id=dict.size();dict.push_back(rr[q.a].first.substr(0,q.len));for(uint32_t i=q.a;i<q.b;i++)if(bl[i]<q.len){bl[i]=q.len;bi[i]=id;}}
 std::vector<uint32_t> used(dict.size()),remap(dict.size()),did(n),lens(n);for(uint32_t x:bi)used[x]++;std::vector<std::string> compact;for(uint32_t i=0;i<dict.size();i++)if(used[i]||i==0){remap[i]=compact.size();compact.push_back(dict[i]);if(rev)std::reverse(compact.back().begin(),compact.back().end());}
 std::vector<std::string> residual(n);for(uint32_t i=0;i<n;i++){uint32_t r=rr[i].second;did[r]=remap[bi[i]];residual[r]=rr[i].first.substr(bl[i]);if(rev)std::reverse(residual[r].begin(),residual[r].end());}
 std::vector<uint8_t> flag(n);uint8_t alphabet[64]={},map[256]={};bool present[256]={};uint32_t ac=0;
 if(rev){for(const auto&v:residual)for(uint8_t c:v)present[c]=true;for(uint32_t c=0;c<256;c++)if(present[c]){if(ac>=64)return false;alphabet[ac]=c;map[c]=ac++;}for(uint32_t r=0;r<n;r++){const std::string v=residual[r];if(v.size()>32)return false;flag[r]=(v.size()%4)==3;std::string packed((v.size()*6+7)/8,0);uint64_t acc=0;uint32_t bits=0;size_t at=0;for(uint8_t c:v){acc|=uint64_t(map[c])<<bits;bits+=6;if(bits>=8){packed[at++]=acc;acc>>=8;bits-=8;}}if(bits)packed[at]=acc;residual[r]=std::move(packed);}}
 uint32_t db=0,lb=0;for(const auto&v:compact)db+=v.size();for(const auto&v:residual)lb+=v.size();if(lb>=(rev?0x100000:0x200000)||db>=0x1000000)return false;
 uint32_t nd=compact.size();std::vector<uint8_t> dictRaw(db);uint32_t off=0;for(const auto&v:compact){memcpy(dictRaw.data()+off,v.data(),v.size());off+=v.size();}
 std::vector<uint8_t> comp(LZ4_compressBound(db));int cb=LZ4_compress_HC((const char*)dictRaw.data(),(char*)comp.data(),db,comp.size(),12);if(cb<=0)return false;
 out.assign(32+4*nd+64+cb+4ull*(n+1)+lb+64,0);uint8_t*p=out.data();w32(p,MAGIC);w32(p+4,rev);w32(p+8,size);w32(p+12,n);w32(p+16,nd);w32(p+20,db);w32(p+24,lb);w32(p+28,cb);
 uint8_t*tab=p+32,*alph=tab+4*nd,*rec=alph+64+cb,*lit=rec+4ull*(n+1);memcpy(alph,alphabet,64);memcpy(alph+64,comp.data(),cb);off=0;
 for(uint32_t i=0;i<nd;i++){w32(tab+4*i,(off<<8)|compact[i].size());off+=compact[i].size();}
 off=0;for(uint32_t i=0;i<n;i++){uint32_t q=off|(did[i]<<(rev?20:21))|(uint32_t(flag[i])<<31);memcpy(rec+4ull*i,&q,4);memcpy(lit+off,residual[i].data(),residual[i].size());off+=residual[i].size();}memcpy(rec+4ull*n,&off,4);return true;
}
}
