#pragma once
#include "context.h"
#include <memory>
#include <string>
namespace tpl {
static void w16(std::vector<uint8_t>&o,uint16_t x){o.push_back(x);o.push_back(x>>8);}
static uint16_t r16(const uint8_t*p){return p[0]|uint16_t(p[1])<<8;}
static unsigned bits(uint64_t x){return x?64-__builtin_clzll(x):0;}
static void put(std::vector<uint8_t>&o,uint64_t pos,uint64_t x,unsigned width){for(unsigned j=0;j<width;){unsigned k=std::min(8u-unsigned(pos&7),width-j);o[pos>>3]|=uint8_t((x>>j)&((1u<<k)-1))<<(pos&7);pos+=k;j+=k;}}
static uint64_t get(const uint8_t*p,uint64_t pos,unsigned width){if(!width)return 0;__uint128_t x;memcpy(&x,p+(pos>>3),16);x>>=pos&7;return uint64_t(x)&(width==64?UINT64_MAX:((1ull<<width)-1));}
struct Field{uint16_t pos=0;uint8_t width=0,nbits=0;uint64_t lo=0,hi=0;std::vector<uint64_t>values;};
struct Template{std::string literal;std::vector<Field>fields;uint32_t totalbits=0;};
#ifdef LAB_ENCODER
static std::vector<uint8_t> encode(const uint8_t*raw,size_t n,unsigned max_templates=64,unsigned residual_G=5){
 if(n<3000000||n>UINT32_MAX||n<4||memcmp(raw,"http",4))return {};
 struct Group{Template t;uint32_t count=0,id=0;std::string key;};
 std::unordered_map<std::string,uint32_t>lookup;std::vector<Group>gs;std::vector<uint32_t>rows,starts,lens;size_t start=0;
 for(size_t end=0;end<=n;++end){if(end<n&&raw[end]!='\n')continue;if(end==n&&start==n)break;size_t len=end-start+(end<n);starts.push_back(start);lens.push_back(len);uint32_t gid=UINT32_MAX;
  if(len<=65535){std::string key((const char*)raw+start,len);std::vector<Field>fs;bool good=true;for(size_t j=0;j<len;){if(key[j]<'0'||key[j]>'9'){++j;continue;}size_t b=j;uint64_t value=0;while(j<len&&key[j]>='0'&&key[j]<='9'){if(j-b>=19){good=false;break;}value=value*10+key[j]-'0';key[j++]='0';}if(!good)break;fs.push_back({uint16_t(b),uint8_t(j-b),0,value,value});}if(good&&!fs.empty()&&fs.size()<=255){auto it=lookup.find(key);if(it==lookup.end()){gid=gs.size();lookup.emplace(key,gid);Group g;g.key=std::move(key);g.t.literal.assign((const char*)raw+start,len);g.t.fields=std::move(fs);g.count=1;gs.push_back(std::move(g));}else{gid=it->second;auto&g=gs[gid];g.count++;for(size_t j=0;j<fs.size();++j){g.t.fields[j].lo=std::min(g.t.fields[j].lo,fs[j].lo);g.t.fields[j].hi=std::max(g.t.fields[j].hi,fs[j].hi);}}}}
  rows.push_back(gid);start=end+1;
 }
 std::vector<uint32_t>order(gs.size());for(uint32_t i=0;i<gs.size();++i)order[i]=i;std::sort(order.begin(),order.end(),[&](uint32_t a,uint32_t b){return gs[a].count!=gs[b].count?gs[a].count>gs[b].count:gs[a].key<gs[b].key;});size_t nt=0,covered=0;for(auto id:order){if(nt==std::min(254u,max_templates)||gs[id].count<8)break;gs[id].id=++nt;covered+=gs[id].count;for(auto&f:gs[id].t.fields){f.nbits=bits(f.hi-f.lo);gs[id].t.totalbits+=f.nbits;}}
 if(covered<rows.size()/3||nt==0)return {};
 // Sparse numeric alphabets (years, newspaper IDs) use local value tables.
 for(size_t i=0;i<rows.size();++i)if(rows[i]!=UINT32_MAX&&gs[rows[i]].id){for(auto&f:gs[rows[i]].t.fields){uint64_t value=0;for(unsigned j=0;j<f.width;++j)value=value*10+raw[starts[i]+f.pos+j]-'0';f.values.push_back(value);}}
 for(size_t j=0;j<nt;++j){auto&g=gs[order[j]];g.t.totalbits=0;for(auto&f:g.t.fields){auto&v=f.values;std::sort(v.begin(),v.end());v.erase(std::unique(v.begin(),v.end()),v.end());unsigned db=bits(v.size()-1);if(f.nbits>db&&uint64_t(g.count)*(f.nbits-db)>(4+8*v.size())*8+32)f.nbits=db;else v.clear();g.t.totalbits+=f.nbits;}}

 std::vector<uint64_t>freq(nt+1);uint64_t numericbits=0;std::vector<uint8_t>residual;for(size_t i=0;i<rows.size();++i){uint32_t type=rows[i]==UINT32_MAX?0:gs[rows[i]].id;freq[type]++;if(type)numericbits+=gs[rows[i]].t.totalbits;else residual.insert(residual.end(),raw+starts[i],raw+starts[i]+lens[i]);}
 if(residual.empty())return {};auto rem=ctx::encode(residual.data(),residual.size(),32768,9,residual_G);auto hl=ctx::makelens(freq);auto hc=ctx::codes(hl);uint64_t typebits=0;for(size_t i=0;i<freq.size();++i)typebits+=freq[i]*hl[i];std::vector<uint8_t>types((typebits+7)/8+16),numbers((numericbits+7)/8+16);uint64_t tp=0,np=0;
 for(size_t i=0;i<rows.size();++i){uint32_t type=rows[i]==UINT32_MAX?0:gs[rows[i]].id;put(types,tp,hc[type],hl[type]);tp+=hl[type];if(type){const auto&g=gs[rows[i]];for(auto&f:g.t.fields){uint64_t value=0;for(unsigned j=0;j<f.width;++j)value=value*10+raw[starts[i]+f.pos+j]-'0';uint64_t code=f.values.empty()?value-f.lo:std::lower_bound(f.values.begin(),f.values.end(),value)-f.values.begin();put(numbers,np,code,f.nbits);np+=f.nbits;}}}
 std::vector<uint8_t>o;ctx::w32(o,0x314c5054);ctx::w32(o,n);ctx::w32(o,rows.size());ctx::w32(o,nt);ctx::w64(o,typebits);ctx::w64(o,numericbits);ctx::w64(o,rem.size());for(size_t j=0;j<nt;++j){auto&t=gs[order[j]].t;w16(o,t.literal.size());o.push_back(t.fields.size());o.push_back(0);o.insert(o.end(),t.literal.begin(),t.literal.end());for(auto&f:t.fields){w16(o,f.pos);o.push_back(f.width);o.push_back(f.nbits|(f.values.empty()?0:128));ctx::w64(o,f.lo);if(!f.values.empty()){ctx::w32(o,f.values.size());for(auto v:f.values)ctx::w64(o,v);}}}o.insert(o.end(),hl.begin(),hl.end());o.insert(o.end(),types.begin(),types.end());o.insert(o.end(),numbers.begin(),numbers.end());o.insert(o.end(),rem.begin(),rem.end());return o;
}
#endif
struct State{
 bool valid=false;uint32_t rawbytes=0,nrows=0;std::vector<Template>templates;std::vector<uint64_t>index;const uint8_t*numbers=nullptr;std::unique_ptr<ctx::State>residual;
 State(const uint8_t*a,size_t n){try{
  if(n<40||ctx::rd32(a)!=0x314c5054)return;rawbytes=ctx::rd32(a+4);nrows=ctx::rd32(a+8);uint32_t nt=ctx::rd32(a+12);uint64_t typebits=ctx::rd64(a+16),numericbits=ctx::rd64(a+24),rembytes=ctx::rd64(a+32);if(!nt||nt>254||nrows>rawbytes||nrows>typebits||typebits>16ull*nrows||numericbits>512ull*rawbytes||rembytes>n)return;
  const uint8_t*p=a+40,*end=a+n;templates.resize(nt+1);for(uint32_t j=1;j<=nt;++j){if(end-p<4)return;unsigned len=r16(p),nf=p[2];p+=4;if(size_t(end-p)<len+12ull*nf)return;auto&t=templates[j];t.literal.assign((const char*)p,len);p+=len;for(unsigned k=0;k<nf;++k){if(end-p<12)return;Field f;f.pos=r16(p);f.width=p[2];bool dict=p[3]&128;f.nbits=p[3]&127;f.lo=ctx::rd64(p+4);p+=12;if(dict){if(end-p<4)return;uint32_t nv=ctx::rd32(p);p+=4;if(!nv||nv>65536||size_t(end-p)<uint64_t(nv)*8)return;f.values.resize(nv);for(auto&v:f.values){v=ctx::rd64(p);p+=8;}if(bits(nv-1)!=f.nbits)return;}if(f.width<1||f.width>19||f.nbits>64||f.pos+f.width>len)return;t.totalbits+=f.nbits;t.fields.push_back(f);}}
  if(size_t(end-p)<nt+1)return;std::vector<uint8_t>hl(p,p+nt+1);p+=nt+1;for(auto b:hl)if(b>16)return;auto hc=ctx::codes(hl);std::vector<ctx::Tab>tab(65536);for(uint32_t j=0;j<=nt;++j)if(hl[j])for(uint32_t c=hc[j];c<65536;c+=1u<<hl[j]){if(tab[c].bits)return;tab[c]={uint16_t(j),hl[j],0};}
  uint64_t tbytes=(typebits+7)/8+16,nbytes=(numericbits+7)/8+16;if(tbytes>size_t(end-p)||nbytes>size_t(end-p)-tbytes||rembytes!=size_t(end-p)-tbytes-nbytes)return;const uint8_t*types=p;numbers=p+tbytes;residual.reset(new ctx::State(numbers+nbytes,rembytes));if(!residual->valid)return;
  index.resize(nrows);uint64_t tp=0,np=0,rawtotal=residual->rawsize;uint32_t rid=0;for(uint32_t i=0;i<nrows;++i){if(tp>=typebits)return;auto e=tab[(ctx::rd64(types+(tp>>3))>>(tp&7))&65535];if(!e.bits||e.sym>nt)return;tp+=e.bits;if(e.sym){index[i]=(np<<8)|e.sym;np+=templates[e.sym].totalbits;rawtotal+=templates[e.sym].literal.size();}else{index[i]=uint64_t(rid++)<<8;}}if(tp!=typebits||np!=numericbits||rid!=residual->nrows||rawtotal!=rawbytes)return;valid=true;
 }catch(...){valid=false;}}
};
static int64_t row(State*s,uint32_t id,uint8_t*out,size_t cap){if(!s||!s->valid||id>=s->nrows)return -1;uint64_t ref=s->index[id];unsigned type=ref&255;ref>>=8;if(!type)return ctx::row(s->residual.get(),ref,out,cap);const auto&t=s->templates[type];if(cap<t.literal.size())return -1;memcpy(out,t.literal.data(),t.literal.size());for(auto&f:t.fields){if(!f.nbits)continue;uint64_t x=get(s->numbers,ref,f.nbits);if(f.values.empty())x+=f.lo;else{if(x>=f.values.size())return -1;x=f.values[x];}ref+=f.nbits;for(unsigned j=0;j<f.width;++j){out[f.pos+f.width-1-j]='0'+x%10;x/=10;}}return t.literal.size();}
static int64_t decode(State*s,uint8_t*out,size_t cap){if(!s||!s->valid||cap<s->rawbytes)return -1;size_t pos=0;uint64_t cursor=0;uint32_t current=UINT32_MAX;for(uint32_t i=0;i<s->nrows;++i){uint64_t ref=s->index[i];int64_t z=(ref&255)?row(s,i,out+pos,cap-pos):ctx::row_seek(s->residual.get(),ref>>8,out+pos,cap-pos,cursor,current);if(z<0)return -1;pos+=z;}return pos==s->rawbytes?pos:-1;}
static int64_t rows(State*s,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets){if(!s||!s->valid||!offsets)return -1;offsets[0]=0;size_t pos=0;uint64_t cursor=0;uint32_t current=UINT32_MAX;for(size_t i=0;i<count;++i){if(ids[i]>=s->nrows)return -1;uint64_t ref=s->index[ids[i]];int64_t z=(ref&255)?row(s,ids[i],out+pos,cap-pos):ctx::row_seek(s->residual.get(),ref>>8,out+pos,cap-pos,cursor,current);if(z<0)return -1;pos+=z;offsets[i+1]=pos;}return pos;}
}
