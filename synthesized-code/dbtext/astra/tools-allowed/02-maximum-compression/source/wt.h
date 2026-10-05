#pragma once
#include "ppmd_codec.h"
#include "entropy_plan.h"
#include <algorithm>
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <cstring>
#include <zstd.h>
#include <brotli/decode.h>
#include <lzma.h>
#include <bzlib.h>
#ifdef ENCODER
#include <brotli/encode.h>
#endif
namespace WT {
using B=uint8_t;using U=uint32_t;using V=std::vector<B>;
static const uint64_t magic=0x31544f44524f5757ULL;
static U rd(const B*p){U x;memcpy(&x,p,4);return x;}
static void put(V&v,U x){size_t at=v.size();v.resize(at+4);memcpy(v.data()+at,&x,4);}
static void var(V&v,U x){while(x>=128){v.push_back((x&127)|128);x>>=7;}v.push_back(x);}
static bool uv(const B*&p,const B*e,U&x){x=0;for(U sh=0;sh<=28&&p<e;sh+=7){U q=*p++;if(sh==28&&q>15)return false;x|=(q&127)<<sh;if(q<128)return true;}return false;}
static V unc(const B*p,size_t n,size_t outn,U m){if(m==5)return PP::decode(p,n,outn);if(m!=1)return {};V v(outn);size_t k=outn;if(BrotliDecoderDecompress(n,p,&k,v.data())!=BROTLI_DECODER_RESULT_SUCCESS||k!=outn)return {};return v;}

#ifdef ENCODER
static V comp(const V&v,U&m){if(EP::wt.codec>=0){m=EP::wt.codec;auto c=EP::wt;if(c.codec==2)c.codec=4;else if(c.codec==4)c.codec=6;return EP::compress(v,c,true);}
 V best(ZSTD_compressBound(v.size()));auto n=ZSTD_compress(best.data(),best.size(),v.data(),v.size(),19);if(ZSTD_isError(n))return {};best.resize(n);m=0;
 V b(BrotliEncoderMaxCompressedSize(v.size()));size_t k=b.size();if(BrotliEncoderCompress(11,22,BROTLI_MODE_GENERIC,v.size(),v.data(),&k,b.data())&&k<best.size()){b.resize(k);best.swap(b);m=1;}
 b.resize(lzma_stream_buffer_bound(v.size()));k=0;if(lzma_easy_buffer_encode(9,LZMA_CHECK_NONE,nullptr,v.data(),v.size(),b.data(),&k,b.size())==LZMA_OK&&k<best.size()){b.resize(k);best.swap(b);m=2;}
 b.resize(v.size()+v.size()/100+1024);unsigned int bk=b.size();if(BZ2_bzBuffToBuffCompress((char*)b.data(),&bk,(char*)v.data(),v.size(),9,0,30)==BZ_OK&&bk<best.size()){b.resize(bk);best.swap(b);m=3;}
 auto pp=PP::encode(v.data(),v.size(),PP::wt_order,24);if(!pp.empty()&&pp.size()<best.size()){best.swap(pp);m=5;}
 if(v.size()<best.size()){best=v;m=4;}return best;}
static bool alpha(B c){return(c>='a'&&c<='z')||(c>='A'&&c<='Z');}
static size_t ulen(const B*p,size_t n,size_t i){B c=p[i];size_t l=(c>=194&&c<=223)?2:((c>=224&&c<=239)?3:((c>=240&&c<=244)?4:1));if(i+l>n)return 1;for(size_t j=1;j<l;j++)if((p[i+j]&192)!=128)return 1;return l;}
static size_t wordend(const B*p,size_t n,size_t i,U mode){if(mode==2){if(p[i]<=32)return i;size_t e=i+1;while(e<n&&p[e]>32)e++;return e;}if(!alpha(p[i]))return i;size_t e=i+1;while(e<n&&alpha(p[e]))e++;if(mode==1&&e<n&&(p[e]==' '||p[e]=='.'||p[e]==','||p[e]=='_'||p[e]=='-'))e++;return e;}
static V encmode(const B*raw,size_t size,U mode,bool pairs=false){
 std::unordered_map<std::string,U>freq;
 for(size_t i=0;i<size;){size_t e=wordend(raw,size,i,mode);if(e>i){if(e-i>=2)freq[std::string((char*)raw+i,e-i)]++;i=e;}else i++;}
 std::vector<std::string>dict;dict.reserve(freq.size()+256);for(U i=0;i<256;i++)dict.emplace_back(1,char(i));
 std::vector<std::pair<uint64_t,std::string>>chosen;
 for(auto&f:freq)if(f.second>=2&&f.first.size()>1&&(f.first.size()-1)*f.second>f.first.size()+6)chosen.emplace_back((f.first.size()-1)*f.second-f.first.size(),f.first);
 std::sort(chosen.begin(),chosen.end(),[](auto&a,auto&b){return a.first!=b.first?a.first>b.first:a.second<b.second;});if(chosen.size()>65000)chosen.resize(65000);
 std::unordered_map<std::string,U>lookup;lookup.reserve(chosen.size()*2);
 for(size_t i=0;i<size;){size_t l=ulen(raw,size,i);if(l>1){std::string unit((char*)raw+i,l);if(!lookup.count(unit)){lookup[unit]=dict.size();dict.push_back(unit);}}i+=l;}
 for(auto&x:chosen)if(!lookup.count(x.second)){lookup[x.second]=dict.size();dict.push_back(x.second);}
 std::vector<U>tokens,lens;tokens.reserve(size);U rowstart=0;
 for(size_t i=0;i<size;){size_t e=wordend(raw,size,i,mode);bool found=false;if(e>i){auto it=lookup.find(std::string((char*)raw+i,e-i));if(it!=lookup.end()){tokens.push_back(it->second);i=e;found=true;}}if(!found&&raw[i]>=128){size_t l=ulen(raw,size,i);if(l>1){tokens.push_back(lookup[std::string((char*)raw+i,l)]);i+=l;found=true;}}if(!found){B c=raw[i++];tokens.push_back(c);if(c==10){lens.push_back(tokens.size()-rowstart);rowstart=tokens.size();}}}
 if(rowstart<tokens.size())lens.push_back(tokens.size()-rowstart);

 
 if(pairs)for(U round=0;round<16&&dict.size()<60000;round++){
  std::unordered_map<uint64_t,U>freq;freq.reserve(tokens.size());U start=0;
  for(U len:lens){U end=start+len;for(U i=start;i+1<end;i++){U x=tokens[i],y=tokens[i+1];if(dict[x].size()+dict[y].size()<=128)freq[uint64_t(x)|(uint64_t(y)<<32)]++;}start=end;}
  std::vector<std::pair<U,uint64_t>>pairs;for(auto &f:freq)if(f.second>=8)pairs.emplace_back(f.second,f.first);std::sort(pairs.begin(),pairs.end(),[](auto&a,auto&b){return a.first!=b.first?a.first>b.first:a.second<b.second;});if(pairs.empty())break;if(pairs.size()>128)pairs.resize(128);
  std::unordered_map<uint64_t,U>map;for(auto &p:pairs){U x=p.second,y=p.second>>32;map[p.second]=dict.size();dict.push_back(dict[x]+dict[y]);}
  std::vector<U>next;next.reserve(tokens.size());start=0;for(U &len:lens){U end=start+len,newstart=next.size();for(U i=start;i<end;i++){if(i+1<end){auto p=map.find(uint64_t(tokens[i])|(uint64_t(tokens[i+1])<<32));if(p!=map.end()){next.push_back(p->second);i++;continue;}}next.push_back(tokens[i]);}start=end;len=next.size()-newstart;}tokens.swap(next);
 }
 if(tokens.size()*10>size*9)return {};
 std::vector<U>counts(dict.size()),order;for(auto t:tokens)counts[t]++;for(U i=0;i<dict.size();i++)if(counts[i])order.push_back(i);
 std::sort(order.begin(),order.end(),[&](U a,U b){return counts[a]!=counts[b]?counts[a]>counts[b]:a<b;});std::vector<U>map(dict.size());for(U i=0;i<order.size();i++)map[order[i]]=i;
 V buf;for(U id:order){var(buf,dict[id].size());buf.insert(buf.end(),dict[id].begin(),dict[id].end());}
 U db=buf.size();for(U x:lens)var(buf,x);U tb=buf.size();for(U x:tokens)var(buf,map[x]);U m;auto compressed=comp(buf,m);
 V out(8);memcpy(out.data(),&magic,8);put(out,size);put(out,lens.size());put(out,order.size());put(out,tokens.size());put(out,db);put(out,tb);put(out,buf.size());put(out,m);out.insert(out.end(),compressed.begin(),compressed.end());return out;
}
static bool encode(const B*raw,size_t size,V&out){if(!size)return false;V best;for(U mode=0;mode<6;mode++){auto v=encmode(raw,size,mode%3,mode>=3);if(!v.empty()&&(best.empty()||v.size()<best.size()))best.swap(v);}if(best.empty())return false;out.swap(best);return true;}
#endif
struct Word{U at,len;};
struct State{U raw,nrow;V data;std::vector<Word>dict;std::vector<U>tokens,pos;};
static State* open(const B*a,size_t n){if(n<40||memcmp(a,&magic,8))return nullptr;U raw=rd(a+8),nr=rd(a+12),nd=rd(a+16),nt=rd(a+20),db=rd(a+24),tb=rd(a+28),un=rd(a+32),m=rd(a+36);if(raw>(16u<<20)||nr>1000000||nr>raw||nd>65536||nt>raw||db>tb||tb>un||un>(32u<<20))return nullptr;auto data=unc(a+40,n-40,un,m);if(data.size()!=un)return nullptr;auto*s=new State{raw,nr};s->data.swap(data);s->dict.resize(nd);const B*p=s->data.data(),*end=p+db;
 for(U i=0;i<nd;i++){U l;if(!uv(p,end,l)||!l||l>size_t(end-p)){delete s;return nullptr;}s->dict[i]={U(p-s->data.data()),l};p+=l;}if(p!=end){delete s;return nullptr;}end=s->data.data()+tb;s->pos.resize(nr+1);uint64_t sum=0;for(U i=0;i<nr;i++){U l;if(!uv(p,end,l)){delete s;return nullptr;}sum+=l;if(sum>nt){delete s;return nullptr;}s->pos[i+1]=sum;}if(p!=end||sum!=nt){delete s;return nullptr;}
 end=s->data.data()+un;s->tokens.reserve(nt);for(U i=0;i<nt;i++){U id;if(!uv(p,end,id)||id>=nd){delete s;return nullptr;}s->tokens.push_back(id);}if(p!=end){delete s;return nullptr;}
 s->data.resize(db);return s;}
static int64_t emit(State*s,U lo,U hi,B*out,size_t cap){size_t w=0;for(U i=lo;i<hi;i++){auto d=s->dict[s->tokens[i]];if(d.len>cap-w)return -1;memcpy(out+w,s->data.data()+d.at,d.len);w+=d.len;}return w;}
static int64_t decode(State*s,B*out,size_t cap){if(cap<s->raw)return -1;auto n=emit(s,0,s->tokens.size(),out,cap);return n==s->raw?n:-1;}
static int64_t rows(State*s,const uint64_t*ids,size_t count,B*out,size_t cap,uint64_t*off){off[0]=0;size_t w=0;uint64_t prev=0;for(size_t i=0;i<count;i++){uint64_t id=ids[i];if(id>=s->nrow||(i&&id<prev))return -1;prev=id;auto k=emit(s,s->pos[id],s->pos[id+1],out+w,cap-w);if(k<0)return -1;w+=k;off[i+1]=w;}return w;}
static void close(State*s){delete s;}
}
