#pragma once
#include "ppmd_codec.h"
#include "entropy_plan.h"
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <limits>
#include <zstd.h>
#include <bzlib.h>
#include <lzma.h>
#include <brotli/decode.h>
#ifdef ENCODER
#include <brotli/encode.h>
#endif

namespace FC {
static const uint32_t MAGIC=0x33434647;
struct Header { uint32_t magic,n; uint64_t raw,coderaw,codecomp,mapcomp; uint32_t bits,codec; };
struct Node { uint32_t off,pre,len,parent; };
struct State { Header h;std::vector<uint8_t> code;std::vector<uint32_t> rank;std::vector<Node> node; };
struct Fenwick { std::vector<uint32_t>b;uint32_t n;Fenwick(uint32_t n):b(n+1),n(n){for(uint32_t i=1;i<=n;++i)b[i]=i&-i;}uint32_t sum(uint32_t i){uint32_t r=0;for(;i;i-=i&-i)r+=b[i];return r;}void remove(uint32_t i){for(++i;i<=n;i+=i&-i)--b[i];}uint32_t kth(uint32_t k){uint32_t p=0;uint32_t step=1;while(step<=n/2)step<<=1;for(;step;step>>=1){uint32_t j=p+step;if(j<=n&&b[j]<=k){p=j;k-=b[j];}}return p;}};
struct BitOut {std::vector<uint8_t>b;unsigned nb=0;void bit(unsigned v){if(!nb)b.push_back(0);b.back()|=v<<(7-nb);nb=(nb+1)&7;}};
struct BitIn {const uint8_t*p;size_t n,at=0;unsigned bit(){unsigned v=at/8<n?((p[at/8]>>(7-at%8))&1):0;++at;return v;}};
#ifdef ENCODER
static std::vector<uint8_t> permutation_encode(const std::vector<uint32_t>&r,uint32_t universe=0){if(!universe)universe=r.size();if(r.size()>universe)return {};Fenwick f(universe);BitOut out;uint32_t lo=0,hi=UINT32_MAX;uint64_t pending=0;auto emit=[&](unsigned b){out.bit(b);while(pending){out.bit(1-b);--pending;}};
 for(uint32_t i=0;i<r.size();++i){uint32_t total=universe-i,k=f.sum(r[i]);f.remove(r[i]);uint64_t range=uint64_t(hi)-lo+1;hi=lo+range*(k+1)/total-1;lo=lo+range*k/total;
  for(;;){if(hi<0x80000000u)emit(0);else if(lo>=0x80000000u){emit(1);lo-=0x80000000u;hi-=0x80000000u;}else if(lo>=0x40000000u&&hi<0xc0000000u){++pending;lo-=0x40000000u;hi-=0x40000000u;}else break;lo<<=1;hi=(hi<<1)|1;}
 }++pending;emit(lo<0x40000000u?0:1);return out.b;
}
#endif
static bool permutation_decode(const uint8_t*p,size_t n,std::vector<uint32_t>&r,uint32_t universe=0){if(!universe)universe=r.size();if(r.size()>universe)return false;Fenwick f(universe);BitIn in{p,n};uint32_t lo=0,hi=UINT32_MAX,code=0;for(unsigned i=0;i<32;++i)code=(code<<1)|in.bit();
 for(uint32_t i=0;i<r.size();++i){uint32_t total=universe-i;uint64_t range=uint64_t(hi)-lo+1;uint32_t k=((uint64_t(code)-lo+1)*total-1)/range;if(code<lo||code>hi||k>=total)return false;r[i]=f.kth(k);if(r[i]>=universe)return false;f.remove(r[i]);hi=lo+range*(k+1)/total-1;lo=lo+range*k/total;
  for(;;){if(hi<0x80000000u){}else if(lo>=0x80000000u){lo-=0x80000000u;hi-=0x80000000u;code-=0x80000000u;}else if(lo>=0x40000000u&&hi<0xc0000000u){lo-=0x40000000u;hi-=0x40000000u;code-=0x40000000u;}else break;lo<<=1;hi=(hi<<1)|1;code=(code<<1)|in.bit();}
 }return in.at<=n*8+32;
}
static bool getvi(const uint8_t*&p,const uint8_t*e,uint32_t&v){v=0;for(unsigned b=0;b<35;b+=7){if(p==e)return false;unsigned c=*p++;if(b==28&&c>15)return false;v|=(c&127)<<b;if(!(c&128))return true;}return false;}
#ifdef ENCODER
static void putvi(std::vector<uint8_t>&v,uint32_t x){while(x>=128){v.push_back(uint8_t(x)|128);x>>=7;}v.push_back(x);}
static std::vector<uint8_t> compress(const std::vector<uint8_t>&d,unsigned&codec){
 if(EP::fc.codec>=0){codec=EP::fc.codec;return EP::compress(d,EP::fc);}
 std::vector<uint8_t> z(ZSTD_compressBound(d.size()));size_t n=ZSTD_compress(z.data(),z.size(),d.data(),d.size(),19);if(ZSTD_isError(n))z.clear();else z.resize(n);codec=0;
 std::vector<uint8_t> br(BrotliEncoderMaxCompressedSize(d.size()));size_t bn=br.size();if(BrotliEncoderCompress(11,BROTLI_DEFAULT_WINDOW,BROTLI_MODE_GENERIC,d.size(),d.data(),&bn,br.data())&&(z.empty()||bn<z.size())){br.resize(bn);codec=1;z=std::move(br);}
 std::vector<uint8_t> bz(d.size()+d.size()/100+601);for(int level=1;level<=9;++level){unsigned bzn=bz.size();if(BZ2_bzBuffToBuffCompress((char*)bz.data(),&bzn,(char*)d.data(),d.size(),level,0,30)==BZ_OK&&(z.empty()||bzn<z.size())){codec=3;z.assign(bz.begin(),bz.begin()+bzn);}}
 std::vector<uint8_t> xz(lzma_stream_buffer_bound(d.size()));size_t xzn=0;if(lzma_easy_buffer_encode(9,LZMA_CHECK_CRC32,nullptr,d.data(),d.size(),xz.data(),&xzn,xz.size())==LZMA_OK&&(z.empty()||xzn<z.size())){xz.resize(xzn);codec=4;z=std::move(xz);}
 auto pp=PP::encode(d.data(),d.size(),4,24);if(!pp.empty()&&(z.empty()||pp.size()<z.size())){z=std::move(pp);codec=5;}return z;
}
static bool encode_direction(const uint8_t*raw,size_t size,std::vector<uint8_t>&out,unsigned transform){
 if(size>16u*1024*1024)return false;
 struct Row{uint32_t off,len;};std::vector<Row> rows;uint32_t start=0;
 for(uint32_t i=0;i<size;++i)if(raw[i]=='\n'){rows.push_back({start,i+1-start});start=i+1;}
 if(start<size)rows.push_back({start,uint32_t(size-start)});
 if(rows.size()>1000000)return false;std::vector<uint8_t>reversed;if(transform){reversed.assign(raw,raw+size);for(const Row&r:rows)std::reverse(reversed.begin()+r.off,reversed.begin()+r.off+r.len);raw=reversed.data();}uint32_t n=rows.size();std::vector<uint32_t> order(n),rank(n);for(uint32_t i=0;i<n;++i)order[i]=i;
 std::sort(order.begin(),order.end(),[&](uint32_t a,uint32_t b){const Row&A=rows[a],&B=rows[b];int c=memcmp(raw+A.off,raw+B.off,std::min(A.len,B.len));return c?c<0:A.len<B.len;});
 std::vector<uint8_t> code;code.reserve(size);Row prev{0,0};
 for(uint32_t i=0;i<n;++i){rank[order[i]]=i;Row r=rows[order[i]];uint32_t p=0;while(p<std::min(r.len,prev.len)&&raw[r.off+p]==raw[prev.off+p])++p;putvi(code,p);putvi(code,r.len-p);code.insert(code.end(),raw+r.off+p,raw+r.off+r.len);prev=r;}
 unsigned bits=0;while((uint64_t(1)<<bits)<n)++bits;std::vector<uint8_t> map((uint64_t(n)*bits+7)/8,0);uint64_t val=0;unsigned nb=0;size_t w=0;
 for(uint32_t r:rank){val|=uint64_t(r)<<nb;nb+=bits;while(nb>=8){map[w++]=val;val>>=8;nb-=8;}}if(nb)map[w]=val;
 unsigned cc,mc;auto cz=compress(code,cc);std::vector<uint8_t> mz;auto mp=permutation_encode(rank);if(EP::map.codec==7){mz=std::move(mp);mc=2;}else{auto saved=EP::fc;EP::fc=EP::map;mz=compress(map,mc);EP::fc=saved;if(mp.size()<mz.size()){mz=std::move(mp);mc=2;}}if(cz.empty()||mz.empty())return false;
 Header h{MAGIC,n,size,code.size(),cz.size(),mz.size(),bits,cc|(mc<<8)|(transform<<16)};out.resize(sizeof(h));memcpy(out.data(),&h,sizeof(h));out.insert(out.end(),cz.begin(),cz.end());out.insert(out.end(),mz.begin(),mz.end());return true;
}
static bool encode(const uint8_t*raw,size_t size,std::vector<uint8_t>&out){if(EP::direction>=0)return encode_direction(raw,size,out,EP::direction);std::vector<uint8_t>other;bool a=encode_direction(raw,size,out,0);for(unsigned transform=1;transform<=1;++transform){bool b=encode_direction(raw,size,other,transform);if(b&&(!a||other.size()<out.size())){out=std::move(other);a=true;}}return a;}

#endif
static bool decompress(unsigned codec,const uint8_t*src,size_t n,uint8_t*dst,size_t outn){
 if(codec==5){auto v=PP::decode(src,n,outn);if(v.size()!=outn)return false;if(outn)memcpy(dst,v.data(),outn);return true;}
 if(codec==1){size_t r=outn;return BrotliDecoderDecompress(n,src,&r,dst)==BROTLI_DECODER_RESULT_SUCCESS&&r==outn;}return false;
}
static State* open(const uint8_t*a,size_t size){
 if(size<sizeof(Header))return nullptr;Header h;memcpy(&h,a,sizeof(h));if(h.magic!=MAGIC||(h.codec>>16)>1||h.bits>32||h.n>1000000||h.n>h.raw||h.n>h.coderaw/2||h.raw>16u*1024*1024||h.coderaw>64u*1024*1024||h.coderaw>h.raw+10ull*h.n||h.codecomp>size-sizeof(h)||h.mapcomp!=size-sizeof(h)-h.codecomp)return nullptr;
 State*s=nullptr;try{s=new State;s->h=h;s->code.resize(h.coderaw);s->node.resize(h.n);s->rank.resize(h.n);std::vector<uint8_t>map((uint64_t(h.n)*h.bits+7)/8);
 if(!decompress(h.codec&255,a+sizeof(h),h.codecomp,s->code.data(),s->code.size())){delete s;return nullptr;}
 if(((h.codec>>8)&255)==2){if(!permutation_decode(a+sizeof(h)+h.codecomp,h.mapcomp,s->rank)){delete s;return nullptr;}}
 else{if(!decompress((h.codec>>8)&255,a+sizeof(h)+h.codecomp,h.mapcomp,map.data(),map.size())){delete s;return nullptr;}
 uint64_t val=0;unsigned nb=0;size_t r=0;uint64_t mask=(uint64_t(1)<<h.bits)-1;for(uint32_t i=0;i<h.n;++i){while(nb<h.bits){val|=uint64_t(map[r++])<<nb;nb+=8;}s->rank[i]=uint32_t(val&mask);val>>=h.bits;nb-=h.bits;if(s->rank[i]>=h.n){delete s;return nullptr;}}}
 if(((h.codec>>8)&255)!=2){std::vector<uint8_t>seen(h.n,0);for(uint32_t r:s->rank){if(seen[r]){delete s;return nullptr;}seen[r]=1;}}
 const uint8_t*p=s->code.data(),*e=p+s->code.size();std::vector<uint32_t>stk;stk.reserve(256);uint32_t lastlen=0;uint64_t total=0;
 for(uint32_t i=0;i<h.n;++i){uint32_t pre,len;if(!getvi(p,e,pre)||!getvi(p,e,len)||pre>lastlen||size_t(e-p)<len||uint64_t(pre)+len==0||uint64_t(pre)+len>h.raw){delete s;return nullptr;}while(!stk.empty()&&s->node[stk.back()].pre>=pre)stk.pop_back();uint32_t par=stk.empty()?UINT32_MAX:stk.back();if(pre&&par==UINT32_MAX){delete s;return nullptr;}s->node[i]={uint32_t(p-s->code.data()),pre,pre+len,par};stk.push_back(i);p+=len;lastlen=pre+len;total+=lastlen;}
 if(p!=e||total!=h.raw){delete s;return nullptr;}return s;
 }catch(...){delete s;return nullptr;}
}
static inline uint32_t row(State*s,uint32_t r,uint8_t*out){uint32_t len=s->node[r].len,rem=len;do{const Node&n=s->node[r];if(rem>n.pre)memcpy(out+n.pre,s->code.data()+n.off,rem-n.pre);rem=n.pre;r=n.parent;}while(rem);if((s->h.codec>>16)==1)std::reverse(out,out+len);return len;}
static int64_t decode(State*s,uint8_t*out,size_t cap){if(!s||cap<s->h.raw)return -1;size_t at=0;for(uint32_t i=0;i<s->h.n;++i)at+=row(s,s->rank[i],out+at);return at;}
static int64_t rows(State*s,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offs){if(!s||!offs)return -1;size_t at=0;offs[0]=0;for(size_t i=0;i<count;++i){if(ids[i]>=s->h.n)return -1;uint32_t r=s->rank[ids[i]],len=s->node[r].len;if(len>cap-at)return -1;row(s,r,out+at);at+=len;offs[i+1]=at;}return at;}
}
using FcState=FC::State;
#ifdef ENCODER
static bool fc_encode(const uint8_t*raw,size_t n,std::vector<uint8_t>&out){return FC::encode(raw,n,out);}
#endif
static FcState* fc_open(const uint8_t*a,size_t n){return FC::open(a,n);}
static int64_t fc_decode(FcState*s,uint8_t*out,size_t cap){return FC::decode(s,out,cap);}
static int64_t fc_rows(FcState*s,const uint64_t*ids,size_t n,uint8_t*out,size_t cap,uint64_t*offs){return FC::rows(s,ids,n,out,cap,offs);}
static void fc_close(FcState*s){delete s;}
