#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <queue>
#include <algorithm>
#include <limits>
namespace row_index {
// This stream stores its exact row count and exact decoded total. max_total is
// an upper bound (useful when a token bitstream ends inside its final byte).
// unpack verifies the decoded sum against that stored exact total as well.
static constexpr uint32_t MAGIC=0x58444900;
struct Header {uint32_t magic,rows,total,bits,alphabet;};
inline uint32_t reverse(uint32_t x,unsigned n){uint32_t r=0;while(n--){r=(r<<1)|(x&1);x>>=1;}return r;}
inline bool codes(const std::vector<uint8_t>&len,std::vector<uint32_t>&out){uint32_t count[16]{},next[16]{},c=0;unsigned active=0;for(auto l:len){if(l>15)return false;if(l){count[l]++;active++;}}if(!active)return false;for(unsigned l=1;l<=15;++l){c=(c+count[l-1])*2;if(c+count[l]>(1u<<l))return false;next[l]=c;}out.resize(len.size());for(size_t i=0;i<len.size();++i)out[i]=len[i]?reverse(next[len[i]]++,len[i]):0;return true;}
inline std::vector<uint8_t> pack(const std::vector<uint32_t>&differences){
 std::vector<uint8_t>out;if(differences.size()>UINT32_MAX)return out;uint64_t total=0;uint32_t max=0;bool constant=true;for(uint32_t x:differences){total+=x;max=std::max(max,x);if(x!=differences.front())constant=false;}if(total>UINT32_MAX)return out;
 Header h{MAGIC|3,uint32_t(differences.size()),uint32_t(total),differences.empty()?0:differences.front(),0};if(constant){out.resize(sizeof h);memcpy(out.data(),&h,sizeof h);return out;}
 // Always retain a varint fallback for exceptional wide values.
 std::vector<uint8_t>raw;for(uint32_t x:differences){while(x>=128){raw.push_back(128|(x&127));x>>=7;}raw.push_back(x);}if(raw.size()>UINT32_MAX)return {};h.magic=MAGIC|2;h.bits=uint32_t(raw.size());out.resize(sizeof h+raw.size());memcpy(out.data(),&h,sizeof h);memcpy(out.data()+sizeof h,raw.data(),raw.size());
 if(max>65535)return out;
 std::vector<uint64_t>freq(max+1);for(uint32_t x:differences)++freq[x];unsigned active=0;for(auto x:freq)active+=x!=0;if(active>32768)return out;unsigned limit=12;while((1u<<limit)<active)++limit;std::vector<uint8_t>lens(max+1);uint64_t floor=1;
 struct Node {uint64_t weight;int parent;};using Item=std::pair<uint64_t,uint32_t>;
 for(;;){std::vector<Node>nodes(max+1);std::priority_queue<Item,std::vector<Item>,std::greater<Item>>q;for(uint32_t x=0;x<=max;++x){nodes[x]={std::max(freq[x],floor),-1};if(freq[x])q.push({nodes[x].weight,x});}while(q.size()>1){auto a=q.top();q.pop();auto b=q.top();q.pop();uint32_t id=nodes.size();nodes[a.second].parent=id;nodes[b.second].parent=id;nodes.push_back({a.first+b.first,-1});q.push({a.first+b.first,id});}unsigned longest=0;for(uint32_t x=0;x<=max;++x)if(freq[x]){unsigned d=0;for(int k=x;nodes[k].parent>=0;k=nodes[k].parent)++d;lens[x]=d;longest=std::max(longest,d);}if(longest<=limit)break;floor*=2;}
 std::vector<uint32_t>code;if(!codes(lens,code))return out;uint64_t bits=0;for(uint32_t x=0;x<=max;++x)bits+=freq[x]*lens[x];if(bits>UINT32_MAX)return out;size_t dictbytes=(lens.size()+1)/2,bytes=(bits+7)/8,checkpointbytes=differences.size()>=1024?32:0;std::vector<uint8_t>hf(sizeof h+dictbytes+checkpointbytes+bytes+8,0);h.magic=MAGIC|(checkpointbytes?4:1);h.bits=bits;h.alphabet=max+1;memcpy(hf.data(),&h,sizeof h);uint8_t*p=hf.data()+sizeof h;for(size_t i=0;i<lens.size();++i)p[i/2]|=lens[i]<<((i&1)*4);p+=dictbytes;uint8_t*checkpoints=p;p+=checkpointbytes;uint64_t acc=0;unsigned used=0;uint32_t bitpos=0,prefix=0;size_t index=0,chunk=(differences.size()+3)/4;unsigned lane=1;for(uint32_t x:differences){if(checkpointbytes&&lane<4&&index==chunk*lane){memcpy(checkpoints+lane*8,&bitpos,4);memcpy(checkpoints+lane*8+4,&prefix,4);++lane;}acc|=uint64_t(code[x])<<used;used+=lens[x];bitpos+=lens[x];prefix+=x;++index;while(used>=8){*p++=acc;acc>>=8;used-=8;}}if(used)*p=acc;if(hf.size()<out.size())out.swap(hf);return out;
}
inline uint64_t load64(const uint8_t*p){uint64_t x;memcpy(&x,p,8);return x;}
inline bool unpack(const uint8_t*ptr,size_t n,uint32_t*offsets,size_t rows,uint64_t max_total){
 if(!ptr||!offsets||n<sizeof(Header))return false;Header h;memcpy(&h,ptr,sizeof h);if((h.magic&0xffffff00u)!=MAGIC||h.rows!=rows||h.total>max_total)return false;offsets[0]=0;const uint8_t*p=ptr+sizeof h;size_t avail=n-sizeof h;unsigned mode=h.magic&255;uint64_t total=0;
 if(mode==3){if(avail||uint64_t(h.bits)*rows!=h.total)return false;for(size_t i=0;i<rows;++i){total+=h.bits;offsets[i+1]=total;}return true;}
 if(mode==2){if(h.bits!=avail)return false;const uint8_t*end=p+avail;for(size_t i=0;i<rows;++i){uint64_t v=0;unsigned shift=0;for(;;){if(p==end||shift>28)return false;uint8_t b=*p++;v|=uint64_t(b&127)<<shift;if(!(b&128))break;shift+=7;}if(v>UINT32_MAX)return false;total+=v;if(total>h.total)return false;offsets[i+1]=total;}return p==end&&total==h.total;}
 if((mode!=1&&mode!=4)||!h.alphabet||h.alphabet>65536)return false;size_t dictbytes=(h.alphabet+1)/2,bytes=(uint64_t(h.bits)+7)/8;if(dictbytes+bytes+8+(mode==4?32:0)!=avail)return false;std::vector<uint8_t>lens(h.alphabet);unsigned rootbits=0;for(uint32_t i=0;i<h.alphabet;++i){lens[i]=(p[i/2]>>((i&1)*4))&15;rootbits=std::max(rootbits,unsigned(lens[i]));}rootbits=std::max(rootbits,12u);const uint32_t root_size=1u<<rootbits,root_mask=root_size-1;p+=dictbytes;uint32_t checkpoints[10]{};if(mode==4){if(rows<1024)return false;memcpy(checkpoints,p,32);p+=32;checkpoints[8]=h.bits;checkpoints[9]=h.total;if(checkpoints[0]||checkpoints[1])return false;for(unsigned k=0;k<4;++k)if(checkpoints[2*k]>checkpoints[2*k+2]||checkpoints[2*k+1]>checkpoints[2*k+3])return false;}std::vector<uint32_t>code;if(!codes(lens,code))return false;
 // Two short codes can be decoded together using one prefix lookup.
 std::vector<uint32_t>direct(root_size,0);for(uint32_t v=0;v<h.alphabet;++v){unsigned l=lens[v];if(!l)continue;uint32_t entry=(v<<5)|l;for(uint32_t j=code[v];j<root_size;j+=1u<<l)direct[j]=entry;}
 if(mode==4){
  size_t chunk=(rows+3)/4,last=rows-3*chunk;
  uint64_t b0=checkpoints[0],b1=checkpoints[2],b2=checkpoints[4],b3=checkpoints[6];
  uint64_t v0=checkpoints[1],v1=checkpoints[3],v2=checkpoints[5],v3=checkpoints[7];
  uint64_t e0=checkpoints[2],e1=checkpoints[4],e2=checkpoints[6],e3=checkpoints[8];
  uint32_t*o0=offsets+1,*o1=o0+chunk,*o2=o1+chunk,*o3=o2+chunk;
  size_t j=0;
  for(;j<last;++j){
   if(b0>=e0||b1>=e1||b2>=e2||b3>=e3)return false;
   unsigned m0=direct[(load64(p+(b0>>3))>>(b0&7))&root_mask];
   unsigned m1=direct[(load64(p+(b1>>3))>>(b1&7))&root_mask];
   unsigned m2=direct[(load64(p+(b2>>3))>>(b2&7))&root_mask];
   unsigned m3=direct[(load64(p+(b3>>3))>>(b3&7))&root_mask];
   b0+=m0&31;b1+=m1&31;b2+=m2&31;b3+=m3&31;
   v0+=m0>>5;v1+=m1>>5;v2+=m2>>5;v3+=m3>>5;
   o0[j]=v0;o1[j]=v1;o2[j]=v2;o3[j]=v3;
  }
  for(unsigned k=0;k<3;++k){uint64_t b=k==0?b0:k==1?b1:b2,v=k==0?v0:k==1?v1:v2;uint32_t*o=offsets+1+chunk*k;for(size_t t=j;t<chunk;++t){if(b>=checkpoints[2*k+2])return false;unsigned m=direct[(load64(p+(b>>3))>>(b&7))&root_mask];b+=m&31;v+=m>>5;o[t]=v;}if(b!=checkpoints[2*k+2]||v!=checkpoints[2*k+3])return false;}
  return b3==checkpoints[8]&&v3==checkpoints[9];
 }
 uint64_t bit=0;size_t i=0;
 if(uint64_t(h.bits)>rows*7||rows<256){while(i<rows){unsigned m=direct[(load64(p+(bit>>3))>>(bit&7))&root_mask],l=m&31;if(!l||bit+l>h.bits)return false;bit+=l;total+=m>>5;if(total>h.total)return false;offsets[++i]=total;}}
 else{
  std::vector<uint64_t>pairs(root_size);for(uint32_t ix=0;ix<root_size;++ix){uint32_t a=direct[ix],la=a&31;if(!la)continue;uint32_t b=direct[ix>>la],lb=b&31;uint64_t e=(a>>5)|(uint64_t(la)<<32)|(uint64_t(la)<<38);if(lb&&la+lb<=rootbits&&(a>>5)+(b>>5)<=65535)e=(a>>5)|(uint64_t((a>>5)+(b>>5))<<16)|(uint64_t(la+lb)<<32)|(1ULL<<37)|(uint64_t(la)<<38);pairs[ix]=e;}
  while(i<rows){uint64_t e=pairs[(load64(p+(bit>>3))>>(bit&7))&root_mask];unsigned l=(e>>32)&31;if(!l)return false;if((e&(1ULL<<37))&&i+1<rows&&bit+l<=h.bits){uint32_t first=total+(e&65535);total+=(e>>16)&65535;if(total>h.total)return false;offsets[i+1]=first;offsets[i+2]=total;i+=2;bit+=l;}else{l=(e>>38)&31;if(bit+l>h.bits)return false;bit+=l;total+=e&65535;if(total>h.total)return false;offsets[++i]=total;}}
 }
 return bit==h.bits&&total==h.total;
}
}
