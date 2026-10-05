#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include "codec.h"
using namespace std;
static bool dig(char c){return c>='0'&&c<='9';}
static bool hex(char c){return dig(c)||(c>='a'&&c<='f');}
static uint64_t num(const string&s){uint64_t x=0;for(char c:s)x=x*10+c-'0';return x;}
static uint32_t tm(const uint8_t*p){return ((p[0]-48)*10+p[1]-48)*3600000+((p[3]-48)*10+p[4]-48)*60000+((p[6]-48)*10+p[7]-48)*1000+(p[9]-48)*100+(p[10]-48)*10+p[11]-48;}
static bool istime(const uint8_t*p){return dig(p[0])&&dig(p[1])&&p[2]==':'&&dig(p[3])&&dig(p[4])&&p[5]==':'&&dig(p[6])&&dig(p[7])&&p[8]=='.'&&dig(p[9])&&dig(p[10])&&dig(p[11]);}
static bool isuuid(const uint8_t*p){for(int i=0;i<36;i++)if(i==8||i==13||i==18||i==23){if(p[i]!='-')return false;}else if(!hex(p[i]))return false;return true;}
static vector<uint8_t> packed(const string&s){vector<uint8_t>r;int hi=-1;for(char c:s)if(c!='-'){int v=c<='9'?c-'0':c-'a'+10;if(hi<0)hi=v;else{r.push_back(hi*16+v);hi=-1;}}return r;}
struct EF{uint16_t off;uint8_t width,type,nb;bool varying=false;unordered_map<string,uint32_t> dict;vector<string> vals;};
struct ET{string literal;uint16_t toff;vector<EF> f;uint32_t count=0;vector<string>first;};
struct EL{uint32_t tid,ts;vector<string>v;};
static void put(vector<uint8_t>&v,uint64_t x,int n){for(int i=0;i<n;i++)v.push_back(x>>(8*i));}
extern "C" int64_t lab_encode(const uint8_t*raw,size_t size,uint8_t*out,size_t cap){try{
 vector<ET>ts;vector<EL>ls;unordered_map<string,uint32_t> map;unordered_map<string,uint32_t>ucount;
 for(size_t p=0;p<size;){size_t e=p;while(e<size&&raw[e]!='\n')e++;if(e<size)e++;size_t len=e-p;if(len>65535)return -1;size_t tp=0;while(tp+12<=len&&!istime(raw+p+tp))tp++;if(tp+12>len)return -1;
  string lit((const char*)raw+p,len),key=lit;memset(&key[tp],0,12);vector<EF>fs;vector<string>vs;
  for(size_t j=tp+12;j<len;){size_t n=0;int type=0;if(j+36<=len&&isuuid(raw+p+j)){n=36;type=1;}else if(dig(raw[p+j])){n=1;while(j+n<len&&dig(raw[p+j+n]))n++;if(n>9){j+=n;continue;}}else{j++;continue;}
   fs.push_back(EF{(uint16_t)j,(uint8_t)n,(uint8_t)type,0});vs.push_back(lit.substr(j,n));if(type==1)ucount[vs.back()]++;memset(&key[j],0,n);j+=n;
  }
  uint32_t id;auto it=map.find(key);if(it==map.end()){id=ts.size();map.emplace(move(key),id);ET t;t.literal=move(lit);t.toff=tp;t.f=move(fs);t.first=vs;ts.push_back(move(t));}else id=it->second;
  ET&t=ts[id];for(size_t j=0;j<vs.size();j++)if(vs[j]!=t.first[j])t.f[j].varying=true;t.count++;ls.push_back(EL{id,tm(raw+p+tp),move(vs)});p=e;
 }
 for(auto&l:ls){auto&t=ts[l.tid];for(size_t j=0;j<t.f.size();j++){auto&f=t.f[j];if(f.varying&&f.dict.find(l.v[j])==f.dict.end()){uint32_t id=f.vals.size();f.dict.emplace(l.v[j],id);f.vals.push_back(l.v[j]);}}}
 vector<string>uuid;unordered_map<string,uint32_t>um;
 for(auto&t:ts)for(auto&f:t.f)if(f.varying){if(f.type==1){size_t singles=0;for(auto&s:f.vals)if(ucount[s]==1)singles++;
  if(singles*16ull+t.count*2ull<t.count*16ull){f.type=2;f.nb=2;for(auto&s:f.vals)if(um.find(s)==um.end()){um.emplace(s,uuid.size());uuid.push_back(s);}}else{f.type=1;f.nb=16;}
 }else{
   uint64_t maxv=0;for(auto&s:f.vals)maxv=max(maxv,num(s));f.nb=maxv<256?1:maxv<65536?2:maxv<16777216?3:4;int ib=f.vals.size()<=256?1:2;
   if(f.vals.size()<=65535&&f.vals.size()*f.width+(uint64_t)t.count*ib+4<(uint64_t)t.count*f.nb){f.type=3;f.nb=ib;}else f.type=0;
  }}
 if(uuid.size()>65535||ts.size()>65535)return -1;
 vector<uint32_t>order(ts.size()),remap(ts.size());for(uint32_t i=0;i<ts.size();i++)order[i]=i;stable_sort(order.begin(),order.end(),[&](int a,int b){return ts[a].count>ts[b].count;});for(uint32_t i=0;i<ts.size();i++)remap[order[i]]=i;
 vector<uint8_t>a;put(a,0x31474f4c504d5453ull,8);put(a,size,8);put(a,ls.size(),4);put(a,ts.size(),4);put(a,uuid.size(),4);put(a,0,4);put(a,0,8);
 for(auto&s:uuid){auto b=packed(s);a.insert(a.end(),b.begin(),b.end());}
 for(auto ti:order){auto&t=ts[ti];int nf=0,rb=0;for(auto&f:t.f)if(f.varying){nf++;rb+=f.nb;}
  put(a,t.literal.size(),2);put(a,t.toff,2);put(a,nf,2);put(a,rb,2);a.insert(a.end(),t.literal.begin(),t.literal.end());
  for(auto&f:t.f)if(f.varying){put(a,f.off,2);put(a,f.width,1);put(a,f.type,1);put(a,f.nb,1);put(a,0,1);put(a,f.type==3?f.vals.size():0,2);if(f.type==3)for(auto&s:f.vals)a.insert(a.end(),s.begin(),s.end());}
 }
 uint64_t start=a.size();memcpy(a.data()+32,&start,8);uint32_t prev=0;
 for(auto&l:ls){uint32_t id=remap[l.tid];if(id<255)put(a,id,1);else{put(a,255,1);put(a,id,2);}if(l.ts>=prev&&l.ts-prev<65535)put(a,l.ts-prev,2);else{put(a,65535,2);put(a,l.ts,4);}prev=l.ts;
  auto&t=ts[l.tid];for(size_t j=0;j<t.f.size();j++){auto&f=t.f[j];if(!f.varying)continue;auto&s=l.v[j];if(f.type==0)put(a,num(s),f.nb);else if(f.type==1){auto b=packed(s);a.insert(a.end(),b.begin(),b.end());}else if(f.type==2)put(a,um.at(s),2);else put(a,f.dict.at(s),f.nb);}
 }
 if(a.size()>cap)return -1;memcpy(out,a.data(),a.size());return a.size();
}catch(...){return -1;}}
