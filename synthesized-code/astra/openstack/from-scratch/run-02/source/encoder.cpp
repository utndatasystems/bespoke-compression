
#include "common.h"
#include "time_extra.h"
#include <array>
#include "codec.h"
#include <string>
#include <unordered_map>
#include <cstdio>
struct F {unsigned off,len,type;};
struct T {std::string text;std::vector<F> fields;std::vector<std::vector<uint64_t>>cols;size_t count=0;unsigned toff;};
extern "C" int64_t lab_encode(const uint8_t*raw,size_t size,uint8_t*out,size_t cap){
 try{
  if(size>64000000)return -1;
  std::unordered_map<std::string,unsigned> tm,dm;
  std::vector<T>ts;std::vector<std::string>dict;std::vector<uint64_t>order;
  std::array<std::vector<uint64_t>,4>tv;uint64_t prevtime=0,lastapi=0,day=0;int64_t lastdetail=0;
  for(size_t start=0;start<size;){
   size_t end=start;while(end<size&&raw[end]!='\n')++end;if(end<size)++end;
   std::string s((const char*)raw+start,end-start),key=s;
   size_t sp=s.find(' ');if(sp==std::string::npos||sp+24>=s.size())return -1;
   unsigned toff=sp+12;
   auto dn=[&](unsigned p,unsigned n){uint64_t v=0;for(unsigned j=0;j<n;++j){char c=s[p+j];if(c<'0'||c>'9')throw 1;v=v*10+c-'0';}return v;};
   if(s[toff+2]!=':'||s[toff+5]!=':'||s[toff+8]!='.')return -1;
   uint64_t time=((dn(toff,2)*60+dn(toff+3,2))*60+dn(toff+6,2))*1000+dn(toff+9,3);
   if(time<prevtime%86400000)day+=86400000;time+=day;
   size_t dp=s.rfind(" time: ");bool api=dp!=std::string::npos;bool detail=s.find("/servers/detail HTTP/")!=std::string::npos;uint64_t duration=0;
   if(api){dp+=7;size_t point=s.find('.',dp);if(point==std::string::npos||point+8>=s.size())throw 1;duration=(dn(dp,point-dp)*10000000+dn(point+1,7)+5000)/10000;}
   uint64_t p2=(api&&lastapi)?lastapi+duration+5:prevtime;
   uint64_t p3=p2+((api&&lastapi&&detail&&lastdetail<500)?1000:0);
   tv[0].push_back(time-prevtime);tv[1].push_back(zz(time-prevtime-(api?duration:0)));tv[2].push_back(zz(time-p2));tv[3].push_back(zz(time-p3));
   if(api){if(detail)lastdetail=int64_t(time-lastapi-duration);lastapi=time;}prevtime=time;
   std::vector<F>fs;std::vector<uint64_t>values;
   for(size_t p=0;p<s.size();){
    if(p==toff){key.replace(p,12,12,'@');p+=12;continue;}
    size_t n=0;unsigned type=0;uint64_t val=0;
    bool uuid=p+36<=s.size();
    if(uuid)for(unsigned j=0;j<36;++j){if(j==8||j==13||j==18||j==23){if(s[p+j]!='-'){uuid=false;break;}}else if(!ishex(s[p+j])){uuid=false;break;}}
    if(uuid){n=36;type=1;}
    else {size_t j=p;while(j<s.size()&&ishex(s[j]))++j;if(j-p>=32){n=std::min<size_t>(j-p,40);type=1;}}
    if(type==1){std::string x=s.substr(p,n);auto it=dm.find(x);if(it==dm.end()){val=dict.size();dm.emplace(x,val);dict.push_back(x);}else val=it->second;}
    else if(s[p]>='0'&&s[p]<='9'){n=1;while(p+n<s.size()&&s[p+n]>='0'&&s[p+n]<='9')++n;if(n>18)throw 1;val=dn(p,n);}
    if(!n){++p;continue;}
    fs.push_back({unsigned(p),unsigned(n),type});values.push_back(val);key.replace(p,n,n,type?'#':'~');p+=n;
   }
   auto it=tm.find(key);unsigned id;
   if(it==tm.end()){id=ts.size();tm.emplace(std::move(key),id);T t;t.text=s;t.fields=fs;t.cols.resize(fs.size());t.toff=toff;ts.push_back(std::move(t));}else id=it->second;
   auto&t=ts[id];if(t.fields.size()!=fs.size())throw 1;for(size_t j=0;j<values.size();++j)t.cols[j].push_back(values[j]);++t.count;order.push_back(id);start=end;
  }
  std::vector<uint32_t>used(dict.size());
  for(auto&t:ts)for(size_t j=0;j<t.cols.size();++j){auto&c=t.cols[j];bool varying=false;for(auto v:c)if(v!=c[0]){varying=true;break;}if(!varying)c.clear();else if(t.fields[j].type)for(auto v:c)++used[v];}
  std::vector<unsigned>serial;
  for(size_t j=0;j<dict.size();++j)if(used[j]>1)serial.push_back(j);
  size_t nrepeat=serial.size();
  for(auto&t:ts)for(size_t j=0;j<t.cols.size();++j)if(t.fields[j].type==1)for(auto v:t.cols[j])if(used[v]==1)serial.push_back(v);
  std::vector<uint64_t>remap(dict.size());size_t nd=serial.size();for(size_t i=0;i<nd;++i)remap[serial[i]]=i;
  Bytes db;
  std::vector<unsigned>exception;
  for(size_t j=0;j<dict.size();++j)if(used[j]){auto &x=dict[j];if(x.size()!=36||x[14]!='4'||x[19]<'8'||x[19]>'b')exception.push_back(j);}
  db.push_back(2);put(db,nrepeat);put(db,exception.size());
  for(unsigned j:exception){
   put(db,remap[j]);auto&s=dict[j];put(db,s.size());int first=-1;
   for(auto c:s)if(c!='-'){unsigned v=hx(c);if(first<0)first=v;else{db.push_back((first<<4)|v);first=-1;}}if(first>=0)db.push_back(first<<4);
  }
  uint32_t bitbuf=0;unsigned nbits=0;
  for(auto j:serial){
   auto&s=dict[j];if(s.size()!=36||s[14]!='4'||s[19]<'8'||s[19]>'b')continue;
   unsigned nib=0;for(auto ch:s)if(ch!='-'){
    unsigned v=hx(ch),w=4;if(nib==12)w=0;else if(nib==16){w=2;v&=3;}++nib;
    if(!w)continue;bitbuf|=v<<nbits;nbits+=w;if(nbits>=8){db.push_back(bitbuf);bitbuf>>=8;nbits-=8;}
   }
  }
  if(nbits)db.push_back(bitbuf);
  Bytes meta,columns;size_t ct[3]={0,0,0};
  for(auto&t:ts){
   t.text.replace(t.toff,12,12,'0');
   for(size_t j=0;j<t.cols.size();++j)if(!t.cols[j].empty())t.text.replace(t.fields[j].off,t.fields[j].len,t.fields[j].len,'0');
  }
  for(auto&t:ts){
   put(meta,t.text.size());meta.insert(meta.end(),t.text.begin(),t.text.end());put(meta,t.count);put(meta,t.toff);
   unsigned nf=0;for(auto&c:t.cols)if(!c.empty())++nf;put(meta,nf);
   for(size_t j=0;j<t.cols.size();++j)if(!t.cols[j].empty()){
    auto f=t.fields[j];auto&c=t.cols[j];
    if(f.type==0&&f.len==7){
     bool quant=true;for(auto v:c){uint64_t q=(v*4194304+5000000)/10000000;if(((q*10000000+2097152)>>22)!=v){quant=false;break;}}
     if(quant){f.type=2;for(auto&v:c)v=(v*4194304+5000000)/10000000;}
    }
    put(meta,f.off);put(meta,f.len);put(meta,f.type);
    if(f.type==1)for(auto&v:c)v=used[v]==1?0:remap[v]+1;Bytes ec=ints_encode(c);ct[f.type]+=ec.size();block(columns,ec);
   }
  }
  Bytes md=lz::encode_optimal(meta.data(),meta.size());
  
  Bytes b;const char magic[]="OSTK005";b.insert(b.end(),magic,magic+8);
  put(b,size);put(b,order.size());put(b,ts.size());put(b,nd);put(b,meta.size());block(b,md);block(b,db);Bytes o=ints_encode(order,true),ti=time_extra::encode(order,ts.size(),tv);block(b,o);block(b,ti);b.insert(b.end(),columns.begin(),columns.end());
  fprintf(stderr,"archive %zu: templates=%zu dict=%zu meta=%zu->%zu db=%zu cols=%zu order=%zu time=%zu total=%zu\n",size,ts.size(),nd,meta.size(),md.size(),db.size(),columns.size(),o.size(),ti.size(),b.size());
  fprintf(stderr,"  columns numeric=%zu UUID=%zu quantized=%zu\n",ct[0],ct[1],ct[2]);if(b.size()>cap)return -1;memcpy(out,b.data(),b.size());return b.size();
 }catch(...){return -1;}
}
