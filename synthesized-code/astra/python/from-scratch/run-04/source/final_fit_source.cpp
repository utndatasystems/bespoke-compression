#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cmath>
#include "analysis_costs_refit.h"
using namespace std;
struct Seq{uint32_t ll,ml,d;};
struct Cands{uint32_t len[14],dist[14];};
static inline uint64_t read64(const uint8_t*p){uint64_t v;memcpy(&v,p,8);return v;}
static inline uint32_t read32(const uint8_t*p){uint32_t v;memcpy(&v,p,4);return v;}
static inline uint32_t hash4(const uint8_t*p){return(read32(p)*2654435761u)>>10;}
static inline uint32_t hash6(const uint8_t*p){return ((read64(p)<<16)*0x9e3779b185ebca87ull)>>42;}
static inline uint32_t hash8(const uint8_t*p){return (read64(p)*0x9e3779b185ebca87ull)>>42;}
static inline uint32_t hash12(const uint8_t*p){return ((read64(p)^uint64_t(read32(p+8))*0xbf58476d1ce4e5b9ull)*0x9e3779b185ebca87ull)>>42;}
static inline uint32_t hash16(const uint8_t*p){return ((read64(p)^read64(p+8)*0xbf58476d1ce4e5b9ull)*0x9e3779b185ebca87ull)>>42;}
static inline uint32_t hash24(const uint8_t*p){return ((read64(p)^read64(p+8)*0xbf58476d1ce4e5b9ull^read64(p+16)*0x94d049bb133111ebull)*0x9e3779b185ebca87ull)>>42;}
static inline int band(uint32_t d){return (31-__builtin_clz(d))/2;}
static inline uint32_t matchlen(const uint8_t*a,size_t p,size_t q,size_t end){
 size_t l=4;while(p+l+8<=end){uint64_t x=read64(a+p+l)^read64(a+q+l);if(x)return l+(__builtin_ctzll(x)>>3);l+=8;}while(p+l<end&&a[p+l]==a[q+l])++l;return l;
}
void optparse(const uint8_t*a,size_t n,vector<Seq>&s,vector<uint8_t>&lit,int depth=16,float penalty=8,int window=67108864,size_t start=0){
 vector<int32_t> h4(1<<22,-1),h6(1<<22,-1),h8(1<<22,-1),h12(1<<22,-1),h16(1<<22,-1),h24(1<<22,-1),prev(n,-1);
 const size_t BS=1048576; vector<Cands> cs(BS);vector<float> dp(BS+1),bm(BS+1);vector<uint8_t> lc(BS);vector<uint32_t> dl(BS),dd(BS);
 size_t anchor=start;
 for(size_t p=0;p<start&&p+8<n;++p){uint32_t h=hash6(a+p);prev[p]=h6[h];h6[h]=p;h4[hash4(a+p)]=p;h8[hash8(a+p)]=p;h12[hash12(a+p)]=p;h16[hash16(a+p)]=p;h24[hash24(a+p)]=p;}
 for(size_t base=start;base<n;base+=BS){
  size_t end=min(n,base+BS),bn=end-base;
  memset(cs.data(),0,bn*sizeof(Cands));
  for(size_t p=base;p+8<end;++p){
   Cands &c=cs[p-base];uint32_t hh4=hash4(a+p),hh6=hash6(a+p);int old4=h4[hh4],old6=h6[hh6];
   h4[hh4]=h6[hh6]=p;prev[p]=old6;
   uint32_t hh8=hash8(a+p),hh12=hash12(a+p),hh16=hash16(a+p),hh24=hash24(a+p);
   int old8=h8[hh8],old12=h12[hh12],old16=h16[hh16],old24=h24[hh24];h8[hh8]=h12[hh12]=h16[hh16]=h24[hh24]=p;
   for(int q:{old8,old12,old16,old24})if(q>=0&&p-q<=uint32_t(window)&&read32(a+p)==read32(a+q)){
    uint32_t len=matchlen(a,p,q,end),d=p-q;int b=band(d);if(len>c.len[b]){c.len[b]=len;c.dist[b]=d;}
   }
   if(old4>=0 && p-old4<=uint32_t(window)&&read32(a+p)==read32(a+old4)){
    uint32_t len=matchlen(a,p,old4,end),d=p-old4;int b=band(d);if(len>c.len[b]){c.len[b]=len;c.dist[b]=d;}
   }
   int q=old6,it=depth;
   while(q>=0&&p-q<=uint32_t(window)&&it--){
    uint32_t d=p-q;int b=band(d);uint32_t best=c.len[b];
    if((!best||a[q+best]==a[p+best])&&read32(a+p)==read32(a+q)){
     uint32_t len=matchlen(a,p,q,end);if(len>best){c.len[b]=len;c.dist[b]=d;}
    }
    q=prev[q];
   }
  }
  dp[bn]=0;bm[bn]=0;
  for(size_t i=bn;i-->0;){
   float best=1e30f;uint32_t bestlen=0,bestdist=0;Cands &c=cs[i];
   for(int b=0;b<14;++b){
    uint32_t m=c.len[b],d=c.dist[b];if(m<4)continue;
    float overhead=distcost[31-__builtin_clz(d)]+penalty;
    uint32_t cap=min(m,64u);
    for(uint32_t len=4;len<=cap;++len){
     float v=dp[i+len]+mlcost[len]+overhead;if(v<best){best=v;bestlen=len;bestdist=d;}
    }
    if(m>64){
     uint32_t starts[]={80,96,128,160,192,224,255,384,512,768,1024,2048,4096,8192,16384,32768,65536,131072,262144};
     for(uint32_t len:starts){if(len>=m)break;float lc=mlcost[min(len,258u)]+(len>=258?32.0f:0);float v=dp[i+len]+lc+overhead;if(v<best){best=v;bestlen=len;bestdist=d;}}
     float lc=mlcost[min(m,258u)]+(m>=258?32.0f:0);float v=dp[i+m]+lc+overhead;if(v<best){best=v;bestlen=m;bestdist=d;}
    }
   }
   bm[i]=best;dl[i]=bestlen;dd[i]=bestdist;
   float cbest=dp[i+1]+litcost[a[base+i]]+4.0f;uint8_t cb=255;float litbits=0;
   for(uint32_t l=0;l<=32&&i+l<=bn;++l){
    float v=litbits+llcost[l]+bm[i+l];
    if(v<cbest){cbest=v;cb=l;}
    if(i+l<bn)litbits+=litcost[a[base+i+l]];
   }
   dp[i]=cbest;lc[i]=cb;
  }
  for(size_t i=0;i<bn;){uint8_t l=lc[i];if(l==255){++i;continue;}i+=l;if(i>=bn)break;uint32_t len=dl[i],d=dd[i];if(d){size_t p=base+i;s.push_back({uint32_t(p-anchor),len,d});lit.insert(lit.end(),a+anchor,a+p);anchor=p+len;}i+=len;}
  if((base/BS)%64==63)fprintf(stderr,"at %zu seq=%zu lit=%zu\n",end,s.size(),lit.size());
 }
 s.push_back({uint32_t(n-anchor),0,0});lit.insert(lit.end(),a+anchor,a+n);
}
int main(int argc,char**argv){
 const char*input=argc>1?argv[1]:"/inputs/python-source.py";const char*out=argc>2?argv[2]:"/work/optparse.bin";
 int depth=argc>3?atoi(argv[3]):16;float penalty=argc>4?atof(argv[4]):8;size_t limit=argc>5?atol(argv[5]):0;size_t first=argc>6?atol(argv[6]):0;
 FILE*f=fopen(input,"rb");fseek(f,0,SEEK_END);size_t n=ftell(f);if(limit)n=min(n,limit);rewind(f);vector<uint8_t>a(n+64);fread(a.data(),1,n,f);fclose(f);
 auto start=chrono::steady_clock::now();vector<Seq>s;vector<uint8_t>lit;optparse(a.data(),n,s,lit,depth,penalty,67108864,first);
 double sec=chrono::duration<double>(chrono::steady_clock::now()-start).count();fprintf(stderr,"n=%zu seq=%zu lit=%zu sec=%.3f\n",n,s.size(),lit.size(),sec);
 f=fopen(out,"wb");uint64_t ns=s.size(),nl=lit.size();uint64_t fragment_n=n-first;fwrite(&fragment_n,8,1,f);fwrite(&ns,8,1,f);fwrite(&nl,8,1,f);fwrite(s.data(),12,s.size(),f);fwrite(lit.data(),1,lit.size(),f);fclose(f);
}
