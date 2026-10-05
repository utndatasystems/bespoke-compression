#pragma once
#include "textbit.h"
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include <limits>
#ifdef ENCODER
#include <unordered_map>
#include <map>
#endif
namespace url {
static constexpr uint32_t MAGIC=0x354c5255u;
static inline uint16_t r16(const uint8_t*p){uint16_t x;memcpy(&x,p,2);return x;}
static inline uint32_t r32(const uint8_t*p){uint32_t x;memcpy(&x,p,4);return x;}
static inline uint64_t r64(const uint8_t*p){uint64_t x;memcpy(&x,p,8);return x;}
#ifdef ENCODER
static void w8(std::vector<uint8_t>&o,uint32_t x){o.push_back(x);}
static void w16(std::vector<uint8_t>&o,uint32_t x){o.push_back(x);o.push_back(x>>8);}
static void w32(std::vector<uint8_t>&o,uint32_t x){for(int j=0;j<4;j++)o.push_back(x>>(8*j));}
static void w64(std::vector<uint8_t>&o,uint64_t x){for(int j=0;j<8;j++)o.push_back(x>>(8*j));}
static void vn(std::vector<uint8_t>&o,uint32_t x){if(x<255)w8(o,x);else {w8(o,255);w16(o,x);}}
static unsigned bw(uint32_t x){return x?32-__builtin_clz(x):0;}
struct EF {uint8_t pos,width,bits,lookup;uint32_t base;std::vector<uint32_t> values;};
struct ET {std::string base;std::vector<EF>fields;std::vector<uint32_t> rows;uint32_t bits=0,id=0;int64_t score=0;};
static bool nums(const std::string&s,std::string&key,std::vector<uint32_t>&vals,std::vector<uint8_t>&pos,std::vector<uint8_t>&width){
 if(s.size()>255)return false;key=s; vals.clear();pos.clear();width.clear();
 for(size_t i=0;i<s.size();){if(s[i]<'0'||s[i]>'9'){i++;continue;}size_t a=i;uint64_t v=0;while(i<s.size()&&s[i]>='0'&&s[i]<='9'){v=v*10+s[i]-'0';key[i++]=1;}if(i-a>9||v>0xffffffffu)return false;pos.push_back(a);width.push_back(i-a);vals.push_back(v);}return !vals.empty();
}
static void fc(std::vector<uint8_t>&o,const std::vector<std::string>&v,std::vector<uint8_t>&pool,bool addlf){
 for(uint32_t i=0;i<v.size();i++){uint32_t p=0;if(i){const auto&q=v[i-1];while(p<v[i].size()&&p<q.size()&&v[i][p]==q[p])p++;}vn(o,p);vn(o,v[i].size()-p);pool.insert(pool.end(),v[i].begin()+p,v[i].end());if(addlf)pool.push_back('\n');}
}
static bool encode(const uint8_t*raw,size_t n,std::vector<uint8_t>&out){
 if(n<500000||n>9000000)return false;
 const char*mark="reference.data.gov.uk";size_t checks=std::min(n,size_t(8192)); bool found=false;
 for(size_t i=0;i+21<checks;i++)if(!memcmp(raw+i,mark,21)){found=true;break;}if(!found)return false;
 std::vector<std::string>r;size_t a=0;for(size_t i=0;i<n;i++)if(raw[i]=='\n'){r.emplace_back((const char*)raw+a,i+1-a);a=i+1;}if(a<n)r.emplace_back((const char*)raw+a,n-a);if(r.size()>655350)return false;
 std::vector<int>rt(r.size(),-1);std::vector<ET>ts;std::unordered_map<std::string,uint32_t>tm;std::string key;std::vector<uint32_t>vals;std::vector<uint8_t>pos,width;
 for(uint32_t i=0;i<r.size();i++){if(r[i].find("dbtropes.org/")!=std::string::npos)continue;if(!nums(r[i],key,vals,pos,width))continue;auto it=tm.find(key);uint32_t j;if(it==tm.end()){j=ts.size();tm.emplace(key,j);ET t;t.base=r[i];ts.push_back(std::move(t));}else j=it->second;ts[j].rows.push_back(i);}
 std::vector<uint32_t>sel;
 for(uint32_t j=0;j<ts.size();j++){auto&t=ts[j];if(t.rows.size()<20)continue;nums(t.base,key,vals,pos,width);std::vector<std::vector<uint32_t>>fv(vals.size());for(auto i:t.rows){nums(r[i],key,vals,pos,width);for(size_t k=0;k<vals.size();k++)fv[k].push_back(vals[k]);}
 uint32_t cost=4+t.base.size();for(size_t k=0;k<fv.size();k++){auto v=fv[k];std::sort(v.begin(),v.end());v.erase(std::unique(v.begin(),v.end()),v.end());if(v.size()==1)continue;EF f;f.pos=pos[k];f.width=width[k];f.base=v[0];f.bits=bw(v.back()-v[0]);f.lookup=0;unsigned db=bw(v.size()-1);if(v.size()<=2048&&uint64_t(f.bits-db)*t.rows.size()>uint64_t(v.size())*32+80){f.lookup=1;f.bits=db;f.values=std::move(v);cost+=4*f.values.size()+2;}t.bits+=f.bits;cost+=8;t.fields.push_back(std::move(f));}
 if(t.bits>32||t.fields.empty()||t.fields.size()>255)continue;t.score=int64_t(t.rows.size())*(int64_t(t.base.size())-(t.bits<=24?4:5))-cost;if(t.score>0)sel.push_back(j);
 }
 std::sort(sel.begin(),sel.end(),[&](uint32_t a,uint32_t b){return ts[a].score>ts[b].score;});if(sel.size()>128)sel.resize(128);
 for(uint32_t j=0;j<sel.size();j++){ts[sel[j]].id=j;for(uint32_t i:ts[sel[j]].rows)rt[i]=sel[j];}
 std::vector<std::string>stems,fallback;std::vector<uint32_t>hex(r.size());std::vector<uint8_t>hexlen(r.size());std::vector<std::string>stemrow(r.size());
 for(uint32_t i=0;i<r.size();i++)if(rt[i]<0){const auto&s=r[i];size_t p=s.rfind("/int_");if(p!=std::string::npos&&s.compare(0,28,"http://dbtropes.org/resource")==0&&s.back()=='\n'){size_t z=s.size()-p-6;uint32_t v=0;bool ok=z>0&&z<=8;for(size_t k=p+5;k+1<s.size();k++){unsigned c=(unsigned char)s[k];unsigned d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:99;if(d>15)ok=false;v=(v<<4)|d;}if(ok&&z==std::max(1u,(bw(v)+3)/4)){hex[i]=v;hexlen[i]=z;stemrow[i]=s.substr(0,p);stems.push_back(stemrow[i]);continue;}}
 fallback.push_back(s);}
 std::sort(stems.begin(),stems.end());stems.erase(std::unique(stems.begin(),stems.end()),stems.end());std::sort(fallback.begin(),fallback.end());fallback.erase(std::unique(fallback.begin(),fallback.end()),fallback.end());if(stems.size()>65535||fallback.size()>65535)return false;
 std::unordered_map<std::string,uint16_t>si,fi;for(uint32_t i=0;i<stems.size();i++)si.emplace(stems[i],i);for(uint32_t i=0;i<fallback.size();i++)fi.emplace(fallback[i],i);
 std::vector<uint8_t>types((r.size()+3)/4),streams[4];uint32_t counts[4]={};
 for(uint32_t i=0;i<r.size();i++){unsigned kind;auto pushp=[&](std::vector<uint8_t>&o,uint32_t p,unsigned z){for(unsigned b=0;b<z;b++)o.push_back(p>>(8*b));};
 if(rt[i]>=0){auto&t=ts[rt[i]];kind=t.bits<=24?1:3;uint32_t pack=0,shift=0;for(auto&f:t.fields){uint32_t v=0;for(unsigned k=0;k<f.width;k++)v=v*10+(r[i][f.pos+k]-'0');if(f.lookup)v=std::lower_bound(f.values.begin(),f.values.end(),v)-f.values.begin();else v-=f.base;pack|=v<<shift;shift+=f.bits;}w8(streams[kind],t.id);pushp(streams[kind],pack,kind==1?3:4);
 }else if(hexlen[i]){kind=2;w16(streams[2],si.at(stemrow[i]));w32(streams[2],hex[i]);}
 else{kind=0;w16(streams[0],fi.at(r[i]));}types[i/4]|=kind<<((i%4)*2);counts[kind]++;}
 out.clear();w32(out,MAGIC);w64(out,n);w32(out,r.size());w32(out,fallback.size());w32(out,stems.size());w32(out,sel.size());for(auto c:counts)w32(out,c);
 for(auto j:sel){auto&t=ts[j];w16(out,t.base.size());w8(out,t.fields.size());w8(out,t.bits);out.insert(out.end(),t.base.begin(),t.base.end());for(auto&f:t.fields){w8(out,f.pos);w8(out,f.width);w8(out,f.bits);w8(out,f.lookup);w32(out,f.base);if(f.lookup){w16(out,f.values.size());for(auto v:f.values)w32(out,v);}}}
 std::vector<uint8_t>pool,packed;fc(out,stems,pool,true);fc(out,fallback,pool,false);if(!tb::encode(pool.data(),pool.size(),packed))return false;w32(out,packed.size());out.insert(out.end(),packed.begin(),packed.end());out.insert(out.end(),types.begin(),types.end());for(auto&s:streams)out.insert(out.end(),s.begin(),s.end());return true;
}
#else
struct F{uint8_t pos,width,bits,shift;uint32_t base,mask;std::vector<uint32_t>values;};
struct T{const uint8_t*base;uint16_t len;std::vector<F>fields;};
struct C{uint32_t parent,raw,first;uint16_t pre,len,enc;};
struct State{uint64_t rawsize;uint32_t nr;std::vector<T>ts;std::vector<C>st,fa;const uint8_t*streams[4];std::vector<uint32_t>refs;tb::State*suffix=nullptr;~State(){delete suffix;}};
static bool readn(const uint8_t*&p,const uint8_t*e,uint32_t&v){if(p>=e)return false;v=*p++;if(v==255){if(e-p<2)return false;v=r16(p);p+=2;}return true;}
static bool readfc(const uint8_t*&p,const uint8_t*e,uint32_t n,std::vector<C>&out){out.reserve(n);std::vector<uint32_t>stack;stack.reserve(256);for(uint32_t i=0;i<n;i++){uint32_t pre,suf;if(!readn(p,e,pre)||!readn(p,e,suf)||pre+suf>511)return false;if((!i&&pre)||(i&&pre>out[i-1].len))return false;while(!stack.empty()&&out[stack.back()].pre>=pre)stack.pop_back();uint32_t parent=stack.empty()?i:stack.back();out.push_back({parent,0,0,(uint16_t)pre,(uint16_t)(pre+suf),0});stack.push_back(i);}return true;}
static State* open(const uint8_t*p,size_t n){if(n<44||r32(p)!=MAGIC)return nullptr;const uint8_t*e=p+n;State*s=new State;s->rawsize=r64(p+4);s->nr=r32(p+12);uint32_t nf=r32(p+16),ns=r32(p+20),nt=r32(p+24),ct[4];for(unsigned k=0;k<4;k++)ct[k]=r32(p+28+4*k);p+=44;if(nt>256||nf>65535||ns>65535||uint64_t(ct[0])+ct[1]+ct[2]+ct[3]!=s->nr||uint64_t(s->nr)>4ull*n||s->nr>(1u<<29)||uint64_t(nf)+ns>n/2||uint64_t(nt)>n/4)goto bad;
 s->ts.reserve(nt);for(uint32_t i=0;i<nt;i++){if(e-p<4)goto bad;T t;t.len=r16(p);uint32_t fields=p[2];p+=4;if(e-p<t.len)goto bad;t.base=p;p+=t.len;unsigned shift=0;for(unsigned j=0;j<fields;j++){if(e-p<8)goto bad;F f;f.pos=p[0];f.width=p[1];f.bits=p[2];f.shift=shift;f.base=r32(p+4);unsigned lookup=p[3];p+=8;if(f.pos+f.width>t.len||!f.bits||f.bits>32||f.bits+shift>32||!f.width||f.width>9)goto bad;f.mask=f.bits==32?~0u:((1u<<f.bits)-1);shift+=f.bits;if(lookup){if(e-p<2)goto bad;unsigned cnt=r16(p);p+=2;if(!cnt||f.bits>11||cnt>(1u<<f.bits)||e-p<4*cnt)goto bad;f.values.resize(1u<<f.bits);for(unsigned k=0;k<cnt;k++){f.values[k]=r32(p);p+=4;}}t.fields.push_back(std::move(f));}s->ts.push_back(std::move(t));}
 if(!readfc(p,e,ns,s->st)||!readfc(p,e,nf,s->fa))goto bad;
 {if(e-p<4)goto bad;uint32_t psz=r32(p);p+=4;if(e-p<psz)goto bad;s->suffix=tb::open(p,psz);p+=psz;if(!s->suffix||s->suffix->h.rows!=ns+nf)goto bad;uint32_t rawoff=0,id=0;for(auto*vec:{&s->st,&s->fa})for(C&c:*vec){c.first=s->suffix->offset[id];uint32_t enc=s->suffix->offset[id+1]-s->suffix->offset[id];if(enc>65535)goto bad;c.enc=enc;c.raw=rawoff;rawoff+=c.len-c.pre+(id<ns);id++;}if(rawoff!=s->suffix->h.raw)goto bad;
 id=0;for(auto*vec:{&s->st,&s->fa})for(C&c:*vec){uint64_t bit=uint64_t(c.first)*s->suffix->h.bits;uint32_t mask=(1u<<s->suffix->h.bits)-1,len=0,want=c.len-c.pre+(id<ns);for(unsigned j=0;j<c.enc;j++){uint32_t pack;memcpy(&pack,s->suffix->data+(bit>>3),4);uint32_t v=(pack>>(bit&7))&mask;bit+=s->suffix->h.bits;if(v>=s->suffix->h.ns)goto bad;len+=s->suffix->len[v];if(len>want)goto bad;}if(len!=want)goto bad;id++;}
 }

 {uint64_t z=(s->nr+3ull)/4;if(uint64_t(e-p)<z+2ull*ct[0]+4ull*ct[1]+6ull*ct[2]+5ull*ct[3])goto bad;const uint8_t*types=p;p+=z;unsigned sizes[4]={2,4,6,5};for(unsigned j=0;j<4;j++){s->streams[j]=p;p+=uint64_t(ct[j])*sizes[j];}if(p!=e)goto bad;s->refs.resize(s->nr);uint32_t rank[4]={};for(uint32_t i=0;i<s->nr;i++){uint32_t k=(types[i/4]>>((i%4)*2))&3;s->refs[i]=(rank[k]++<<2)|k;}for(int j=0;j<4;j++)if(rank[j]!=ct[j])goto bad;
 uint64_t total=0;for(uint32_t kind=0;kind<4;kind++){const uint8_t*q=s->streams[kind];for(uint32_t i=0;i<ct[kind];i++,q+=sizes[kind]){uint32_t len;if(kind==0){uint32_t id=r16(q);if(id>=nf)goto bad;len=s->fa[id].len;}else if(kind==2){uint32_t id=r16(q),v=r32(q+2);if(id>=ns)goto bad;len=s->st[id].len+6+(v?(35-__builtin_clz(v))/4:1);}else{uint32_t id=*q;if(id>=nt)goto bad;len=s->ts[id].len;}total+=len;}}if(total!=s->rawsize)goto bad;
 }
 return s;bad:delete s;return nullptr;}
static const char pairs[]="00010203040506070809101112131415161718192021222324252627282930313233343536373839404142434445464748495051525354555657585960616263646566676869707172737475767778798081828384858687888990919293949596979899";
static inline void digits(uint8_t*d,uint32_t v,unsigned w){while(w>=2){unsigned a=v%100;v/=100;w-=2;memcpy(d+w,pairs+a*2,2);}if(w)*d='0'+v;}
static inline uint32_t templateout(State*s,const uint8_t*p,unsigned kind,uint8_t*out){const T&t=s->ts[*p++];uint32_t v=kind==1?(uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)):r32(p);memcpy(out,t.base,t.len);for(const F&f:t.fields){uint32_t x=(v>>f.shift)&f.mask;x=f.values.empty()?x+f.base:f.values[x];digits(out+f.pos,x,f.width);}return t.len;}
static inline void suffixcopy(State*s,const C&c,uint8_t*out,uint32_t need,uint8_t*limit){uint64_t bit=uint64_t(c.first)*s->suffix->h.bits;uint32_t mask=(1u<<s->suffix->h.bits)-1;while(need){uint32_t pack;memcpy(&pack,s->suffix->data+(bit>>3),4);uint32_t v=(pack>>(bit&7))&mask;bit+=s->suffix->h.bits;unsigned n=std::min<unsigned>(s->suffix->len[v],need);if(limit-out>=32)_mm256_storeu_si256((__m256i*)out,_mm256_loadu_si256((const __m256i*)s->suffix->dict[v].data()));else memcpy(out,s->suffix->dict[v].data(),n);out+=n;need-=n;}}
static inline void onefc(State*s,const std::vector<C>&v,uint32_t i,uint8_t*out){uint32_t cover=v[i].len,total=cover,n=0,indices[512],needs[512];for(;;){const C&c=v[i];if(c.pre<cover){indices[n]=i;needs[n++]=cover-c.pre;cover=c.pre;}if(!cover)break;i=c.parent;}while(n){--n;const C&c=v[indices[n]];suffixcopy(s,c,out+c.pre,needs[n],out+total);}}
static inline uint32_t tropeout(State*s,const uint8_t*p,uint8_t*out){uint32_t id=r16(p),n=s->st[id].len;onefc(s,s->st,id,out);memcpy(out+n,"/int_",5);uint32_t v=r32(p+2),w=v?(35-__builtin_clz(v))/4:1;static const char hex[]="0123456789abcdef";for(unsigned j=0;j<w;j++)out[n+5+w-1-j]=hex[(v>>(4*j))&15];out[n+5+w]='\n';return n+6+w;}
struct Expanded{std::vector<uint8_t>bytes;std::vector<uint32_t>off;};
static Expanded expand(const std::vector<C>&v,const uint8_t*pool){Expanded x;x.off.resize(v.size()+1);uint32_t total=0;for(uint32_t i=0;i<v.size();i++){x.off[i]=total;total+=v[i].len;}x.off[v.size()]=total;x.bytes.resize(total);for(uint32_t i=0;i<v.size();i++){auto&c=v[i];uint8_t*q=x.bytes.data()+x.off[i];if(c.pre){memcpy(q,x.bytes.data()+x.off[c.parent],c.pre);}memcpy(q+c.pre,pool+c.raw,c.len-c.pre);}return x;}
static int64_t decode(State*s,uint8_t*out,size_t cap,uint64_t*offs=nullptr){if(offs)offs[0]=0;if(cap<s->rawsize)return -1;std::vector<uint8_t>pool(s->suffix->h.raw);if(tb::decode(s->suffix,pool.data(),pool.size())<0)return -1;Expanded a=expand(s->fa,pool.data()),b=expand(s->st,pool.data());uint8_t*q=out;const uint8_t*p[4];for(int k=0;k<4;k++)p[k]=s->streams[k];for(uint32_t i=0;i<s->nr;i++){unsigned k=s->refs[i]&3;if(k==0){uint32_t id=r16(p[0]);p[0]+=2;unsigned z=s->fa[id].len;memcpy(q,a.bytes.data()+a.off[id],z);q+=z;}else if(k==2){uint32_t id=r16(p[2]);unsigned z=s->st[id].len;memcpy(q,b.bytes.data()+b.off[id],z);memcpy(q+z,"/int_",5);uint32_t v=r32(p[2]+2),w=v?(35-__builtin_clz(v))/4:1;static const char hx[]="0123456789abcdef";for(unsigned j=0;j<w;j++)q[z+5+w-1-j]=hx[(v>>(4*j))&15];q[z+5+w]='\n';q+=z+6+w;p[2]+=6;}else{q+=templateout(s,p[k],k,q);p[k]+=k==1?4:5;}if(offs)offs[i+1]=q-out;}return q-out==s->rawsize?q-out:-1;}
static int64_t rows(State*s,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offs){if(!s||!offs||(!ids&&count)||(!out&&cap))return -1;if(count==s->nr){bool all=true;for(size_t i=0;i<count;i++)if(ids[i]!=i){all=false;break;}if(all)return decode(s,out,cap,offs);}offs[0]=0;uint8_t*q=out;static const unsigned sizes[4]={2,4,6,5};for(size_t i=0;i<count;i++){if(ids[i]>=s->nr)return -1;uint32_t r=s->refs[ids[i]],k=r&3;const uint8_t*p=s->streams[k]+size_t(r>>2)*sizes[k];uint32_t len=k==0?s->fa[r16(p)].len:k==2?s->st[r16(p)].len+6+(r32(p+2)?(35-__builtin_clz(r32(p+2)))/4:1):s->ts[*p].len;if(size_t(q-out)+len>cap)return -1;if(k==0)onefc(s,s->fa,r16(p),q);else if(k==2)tropeout(s,p,q);else templateout(s,p,k,q);q+=len;offs[i+1]=q-out;}return q-out;}
#endif
}
