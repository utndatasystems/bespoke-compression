#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <string>
#include <charconv>
#include <algorithm>
#ifdef ENCODER
#include <queue>
#endif

static inline uint32_t loc_r32(const uint8_t*p){uint32_t x;memcpy(&x,p,4);return x;}
static inline uint64_t loc_r64(const uint8_t*p){uint64_t x;memcpy(&x,p,8);return x;}
static inline uint64_t loc_low40(const uint8_t*p){return uint64_t(loc_r32(p))|(uint64_t(p[4])<<32);}
struct loc_entry{uint16_t pair;uint8_t nbits;uint8_t pad;};
struct loc_state{const uint8_t*data;const uint8_t*idx;const uint8_t*superidx;const uint8_t*low;const uint8_t*hi;uint32_t n,nullrow,maxbits,dictsize;uint64_t rawsize,b0,b1;std::vector<loc_entry> table;std::string t[4];};
static inline uint32_t loc_readbits(const loc_state*s,uint32_t pos){return uint32_t(loc_r64(s->hi+(pos>>3))>>(pos&7))&((1u<<s->maxbits)-1);}
static inline loc_entry loc_next(const loc_state*s,uint32_t &pos){loc_entry v=s->table[loc_readbits(s,pos)];pos+=v.nbits;return v;}
static inline size_t loc_format(const loc_state*s,uint32_t row,uint16_t pair,uint8_t*out){
 if(row==s->nullrow){memcpy(out,s->t[3].data(),s->t[3].size());return s->t[3].size();}
 const uint8_t*p=s->low+size_t(row)*10;uint64_t a=s->b0+loc_low40(p)+(uint64_t(pair&255)<<40),b=s->b1+loc_low40(p+5)+(uint64_t(pair>>8)<<40);double x,y;memcpy(&x,&a,8);memcpy(&y,&b,8);char*q=(char*)out;
 memcpy(q,s->t[0].data(),s->t[0].size());q+=s->t[0].size();q=std::to_chars(q,q+32,x).ptr;memcpy(q,s->t[1].data(),s->t[1].size());q+=s->t[1].size();q=std::to_chars(q,q+32,y).ptr;memcpy(q,s->t[2].data(),s->t[2].size());q+=s->t[2].size();return q-(char*)out;
}
static void* locations_open(const uint8_t*data,size_t size){
 if(!data||size<68||loc_r32(data)!=50||loc_r32(data+4)!=1)return nullptr;loc_state*s=new loc_state();s->data=data;s->n=loc_r32(data+8);s->nullrow=loc_r32(data+12);s->rawsize=loc_r64(data+16);s->b0=loc_r64(data+24);s->b1=loc_r64(data+32);s->dictsize=loc_r32(data+40);s->maxbits=loc_r32(data+44);
 if(!s->n||s->dictsize<2||s->dictsize>65536||s->maxbits>20||!s->maxbits||(s->nullrow!=~0u&&s->nullrow>=s->n)){delete s;return nullptr;}
 const uint8_t*p=data+48;uint8_t lens[4];memcpy(lens,p,4);p+=4;if(uint32_t(lens[0])+lens[1]+lens[2]>24||lens[3]>96){delete s;return nullptr;}
 size_t nblocks=(size_t(s->n)+31)/32,nsuper=(size_t(s->n)+8191)/8192;
 size_t need=52+size_t(lens[0])+lens[1]+lens[2]+lens[3]+size_t(s->dictsize)*3+nblocks*2+nsuper*4+size_t(s->n)*10+8;if(need>size){delete s;return nullptr;}
 for(int i=0;i<4;i++){s->t[i].assign((const char*)p,lens[i]);p+=lens[i];}
 std::vector<uint16_t>pair(s->dictsize);std::vector<uint8_t>bl(s->dictsize);uint32_t counts[21]={};for(uint32_t i=0;i<s->dictsize;i++){pair[i]=uint16_t(p[0])|(uint16_t(p[1])<<8);bl[i]=p[2];p+=3;if(!bl[i]||bl[i]>s->maxbits||(i&&pair[i]<=pair[i-1])){delete s;return nullptr;}counts[bl[i]]++;}
 uint32_t next[21]={},c=0;for(uint32_t b=1;b<=s->maxbits;b++){c=(c+counts[b-1])<<1;next[b]=c;if(c+counts[b]>(1u<<b)){delete s;return nullptr;}}if(c+counts[s->maxbits]!=(1u<<s->maxbits)){delete s;return nullptr;}
 s->table.resize(1u<<s->maxbits);for(uint32_t i=0;i<s->dictsize;i++){uint32_t len=bl[i],x=next[len]++,rev=0;for(uint32_t j=0;j<len;j++){rev=(rev<<1)|(x&1);x>>=1;}for(uint32_t z=rev;z<s->table.size();z+=(1u<<len))s->table[z]={pair[i],uint8_t(len),0};}
 s->superidx=p;p+=nsuper*4;s->idx=p;p+=nblocks*2;s->low=p;p+=size_t(s->n)*10;s->hi=p;
 uint64_t available=(size-size_t(p-data)-8)*8;uint32_t bit=0;
 for(uint32_t row=0;row<s->n;row++){
  if((row&31)==0){uint32_t b=row>>5;uint64_t indexed=uint64_t(loc_r32(s->superidx+size_t(b>>8)*4))+uint32_t(s->idx[size_t(b)*2])+(uint32_t(s->idx[size_t(b)*2+1])<<8);if(indexed!=bit){delete s;return nullptr;}}
  if(bit>=available){delete s;return nullptr;}loc_entry e=loc_next(s,bit);if(bit>available){delete s;return nullptr;}
 }
 if((uint64_t(bit)+7)/8!=(available+7)/8){delete s;return nullptr;}return s;
}
static int64_t locations_decode(void*v,uint8_t*out,size_t cap){const loc_state*s=(const loc_state*)v;if(!s||cap<s->rawsize)return -1;uint32_t bit=0;uint8_t*q=out;for(uint32_t row=0;row<s->n;row++){loc_entry e=loc_next(s,bit);if(size_t(q-out)+96<=cap){q+=loc_format(s,row,e.pair,q);}else{uint8_t tmp[96];size_t n=loc_format(s,row,e.pair,tmp);if(n>cap-size_t(q-out))return -1;memcpy(q,tmp,n);q+=n;}}return uint64_t(q-out)==s->rawsize?q-out:-1;}
static int64_t locations_rows(void*v,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets){const loc_state*s=(const loc_state*)v;if(!s)return -1;size_t total=0;offsets[0]=0;uint32_t block=~0u,row=0,bit=0;for(size_t i=0;i<count;i++){if(ids[i]>=s->n)return -1;uint32_t target=uint32_t(ids[i]),b=target>>5;if(b!=block||row>target){block=b;row=b<<5;bit=loc_r32(s->superidx+size_t(b>>8)*4)+uint32_t(s->idx[size_t(b)*2])+(uint32_t(s->idx[size_t(b)*2+1])<<8);}loc_entry e{};do{e=loc_next(s,bit);}while(row++<target);size_t n;if(cap-total>=96){n=loc_format(s,target,e.pair,out+total);}else{uint8_t tmp[96];n=loc_format(s,target,e.pair,tmp);if(n>cap-total)return -1;memcpy(out+total,tmp,n);}total+=n;offsets[i+1]=total;}return total;}
static void locations_close(void*v){delete (loc_state*)v;}
#ifdef ENCODER
static inline void loc_w32(std::vector<uint8_t>&v,uint32_t x){size_t p=v.size();v.resize(p+4);memcpy(v.data()+p,&x,4);}
static inline void loc_w64(std::vector<uint8_t>&v,uint64_t x){size_t p=v.size();v.resize(p+8);memcpy(v.data()+p,&x,8);}
static bool locations_encode(const uint8_t*raw,size_t size,std::vector<uint8_t>&out){
 std::vector<uint64_t>a,b;uint32_t nullrow=~0u;size_t pos=0;uint64_t base0=~0ull,base1=~0ull;std::string t[4]={"(",", ",")\n","NULL\n"};
 while(pos<size){size_t end=pos;while(end<size&&raw[end]!='\n')end++;if(end==size)return false;end++;uint32_t row=a.size();if(end-pos==5&&!memcmp(raw+pos,"NULL\n",5)){if(nullrow!=~0u)return false;nullrow=row;a.push_back(0);b.push_back(0);pos=end;continue;}if(raw[pos]!='('||end-pos<8||raw[end-2]!=')')return false;size_t comma=pos+1;while(comma<end&&raw[comma]!=',')comma++;if(comma+2>=end||raw[comma+1]!=' ')return false;
 double x,y;auto r0=std::from_chars((const char*)raw+pos+1,(const char*)raw+comma,x);auto r1=std::from_chars((const char*)raw+comma+2,(const char*)raw+end-2,y);if(r0.ec!=std::errc()||r1.ec!=std::errc()||r0.ptr!=(const char*)raw+comma||r1.ptr!=(const char*)raw+end-2)return false;char buf[96];auto f0=std::to_chars(buf,buf+96,x);if(size_t(f0.ptr-buf)!=comma-pos-1||memcmp(buf,raw+pos+1,f0.ptr-buf))return false;auto f1=std::to_chars(buf,buf+96,y);if(size_t(f1.ptr-buf)!=end-comma-4||memcmp(buf,raw+comma+2,f1.ptr-buf))return false;uint64_t u,w;memcpy(&u,&x,8);memcpy(&w,&y,8);a.push_back(u);b.push_back(w);base0=std::min(base0,u);base1=std::min(base1,w);pos=end;}
 if(a.empty())return false;uint32_t n=a.size();std::vector<uint32_t> freq(65536);std::vector<uint16_t>pairs(n);for(uint32_t i=0;i<n;i++){if(i==nullrow){a[i]=base0;b[i]=base1;}uint64_t x=a[i]-base0,y=b[i]-base1;if((x>>40)>255||(y>>40)>255)return false;pairs[i]=uint16_t((x>>40)|((y>>40)<<8));freq[pairs[i]]++;}
 struct Node{uint32_t count;int left,right,symbol;};std::vector<Node>nodes;using Item=std::pair<uint32_t,int>;std::priority_queue<Item,std::vector<Item>,std::greater<Item>>q;std::vector<uint16_t>dict;for(uint32_t i=0;i<65536;i++)if(freq[i]){dict.push_back(i);int k=nodes.size();nodes.push_back({freq[i],-1,-1,int(i)});q.push({freq[i],k});}if(q.size()<2)return false;while(q.size()>1){Item x=q.top();q.pop();Item y=q.top();q.pop();int k=nodes.size();nodes.push_back({x.first+y.first,x.second,y.second,-1});q.push({x.first+y.first,k});}
 std::vector<uint8_t> bl(65536);std::vector<std::pair<int,int>>stack;stack.push_back({q.top().second,0});uint32_t maxbits=0;while(!stack.empty()){auto z=stack.back();stack.pop_back();Node&nd=nodes[z.first];if(nd.symbol>=0){bl[nd.symbol]=z.second;maxbits=std::max(maxbits,uint32_t(z.second));}else{stack.push_back({nd.left,z.second+1});stack.push_back({nd.right,z.second+1});}}if(maxbits>20)return false;
 uint32_t counts[21]={},next[21]={},code=0;for(uint16_t p:dict)counts[bl[p]]++;for(uint32_t j=1;j<=maxbits;j++){code=(code+counts[j-1])<<1;next[j]=code;}std::vector<uint32_t>codes(65536);for(uint16_t p:dict){uint32_t x=next[bl[p]]++,rev=0;for(uint32_t j=0;j<bl[p];j++){rev=(rev<<1)|(x&1);x>>=1;}codes[p]=rev;}
 out.clear();loc_w32(out,50);loc_w32(out,1);loc_w32(out,n);loc_w32(out,nullrow);loc_w64(out,size);loc_w64(out,base0);loc_w64(out,base1);loc_w32(out,dict.size());loc_w32(out,maxbits);for(int i=0;i<4;i++)out.push_back(uint8_t(t[i].size()));for(int i=0;i<4;i++)out.insert(out.end(),t[i].begin(),t[i].end());for(uint16_t p:dict){out.push_back(p&255);out.push_back(p>>8);out.push_back(bl[p]);}
 uint32_t nblocks=(n+31)/32,nsuper=(n+8191)/8192;size_t superoff=out.size();out.resize(out.size()+size_t(nsuper)*4);size_t idxoff=out.size();out.resize(out.size()+size_t(nblocks)*2);for(uint32_t i=0;i<n;i++){uint64_t x=a[i]-base0,y=b[i]-base1;for(int j=0;j<5;j++)out.push_back(uint8_t(x>>(j*8)));for(int j=0;j<5;j++)out.push_back(uint8_t(y>>(j*8)));}
 size_t stream=out.size();uint32_t bits=0,superbase=0;for(uint32_t i=0;i<n;i++){if((i&8191)==0){superbase=bits;memcpy(out.data()+superoff+size_t(i>>13)*4,&bits,4);}if((i&31)==0){uint32_t rel=bits-superbase;if(rel>65535)return false;out[idxoff+size_t(i>>5)*2]=uint8_t(rel);out[idxoff+size_t(i>>5)*2+1]=uint8_t(rel>>8);}uint16_t p=pairs[i];size_t needed=stream+(bits+bl[p]+7)/8+8;if(out.size()<needed)out.resize(needed);uint64_t shifted=uint64_t(codes[p])<<(bits&7);size_t at=stream+(bits>>3);for(int j=0;j<4;j++)out[at+j]|=uint8_t(shifted>>(j*8));bits+=bl[p];}out.resize(stream+(bits+7)/8+8);return true;
}
#endif
