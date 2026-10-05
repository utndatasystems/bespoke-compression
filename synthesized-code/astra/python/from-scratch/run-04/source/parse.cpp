
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <chrono>
using namespace std;
struct Seq{uint32_t ll,ml,d;};
uint64_t rd64(const uint8_t*p){uint64_t v;memcpy(&v,p,8);return v;}
uint32_t hash4(const uint8_t*p){uint32_t v;memcpy(&v,p,4);return (v*2654435761u)>>8;}
int main(int argc,char**argv) {
 const char *name=argc>1?argv[1]:"/inputs/python-source.py";
 int window=argc>2?atoi(argv[2]):16777216, depth=argc>3?atoi(argv[3]):64, minmatch=argc>4?atoi(argv[4]):4;
 FILE*f=fopen(name,"rb");fseek(f,0,SEEK_END);size_t n=ftell(f);rewind(f);vector<uint8_t>a(n+64);fread(a.data(),1,n,f);fclose(f);
 vector<int32_t>head(1<<24,-1),prev(n,-1);
 auto start=chrono::steady_clock::now();
 vector<Seq>s;vector<uint8_t>lit;size_t anchor=0;
 uint64_t hist[32]={}, lens[40]={}, litn=0;
 auto match=[&](size_t p,uint32_t&d)->uint32_t{
  uint32_t h=hash4(&a[p]), best=minmatch-1;int k=head[h], bottom=p>window?p-window:0, it=depth;
  while(k>=bottom && it--){
   if(a[k+best]==a[p+best] && !memcmp(&a[k],&a[p],4)){
    size_t l=4;while(p+l+8<=n && rd64(&a[k+l])==rd64(&a[p+l]))l+=8;
    while(p+l<n&&a[k+l]==a[p+l])++l;
    if(l>best){best=l;d=p-k;if(l>=1024)break;}
   }k=prev[k];
  }
  return best>=unsigned(minmatch)?best:0;
 };
 auto add=[&](size_t p){auto h=hash4(&a[p]);prev[p]=head[h];head[h]=p;};
 size_t p=0;
 while(p+32<n){
  uint32_t d=0, ml=match(p,d);
  add(p);
  if(!ml){++p;continue;}
  uint32_t nd=0,nml=match(p+1,nd);
  // prefer longer next match, otherwise short-distance current.
  if(nml>ml+1){++p;continue;}
  size_t ll=p-anchor; s.push_back({uint32_t(ll),ml,d});lit.insert(lit.end(),&a[anchor],&a[p]);
  ++hist[31-__builtin_clz(d)];++lens[min(ml,39u)];
  for(size_t i=1;i<ml;++i)if(p+i+4<=n)add(p+i);
  p+=ml;anchor=p;
 }
 s.push_back({uint32_t(n-anchor),0,0});lit.insert(lit.end(),&a[anchor],&a[n]);
 double sec=chrono::duration<double>(chrono::steady_clock::now()-start).count();
 fprintf(stderr,"n=%zu seq=%zu literals=%zu time=%.3f\n",n,s.size(),lit.size(),sec);
 for(int i=0;i<32;++i)fprintf(stderr,"d%d %lu\n",i,hist[i]);
 for(int i=4;i<40;++i)fprintf(stderr,"l%d %lu\n",i,lens[i]);
 f=fopen(argc>5?argv[5]:"/work/parse.bin","wb");uint64_t ns=s.size(),nl=lit.size();fwrite(&n,8,1,f);fwrite(&ns,8,1,f);fwrite(&nl,8,1,f);fwrite(s.data(),12,s.size(),f);fwrite(lit.data(),1,lit.size(),f);fclose(f);
}