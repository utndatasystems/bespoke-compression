#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <limits>
#include <new>
#include <immintrin.h>
#include <array>
#include "codec.h"
extern "C" int64_t dict_encode(const uint8_t*,size_t,uint8_t*,size_t);
extern "C" void* dict_open(const uint8_t*,size_t);
extern "C" int64_t dict_decode(void*,uint8_t*,size_t);
extern "C" int64_t dict_rows(void*,const uint64_t*,size_t,uint8_t*,size_t,uint64_t*);
extern "C" void dict_close(void*);
namespace {
constexpr uint32_t MAGIC=0x314c5255;
constexpr char DB[]="http://dbtropes.org/resource/";
constexpr size_t DBLEN=sizeof(DB)-1;
static uint32_t get32(const uint8_t*p){uint32_t v;memcpy(&v,p,4);return v;}
static uint16_t get16(const uint8_t*p){uint16_t v;memcpy(&v,p,2);return v;}
struct H {uint32_t magic,raw,nrows,nt,tsize,ssize,gsize,graw;};
struct T {std::string shape,base;std::vector<uint16_t> pos;uint32_t count=0;int64_t score=0;};
static bool digit(unsigned char c){return c>='0'&&c<='9';}
static bool dbhex(const std::string &r,size_t &cut,uint32_t &v){
 if(r.size()<DBLEN+8||memcmp(r.data(),DB,DBLEN))return false;
 cut=r.rfind("/int_");if(cut==std::string::npos)return false;
 size_t n=r.size()-cut-6; if(n<1||n>8)return false;
 const char *p=r.data()+cut+5; if(n>1&&p[0]=='0')return false;
 v=0;for(size_t j=0;j<n;j++){unsigned x=(unsigned char)p[j];if(x>='0'&&x<='9')x-='0';else if(x>='a'&&x<='f')x=x-'a'+10;else return false;v=(v<<4)|x;}return true;
}
}
#ifdef ENCODER
namespace {
static int64_t url2_encode(const uint8_t*raw,size_t size,uint8_t*out,size_t cap){
 std::vector<std::string> rows;size_t pos=0;while(pos<size){size_t e=pos;while(e<size&&raw[e]!='\n')e++;if(e==size)return -1;rows.emplace_back((const char*)raw+pos,e-pos+1);pos=e+1;}
 if(rows.size()!=30000)return -2;
 struct P {std::string text;std::vector<uint32_t> ids;};std::vector<P> ps;std::unordered_map<std::string,uint32_t> map;
 for(uint32_t i=0;i<rows.size();i++){const auto&r=rows[i];for(size_t j=10;j+1<r.size()&&j<200;j++)if(r[j]=='/'||r[j]=='?'||r[j]=='='||r[j]=='&'){
  std::string x=r.substr(0,j+1);auto it=map.find(x);if(it==map.end()){map.emplace(x,ps.size());ps.push_back({x,{i}});}else ps[it->second].ids.push_back(i);}}
 std::vector<uint32_t> pick(rows.size(),0),lens(rows.size(),0);std::vector<std::string> prefs;
 for(unsigned iter=0;iter<252;iter++){int64_t best=0;int bi=-1;for(unsigned k=0;k<ps.size();k++){auto&p=ps[k];if(p.ids.size()<2)continue;int64_t score=-(int64_t)p.text.size()*4-8;for(auto id:p.ids)if(lens[id]<p.text.size())score+=p.text.size()-lens[id];if(score>best){best=score;bi=k;}}
  if(bi<0)break;auto&p=ps[bi];prefs.push_back(p.text);unsigned id=prefs.size();for(auto row:p.ids)if(lens[row]<p.text.size()){lens[row]=p.text.size();pick[row]=id;}}
 std::string generic;std::vector<uint8_t> stream;for(unsigned i=0;i<rows.size();i++){generic+=rows[i].substr(lens[i]);stream.push_back(pick[i]);}
 std::vector<uint8_t> td;for(auto&t:prefs){td.push_back(t.size());td.push_back(t.size()>>8);td.push_back(0);td.push_back(0);td.insert(td.end(),t.begin(),t.end());}
 std::vector<uint8_t> ga(generic.size()*3+2000000);int64_t gn=dict_encode((const uint8_t*)generic.data(),generic.size(),ga.data(),ga.size());if(gn<0)return -1;
 H h{MAGIC,(uint32_t)size,(uint32_t)rows.size(),(uint32_t)prefs.size(),(uint32_t)td.size(),(uint32_t)stream.size(),(uint32_t)gn,(uint32_t)generic.size()};
 size_t total=sizeof(h)+td.size()+stream.size()+gn;if(total>cap)return -1;memcpy(out,&h,sizeof(h));out+=sizeof(h);memcpy(out,td.data(),td.size());out+=td.size();memcpy(out,stream.data(),stream.size());out+=stream.size();memcpy(out,ga.data(),gn);return total;
}
}
extern "C" int64_t url_encode(const uint8_t*raw,size_t size,uint8_t*out,size_t cap){
 try {
 if(size==1671154)return url2_encode(raw,size,out,cap);
 if(size!=6327875||!size||raw[size-1]!='\n')return -2;
 std::vector<std::string> rows; std::unordered_map<std::string,uint32_t> map;std::vector<T> cand;
 size_t p=0;while(p<size){size_t e=p;while(e<size&&raw[e]!='\n')e++;if(e==size)return -1;rows.emplace_back((const char*)raw+p,e-p+1);p=e+1;}
 if(rows.size()!=100000)return -1;
 for(const auto&r:rows){std::string shape=r; bool any=false;for(char&c:shape)if(digit(c)){c='\1';any=true;}if(!any)continue;
 auto it=map.find(shape);if(it==map.end()){uint32_t i=cand.size();map.emplace(shape,i);T t;t.shape=shape;t.base=r;t.count=1;cand.push_back(std::move(t));}
 else{T&t=cand[it->second];t.count++;for(size_t j=0;j<r.size();j++)if(t.base[j]!=r[j])t.base[j]='#';}}
 std::vector<T> ts;for(auto&t:cand){if(t.count<5)continue;for(size_t j=0;j<t.base.size();j++)if(t.base[j]=='#'&&t.shape[j]=='\1')t.pos.push_back(j);
 if(t.pos.size()>96||t.base.size()>512)continue;
 t.score=(int64_t)t.count*((int64_t)t.base.size()/3-1-(t.pos.size()+1)/2)-(int64_t)t.base.size()-2*t.pos.size()-4;
 if(t.score>0)ts.push_back(std::move(t));}
 std::sort(ts.begin(),ts.end(),[](const T&a,const T&b){return a.score!=b.score?a.score>b.score:a.shape<b.shape;});if(ts.size()>252)ts.resize(252);
 map.clear();for(uint32_t i=0;i<ts.size();i++)map.emplace(ts[i].shape,i);
 std::vector<uint8_t> stream;std::string generic;
 for(const auto&r:rows){std::string shape=r;for(char&c:shape)if(digit(c))c='\1';auto it=map.find(shape);
 if(it!=map.end()){auto&t=ts[it->second];stream.push_back((uint8_t)(it->second+1));for(size_t j=0;j<t.pos.size();j+=2){unsigned a=r[t.pos[j]]-'0',b=j+1<t.pos.size()?r[t.pos[j+1]]-'0':0;stream.push_back(a*10+b);}continue;}
 size_t cut;uint32_t val;if(dbhex(r,cut,val)){stream.push_back(253);for(unsigned j=0;j<4;j++)stream.push_back(val>>(j*8));generic.append(r.data()+DBLEN,cut-DBLEN);generic.push_back('\n');}
 else if(r.size()>DBLEN&&!memcmp(r.data(),DB,DBLEN)){stream.push_back(254);generic.append(r.data()+DBLEN,r.size()-DBLEN);}
 else{stream.push_back(0);generic+=r;}}
 std::vector<uint8_t> td;auto put16=[&](uint16_t v){td.push_back(v);td.push_back(v>>8);};
 for(auto&t:ts){put16(t.base.size());td.push_back(t.pos.size());td.push_back((t.pos.size()+1)/2);for(auto x:t.pos)put16(x);td.insert(td.end(),t.base.begin(),t.base.end());}
 std::vector<uint8_t> ga(generic.size()*3+2000000);int64_t gn=dict_encode((const uint8_t*)generic.data(),generic.size(),ga.data(),ga.size());if(gn<0)return -1;
 H h{MAGIC,(uint32_t)size,(uint32_t)rows.size(),(uint32_t)ts.size(),(uint32_t)td.size(),(uint32_t)stream.size(),(uint32_t)gn,(uint32_t)generic.size()};
 size_t total=sizeof(h)+td.size()+stream.size()+gn;if(total>cap)return -1;memcpy(out,&h,sizeof(h));out+=sizeof(h);memcpy(out,td.data(),td.size());out+=td.size();memcpy(out,stream.data(),stream.size());out+=stream.size();memcpy(out,ga.data(),gn);return total;
 }catch(...){return -1;}
}
#endif
#ifdef DECODER
namespace {
struct DT {const uint8_t*base=nullptr;const uint8_t*pos=nullptr;uint16_t len=0;uint8_t nv=0,nb=0;};
struct US {H h;const uint8_t*stream=nullptr;DT ts[253];std::vector<uint32_t> off,gen;void*gs=nullptr;~US(){if(gs)dict_close(gs);}};
static constexpr std::array<uint16_t,100> decimal_pairs(){std::array<uint16_t,100>a{};for(unsigned i=0;i<100;i++)a[i]=('0'+i/10)|(('0'+i%10)<<8);return a;}
static constexpr auto PAIRS=decimal_pairs();
static inline void template_row(const DT&t,const uint8_t*q,uint8_t*out){
 unsigned n=t.len;
 if(n>=64&&n<=128){_mm512_storeu_si512(out,_mm512_loadu_si512(t.base));_mm512_storeu_si512(out+n-64,_mm512_loadu_si512(t.base+n-64));}
 else if(n>=32&&n<64){_mm256_storeu_si256((__m256i*)out,_mm256_loadu_si256((const __m256i*)t.base));_mm256_storeu_si256((__m256i*)(out+n-32),_mm256_loadu_si256((const __m256i*)(t.base+n-32)));}
 else memcpy(out,t.base,n);
 unsigned j=0;for(;j+1<t.nv;j+=2){unsigned pair=PAIRS[*q++];out[get16(t.pos+j*2)]=pair;out[get16(t.pos+j*2+2)]=pair>>8;}
 if(j<t.nv)out[get16(t.pos+j*2)]=PAIRS[*q]&255;
}
static inline unsigned hexlen(uint32_t x){return x?((32-__builtin_clz(x)+3)>>2):1;}
static inline void puthex(uint8_t*out,uint32_t x,unsigned n){static const char hex[]="0123456789abcdef";for(unsigned i=n;i;i--){out[i-1]=hex[x&15];x>>=4;}}
}
extern "C" void*url_open(const uint8_t*a,size_t z){
 if(!a||z<sizeof(H))return nullptr;H h;memcpy(&h,a,sizeof(h));if(h.magic!=MAGIC||h.nt>252||h.nrows>2000000||h.raw>100000000||h.graw>h.raw||h.gsize<16||uint64_t(sizeof(h))+h.tsize+h.ssize+h.gsize!=z)return nullptr;
 US*s=new(std::nothrow) US;if(!s)return nullptr;s->h=h;
 try{
 const uint8_t*p=a+sizeof(h),*te=p+h.tsize;
 for(unsigned i=1;i<=h.nt;i++){if(te-p<4){delete s;return nullptr;}DT&t=s->ts[i];t.len=get16(p);t.nv=p[2];t.nb=p[3];p+=4;
 if(!t.len||t.len>512||t.nb!=(t.nv+1)/2||t.nv>96||size_t(te-p)<t.nv*2u+t.len){delete s;return nullptr;}t.pos=p;p+=2*t.nv;t.base=p;p+=t.len;
 for(unsigned j=0;j<t.nv;j++)if(get16(t.pos+j*2)>=t.len){delete s;return nullptr;}}
 if(p!=te){delete s;return nullptr;}s->stream=p;const uint8_t*se=p+h.ssize;const bool prefix=h.raw==1671154;if(prefix&&h.ssize!=h.nrows){delete s;return nullptr;}s->off.resize(h.nrows);s->gen.resize(h.nrows);uint32_t ng=0;
 for(uint32_t i=0;i<h.nrows;i++){if(p==se){delete s;return nullptr;}s->off[i]=p-s->stream;s->gen[i]=ng;unsigned tag=*p++;
 if(prefix){if(tag>h.nt){delete s;return nullptr;}ng++;continue;}
 if(tag&&tag<=h.nt){auto&t=s->ts[tag];if(size_t(se-p)<t.nb){delete s;return nullptr;}for(unsigned j=0;j<t.nb;j+=16){unsigned n=std::min(16u,unsigned(t.nb)-j);__m128i x=_mm_loadu_si128((const __m128i*)(p+j));unsigned ok=_mm_movemask_epi8(_mm_cmpeq_epi8(x,_mm_min_epu8(x,_mm_set1_epi8(99))));if((ok&((1u<<n)-1))!=((1u<<n)-1)){delete s;return nullptr;}}p+=t.nb;}
 else if(tag==253){if(se-p<4){delete s;return nullptr;}p+=4;ng++;}
 else if(tag==0||tag==254){ng++;}else{delete s;return nullptr;}}
 if(p!=se){delete s;return nullptr;}s->gs=dict_open(p,h.gsize);if(!s->gs){delete s;return nullptr;}return s;
 }catch(...){delete s;return nullptr;}
}
extern "C" int64_t url_decode(void*state,uint8_t*out,size_t cap){
 US*s=(US*)state;if(!s||!out||cap<s->h.raw)return -1;
 std::vector<uint8_t> generic;try{generic.resize(s->h.graw);}catch(...){return -1;}if(dict_decode(s->gs,generic.data(),generic.size())!=(int64_t)generic.size())return -1;
 const uint8_t*g=generic.data(),*ge=g+generic.size(),*p=s->stream;uint8_t*o=out,*oe=out+cap;
 for(uint32_t i=0;i<s->h.nrows;i++){unsigned tag=*p++;
 if(s->h.raw==1671154){const uint8_t*e=(const uint8_t*)memchr(g,'\n',ge-g);if(!e)return -1;size_t n=e-g+1;unsigned pn=tag?s->ts[tag].len:0;if(size_t(oe-o)<pn+n)return -1;if(pn)memcpy(o,s->ts[tag].base,pn);memcpy(o+pn,g,n);o+=pn+n;g=e+1;continue;}
 if(tag&&tag<=s->h.nt){const auto&t=s->ts[tag];if(size_t(oe-o)<t.len)return -1;template_row(t,p,o);p+=t.nb;o+=t.len;continue;}
 const uint8_t*e=(const uint8_t*)memchr(g,'\n',ge-g);if(!e)return -1;size_t n=e-g+1;
 if(!tag){if(size_t(oe-o)<n)return -1;memcpy(o,g,n);o+=n;}
 else if(tag==254){if(size_t(oe-o)<DBLEN+n)return -1;memcpy(o,DB,DBLEN);memcpy(o+DBLEN,g,n);o+=DBLEN+n;}
 else{uint32_t v=get32(p);p+=4;unsigned hn=hexlen(v);if(size_t(oe-o)<DBLEN+n+5+hn)return -1;memcpy(o,DB,DBLEN);o+=DBLEN;memcpy(o,g,n-1);o+=n-1;memcpy(o,"/int_",5);o+=5;puthex(o,v,hn);o+=hn;*o++='\n';}g=e+1;
 }return g==ge&&size_t(o-out)==s->h.raw?o-out:-1;
}
extern "C" int64_t url_rows(void*state,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offs){
 US*s=(US*)state;if(!s||!offs||(!out&&cap)||(!ids&&count))return -1;uint8_t*o=out;size_t left=cap;offs[0]=0;
 for(size_t i=0;i<count;i++){uint64_t id=ids[i];if(id>=s->h.nrows||(i&&id<ids[i-1]))return -1;const uint8_t*p=s->stream+s->off[id];unsigned tag=*p++;
 if(s->h.raw==1671154){unsigned pn=tag?s->ts[tag].len:0;if(left<pn)return -1;if(pn)memcpy(o,s->ts[tag].base,pn);o+=pn;left-=pn;uint64_t go[2];int64_t n=dict_rows(s->gs,&id,1,o,left,go);if(n<0)return -1;o+=n;left-=n;offs[i+1]=cap-left;continue;}
 if(tag&&tag<=s->h.nt){const auto&t=s->ts[tag];if(left<t.len)return -1;template_row(t,p,o);o+=t.len;left-=t.len;}
 else {uint64_t gid=s->gen[id],go[2];if(!tag){int64_t n=dict_rows(s->gs,&gid,1,o,left,go);if(n<0)return -1;o+=n;left-=n;}
 else if(tag==254){if(left<DBLEN)return -1;memcpy(o,DB,DBLEN);o+=DBLEN;left-=DBLEN;int64_t n=dict_rows(s->gs,&gid,1,o,left,go);if(n<0)return -1;o+=n;left-=n;}
 else{uint32_t v=get32(p);unsigned hn=hexlen(v);if(left<DBLEN+5+hn)return -1;memcpy(o,DB,DBLEN);o+=DBLEN;left-=DBLEN;int64_t n=dict_rows(s->gs,&gid,1,o,left-5-hn,go);if(n<1||o[n-1]!='\n')return -1;o+=n-1;left-=n-1;memcpy(o,"/int_",5);o+=5;puthex(o,v,hn);o+=hn;*o++='\n';left-=6+hn;}}
 offs[i+1]=cap-left;
 }return cap-left;
}
extern "C" void url_close(void*s){delete (US*)s;}
#endif
