#pragma once
#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>
namespace phrase_dict {
// A small independent LZ parser for dictionary metadata; no external codec.
inline std::vector<uint8_t> pack(const std::vector<uint8_t>&r){
 std::vector<uint8_t>o;uint32_t n=r.size();o.resize(4);memcpy(o.data(),&n,4);std::vector<int32_t>h(1<<18,-1);size_t pos=0,lit=0;
 auto hash=[&](size_t p){uint32_t x;memcpy(&x,r.data()+p,4);return (x*2654435761u)>>14;};
 auto literal=[&](size_t end){while(lit<end){size_t l=std::min<size_t>(128,end-lit);o.push_back(l-1);o.insert(o.end(),r.begin()+lit,r.begin()+lit+l);lit+=l;}};
 while(pos+4<=r.size()) {auto k=hash(pos);int32_t prior=h[k];h[k]=pos;size_t len=0;
 if(prior>=0&&pos-size_t(prior)<=65535&&memcmp(r.data()+prior,r.data()+pos,4)==0){len=4;while(len<131&&pos+len<r.size()&&r[prior+len]==r[pos+len])len++;}
 if(len>=4){literal(pos);o.push_back(128+len-4);unsigned d=pos-prior;o.push_back(d);o.push_back(d>>8);size_t stop=pos+len;pos++;while(pos<stop){if(pos+4<=r.size())h[hash(pos)]=pos;pos++;}lit=pos;}else pos++;
 }literal(r.size());return o;
}
inline bool unpack(const uint8_t*p,size_t n,std::vector<uint8_t>&r){
 if(n<4)return false;uint32_t size;memcpy(&size,p,4);if(size>4000000)return false;r.resize(size);p+=4;n-=4;size_t off=0;
 while(n){unsigned c=*p++;n--;if(c<128){unsigned len=c+1;if(len>n||len>size-off)return false;memcpy(r.data()+off,p,len);p+=len;n-=len;off+=len;}else{unsigned len=c-128+4;if(n<2||len>size-off)return false;unsigned d=p[0]|unsigned(p[1])<<8;p+=2;n-=2;if(!d||d>off)return false;size_t src=off-d;if(d>=len)memcpy(r.data()+off,r.data()+src,len);else for(unsigned j=0;j<len;j++)r[off+j]=r[src+j];off+=len;}}
 return off==size;
}
}
