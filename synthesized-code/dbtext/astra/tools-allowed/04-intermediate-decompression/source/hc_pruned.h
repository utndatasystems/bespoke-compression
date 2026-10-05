#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <algorithm>
#include <zstd.h>
#include <immintrin.h>
struct HCH {uint32_t magic,n,raw,k,ds,ixs,ts,bits,contexts;};
struct HCS {HCH h;const uint8_t*tok;std::vector<uint32_t>off,table;std::vector<uint16_t>rlen;std::vector<uint8_t>dict,len;uint8_t init;bool fast_rows=true;};
static unsigned hc_rev(unsigned x,unsigned n){x=((x&0x5555)<<1)|((x>>1)&0x5555);x=((x&0x3333)<<2)|((x>>2)&0x3333);x=((x&0x0f0f)<<4)|((x>>4)&0x0f0f);x=(x<<8)|(x>>8);return(x&65535)>>(16-n);}
static bool hc_open(const uint8_t*a,size_t z,HCS&s){
 s.fast_rows=true;if(z<sizeof(HCH))return false;memcpy(&s.h,a,sizeof(HCH));auto&h=s.h;if(h.magic!=0x32584348||!h.k||h.k>16384||h.n>10000000||!h.contexts||h.contexts>8||(uint64_t)sizeof(HCH)+h.ds+h.ixs+h.ts+16!=z||(h.bits+7ull)/8!=h.ts)return false;
 unsigned C=h.contexts;std::vector<uint8_t>d(256+size_t(h.k)*(33+(C+1)/2));size_t dz=ZSTD_decompress(d.data(),d.size(),a+sizeof(HCH),h.ds);if(ZSTD_isError(dz)||dz<256)return false;const uint8_t*ctx=d.data();for(unsigned i=0;i<256;i++)if(ctx[i]>=C)return false;s.init=ctx[10];
 s.dict.clear();s.dict.reserve(size_t(h.k)*32+32);std::vector<uint32_t>dict_off(h.k);s.len.resize(h.k);std::vector<uint8_t>nb(size_t(C)*h.k);std::vector<unsigned>counts(C*16),codes(C*16);size_t at=256;
 for(unsigned i=0;i<h.k;i++){if(at+1+(C+1)/2>dz)return false;unsigned len=d[at++];if(!len||len>32)return false;s.len[i]=len;for(unsigned c=0;c<C;c++){unsigned b=(d[at+c/2]>>(4*(c%2)))&15;if(b>14)return false;nb[c*h.k+i]=b;if(b)counts[c*16+b]++;}at+=(C+1)/2;if(at+len>dz)return false;dict_off[i]=s.dict.size();s.dict.insert(s.dict.end(),d.data()+at,d.data()+at+len);at+=len;}if(at!=dz)return false;s.dict.resize(s.dict.size()+32,0);
 s.table.assign(C*16384,0);for(unsigned c=0;c<C;c++){unsigned code=0;for(unsigned b=1;b<=14;b++){code=(code+counts[c*16+b-1])*2;codes[c*16+b]=code;if(code+counts[c*16+b]>(1u<<b))return false;}}
 for(unsigned c=0;c<C;c++)for(unsigned i=0;i<h.k;i++){unsigned b=nb[c*h.k+i];if(!b)continue;unsigned r=hc_rev(codes[c*16+b]++,b),next=ctx[s.dict[dict_off[i]+s.len[i]-1]],v=dict_off[i]|(s.len[i]<<19)|(b<<25)|(next<<29);for(unsigned x=r;x<16384;x+=1u<<b)s.table[c*16384+x]=v;}
 std::vector<uint8_t>ix(size_t(h.n)*2);size_t iz=ZSTD_decompress(ix.data(),ix.size(),a+sizeof(HCH)+h.ds,h.ixs);if(ZSTD_isError(iz)||iz!=ix.size())return false;s.off.resize(size_t(h.n)+1);s.rlen.resize(h.n);s.tok=a+sizeof(HCH)+h.ds+h.ixs;uint64_t co=0,ro=0;
 for(unsigned i=0;i<h.n;i++){s.off[i]=co;co+=ix[i]|(unsigned(ix[i+h.n])<<8);if(co>h.bits)return false;}s.off[h.n]=co;if(co!=h.bits)return false;
 if(false){for(unsigned i=0;i<h.n;i++){unsigned p=s.off[i],end=s.off[i+1],len=0,ctx=s.init;while(p<end){uint32_t bits;memcpy(&bits,s.tok+(p>>3),4);unsigned v=s.table[ctx*16384+((bits>>(p&7))&16383)],b=(v>>25)&15;if(!b||b>end-p)return false;len+=(v>>19)&63;p+=b;ctx=v>>29;}if(len>65535)return false;s.rlen[i]=len;if(!len||len>16384)s.fast_rows=false;ro+=len;}return ro==h.raw;}
 struct Check {unsigned p,end,sz,row,ctx;};Check x[4];unsigned next=0,done=0;
 for(int k=0;k<4;k++){unsigned i=next++;x[k]={i<h.n?s.off[i]:0,i<h.n?s.off[i+1]:0,0,i,s.init};}
 auto step=[&](Check&x)->bool{if(x.row>=h.n)return true;if(x.p<x.end){uint32_t bits;memcpy(&bits,s.tok+(x.p>>3),4);unsigned v=s.table[x.ctx*16384+((bits>>(x.p&7))&16383)],nb=(v>>25)&15;if(!nb||nb>x.end-x.p)return false;x.p+=nb;x.sz+=(v>>19)&63;x.ctx=v>>29;if(x.sz>65535)return false;}
  if(x.p==x.end){s.rlen[x.row]=x.sz;if(!x.sz||x.sz>16384)s.fast_rows=false;ro+=x.sz;done++;unsigned i=next++;x={i<h.n?s.off[i]:0,i<h.n?s.off[i+1]:0,0,i,s.init};}return true;};
 while(done<h.n)if(!step(x[0])||!step(x[1])||!step(x[2])||!step(x[3]))return false;return ro==h.raw;
}
static inline uint8_t*hc_expand(const HCS&s,unsigned p,unsigned end,uint8_t*out){const uint8_t*d=s.dict.data();const uint32_t*table=s.table.data();unsigned ctx=s.init;while(p<end){uint64_t bits;memcpy(&bits,s.tok+(p>>3),8);bits>>=p&7;for(int j=0;j<3&&p<end;j++){unsigned v=table[ctx*16384+(bits&16383)],b=(v>>25)&15;_mm256_storeu_si256((__m256i*)out,_mm256_loadu_si256((const __m256i*)(d+(v&524287))));out+=(v>>19)&63;p+=b;bits>>=b;ctx=v>>29;}}return out;}
static int64_t hc_decode_scalar(HCS&s,uint8_t*out,size_t cap){if(cap<s.h.raw)return -1;size_t pos=0;uint8_t tmp[65568];for(unsigned i=0;i<s.h.n;i++){size_t len=s.rlen[i];if(pos+len+32<=cap)hc_expand(s,s.off[i],s.off[i+1],out+pos);else{hc_expand(s,s.off[i],s.off[i+1],tmp);memcpy(out+pos,tmp,len);}pos+=len;}return pos;}
static int64_t hc_rows_scalar(HCS&s,const uint64_t*ids,size_t n,uint8_t*out,size_t cap,uint64_t*offsets){size_t pos=0;offsets[0]=0;uint8_t tmp[65568];for(size_t j=0;j<n;j++){uint64_t id=ids[j];if(id>=s.h.n)return -1;size_t len=s.rlen[id];if(len>cap-pos)return -1;if(pos+len+32<=cap)hc_expand(s,s.off[id],s.off[id+1],out+pos);else{hc_expand(s,s.off[id],s.off[id+1],tmp);memcpy(out+pos,tmp,len);}pos+=len;offsets[j+1]=pos;}return pos;}
#ifndef HC_FAST_LANES
#define HC_FAST_LANES 4
#endif
#ifndef HC_FAST_MIN_BITS
#ifdef __AVX512VL__
#define HC_FAST_MIN_BITS 64
#else
#define HC_FAST_MIN_BITS 128
#endif
#endif
struct HCRun {const uint8_t*src;uint64_t bits;uint8_t*dst;unsigned have,left,ctx;
#ifndef __AVX512VL__
 uint8_t*target;
#endif
};
static inline void hc_init_run(const HCS&s,unsigned id,uint8_t*out,uint8_t*buf,HCRun&x,unsigned length){unsigned p=s.off[id];x.src=s.tok+(p>>3);memcpy(&x.bits,x.src,8);x.bits>>=p&7;x.have=64-(p&7);x.left=length;x.ctx=s.init;
#ifdef __AVX512VL__
 x.dst=out;
#else
 x.dst=buf;x.target=out;
#endif
}
static inline bool hc_assign(const HCS&s,HCRun&x,uint8_t*buf,uint8_t*out,size_t cap,const uint64_t*ids,uint64_t*offs,unsigned row,size_t&pos){
 uint64_t id=ids[row];if(id>=s.h.n)return false;unsigned len=s.rlen[id];if(len>cap-pos)return false;
 hc_init_run(s,id,out+pos,buf,x,len);pos+=len;offs[row+1]=pos;return true;
}
static inline void hc_fast_step(const HCS&s,HCRun&x,uint8_t*buf,uint8_t*out,size_t cap,const uint64_t*ids,uint64_t*offs,size_t n,unsigned&next,unsigned&done,size_t&pos,bool&bad){
 if(!x.left)return;
 if(x.have<14){unsigned used=64-x.have;x.src+=used>>3;memcpy(&x.bits,x.src,8);x.bits>>=used&7;x.have=64-(used&7);}
 unsigned v=s.table[x.ctx*16384+(x.bits&16383)],b=(v>>25)&15,len=(v>>19)&63;
#ifdef __AVX512VL__
 _mm256_mask_storeu_epi8(x.dst,(__mmask32)((1ull<<len)-1),_mm256_loadu_si256((const __m256i*)(s.dict.data()+(v&524287))));
#else
 _mm256_storeu_si256((__m256i*)x.dst,_mm256_loadu_si256((const __m256i*)(s.dict.data()+(v&524287))));
#endif
 x.dst+=len;x.left-=len;x.bits>>=b;x.have-=b;x.ctx=v>>29;
 if(!x.left){
#ifndef __AVX512VL__
 memcpy(x.target,buf,x.dst-buf);
#endif
 done++;if(next<n){unsigned row=next++;if(!hc_assign(s,x,buf,out,cap,ids,offs,row,pos)){bad=true;next=n;done=n;}}
 }
}
static int64_t hc_rows(HCS&s,const uint64_t*ids,size_t n,uint8_t*out,size_t cap,uint64_t*offs){
 if(s.h.bits<s.h.n*(uint64_t)HC_FAST_MIN_BITS||n<4||!s.fast_rows)return hc_rows_scalar(s,ids,n,out,cap,offs);
 size_t pos=0;offs[0]=0;alignas(64)uint8_t buf[4][16416];HCRun x[4];
 for(unsigned k=0;k<4;k++)if(!hc_assign(s,x[k],buf[k],out,cap,ids,offs,k,pos))return -1;
 unsigned next=4,done=0;bool bad=false;
 while(done<n){
  hc_fast_step(s,x[0],buf[0],out,cap,ids,offs,n,next,done,pos,bad);
  hc_fast_step(s,x[1],buf[1],out,cap,ids,offs,n,next,done,pos,bad);
  hc_fast_step(s,x[2],buf[2],out,cap,ids,offs,n,next,done,pos,bad);
  hc_fast_step(s,x[3],buf[3],out,cap,ids,offs,n,next,done,pos,bad);
 }
 return bad?-1:pos;
}
static int64_t hc_decode(HCS&s,uint8_t*out,size_t cap){
 if(cap<s.h.raw)return -1;std::vector<uint64_t> ids(s.h.n), offsets(size_t(s.h.n)+1);for(unsigned i=0;i<s.h.n;i++)ids[i]=i;return hc_rows(s,ids.data(),ids.size(),out,cap,offsets.data());
}
#ifdef ENCODER
#include <queue>
#include <cmath>
#include <cstdio>
#include <unordered_map>
struct HCCluster {std::vector<std::pair<unsigned,unsigned>>freq;std::vector<uint8_t>members;uint64_t n=0;bool active=true;double entropy=0;};
// Requires the parent BPE header to be included first, defining PS.
static bool hc_transform(const PS&s,std::vector<uint8_t>&out,unsigned C=4,const uint8_t*raw=nullptr,size_t rawsize=0,unsigned dp_passes=3){
 if(C<1||C>8||s.h.k>16384||(raw&&rawsize!=s.h.raw))return false;unsigned K=s.h.k;
 std::vector<uint32_t>freq(size_t(K)*256);std::vector<std::vector<uint16_t>>rows(s.h.n);unsigned N=0;
 for(unsigned r=0;r<s.h.n;r++){unsigned p=s.off[r],ctx=10;while(p<s.off[r+1]){uint32_t bits;memcpy(&bits,s.tok+(p>>3),4);unsigned v=s.table[(bits>>(p&7))&s.mask],b=v>>25,id=(v&524287)>>5;freq[ctx*K+id]++;N++;rows[r].push_back(id);ctx=s.dict[id*32+s.len[id]-1];p+=b;}}
 std::vector<double>lg(N+1);for(unsigned i=1;i<=N;i++)lg[i]=i*log2(i);std::vector<HCCluster>nodes;
 for(unsigned j=0;j<256;j++){HCCluster c;c.members.push_back(j);for(unsigned i=0;i<K;i++)if(freq[j*K+i]){unsigned n=freq[j*K+i];c.freq.emplace_back(i,n);c.n+=n;c.entropy-=lg[n];}if(c.n){c.entropy+=lg[c.n];nodes.push_back(std::move(c));}}
 auto cost=[&](const HCCluster&a,const HCCluster&b){double co=lg[a.n+b.n]-lg[a.n]-lg[b.n];size_t i=0,j=0;while(i<a.freq.size()&&j<b.freq.size()){auto[x,n]=a.freq[i];auto[y,m]=b.freq[j];if(x<y)i++;else if(y<x)j++;else{co-=lg[n+m]-lg[n]-lg[m];i++;j++;}}return co;};
 using Q=std::pair<double,std::pair<unsigned,unsigned>>;std::priority_queue<Q,std::vector<Q>,std::greater<Q>>pq;
 for(unsigned i=0;i<nodes.size();i++)for(unsigned j=0;j<i;j++)pq.push({cost(nodes[i],nodes[j]),{i,j}});
 unsigned active=nodes.size();if(active<C)C=active;
 while(active>C){while(!pq.empty()&&(!nodes[pq.top().second.first].active||!nodes[pq.top().second.second].active))pq.pop();auto q=pq.top();pq.pop();unsigned x=q.second.first,y=q.second.second;HCCluster c;auto&a=nodes[x];auto&b=nodes[y];c.n=a.n+b.n;c.entropy=a.entropy+b.entropy+q.first;c.members=a.members;c.members.insert(c.members.end(),b.members.begin(),b.members.end());size_t i=0,j=0;while(i<a.freq.size()||j<b.freq.size()){if(j==b.freq.size()||(i<a.freq.size()&&a.freq[i].first<b.freq[j].first))c.freq.push_back(a.freq[i++]);else if(i==a.freq.size()||b.freq[j].first<a.freq[i].first)c.freq.push_back(b.freq[j++]);else{c.freq.emplace_back(a.freq[i].first,a.freq[i].second+b.freq[j].second);i++;j++;}}a.active=b.active=false;unsigned id=nodes.size();nodes.push_back(std::move(c));for(unsigned j=0;j<id;j++)if(nodes[j].active)pq.push({cost(nodes[id],nodes[j]),{id,j}});active--;}
 std::vector<uint8_t>ctx(256);unsigned ci=0;for(auto&n:nodes)if(n.active){for(auto b:n.members)ctx[b]=ci;ci++;}
 std::vector<unsigned>order(K),remap(K);for(unsigned i=0;i<K;i++)order[i]=i;std::sort(order.begin(),order.end(),[&](unsigned a,unsigned b){return std::lexicographical_compare(s.dict.data()+a*32,s.dict.data()+a*32+s.len[a],s.dict.data()+b*32,s.dict.data()+b*32+s.len[b]);});for(unsigned i=0;i<K;i++)remap[order[i]]=i;
 std::vector<uint32_t>cf(C*K);for(unsigned b=0;b<256;b++)for(unsigned i=0;i<K;i++)cf[ctx[b]*K+remap[i]]+=freq[b*K+i];
 std::vector<unsigned>nb(C*K),code(C*K);
 // Only encoder fitting uses raw bytes. All resulting codes and context maps
 // are serialized in this archive; decoding depends on none of this input.
 struct DPTrie {int sym=-1;};std::vector<DPTrie>trie(1);
 std::unordered_map<uint64_t,int>edge;
 if(raw){edge.reserve(K*4);for(unsigned v=0;v<K;v++){int st=0;for(unsigned j=0;j<s.len[v];j++){uint64_t key=(uint64_t(st)<<8)|s.dict[v*32+j];auto it=edge.find(key);if(it==edge.end()){int ns=trie.size();trie.emplace_back();edge.emplace(key,ns);st=ns;}else st=it->second;}trie[st].sym=v;}}
 for(unsigned pass=0;;pass++){
 std::fill(nb.begin(),nb.end(),0);std::fill(code.begin(),code.end(),0);
 for(unsigned c=0;c<C;c++){
  struct HN {uint64_t freq;int left,right;};std::vector<HN>hn;std::vector<unsigned>ids;for(unsigned i=0;i<K;i++)if(cf[c*K+i]){hn.push_back({cf[c*K+i],-1,-1});ids.push_back(i);}unsigned n=ids.size();std::vector<unsigned>leaves(n);for(unsigned i=0;i<n;i++)leaves[i]=i;std::sort(leaves.begin(),leaves.end(),[&](unsigned a,unsigned b){return hn[a].freq!=hn[b].freq?hn[a].freq<hn[b].freq:a<b;});std::vector<unsigned>len(n);
  if(n==1)len[0]=1;else if(n){std::vector<unsigned>level=leaves;for(unsigned depth=1;depth<14;depth++){std::vector<unsigned>pk;for(size_t j=0;j+1<level.size();j+=2){unsigned a=level[j],b=level[j+1];pk.push_back(hn.size());hn.push_back({hn[a].freq+hn[b].freq,(int)a,(int)b});}std::vector<unsigned>merged;size_t i=0,j=0;while(i<leaves.size()&&j<pk.size())if(hn[leaves[i]].freq<=hn[pk[j]].freq)merged.push_back(leaves[i++]);else merged.push_back(pk[j++]);merged.insert(merged.end(),leaves.begin()+i,leaves.end());merged.insert(merged.end(),pk.begin()+j,pk.end());level.swap(merged);}if(level.size()<2*n-2)return false;std::vector<unsigned>stack(level.begin(),level.begin()+2*n-2);while(!stack.empty()){unsigned v=stack.back();stack.pop_back();if(v<n)len[v]++;else{stack.push_back(hn[v].left);stack.push_back(hn[v].right);}}}
  unsigned cnt[16]={},codes[16]={};for(unsigned j=0;j<n;j++){if(!len[j]||len[j]>14)return false;nb[c*K+ids[j]]=len[j];cnt[len[j]]++;}unsigned cc=0;for(unsigned b=1;b<=14;b++){cc=(cc+cnt[b-1])*2;codes[b]=cc;}for(unsigned i=0;i<K;i++)if(nb[c*K+i])code[c*K+i]=hc_rev(codes[nb[c*K+i]]++,nb[c*K+i]);
 }
 if(!raw||pass==dp_passes)break;
 size_t rowstart=0;std::vector<uint32_t>cost;std::vector<uint16_t>best;
 for(unsigned r=0;r<s.h.n;r++){
  unsigned n=s.rlen[r];cost.assign(n+1,0x3fffffff);best.resize(n);cost[n]=0;
  for(int i=n-1;i>=0;i--){unsigned cx=ctx[i?raw[rowstart+i-1]:10];int state=0;
   for(unsigned j=i;j<n&&j<unsigned(i)+32;j++){
    auto it=edge.find((uint64_t(state)<<8)|raw[rowstart+j]);if(it==edge.end())break;state=it->second;int old=trie[state].sym;
    if(old>=0){unsigned bits=nb[cx*K+remap[old]];if(!bits)bits=14;
     if(bits+cost[j+1]<cost[i]){cost[i]=bits+cost[j+1];best[i]=old;}
    }
   }
  }
  if(cost[0]==0x3fffffff)return false;
  rows[r].clear();for(unsigned i=0;i<n;){unsigned v=best[i];rows[r].push_back(v);i+=s.len[v];}rowstart+=n;
 }
 if(rowstart!=rawsize)return false;
 std::fill(cf.begin(),cf.end(),0);
 for(auto&row:rows){unsigned cx=ctx[10];for(unsigned old:row){cf[cx*K+remap[old]]++;cx=ctx[s.dict[old*32+s.len[old]-1]];}}
 }
 unsigned activeK=0;std::vector<uint8_t>d=ctx;for(unsigned i=0;i<K;i++){bool active=false;for(unsigned c=0;c<C;c++)active|=nb[c*K+i]!=0;if(!active)continue;activeK++;unsigned old=order[i];d.push_back(s.len[old]);for(unsigned c=0;c<C;c+=2)d.push_back(nb[c*K+i]|(c+1<C?nb[(c+1)*K+i]<<4:0));d.insert(d.end(),s.dict.begin()+old*32,s.dict.begin()+old*32+s.len[old]);}
 std::vector<uint8_t>tok;std::vector<uint16_t>ix;uint64_t buf=0;unsigned used=0,totalbits=0;
 for(auto&row:rows){unsigned start=totalbits,c=ctx[10];for(unsigned old:row){unsigned id=remap[old],n=nb[c*K+id];if(!n)return false;buf|=uint64_t(code[c*K+id])<<used;used+=n;totalbits+=n;while(used>=8){tok.push_back(buf);buf>>=8;used-=8;}c=ctx[s.dict[old*32+s.len[old]-1]];}if(totalbits-start>65535)return false;ix.push_back(totalbits-start);}if(used)tok.push_back(buf);
 std::vector<uint8_t>ixpacked(ix.size()*2);for(size_t i=0;i<ix.size();i++){ixpacked[i]=ix[i];ixpacked[i+ix.size()]=ix[i]>>8;}
 std::vector<uint8_t>dc(ZSTD_compressBound(d.size())),ic(ZSTD_compressBound(ixpacked.size()));size_t dz=ZSTD_compress(dc.data(),dc.size(),d.data(),d.size(),19),iz=ZSTD_compress(ic.data(),ic.size(),ixpacked.data(),ixpacked.size(),19);if(ZSTD_isError(dz)||ZSTD_isError(iz))return false;
 HCH h={0x32584348,s.h.n,s.h.raw,activeK,(uint32_t)dz,(uint32_t)iz,(uint32_t)tok.size(),totalbits,C};fprintf(stderr,"PRUNE K=%u active=%u omitted=%u\n",K,activeK,K-activeK);out.resize(sizeof(h)+dz+iz+tok.size()+16);memcpy(out.data(),&h,sizeof(h));memcpy(out.data()+sizeof(h),dc.data(),dz);memcpy(out.data()+sizeof(h)+dz,ic.data(),iz);memcpy(out.data()+sizeof(h)+dz+iz,tok.data(),tok.size());return true;
}
#endif
