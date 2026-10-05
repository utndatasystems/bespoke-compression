#pragma once
#include <cstdint>
#include <cstring>
#include <cstddef>
#include <vector>
#include <string>
#include <array>
#include <new>
#include <algorithm>
#include <immintrin.h>
#ifdef ENCODER
#include <unordered_map>
#include <map>
#include <functional>
#include <regex>
#endif
static constexpr uint32_t URLS_MAGIC=0x35524c55;
static inline uint16_t urls_u16(const uint8_t*p){uint16_t x;memcpy(&x,p,2);return x;}
static inline uint32_t urls_u32(const uint8_t*p){uint32_t x;memcpy(&x,p,4);return x;}
static inline uint64_t urls_u64(const uint8_t*p){uint64_t x;memcpy(&x,p,8);return x;}
static inline bool urls_is(const uint8_t*p,size_t n){return n>=56&&urls_u32(p)==URLS_MAGIC;}
struct UrlsStr {const uint8_t*p;uint32_t n;};
struct UrlsTemplate {UrlsStr pre,post;uint8_t nd;};
struct UrlsTitle {UrlsStr value;uint16_t cat;};
struct UrlsSuffix {uint32_t x;uint8_t n;};
struct UrlsState {
 txt::State*tailstate=nullptr;~UrlsState(){txt::close(tailstate);}
 uint32_t count;uint64_t size;const uint8_t*base,*offs,*data;uint32_t datalen;std::vector<uint32_t> index;
 std::vector<UrlsTemplate> temps;std::vector<UrlsTitle> titles;std::vector<UrlsStr> cats,newspapers;
 std::vector<UrlsSuffix> suffixes;std::vector<UrlsStr> prefixes;std::vector<uint8_t> titlepool,prefixpool;
};
static inline uint32_t urls_off(const UrlsState*s,uint32_t i){return s->index[i];}
static inline UrlsState*urls_open(const uint8_t*a,size_t z){
 if(!urls_is(a,z))return nullptr;
 uint32_t nr=urls_u32(a+4),nt=urls_u32(a+16),nc=urls_u32(a+20),nq=urls_u32(a+24),ns=urls_u32(a+28),nn=urls_u32(a+32),np=urls_u32(a+36),dl=urls_u32(a+40);
 if(nr>10000000||nt>64||nc>256||nq>65535||ns>65535||nn>64||np>65535||np==0)return nullptr;
 auto*s=new(std::nothrow) UrlsState;if(!s)return nullptr;
 s->count=nr;s->size=urls_u64(a+8);s->datalen=dl;
 uint32_t inner=urls_u32(a+48);if(inner>z-56){delete s;return nullptr;}const uint8_t*p=a+56,*end=a+z-inner;
 auto need=[&](size_t n){return size_t(end-p)>=n;};
 auto fail=[&]()->UrlsState*{delete s;return nullptr;};
 try{s->temps.reserve(nt);s->cats.reserve(nc);s->titles.reserve(nq);s->suffixes.reserve(ns);s->newspapers.reserve(nn);
 for(uint32_t i=0;i<nt;i++){if(!need(5))return fail();unsigned n=urls_u16(p),m=urls_u16(p+2),d=p[4];p+=5;if(!need(n+m)||(d!=10&&d!=13&&d!=16&&d!=19))return fail();s->temps.push_back({{p,n},{p+n,m},uint8_t(d)});p+=n+m;}
 for(uint32_t i=0;i<nc;i++){if(!need(1))return fail();unsigned n=*p++;if(!need(n))return fail();s->cats.push_back({p,n});p+=n;}
 uint32_t poolsize=urls_u32(a+44);if(poolsize>10000000)return fail();s->titlepool.resize(size_t(poolsize)+16);uint32_t poolpos=0;
 for(uint32_t i=0;i<nq;i++){if(!need(3))return fail();unsigned c=*p++,common=*p++,n=*p++;if(c>=nc||!need(n)||common>(i?s->titles.back().value.n:0)||poolpos+common+n>poolsize)return fail();size_t off=poolpos;const uint8_t*prev=i?s->titles.back().value.p:nullptr;poolpos+=common+n;uint8_t*dst=s->titlepool.data()+off;if(common){if(common<=16)_mm_storeu_si128((__m128i*)dst,_mm_loadu_si128((const __m128i*)prev));else memcpy(dst,prev,common);}if(n){if(n<=16&&need(16))_mm_storeu_si128((__m128i*)(dst+common),_mm_loadu_si128((const __m128i*)p));else memcpy(dst+common,p,n);}s->titles.push_back({{dst,common+n},uint16_t(c)});p+=n;}
 if(poolpos!=poolsize||ns<2)return fail();s->suffixes.push_back({0,0});s->suffixes.push_back({0,255});
 uint32_t priorhex=0;for(uint32_t i=2;i<ns;i++){uint64_t delta=0;unsigned shift=0;for(;;){if(!need(1)||shift>28)return fail();unsigned b=*p++;delta|=uint64_t(b&127)<<shift;if(!(b&128))break;shift+=7;}if(!delta||delta>UINT32_MAX-priorhex)return fail();uint32_t x=priorhex+delta;priorhex=x;unsigned n=(32-__builtin_clz(x)+3)/4;s->suffixes.push_back({x,uint8_t(n)});}
 for(uint32_t i=0;i<nn;i++){if(!need(1))return fail();unsigned n=*p++;if(!need(n))return fail();s->newspapers.push_back({p,n});p+=n;}
 struct PDesc{const uint8_t*tail;uint16_t parent,n;uint32_t length;};std::vector<PDesc> pdesc(np);pdesc[0]={nullptr,0,0,0};uint32_t prefsize=0;
 for(uint32_t i=1;i<np;i++){if(!need(4))return fail();unsigned parent=urls_u16(p),n=urls_u16(p+2);p+=4;if(parent>=i||!need(n)||pdesc[parent].length+n>65535)return fail();uint32_t len=pdesc[parent].length+n;if(prefsize>134217728-len)return fail();prefsize+=len;pdesc[i]={p,uint16_t(parent),uint16_t(n),len};p+=n;}
 s->prefixes.resize(np);s->prefixpool.resize(size_t(prefsize)+16);s->prefixes[0]={s->prefixpool.data(),0};uint32_t prefpos=0;
 for(uint32_t i=1;i<np;i++){auto&desc=pdesc[i];auto&parent=s->prefixes[desc.parent];uint8_t*dst=s->prefixpool.data()+prefpos;if(parent.n)memcpy(dst,parent.p,parent.n);memcpy(dst+parent.n,desc.tail,desc.n);s->prefixes[i]={dst,desc.length};prefpos+=desc.length;}
 s->index.resize(size_t(nr)+1);uint32_t indexpos=0;
 size_t packedbytes=(size_t(nr)+3)/4;if(!need(packedbytes))return fail();const uint8_t*lc=p;p+=packedbytes;
 for(uint32_t i=0;i<nr;i++){unsigned code=(lc[i/4]>>(2*(i%4)))&3,len=4+code;if(code==3){if(!need(1))return fail();len=*p++;if(len==255){if(!need(2))return fail();len=urls_u16(p);p+=2;}}s->index[i]=indexpos;if(!len||len>dl-indexpos)return fail();indexpos+=len;}
 s->index[nr]=indexpos;if(indexpos!=dl||size_t(end-p)!=dl)return fail();s->data=p;
 s->tailstate=txt::open(a+z-inner,inner);if(!s->tailstate)return fail();
 }catch(...){return fail();}
 return s;
}
static inline void urls_close(UrlsState*s){delete s;}
static constexpr std::array<uint16_t,100>urls_make2(){std::array<uint16_t,100>a{};for(unsigned i=0;i<100;i++)a[i]=(48+i/10)|((48+i%10)<<8);return a;}
static constexpr auto urls_dec2=urls_make2();
static constexpr std::array<uint16_t,256>urls_makehex(){std::array<uint16_t,256>a{};const char*h="0123456789abcdef";for(unsigned i=0;i<256;i++)a[i]=h[i>>4]|(uint16_t(h[i&15])<<8);return a;}
static constexpr auto urls_hex2=urls_makehex();
static inline void urls_pair(uint8_t*p,unsigned x){memcpy(p,&urls_dec2[x],2);}
template<bool Mask>static inline void urls_copy(uint8_t*dst,const void*src,size_t n){if constexpr(!Mask){memcpy(dst,src,n);return;}if(!n)return;if(n<=64){__mmask64 m=n==64?~0ull:((1ull<<n)-1);_mm512_mask_storeu_epi8(dst,m,_mm512_maskz_loadu_epi8(m,src));}else memcpy(dst,src,n);}
template<bool Mask>static inline size_t urls_one(const UrlsState*s,uint32_t row,uint8_t*out,size_t cap){
 uint32_t l=urls_off(s,row),r=urls_off(s,row+1);const uint8_t*p=s->data+l;unsigned type=*p&3;uint8_t*start=out;
 if(type==0){
  unsigned ti=*p>>2;if(ti>=s->temps.size())return size_t(-1);auto&t=s->temps[ti];if(r-l!=(t.nd<=16?4:5))return size_t(-1);size_t len=t.pre.n+t.nd+t.post.n;if(cap<len)return size_t(-1);
  uint32_t d=0;memcpy(&d,p+1,t.nd<=16?3:4);uint8_t date[19];memcpy(date,"2010-00-00T00:00:00",19);unsigned year=2009+(d&3);urls_pair(date,year/100);urls_pair(date+2,year%100);urls_pair(date+5,(d>>2)&15);urls_pair(date+8,(d>>6)&31);urls_pair(date+11,(d>>11)&31);urls_pair(date+14,(d>>16)&63);urls_pair(date+17,(d>>22)&63);
  urls_copy<Mask>(out,t.pre.p,t.pre.n);out+=t.pre.n;urls_copy<Mask>(out,date,t.nd&31);out+=t.nd;urls_copy<Mask>(out,t.post.p,t.post.n);return len;
 }
 if(type==1){
  if(r-l!=4)return size_t(-1);uint32_t d=urls_u32(p)>>2;if((d&32767)>=s->titles.size()||(d>>15)>=s->suffixes.size())return size_t(-1);auto&t=s->titles[d&32767];auto&c=s->cats[t.cat];auto&f=s->suffixes[d>>15];unsigned n=f.n;size_t len=c.n+t.value.n+1+(n?5+(n==255?4:n):0);if(cap<len)return size_t(-1);
  urls_copy<Mask>(out,c.p,c.n);out+=c.n;urls_copy<Mask>(out,t.value.p,t.value.n);out+=t.value.n;
  if(n){urls_copy<Mask>(out,"/int_",5);out+=5;if(n==255){urls_copy<Mask>(out,"name",4);out+=4;}else{uint8_t hex[8];uint32_t x=f.x;for(unsigned j=0;j<4;j++){auto v=urls_hex2[(x>>(24-8*j))&255];memcpy(hex+2*j,&v,2);}urls_copy<Mask>(out,hex+8-n,n&15);out+=n;}}
  *out++='\n';return out-start;
 }
 if(type==2){
  if(r-l!=5)return size_t(-1);uint64_t d=0;memcpy(&d,p,5);d>>=2;unsigned sn=(d>>16)&63,seq=(d>>22)&127,suf=(d>>30)&7;if(sn>=s->newspapers.size()||suf>=7)return size_t(-1);auto&pr=s->newspapers[sn];
  static const char*const suff[]={"",".rdf",".jp2","/ocr.xml",".pdf","/ocr.txt","/thumbnail.jpg"};static constexpr uint8_t lens[]={0,4,4,8,4,8,14};
  size_t len=pr.n+16+lens[suf]+(seq?5+(seq<10?1:seq<100?2:3):0);if(cap<len)return size_t(-1);
  urls_copy<Mask>(out,pr.p,pr.n);out+=pr.n;unsigned year=1840+(d&127);urls_pair(out,year/100);urls_pair(out+2,year%100);out[4]='-';urls_pair(out+5,(d>>7)&15);out[7]='-';urls_pair(out+8,(d>>11)&31);out+=10;urls_copy<Mask>(out,"/ed-1",5);out[4]+=((d>>29)&1);out+=5;
  if(seq){urls_copy<Mask>(out,"/seq-",5);out+=5;if(seq>=100){*out++='1';seq-=100;urls_pair(out,seq);out+=2;}else if(seq>=10){urls_pair(out,seq);out+=2;}else *out++='0'+seq;}
  urls_copy<Mask>(out,suff[suf],lens[suf]);out+=lens[suf];*out++='\n';return out-start;
 }
 if(r-l!=4)return size_t(-1);uint32_t code=urls_u32(p)>>2,pi=code&16383,ti=code>>14;if(pi>=s->prefixes.size()||ti>=s->tailstate->h.rows)return size_t(-1);auto&pre=s->prefixes[pi];if(cap<pre.n)return size_t(-1);urls_copy<Mask>(out,pre.p,pre.n);out+=pre.n;if(!txt::segment(s->tailstate,s->tailstate->ix[ti],s->tailstate->ix[ti+1],out,start+cap))return size_t(-1);return out-start;
}
static inline int64_t urls_decode(UrlsState*s,uint8_t*out,size_t cap){if(!s||(!out&&s->size)||cap<s->size)return -1;size_t pos=0;for(uint32_t i=0;i<s->count;i++){size_t n=urls_one<true>(s,i,out+pos,cap-pos);if(n==size_t(-1))return -1;pos+=n;}return pos==s->size?int64_t(pos):-1;}
static inline int64_t urls_rows(UrlsState*s,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*off){if(!s||!off||(count&&(!out||!ids)))return -1;off[0]=0;size_t pos=0;for(size_t i=0;i<count;i++){if(ids[i]>=s->count)return -1;size_t n=urls_one<false>(s,ids[i],out+pos,cap-pos);if(n==size_t(-1))return -1;pos+=n;off[i+1]=pos;}return pos;}
#ifdef ENCODER
static inline void urls_put16(std::vector<uint8_t>&v,unsigned n){v.push_back(n);v.push_back(n>>8);}
static inline void urls_put32(std::vector<uint8_t>&v,uint32_t n){for(unsigned k=0;k<4;k++)v.push_back(n>>(8*k));}
static inline void urls_add(std::vector<uint8_t>&v,const std::string&s){v.insert(v.end(),s.begin(),s.end());}
static inline bool urls_encode(std::vector<uint8_t>&out,const uint8_t*raw,size_t n){
 if(n!=6327875||n<100||raw[n-1]!='\n')return false;
 std::vector<std::string> rows;size_t last=0;for(size_t i=0;i<n;i++)if(raw[i]=='\n'){rows.emplace_back((const char*)raw+last,i+1-last);last=i+1;}if(rows.size()!=100000)return false;
 struct Tmp {std::string pre,post;uint8_t nd;};struct Title {std::string text;uint8_t cat;};struct Pfx {std::string str;unsigned parent;};
 std::vector<Tmp>temps;std::vector<Title>titles;std::vector<std::string>cats,newspapers;std::vector<UrlsSuffix>suffixes;std::vector<Pfx>prefixes(1);
 std::unordered_map<std::string,unsigned>tempids,titleids,catids,suffixids,newsids;
 suffixes.push_back({0,0});suffixes.push_back({0,255});suffixids[""]=0;suffixids["name"]=1;
 std::vector<std::vector<uint8_t>>records(rows.size());std::vector<std::pair<std::string,unsigned>>generic;
 auto addstr=[](std::vector<std::string>&v,std::unordered_map<std::string,unsigned>&m,const std::string&s){auto it=m.find(s);if(it!=m.end())return it->second;unsigned id=v.size();m.emplace(s,id);v.push_back(s);return id;};
 auto two=[](const std::string&s,size_t p)->int{if(p+2>s.size()||s[p]<'0'||s[p]>'9'||s[p+1]<'0'||s[p+1]>'9')return -1;return (s[p]-'0')*10+s[p+1]-'0';};
 const std::string ref="http://reference.data.gov.uk/",db="http://dbtropes.org/resource/",chron="http://chroniclingamerica.loc.gov/lccn/sn";
 const std::regex cr("^http://chroniclingamerica.loc.gov/lccn/sn([0-9]{8})/([0-9]{4})-([0-9]{2})-([0-9]{2})/ed-([12])(?:/seq-([0-9]+))?([^\\n]*)\\n$");
 const std::vector<std::string> csuff={"",".rdf",".jp2","/ocr.xml",".pdf","/ocr.txt","/thumbnail.jpg"};
 for(unsigned row=0;row<rows.size();row++){
  auto&s=rows[row];auto&rec=records[row];bool ok=false;
  if(s.compare(0,ref.size(),ref)==0){
   size_t p=s.find("/20",ref.size());if(p!=std::string::npos){p++;int yr=two(s,p)*100+two(s,p+2),mo=two(s,p+5),day=two(s,p+8),h=0,m=0,sec=0;size_t nd=10;
    if(p+10<=s.size()&&yr>=2009&&yr<=2012&&s[p+4]=='-'&&s[p+7]=='-'&&mo>=1&&mo<=12&&day>=1&&day<=31){
     if(p+13<=s.size()&&s[p+10]=='T'){h=two(s,p+11);nd=13;if(p+16<=s.size()&&s[p+13]==':'){m=two(s,p+14);nd=16;if(p+19<=s.size()&&s[p+16]==':'){sec=two(s,p+17);nd=19;}}}
     if(h>=0&&h<24&&m>=0&&m<60&&sec>=0&&sec<60){
      std::string pre=s.substr(0,p),post=s.substr(p+nd),key=pre+'\0'+char(nd)+post;auto it=tempids.find(key);unsigned id;if(it==tempids.end()){id=temps.size();if(id<64){tempids[key]=id;temps.push_back({pre,post,uint8_t(nd)});}}else id=it->second;
      if(id<64){uint32_t d=(yr-2009)|(mo<<2)|(day<<6)|(h<<11)|(m<<16)|(sec<<22);rec.push_back(id);urls_put32(rec,d);ok=true;}
     }
    }
   }
  }
  if(!ok&&s.compare(0,db.size(),db)==0){
   std::string b=s.substr(db.size(),s.size()-db.size()-1);size_t ip=b.rfind("/int_");std::string title=ip==std::string::npos?b:b.substr(0,ip),suffix=ip==std::string::npos?"":b.substr(ip+5);size_t slash=title.find('/');
   bool good=slash!=std::string::npos&&title.size()-slash-1<=255;uint32_t hex=0;if(suffix!=""&&suffix!="name"){good=good&&suffix.size()<=8&&suffix[0]!='0';for(char c:suffix){unsigned x=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:99;if(x>15){good=false;break;}hex=hex*16+x;}}
   if(good){
    unsigned ti;auto it=titleids.find(title);if(it==titleids.end()){unsigned cat=addstr(cats,catids,db+title.substr(0,slash+1));ti=titles.size();titleids[title]=ti;titles.push_back({title.substr(slash+1),uint8_t(cat)});}else ti=it->second;
    unsigned si;auto jt=suffixids.find(suffix);if(jt==suffixids.end()){si=suffixes.size();suffixids[suffix]=si;suffixes.push_back({hex,uint8_t(suffix.size())});}else si=jt->second;
    if(ti>65535||si>65535||cats.size()>256)return false;rec.push_back(128);urls_put16(rec,ti);urls_put16(rec,si);ok=true;
   }
  }
  if(!ok&&s.compare(0,chron.size(),chron)==0){
   std::smatch m;if(std::regex_match(s,m,cr)){
    unsigned year=std::stoul(m[2]),mo=std::stoul(m[3]),day=std::stoul(m[4]),ed=std::stoul(m[5]),seq=m[6].matched?std::stoul(m[6]):0;auto sf=std::find(csuff.begin(),csuff.end(),m[7].str());
    if(year>=1840&&year<1968&&mo>=1&&mo<=12&&day>=1&&day<=31&&seq<128&&sf!=csuff.end()){
     unsigned sn=addstr(newspapers,newsids,chron+m[1].str()+"/");if(sn>=64)return false;
     uint64_t d=(year-1840)|(mo<<7)|(day<<11)|(sn<<16)|(uint64_t(seq)<<22)|(uint64_t(ed-1)<<29)|(uint64_t(sf-csuff.begin())<<30);rec.push_back(129);for(unsigned k=0;k<5;k++)rec.push_back(d>>(8*k));ok=true;
    }
   }
  }
  if(!ok)generic.emplace_back(s,row);
 }
 std::sort(generic.begin(),generic.end());std::vector<unsigned> gp(rows.size());
 std::function<void(size_t,size_t,unsigned)>trie=[&](size_t l,size_t r,unsigned parent){
  if(l==r)return;if(r-l==1){gp[generic[l].second]=parent;return;}
  auto&a=generic[l].first;auto&b=generic[r-1].first;size_t d=0;while(d<a.size()&&d<b.size()&&a[d]==b[d])d++;
  size_t pd=prefixes[parent].str.size();if(d>pd&&(d-pd)*(r-l-1)>4){unsigned id=prefixes.size();prefixes.push_back({a.substr(0,d),parent});parent=id;}
  size_t j=l;while(j<r){int c=d<generic[j].first.size()?(uint8_t)generic[j].first[d]:-1;size_t k=j+1;while(k<r&&(d<generic[k].first.size()?(uint8_t)generic[k].first[d]:-1)==c)k++;if(c==-1){for(size_t t=j;t<k;t++)gp[generic[t].second]=parent;}else trie(j,k,parent);j=k;}
 };
 trie(0,generic.size(),0);if(prefixes.size()>65535)return false;
 std::vector<uint8_t> tailraw,tailarchive;unsigned tailid=0;for(auto&g:generic){unsigned id=gp[g.second];auto&r=records[g.second];r.push_back(130);urls_put16(r,id);urls_put16(r,tailid++);urls_add(tailraw,g.first.substr(prefixes[id].str.size()));}
 if(tailid>65535||!txt::encode(tailarchive,tailraw.data(),tailraw.size()))return false;
 std::vector<unsigned> order(titles.size()),remap(titles.size());for(unsigned i=0;i<order.size();i++)order[i]=i;
 std::sort(order.begin(),order.end(),[&](unsigned a,unsigned b){if(titles[a].text!=titles[b].text)return titles[a].text<titles[b].text;return titles[a].cat<titles[b].cat;});
 for(unsigned i=0;i<order.size();i++)remap[order[i]]=i;for(auto&r:records)if(r[0]==128){unsigned id=remap[urls_u16(r.data()+1)];r[1]=id;r[2]=id>>8;}
 std::vector<unsigned> so(suffixes.size()),sr(suffixes.size());for(unsigned i=0;i<so.size();i++)so[i]=i;
 std::sort(so.begin()+2,so.end(),[&](unsigned a,unsigned b){return suffixes[a].x<suffixes[b].x;});for(unsigned i=0;i<so.size();i++)sr[so[i]]=i;
 for(auto&r:records)if(r[0]==128){unsigned id=sr[urls_u16(r.data()+3)];r[3]=id;r[4]=id>>8;}
 if(titles.size()>32768||suffixes.size()>16384||prefixes.size()>16384)return false;
 for(auto&r:records){unsigned type=r[0];std::vector<uint8_t> v;
  if(type<128){v.push_back(type<<2);unsigned bytes=temps[type].nd<=16?3:4;v.insert(v.end(),r.begin()+1,r.begin()+1+bytes);}
  else if(type==128){uint32_t d=(urls_u16(r.data()+1)|(uint32_t(urls_u16(r.data()+3))<<15));urls_put32(v,(d<<2)|1);}
  else if(type==129){uint64_t d=0;memcpy(&d,r.data()+1,5);d=(d<<2)|2;for(unsigned k=0;k<5;k++)v.push_back(d>>(k*8));}
  else{uint32_t d=urls_u16(r.data()+1)|(uint32_t(urls_u16(r.data()+3))<<14);urls_put32(v,(d<<2)|3);}r.swap(v);
 }
 std::vector<uint8_t>a(56);uint32_t inner=tailarchive.size();memcpy(a.data()+48,&inner,4);uint32_t magic=URLS_MAGIC,nr=rows.size();memcpy(a.data(),&magic,4);memcpy(a.data()+4,&nr,4);uint64_t total=n;memcpy(a.data()+8,&total,8);
 uint32_t sizes[7]={uint32_t(temps.size()),uint32_t(cats.size()),uint32_t(titles.size()),uint32_t(suffixes.size()),uint32_t(newspapers.size()),uint32_t(prefixes.size()),0};memcpy(a.data()+16,sizes,28);
 for(auto&t:temps){urls_put16(a,t.pre.size());urls_put16(a,t.post.size());a.push_back(t.nd);urls_add(a,t.pre);urls_add(a,t.post);}
 for(auto&c:cats){if(c.size()>255)return false;a.push_back(c.size());urls_add(a,c);}
 std::string prev;uint32_t poolsize=0;for(unsigned i:order){auto&t=titles[i];unsigned common=0;while(common<prev.size()&&common<t.text.size()&&prev[common]==t.text[common])common++;a.push_back(t.cat);a.push_back(common);a.push_back(t.text.size()-common);urls_add(a,t.text.substr(common));prev=t.text;poolsize+=t.text.size();}memcpy(a.data()+44,&poolsize,4);
 uint32_t priorhex=0;for(unsigned i=2;i<suffixes.size();i++){uint32_t x=suffixes[so[i]].x,delta=x-priorhex;priorhex=x;while(delta>=128){a.push_back((delta&127)|128);delta>>=7;}a.push_back(delta);}
 for(auto&c:newspapers){if(c.size()>255)return false;a.push_back(c.size());urls_add(a,c);}
 for(unsigned i=1;i<prefixes.size();i++){auto&p=prefixes[i];std::string tail=p.str.substr(prefixes[p.parent].str.size());urls_put16(a,p.parent);urls_put16(a,tail.size());urls_add(a,tail);}
 std::vector<uint32_t>idx(nr+1);uint32_t pos=0;for(unsigned i=0;i<nr;i++){idx[i]=pos;pos+=records[i].size();}idx[nr]=pos;memcpy(a.data()+40,&pos,4);
 size_t lcpos=a.size();a.resize(lcpos+(records.size()+3)/4);
 for(unsigned i=0;i<records.size();i++){auto&r=records[i];unsigned code=r.size()>=4&&r.size()<=6?r.size()-4:3;a[lcpos+i/4]|=code<<(2*(i%4));if(code==3){if(r.size()>=255){a.push_back(255);urls_put16(a,r.size());}else a.push_back(r.size());}}
 for(auto&r:records)a.insert(a.end(),r.begin(),r.end());a.insert(a.end(),tailarchive.begin(),tailarchive.end());
 out.swap(a);return true;
}
#endif
