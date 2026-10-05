#pragma once
// Encoder-only LZ parsing: multi-prefix hash chains and bounded dynamic programming.
// DP learns stream code costs from a 2 MB sample and carries four recent offsets.
#ifndef LZ_PARSE_NICE
#define LZ_PARSE_NICE 128
#endif
#ifndef LZ_PARSE_CHAIN
#define LZ_PARSE_CHAIN 32
#endif
#ifndef LZ_OFFSET_CONTEXT
#define LZ_OFFSET_CONTEXT 0
#endif
#ifndef LZ_LITERAL_CONTEXT
#define LZ_LITERAL_CONTEXT 0
#endif
#ifndef LZ_PARSE_OPTIMAL
#define LZ_PARSE_OPTIMAL 0
#endif
#ifndef LZ_PARSE_REFINE
#define LZ_PARSE_REFINE 0
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>

struct LZSeq { uint32_t literals, length, distance; };
namespace lzp_detail {
inline int envint(const char* name, int fallback) { const char* s=getenv(name); return s?atoi(s):fallback; }
inline uint32_t rd32(const uint8_t* p) { uint32_t x; memcpy(&x,p,4); return x; }
inline uint64_t rd64(const uint8_t* p) { uint64_t x; memcpy(&x,p,8); return x; }
inline uint32_t h8(const uint8_t* p) { return (rd64(p)*0x9e3779b97f4a7c15ull)>>40; }
inline uint32_t h16(const uint8_t* p) { return ((rd64(p)^rd64(p+8)*0x9e3779b97f4a7c15ull)*0xc6a4a7935bd1e995ull)>>42; }
inline uint32_t h32(const uint8_t* p) { return ((rd64(p)^rd64(p+8)*0x9e3779b97f4a7c15ull^rd64(p+16)*0xc6a4a7935bd1e995ull^rd64(p+24))*0x9e3779b97f4a7c15ull)>>42; }
inline uint32_t h64(const uint8_t* p) { return ((rd64(p)^rd64(p+16)*0x9e3779b97f4a7c15ull^rd64(p+32)*0xc6a4a7935bd1e995ull^rd64(p+48))*0x9e3779b97f4a7c15ull)>>42; }
inline uint32_t h4(const uint8_t* p) { return (rd32(p)*0x9e3779b1u)>>9; }
inline uint32_t h3(const uint8_t* p) { return ((rd32(p)&0xffffffu)*0x1e35a7bdu)>>12; }
inline uint32_t matchlen(const uint8_t* a,const uint8_t* b,uint32_t limit) {
 uint32_t k=0; while(k+8<=limit) { uint64_t x,y; memcpy(&x,a+k,8);memcpy(&y,b+k,8); if(x!=y) return k+(__builtin_ctzll(x^y)>>3); k+=8; }
 while(k<limit && a[k]==b[k]) ++k; return k;
}
inline unsigned lg2(uint32_t x) { return 31-__builtin_clz(x); }
inline float len_cost(uint32_t x) { return x<16 ? 3.5f : 3.8f+std::max(0,int(lg2(x))-2); }
struct Match { uint32_t len=0,dist=0; float gain=-1e20f; };
struct Finder {
 const uint8_t* raw; uint32_t n, inserted=0; int depth,nice,window,minmatch,nrep; float litcost;
 std::vector<uint32_t> head,head3,chain,head8,chain8,head16,head32,head64,chain32;
 std::array<uint32_t,8> reps{};
 std::vector<Match>* collecting=nullptr;
 Finder(const uint8_t* r,uint32_t z):raw(r),n(z),depth(envint("LZ_CHAIN",128)),nice(envint("LZ_NICE",256)),window(envint("LZ_WINDOW",134217728)),minmatch(envint("LZ_MINMATCH",3)),nrep(envint("LZ_NREP",4)),litcost(envint("LZ_LITCOST",52)/10.0f),head(1<<23,0xffffffffu),head3(1<<20,0xffffffffu),chain(z,0xffffffffu),head8(1<<24,0xffffffffu),chain8(z,0xffffffffu),head16(1<<22,0xffffffffu),head32(1<<22,0xffffffffu),head64(1<<22,0xffffffffu),chain32(z,0xffffffffu) {}
 void insert(uint32_t p) { if(p+4>n)return;uint32_t h=h4(raw+p);chain[p]=head[h];head[h]=p;head3[h3(raw+p)]=p; if(p+64<=n)head64[h64(raw+p)]=p; if(p+32<=n){uint32_t h=h32(raw+p);chain32[p]=head32[h];head32[h]=p;} if(p+16<=n)head16[h16(raw+p)]=p; if(p+8<=n){uint32_t h=h8(raw+p);chain8[p]=head8[h];head8[h]=p;} }
 void until(uint32_t p) { while(inserted<p){insert(inserted);++inserted;} }
 float distcost(uint32_t d) const { for(int j=0;j<nrep;j++)if(reps[j]==d)return 1.0f+j*0.6f;return 4.5f+lg2(d); }
 float score(uint32_t len,uint32_t d) const { return len*litcost-distcost(d)-len_cost(len-3)-3.0f; }
 void consider(Match& best,uint32_t len,uint32_t d) {if(len<uint32_t(minmatch))return; if(collecting){float dc=distcost(d);bool dominated=false;for(auto e:*collecting)if(e.len>=len&&distcost(e.dist)<=dc){dominated=true;break;}if(!dominated){for(size_t i=0;i<collecting->size();)if((*collecting)[i].len<=len&&distcost((*collecting)[i].dist)>=dc){(*collecting)[i]=collecting->back();collecting->pop_back();}else ++i;collecting->push_back({len,d,0});}}float gain=score(len,d);if(gain>best.gain){best={len,d,gain};}}
 Match find(uint32_t p) {
  until(p); Match best; if(p+4>n){until(p+1);return best;} uint32_t lim=n-p;
  for(int j=0;j<nrep;j++){uint32_t d=reps[j];if(d&&d<=p&&raw[p]==raw[p-d]&&raw[p+1]==raw[p-d+1])consider(best,matchlen(raw+p,raw+p-d,lim),d);}
  uint32_t q3=head3[h3(raw+p)]; if(q3<p && p-q3<uint32_t(window))consider(best,matchlen(raw+p,raw+q3,lim),p-q3);
  if(p+64<=n){uint32_t q=head64[h64(raw+p)];if(q<p && p-q<uint32_t(window) && rd64(raw+p)==rd64(raw+q))consider(best,matchlen(raw+p,raw+q,lim),p-q);}
  if(p+32<=n){uint32_t q=head32[h32(raw+p)];int tries=std::min(depth,64);while(q<p && p-q<uint32_t(window) && tries--){if(rd64(raw+p)==rd64(raw+q)){uint32_t l=matchlen(raw+p,raw+q,lim);consider(best,l,p-q);if(l>=uint32_t(nice))break;}q=chain32[q];}}
  if(p+16<=n){uint32_t q=head16[h16(raw+p)];if(q<p && p-q<uint32_t(window) && rd64(raw+p)==rd64(raw+q))consider(best,matchlen(raw+p,raw+q,lim),p-q);}
  if(p+8<=n && best.len<uint32_t(nice)){uint32_t q=head8[h8(raw+p)];int tries=depth;while(q<p && p-q<uint32_t(window) && tries--){if(rd64(raw+p)==rd64(raw+q)){uint32_t l=matchlen(raw+p,raw+q,lim);consider(best,l,p-q);if(l>=uint32_t(nice))break;}q=chain8[q];}}
  uint32_t q=head[h4(raw+p)];int tries=std::min(depth,32);
  while(best.len<uint32_t(nice) && q<p && p-q<uint32_t(window) && tries--){
   if(rd32(raw+p)==rd32(raw+q)) {
    uint32_t l=matchlen(raw+p,raw+q,lim); consider(best,l,p-q); if(l>=uint32_t(nice))break;
   }
   q=chain[q];
  }
  insert(p);inserted=p+1;
  if(best.gain<=0)best={};return best;
 }
 void used(uint32_t d) {int at=nrep-1;for(int j=0;j<nrep;j++)if(reps[j]==d){at=j;break;}for(int j=at;j>0;j--)reps[j]=reps[j-1];reps[0]=d;}
};
}
inline std::vector<LZSeq> parse_lz_lazy(const uint8_t* raw,size_t n) {
 using namespace lzp_detail; auto start=std::chrono::steady_clock::now(); Finder f(raw,uint32_t(n));
 std::vector<LZSeq> out;out.reserve(n/25);uint32_t p=0,anchor=0; Match cur;bool cached=false;int lazy=envint("LZ_LAZY",1);double cost=0;uint64_t literals=0,matches=0,matched=0,repcount=0;
 while(p+4<=n){ if(!cached)cur=f.find(p);cached=false;
  if(cur.len<3){p++;continue;}
  if(lazy&&cur.len<65536&&p+5<=n){Match nxt=f.find(p+1);if(nxt.len>=3&&nxt.gain>cur.gain+0.05f){p++;cur=nxt;cached=true;continue;}}
  uint32_t ll=p-anchor;out.push_back({ll,cur.len,cur.dist});literals+=ll;matches++;matched+=cur.len;cost+=ll*f.litcost+len_cost(ll)+f.distcost(cur.dist)+len_cost(cur.len-3);for(int j=0;j<f.nrep;j++)if(f.reps[j]==cur.dist){repcount++;break;}f.used(cur.dist);
  p+=cur.len;anchor=p;
 }
 out.push_back({uint32_t(n)-anchor,0,0});literals+=n-anchor;cost+=(n-anchor)*f.litcost;
 double secs=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();fprintf(stderr,"LZ parse: %zu seq, %llu literals, %llu matched, %.2f%% rep, estimated %.0f bytes, %.3fs\n",out.size(),(unsigned long long)literals,(unsigned long long)matched,100.0*repcount/std::max<uint64_t>(1,matches),cost/8,secs);
 return out;
}
namespace lzp_detail {
inline unsigned sym(uint32_t x) { if(x<16)return x;unsigned k=lg2(x),e=k-2;return 16+(k-4)*4+((x>>e)&3); }
inline unsigned xb(uint32_t x) { return x<16?0:lg2(x)-2; }
struct Model {
 std::array<float,256> lit{};
#if LZ_LITERAL_CONTEXT
 std::array<std::array<float,256>,256> litctx{};
#endif
 std::array<float,128> ll{},ml{},oc{};
#if LZ_OFFSET_CONTEXT
 std::array<std::array<float,128>,64> occtx{};
#endif
 Model(const uint8_t*raw,const std::vector<LZSeq>&seq) {
  double lh[256]={},ah[128]={},bh[128]={},ch[128]={};uint32_t reps[4]={};size_t p=0;
#if LZ_LITERAL_CONTEXT
 std::vector<double> ctx(256*256,0.0);
#endif
#if LZ_OFFSET_CONTEXT
 std::vector<double> offsetctx(64*128,0.0);
#endif
  for(auto s:seq){
   ah[sym(s.literals)]++;bh[sym(s.length)]++;
   for(uint32_t j=0;j<s.literals;j++){lh[raw[p+j]]++;
#if LZ_LITERAL_CONTEXT
    unsigned prev=p+j?raw[p+j-1]:0;ctx[prev*256+raw[p+j]]++;
#endif
   }
   p+=s.literals;unsigned code=0;
   if(s.length){unsigned i=0;while(i<4&&reps[i]!=s.distance)i++;if(i==4){code=4+lg2(s.distance);i=3;}else code=i;for(;i;i--)reps[i]=reps[i-1];reps[0]=s.distance;p+=s.length;}
   ch[code]++;
#if LZ_OFFSET_CONTEXT
   unsigned context=(sym(s.length)>>2)*2+(s.literals!=0);offsetctx[context*128+code]++;
#endif
  }
  auto set=[](auto&c,const double*h,unsigned n){double sum=0;for(unsigned i=0;i<n;i++)sum+=h[i]+0.5;for(unsigned i=0;i<n;i++)c[i]=std::min(14.0,-log2((h[i]+0.5)/sum));};
  set(lit,lh,256);set(ll,ah,128);set(ml,bh,128);set(oc,ch,128);
#if LZ_LITERAL_CONTEXT
 double total=128;for(unsigned c=0;c<256;c++)total+=lh[c];
 for(unsigned prev=0;prev<256;prev++){double count=0;for(unsigned c=0;c<256;c++)count+=ctx[prev*256+c];for(unsigned c=0;c<256;c++){double probability=(ctx[prev*256+c]+16.0*(lh[c]+0.5)/total)/(count+16.0);litctx[prev][c]=std::min(14.0,-log2(probability));}}
#endif
  for(unsigned i=20;i<40;i++)oc[i]=std::min(oc[i],5.0f);
#if LZ_OFFSET_CONTEXT
  double global=64;for(unsigned k=0;k<128;k++)global+=ch[k];
  for(unsigned context=0;context<64;context++){double count=0;for(unsigned k=0;k<128;k++)count+=offsetctx[context*128+k];for(unsigned k=0;k<128;k++){double probability=(offsetctx[context*128+k]+32.0*(ch[k]+0.5)/global)/(count+32.0);occtx[context][k]=ch[k]?std::min(14.0,-log2(probability)):oc[k];}}
#endif
 }
 float literal_cost(uint8_t prev,uint8_t c)const{
#if LZ_LITERAL_CONTEXT
 return litctx[prev][c];
#else
 (void)prev;return lit[c];
#endif
 }
 float lcost(uint32_t n)const{return ll[sym(n)]+xb(n);}
 float mcost(uint32_t n)const{return ml[sym(n)]+xb(n);}
 unsigned dsymbol(uint32_t d,const std::array<uint32_t,4>&r)const{for(unsigned j=0;j<4;j++)if(r[j]==d)return j;return 4+lg2(d);}
 float dcode_cost(unsigned code,uint32_t length,uint32_t literals)const{
#if LZ_OFFSET_CONTEXT
 unsigned context=(sym(length)>>2)*2+(literals!=0);return occtx[context][code]+(code>=4?code-4:0);
#else
 (void)length;(void)literals;return oc[code]+(code>=4?code-4:0);
#endif
 }
 float dcost(uint32_t d,const std::array<uint32_t,4>&r)const{for(unsigned j=0;j<4;j++)if(r[j]==d)return oc[j];unsigned k=lg2(d);return oc[4+k]+k;}
};
struct Node { float cost=1e30f;uint32_t prev=0,len=0,dist=0,ll=0;std::array<uint32_t,4> reps{}; };
inline void move_rep(std::array<uint32_t,4>&r,uint32_t d){unsigned j=0;while(j<3&&r[j]!=d)j++;for(;j;j--)r[j]=r[j-1];r[0]=d;}
}
inline std::vector<LZSeq> parse_lz_optimal_core(const uint8_t*raw,size_t n,const lzp_detail::Model&model) {
 using namespace lzp_detail;auto start=std::chrono::steady_clock::now();
 Finder f(raw,uint32_t(n));f.nice=envint("LZ_OPT_NICE",LZ_PARSE_NICE);f.depth=envint("LZ_OPT_CHAIN",LZ_PARSE_CHAIN);f.nrep=4;
 unsigned span=envint("LZ_OPT_SPAN",4096);std::vector<Node> dp(span+1);std::vector<Match> cand;std::vector<Node> path;std::vector<LZSeq> out;out.reserve(n/30);std::array<uint32_t,4> reps{};uint32_t pos=0,pending=0;uint64_t literals=0;
 auto emit_path=[&](uint32_t end){path.clear();for(uint32_t x=end;x;){path.push_back(dp[x]);x=dp[x].prev;}for(auto it=path.rbegin();it!=path.rend();++it){if(it->len){out.push_back({pending,it->len,it->dist});literals+=pending;pending=0;move_rep(reps,it->dist);}else pending++;}};
 while(pos<n){uint32_t width=std::min<uint32_t>(span,n-pos);std::fill(dp.begin(),dp.begin()+width+1,Node{});dp[0].cost=0;dp[0].ll=pending;dp[0].reps=reps;bool escaped=false;
  for(uint32_t i=0;i<width;i++){
   auto&cur=dp[i];if(cur.cost>=1e29f)continue;uint32_t p=pos+i;
   float lc=cur.cost+model.literal_cost(p?raw[p-1]:0,raw[p])+model.lcost(cur.ll+1)-model.lcost(cur.ll);
   if(lc<dp[i+1].cost){dp[i+1]=cur;dp[i+1].cost=lc;dp[i+1].prev=i;dp[i+1].len=dp[i+1].dist=0;dp[i+1].ll=cur.ll+1;}
   for(unsigned j=0;j<4;j++)f.reps[j]=cur.reps[j];cand.clear();f.collecting=&cand;Match best=f.find(p);f.collecting=nullptr;
   if(best.len>=uint32_t(f.nice)){emit_path(i);out.push_back({pending,best.len,best.dist});literals+=pending;pending=0;move_rep(reps,best.dist);pos=p+best.len;escaped=true;break;}
   for(auto m:cand){uint32_t maxlen=std::min(m.len,width-i);
#if LZ_OFFSET_CONTEXT
    unsigned dsymbol=model.dsymbol(m.dist,cur.reps);float dc=cur.cost+model.lcost(0);
#else
    float dc=model.dcost(m.dist,cur.reps)+model.lcost(0);
#endif
    std::array<uint32_t,4> rr=cur.reps;move_rep(rr,m.dist);
    for(uint32_t l=3;l<=maxlen;l++){
#if LZ_OFFSET_CONTEXT
     float c=dc+model.dcode_cost(dsymbol,l,cur.ll)+model.mcost(l);
#else
     float c=cur.cost+dc+model.mcost(l);
#endif
     if(c<dp[i+l].cost){auto&dest=dp[i+l];dest.cost=c;dest.prev=i;dest.len=l;dest.dist=m.dist;dest.ll=0;dest.reps=rr;}}
   }
  }
  if(!escaped){emit_path(width);pos+=width;}
 }
 out.push_back({pending,0,0});literals+=pending;double secs=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();fprintf(stderr,"Optimal LZ: %zu sequences, %llu literals, %.3fs\n",out.size(),(unsigned long long)literals,secs);return out;
}
inline std::vector<LZSeq> parse_lz_optimal(const uint8_t*raw,size_t n) {
 using namespace lzp_detail;size_t train=std::min<size_t>(n,2000000);auto seed=parse_lz_lazy(raw,train);Model model(raw,seed);if(envint("LZ_LEARN",1)){seed=parse_lz_optimal_core(raw,train,model);model=Model(raw,seed);}seed.clear();seed.shrink_to_fit();auto result=parse_lz_optimal_core(raw,n,model);if(envint("LZ_REFINE",LZ_PARSE_REFINE)){model=Model(raw,result);result=parse_lz_optimal_core(raw,n,model);}return result;
}
inline std::vector<LZSeq> parse_lz(const uint8_t*raw,size_t n) {return lzp_detail::envint("LZ_OPTIMAL",LZ_PARSE_OPTIMAL)?parse_lz_optimal(raw,n):parse_lz_lazy(raw,n);}
