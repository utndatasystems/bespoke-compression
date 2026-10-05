#pragma once
#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>
namespace exp_parser_runs {
struct Record {uint32_t lit,len,off;};
struct Choice {uint32_t len,off;};
inline uint64_t load64(const uint8_t*p){uint64_t x;memcpy(&x,p,8);return x;}
inline std::vector<Record> parse(const uint8_t* input,size_t size,int depth=32,int literal_cost=104,int overhead=12,int literal_start=12){
 uint32_t n=size;std::vector<uint8_t>a(n+16);memcpy(a.data(),input,n);std::vector<uint32_t>prev(n),cost(n+1),cost1(n+1),head(1<<24);
 for(uint32_t i=0;i<n;i++){uint32_t h=((load64(&a[i])<<24)*0x9e3779b185ebca87ULL)>>40;prev[i]=head[h];head[h]=i+1;}head.clear();head.shrink_to_fit();
 auto choose=[&](int64_t i,uint32_t& result_cost,uint32_t& result_cost1){uint32_t bc=0xffffffffu;Choice best={0,0};uint32_t q=prev[i],bestl[7]={},besto[7]={};
  for(int t=0;q&&t<depth;t++){uint32_t p=q-1;q=prev[p];if(i+5>=n||memcmp(&a[p],&a[i],5))continue;uint32_t off=i-p;int ob=32-__builtin_clz(off),grp=(ob-1)/4;if(bestl[grp]&&a[p+bestl[grp]]!=a[i+bestl[grp]])continue;uint32_t l=0;while(i+l+8<=n){uint64_t d=load64(&a[p+l])^load64(&a[i+l]);if(d){l+=__builtin_ctzll(d)/8;break;}l+=8;}while(i+l<n&&a[p+l]==a[i+l])++l;if(l>bestl[grp]){bestl[grp]=l;besto[grp]=off;}}
  for(int g=0;g<7;g++)if(bestl[g]>=5){uint32_t ml=bestl[g],off=besto[g];int ob=32-__builtin_clz(off);unsigned oc=(ob-1+overhead)*16;
   auto test=[&](uint32_t l){uint32_t lc=l<16?(l-5)*3:33+(32-__builtin_clz(l))*5;uint32_t c=cost[i+l]+oc+lc;if(c<bc){bc=c;best={l,off};}};
   if(ml<24){for(uint32_t l=5;l<=ml;l++)test(l);}else{for(uint32_t l=5;l<=12;l++)test(l);for(uint32_t l=ml-10;l<=ml;l++)test(l);for(uint32_t l=16;l<ml-10;l*=2)test(l);}
  }result_cost=std::min(bc,cost1[i+1]+literal_cost+literal_start*16);result_cost1=std::min(bc,cost1[i+1]+literal_cost);return best;
 };
 for(int64_t i=n-1;i>=0;i--){uint32_t result_cost,result_cost1;choose(i,result_cost,result_cost1);cost[i]=result_cost;cost1[i]=result_cost1;}
 std::vector<Record>records;uint32_t i=0,lits=0;bool in_literal=false;while(i<n){bool literal=in_literal?cost1[i]==cost1[i+1]+literal_cost:cost[i]==cost1[i+1]+literal_cost+literal_start*16;if(literal){i++;lits++;in_literal=true;}else{uint32_t unused,unused1;Choice c=choose(i,unused,unused1);records.push_back({lits,c.len,c.off});i+=c.len;lits=0;in_literal=false;}}records.push_back({lits,0,0});return records;
}
}
