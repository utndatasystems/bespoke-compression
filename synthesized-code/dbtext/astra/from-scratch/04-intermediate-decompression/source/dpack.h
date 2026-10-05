
#pragma once
#include <queue>
#include <array>
namespace dpack {
static uint32_t u32(const uint8_t*p){uint32_t x;memcpy(&x,p,4);return x;}
#ifdef ENCODER
static std::vector<uint8_t> pack(const std::vector<uint8_t>&in){
 std::vector<uint8_t> plain(1,0);plain.insert(plain.end(),in.begin(),in.end());if(in.size()<256)return plain;
 uint32_t f[256]={};for(auto x:in)f[x]++;
 uint8_t lens[256];unsigned floor=1;
 for(;;){
  using P=std::pair<uint64_t,unsigned>;std::priority_queue<P,std::vector<P>,std::greater<P>>pq;int par[512];std::fill(par,par+512,-1);
  for(unsigned i=0;i<256;i++)pq.push({std::max(f[i],floor),i});
  unsigned next=256;while(pq.size()>1){auto a=pq.top();pq.pop();auto b=pq.top();pq.pop();par[a.second]=par[b.second]=next;pq.push({a.first+b.first,next++});}
  unsigned ml=0;for(unsigned i=0;i<256;i++){unsigned l=0;int j=i;while(par[j]>=0){l++;j=par[j];}lens[i]=l;ml=std::max(ml,l);}if(ml<=10)break;floor*=2;
 }
 unsigned cnt[11]={},nc[11]={},codes[256];for(auto l:lens)cnt[l]++;for(unsigned b=1;b<=10;b++)nc[b]=(nc[b-1]+cnt[b-1])*2;
 for(unsigned i=0;i<256;i++){unsigned x=nc[lens[i]]++,r=0;for(unsigned b=0;b<lens[i];b++){r=r*2+(x&1);x>>=1;}codes[i]=r;}
 std::vector<uint8_t>o(1+4+16+128,0);o[0]=1;uint32_t sz=in.size();memcpy(o.data()+1,&sz,4);
 for(unsigned i=0;i<128;i++)o[21+i]=lens[i*2]|(lens[i*2+1]<<4);
 for(unsigned k=0;k<4;k++){size_t a=in.size()*k/4,z=in.size()*(k+1)/4;uint32_t bits=0;for(size_t i=a;i<z;i++)bits+=lens[in[i]];memcpy(o.data()+5+k*4,&bits,4);
  size_t off=o.size();o.resize(off+(bits+7)/8+4,0);uint32_t p=0;
  for(size_t i=a;i<z;i++){unsigned x=in[i],v=codes[x]<<(p&7);o[off+(p>>3)]|=v;o[off+(p>>3)+1]|=v>>8;o[off+(p>>3)+2]|=v>>16;p+=lens[x];}
 }
 if(o.size()>=plain.size())return plain;return o;
}
#endif
static bool unpack(const uint8_t*p,size_t n,size_t limit,std::vector<uint8_t>&o){
 if(n<1)return false;if(p[0]==0){if(n-1>limit)return false;o.assign(p+1,p+n);return true;}if(p[0]!=1||n<149)return false;
 uint32_t sz=u32(p+1);if(sz>limit)return false;uint16_t table[1024]={};unsigned cnt[11]={},nc[11]={};uint8_t lens[256];
 for(unsigned i=0;i<256;i++){unsigned l=(p[21+i/2]>>((i&1)*4))&15;if(l<1||l>10)return false;lens[i]=l;cnt[l]++;}
 for(unsigned l=1;l<=10;l++){nc[l]=(nc[l-1]+cnt[l-1])*2;if(nc[l]+cnt[l]>(1U<<l))return false;}if(nc[10]+cnt[10]!=1024)return false;
 for(unsigned i=0;i<256;i++){unsigned l=lens[i],x=nc[l]++,r=0;for(unsigned b=0;b<l;b++){r=r*2+(x&1);x>>=1;}for(unsigned j=r;j<1024;j+=1U<<l)table[j]=i|(l<<8);}
 const uint8_t*streams[4];uint32_t ends[4],pos[4]={};size_t off=149;
 for(unsigned k=0;k<4;k++){ends[k]=u32(p+5+4*k);size_t bytes=(uint64_t(ends[k])+7)/8+4;if(bytes>n-off)return false;streams[k]=p+off;off+=bytes;}if(off!=n)return false;
 o.resize(sz);uint32_t starts[4],lengths[4];for(unsigned k=0;k<4;k++){starts[k]=uint64_t(sz)*k/4;lengths[k]=uint64_t(sz)*(k+1)/4-starts[k];}
 unsigned common=lengths[0];for(unsigned j=0;j<common;j++){for(unsigned k=0;k<4;k++){if(pos[k]>=ends[k])return false;unsigned v=table[(u32(streams[k]+(pos[k]>>3))>>(pos[k]&7))&1023];o[starts[k]+j]=v;pos[k]+=v>>8;}}
 for(unsigned k=0;k<4;k++){for(unsigned j=common;j<lengths[k];j++){if(pos[k]>=ends[k])return false;unsigned v=table[(u32(streams[k]+(pos[k]>>3))>>(pos[k]&7))&1023];o[starts[k]+j]=v;pos[k]+=v>>8;}if(pos[k]!=ends[k])return false;}
 return true;
}
}
