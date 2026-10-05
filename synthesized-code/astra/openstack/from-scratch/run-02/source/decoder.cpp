
#include "common.h"
#include "time_extra.h"
#include <string_view>
#include "codec.h"
#include <memory>
struct Field {uint32_t off,len,type;std::vector<uint64_t> v;};
struct Template {Bytes text;std::vector<Field>f;size_t count,idx;uint32_t toff,doff=0,dend=0;bool detail=false;};
struct State {size_t size;std::vector<Template>ts;Bytes dict,lens;std::vector<uint64_t>order,times;Bytes tmodes;};
extern "C" void* lab_open(const uint8_t*archive,size_t size){
 try{
  if(!archive||size<8||memcmp(archive,"OSTK005",8))return nullptr;
  Reader r(archive+8,size-8);std::unique_ptr<State>s(new State);
  s->size=r.get();size_t nr=r.get(),nt=r.get(),nd=r.get(),ms=r.get();
  if(s->size>64000000||nr>300000||!nr||nt>512||!nt||nd>300000||ms>4000000)return nullptr;
  size_t n=r.get();auto p=r.take(n);Bytes meta;if(!lz::decode(p,n,meta,ms)||meta.size()!=ms)return nullptr;
  size_t dn=r.get();Reader d(r.take(dn),dn);s->dict.resize(nd*40);s->lens.resize(nd);
  const char*hex="0123456789abcdef";unsigned mode=d.byte();if(mode!=2)return nullptr;
  size_t nrepeat=d.get(),nextunique=nrepeat;if(nrepeat>nd)return nullptr;
  size_t ne=d.get();if(ne>nd)return nullptr;
  for(size_t k=0;k<ne;++k){
   size_t ix=d.get(),len=d.get();if(ix>=nd||s->lens[ix]||(len!=32&&len!=36&&len!=40))return nullptr;
   s->lens[ix]=len;uint8_t*v=s->dict.data()+40*ix;unsigned nib=0;uint8_t byte=0;
   for(size_t j=0;j<len;++j){if(len==36&&(j==8||j==13||j==18||j==23)){v[j]='-';continue;}if(!(nib&1))byte=d.byte();v[j]=hex[(nib&1)?byte&15:byte>>4];++nib;}
  }
  uint32_t bitbuf=0;unsigned nbits=0;
  for(size_t ix=0;ix<nd;++ix){
   if(s->lens[ix])continue;size_t len=36;if(len!=36&&len!=32&&len!=40)return nullptr;s->lens[ix]=len;uint8_t*v=s->dict.data()+40*ix;bool uuid=len==36;unsigned nib=0;uint8_t byte=0;
   for(size_t j=0;j<len;++j){if(uuid&&(j==8||j==13||j==18||j==23)){v[j]='-';continue;}
    unsigned val;
    if(mode){unsigned w=4;if(nib==12){w=0;val=4;}else if(nib==16)w=2;
     if(w){if(nbits<w){bitbuf|=uint32_t(d.byte())<<nbits;nbits+=8;}val=bitbuf&((1u<<w)-1);bitbuf>>=w;nbits-=w;if(nib==16)val|=8;}}
    else{if(!(nib&1))byte=d.byte();val=(nib&1)?byte&15:byte>>4;}
    v[j]=hex[val];++nib;
   }
  }
  if(!d.done())return nullptr;
  s->order=readints(r,nr);for(auto v:s->order)if(v>=nt)return nullptr;
  size_t tsize=r.get();const uint8_t*tp=r.take(tsize);
  if(!time_extra::decode(tp,tsize,s->order,nt,s->times,s->tmodes))return nullptr;
  s->ts.resize(nt);Reader m(meta);size_t rows=0,total=0,fields=0;
  std::vector<size_t>counts(nt);for(auto i:s->order)++counts[i];
  for(size_t ti=0;ti<nt;++ti){
   auto&t=s->ts[ti];size_t len=m.get();if(len>1000000)return nullptr;auto text=m.take(len);t.text.assign(text,text+len);t.count=m.get();t.toff=m.get();
   if(t.count!=counts[ti]||t.toff>len||len-t.toff<12)return nullptr;rows+=t.count;total+=t.count*len;
   std::string_view view((const char*)text,len);size_t dp=view.rfind(" time: ");
   if(dp!=std::string_view::npos){
    t.doff=dp+7;size_t point=view.find('.',t.doff);
    if(point==std::string_view::npos||point-t.doff>6||point==t.doff||point+8>len)return nullptr;
    t.dend=point+8;
    for(size_t j=t.doff;j<t.dend;++j)if(j!=point&&(text[j]<'0'||text[j]>'9'))return nullptr;
    t.detail=view.find("/servers/detail HTTP/")!=std::string_view::npos;
   }
   size_t nf=m.get();if(nf>len||nf>10000)return nullptr;t.f.resize(nf);
   for(auto&f:t.f){f.off=m.get();f.len=m.get();f.type=m.get();if(f.off>len||f.len>len-f.off||!f.len||f.type>2)return nullptr;
    if((f.type==0&&f.len>18)||(f.type==1&&f.len!=36&&f.len!=32&&f.len!=40))return nullptr;
    fields+=t.count;if(fields>5000000)return nullptr;f.v=readints(r,t.count);
    if(f.type==1){for(auto &v:f.v){if(v>nrepeat)return nullptr;v=v?v-1:nextunique++;if(v>=nd||s->lens[v]!=f.len)return nullptr;}}
    else if(f.type==2){if(f.len!=7)return nullptr;for(auto v:f.v)if(v>=4194304)return nullptr;}
    else {uint64_t lim=1;for(unsigned j=0;j<f.len;++j)lim*=10;for(auto v:f.v)if(v>=lim)return nullptr;}
   }
  }
  if(nextunique!=nd||!m.done()||!r.done()||rows!=nr||total!=s->size)return nullptr;
  return s.release();
 }catch(...){return nullptr;}
}
extern "C" int64_t lab_decode(void*state,uint8_t*out,size_t cap){
 if(!state)return -1;State&s=*(State*)state;if(cap<s.size||(!out&&s.size))return -1;
 for(auto&t:s.ts)t.idx=0;
 uint8_t*p=out;uint64_t prev=0,lastapi=0;int64_t lastdetail=0;
 for(size_t i=0;i<s.order.size();++i){
  auto&t=s.ts[s.order[i]];size_t k=t.idx++;memcpy(p,t.text.data(),t.text.size());
  
  for(auto&f:t.f){uint64_t v=f.v[k];if(f.type==1)memcpy(p+f.off,s.dict.data()+40*v,f.len);else {if(f.type==2)v=(v*10000000+2097152)>>22;decimal(p+f.off,f.len,v);}}
  uint64_t duration=0;bool api=t.doff!=0;
  if(api){for(unsigned j=t.doff;j<t.dend;++j){if(p[j]=='.')continue;if(p[j]<'0'||p[j]>'9')return -1;duration=duration*10+p[j]-'0';}duration=(duration+5000)/10000;}
  unsigned mode=s.tmodes[i];uint64_t prediction=prev;
  if(mode==1&&api)prediction+=duration;
  if(mode>=2&&api&&lastapi){prediction=lastapi+duration+5;if(mode==3&&t.detail&&lastdetail<500)prediction+=1000;}
  uint64_t now=prediction+(mode?unzz(s.times[i]):s.times[i]);
  if(now<prev||now>=345600000)return -1;
  timeformat(p+t.toff,now%86400000);
  if(api){if(t.detail)lastdetail=int64_t(now-lastapi-duration);lastapi=now;}prev=now;
  p+=t.text.size();
 }
 return p-out;
}
extern "C" void lab_close(void*state){delete (State*)state;}
