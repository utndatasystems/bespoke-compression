#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <queue>
#include <algorithm>
namespace rowhuff {
static constexpr unsigned NS=8,TB=13,TN=1u<<TB;
static uint32_t rd32(const uint8_t*p){uint32_t z;memcpy(&z,p,4);return z;}
static uint64_t rd64(const uint8_t*p){uint64_t z;memcpy(&z,p,8);return z;}
static bool tables(const uint8_t*len,uint32_t*tab,uint32_t*codes=nullptr){
 unsigned counts[13]={},next[13]={},code=0;for(unsigned s=0;s<256;s++){if(!len[s]||len[s]>12)return false;counts[len[s]]++;}
 for(unsigned b=1;b<=12;b++){code=(code+counts[b-1])<<1;next[b]=code;if(code+counts[b]>(1u<<b))return false;}if(code+counts[12]!=(1u<<12))return false;
 for(unsigned s=0;s<256;s++){unsigned l=len[s],v=next[l]++,r=0;for(unsigned j=0;j<l;j++){r=(r<<1)|(v&1);v>>=1;}if(codes)codes[s]=r;for(unsigned i=r;i<TN;i+=1u<<l)tab[i]=s|(l<<16)|(1u<<24);}
 uint32_t base[TN];memcpy(base,tab,sizeof base);for(unsigned i=0;i<TN;i++){unsigned bits=0,num=0,v=0;while(num<3){unsigned e=base[i>>bits],l=(e>>16)&255;if(bits+l>TB)break;v|=(e&255)<<(8*num);bits+=l;num++;}tab[i]=v|(bits<<24)|(num<<28);}
 return true;
}
static std::vector<uint8_t> encode(const uint8_t*p,size_t n){
 uint64_t freq[256]={};for(size_t i=0;i<n;i++)freq[p[i]]++;
 struct Node{uint64_t f;int a,b;};std::vector<Node>ns;using P=std::pair<uint64_t,int>;std::priority_queue<P,std::vector<P>,std::greater<P>>q;
 for(unsigned i=0;i<256;i++){ns.push_back({freq[i]?freq[i]:1,-1,-1});q.push({ns.back().f,int(i)});}while(q.size()>1){auto a=q.top();q.pop();auto b=q.top();q.pop();int x=ns.size();ns.push_back({a.first+b.first,a.second,b.second});q.push({a.first+b.first,x});}
 uint8_t len[256]={};auto visit=[&](auto&&self,int id,unsigned d)->void{if(id<256){len[id]=d;return;}self(self,ns[id].a,d+1);self(self,ns[id].b,d+1);};visit(visit,q.top().second,0);
 uint32_t tab[TN],codes[256];if(!tables(len,tab,codes))return {};std::vector<uint8_t>o(260+8*NS);uint32_t nn=n;memcpy(o.data(),&nn,4);memcpy(o.data()+4,len,256);
 for(unsigned k=0;k<NS;k++){size_t start=n*k/NS,end=n*(k+1)/NS;uint64_t bits=0;for(size_t i=start;i<end;i++)bits+=len[p[i]];memcpy(o.data()+260+8*k,&bits,8);size_t at=o.size();o.resize(at+(bits+7)/8+64);uint64_t bit=0;for(size_t i=start;i<end;i++){uint64_t v=(uint64_t)codes[p[i]]<<(bit&7);size_t j=at+(bit>>3);o[j]|=v;o[j+1]|=v>>8;o[j+2]|=v>>16;bit+=len[p[i]];}}
 return o;
}
static bool decode(const uint8_t*p,size_t z,uint8_t*out,size_t n){
 if(z<260+8*NS||rd32(p)!=n)return false;uint32_t tab[TN];if(!tables(p+4,tab))return false;
 const uint8_t*src[NS];uint64_t bits[NS],bp[NS]={};uint8_t*op[NS],*oe[NS];size_t at=260+8*NS;
 for(unsigned k=0;k<NS;k++){bits[k]=rd64(p+260+8*k);if(bits[k]>(uint64_t)n*12)return false;uint64_t nb=(bits[k]+7)/8+64;if(at>z||nb>z-at)return false;src[k]=p+at;at+=nb;op[k]=out+n*k/NS;oe[k]=out+n*(k+1)/NS;}if(at!=z)return false;
 auto one=[&](unsigned k){uint64_t w=rd64(src[k]+(bp[k]>>3))>>(bp[k]&7);uint32_t e=tab[w&(TN-1)];memcpy(op[k],&e,4);op[k]+=e>>28;bp[k]+=(e>>24)&15;};
 for(;;){size_t cnt=SIZE_MAX;for(unsigned k=0;k<NS;k++){cnt=std::min<size_t>(cnt,(oe[k]-op[k] ? (oe[k]-op[k]-1)/3 : 0));cnt=std::min<uint64_t>(cnt,(bits[k]-bp[k])/TB);}if(!cnt)break;for(size_t j=0;j<cnt;j++){one(0);one(1);one(2);one(3);one(4);one(5);one(6);one(7);}}
 for(unsigned k=0;k<NS;k++){while(op[k]<oe[k]){if(bp[k]>=bits[k])return false;uint64_t w=rd64(src[k]+(bp[k]>>3))>>(bp[k]&7);uint8_t sy=tab[w&(TN-1)]&255;unsigned l=p[4+sy];if(l>bits[k]-bp[k])return false;*op[k]++=sy;bp[k]+=l;}if(bp[k]!=bits[k])return false;}
 return true;
}
}
