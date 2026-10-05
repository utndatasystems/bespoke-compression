#include "codec.h"
#include "analyze_ransn.h"
#include "exp_parser_runs.h"
#include <cstdio>
struct Rec {uint32_t lit,len,off;};
struct Header {uint64_t magic,n,count,tail,nlit,nll,nbits,sz[6];};
static constexpr uint64_t MAGIC=0x365245545459505aull;
static int64_t pack(const uint8_t*s,size_t n,const std::vector<Rec>&rec,uint8_t*out,size_t cap){
 std::vector<uint8_t> ctrl,logs,lits,ext,bits,ll;ctrl.reserve(rec.size());logs.reserve(rec.size());lits.reserve(n/20);ext.reserve(rec.size()/5);bits.reserve(rec.size()*2);ll.reserve(rec.size()/8);
 auto addext=[&](size_t v){while(v>=255){ext.push_back(255);v-=255;}ext.push_back(v);};
 uint64_t acc=0,ca=0;unsigned used=0,cu=0;size_t pos=0;uint64_t nbits=0,tail=0,count=0;
 for(auto&r:rec){lits.insert(lits.end(),s+pos,s+pos+r.lit);pos+=r.lit;if(!r.len){tail=r.lit;break;}
  unsigned t=(r.lit?32:0)|std::min(r.len-5,31u);ca|=uint64_t(t)<<cu;cu+=6;if(cu>=8){ctrl.push_back(ca);ca>>=8;cu-=8;}
  if(r.lit){ll.push_back(std::min(r.lit-1,255u));if(r.lit>=256)addext(r.lit-256);}if(r.len>=36)addext(r.len-36);
  unsigned b=31-__builtin_clz(r.off);logs.push_back(b);nbits+=b;acc|=uint64_t(r.off^(1u<<b))<<used;used+=b;while(used>=8){bits.push_back(acc);acc>>=8;used-=8;}pos+=r.len;count++;
 }
 if(cu)ctrl.push_back(ca);ctrl.resize(ctrl.size()+32);if(used)bits.push_back(acc);bits.resize(bits.size()+32);
 uint64_t nlits=lits.size(),nll=ll.size();std::vector<uint8_t> streams[6]={std::move(ctrl),aransn::encode(logs.data(),logs.size(),9),std::move(lits),std::move(ext),std::move(bits),aransn::encode(ll.data(),ll.size(),11)};
 Header h{};h.magic=MAGIC;h.n=n;h.count=count;h.tail=tail;h.nlit=nlits;h.nll=nll;h.nbits=nbits;size_t z=sizeof(h);for(int j=0;j<6;j++){h.sz[j]=streams[j].size();z+=h.sz[j];}if(z>cap)return -1;memcpy(out,&h,sizeof(h));size_t p=sizeof(h);for(int j=0;j<6;j++){memcpy(out+p,streams[j].data(),h.sz[j]);p+=h.sz[j];}
 fprintf(stderr,"exp6 count%llu nll%llu literals%llu ctrl%llu logs%llu ext%llu off%llu ll%llu archive%zu\n",(unsigned long long)count,(unsigned long long)nll,(unsigned long long)nlits,(unsigned long long)h.sz[0],(unsigned long long)h.sz[1],(unsigned long long)h.sz[3],(unsigned long long)h.sz[4],(unsigned long long)h.sz[5],z);return z;
}
extern "C" int64_t lab_encode(const uint8_t*s,size_t n,uint8_t*out,size_t cap){if(n>100000000)return -1;try{if(!n)return pack(s,n,std::vector<Rec>{{0,0,0}},out,cap);auto a=exp_parser_runs::parse(s,n);std::vector<Rec>r;r.reserve(a.size());for(auto&v:a)r.push_back({v.lit,v.len,v.off});return pack(s,n,r,out,cap);}catch(...){return -1;}}
