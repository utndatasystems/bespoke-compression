#include "codec.h"
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <cstdio>
#include <sys/mman.h>
#include "primitive_strcodec.h"
#include "simd_helpers.h"
using std::string;using std::vector;
struct Header{uint64_t magic,raw;uint32_t rows,draw,dcomp,stream,counts[9],reserved;};
static constexpr uint64_t MAGIC=0x34434C504C455931ull;
static inline uint32_t r32(const void*p){uint32_t x;memcpy(&x,p,4);return x;}
static inline uint32_t r24(const uint8_t*p){return r32(p)&0xffffff;}
static inline uint16_t r16(const void*p){uint16_t x;memcpy(&x,p,2);return x;}
static inline void put(vector<uint8_t>&v,uint32_t n,int b){for(int i=0;i<b;i++)v.push_back(n>>(8*i));}
#ifdef ENCODER
struct Dict{vector<string>words;vector<uint32_t>freq,remap;std::unordered_map<string,uint32_t>map;uint32_t add(const string&s){auto it=map.find(s);if(it!=map.end()){freq[it->second]++;return it->second;}uint32_t i=words.size();map.emplace(s,i);words.push_back(s);freq.push_back(1);return i;}void sort(bool byfreq){vector<uint32_t>order(words.size());for(uint32_t i=0;i<order.size();i++)order[i]=i;std::sort(order.begin(),order.end(),[&](uint32_t a,uint32_t b){return byfreq&&freq[a]!=freq[b]?freq[a]>freq[b]:words[a]<words[b];});vector<string>w;remap.resize(words.size());for(auto i:order){remap[i]=w.size();w.push_back(std::move(words[i]));}words=std::move(w);}};
static const char*scanstr(const char*p,const char*e){if(p==e||*p!='"')return nullptr;for(++p;p<e;++p){if(*p=='\\'){if(++p==e)return nullptr;}else if(*p=='"')return p+1;}return nullptr;}
static const char*scanval(const char*p,const char*e){if(p==e)return nullptr;if(*p=='"')return scanstr(p,e);if(*p=='{'||*p=='['){int depth=0;for(;p<e;p++){if(*p=='"'){p=scanstr(p,e);if(!p)return nullptr;--p;}else if(*p=='{'||*p=='[')depth++;else if(*p=='}'||*p==']'){if(!--depth)return p+1;}}return nullptr;}const char*q=p;while(q<e&&*q!=','&&*q!='}'&&*q!='\n')q++;return q;}
static bool fields(const char*&p,const char*end,vector<string>&v,string&tail){v.clear();if(p==end||*p++!='{')return false;while(p<end){auto k=scanstr(p,end);if(!k||k==end||*k!=':')return false;auto q=scanval(k+1,end);if(!q)return false;v.emplace_back(k+1,q);p=q;if(p==end)return false;if(*p==','){p++;continue;}if(*p!='}')return false;const char*t=p++;if(p<end&&*p=='\n')p++;tail.assign(t,p);return true;}return false;}
static bool attributes(const string&s,Dict&d,vector<uint32_t>&ids){ids.clear();if(s=="null"||s=="{}")return true;const char*p=s.data()+1,*e=s.data()+s.size()-1;while(p<e){const char*k=scanstr(p,e);if(!k||k==e||*k!=':')return false;const char*q=scanval(k+1,e);if(!q)return false;ids.push_back(d.add(string(p,q)+","));p=q;if(p<e){if(*p!=',')return false;++p;}}return true;}
struct Row{uint32_t ids[7];string bid,lat,lon;vector<uint32_t>attrs;};

#ifndef PAIRS_COUNT
#define PAIRS_COUNT 4096
#endif
static void combine_attributes(vector<Row>&rows,Dict&d){
 std::unordered_map<uint64_t,uint32_t>freq;freq.reserve(150000);
 for(const auto&r:rows)for(size_t j=1;j<r.attrs.size();j++)freq[(uint64_t(r.attrs[j-1])<<32)|r.attrs[j]]++;
 vector<std::pair<uint64_t,uint32_t>> pairs(freq.begin(),freq.end());
 std::sort(pairs.begin(),pairs.end(),[](auto a,auto b){return a.second!=b.second?a.second>b.second:a.first<b.first;});
 std::unordered_map<uint64_t,uint32_t>replacement;
 for(auto [key,count]:pairs){if(replacement.size()>=PAIRS_COUNT||count<8)break;unsigned a=key>>32,b=unsigned(key);if(d.words[a].size()+d.words[b].size()>512)continue;auto id=d.add(d.words[a]+d.words[b]);replacement.emplace(key,id);}
 size_t before=0,after=0;
 for(auto&r:rows){auto&a=r.attrs;before+=a.size();size_t out=0;for(size_t j=0;j<a.size();){auto it=j+1<a.size()?replacement.find((uint64_t(a[j])<<32)|a[j+1]):replacement.end();if(it!=replacement.end()){a[out++]=it->second;j+=2;}else a[out++]=a[j++];}a.resize(out);after+=out;}
 std::fill(d.freq.begin(),d.freq.end(),0);for(const auto&r:rows)for(unsigned id:r.attrs)d.freq[id]++;
 fprintf(stderr,"pairs=%zu attrs_before=%zu attrs_after=%zu\n",replacement.size(),before,after);
}


static void push16(string&s,unsigned n){s.push_back(n);s.push_back(n>>8);}
static bool make_primitive(Dict*d){
 for(string&s:d[5].words){
  string o;unsigned len=s.size();const string prefix=",\"categories\":\"",suffix="\",\"hours\":";
  if(s.compare(0,prefix.size(),prefix)!=0){o.push_back(1);o+=s;}
  else{if(s.size()<prefix.size()+suffix.size()||s.compare(s.size()-suffix.size(),suffix.size(),suffix)!=0)return false;o.push_back(0);size_t pos=prefix.size(),end=s.size()-suffix.size();while(pos<end){size_t next=s.find(", ",pos);if(next==string::npos||next>end)next=end;push16(o,d[7].add(s.substr(pos,next-pos)+", "));pos=next+2;}}
  push16(o,len);s=std::move(o);
 }
 for(string&s:d[6].words){
  string o;unsigned len=s.size();const char*p=s.data(),*end=p+s.size();const char*objend=scanval(p,end);if(!objend)return false;
  if(*p!='{'||objend-p==2){o.push_back(1);o+=s;}
  else{bool nl=s.back()=='\n';if(string(objend,end)!=(nl?"}\n":"}"))return false;o.push_back(nl?0:2);++p;const char*e=objend-1;while(p<e){auto*k=scanstr(p,e);if(!k||k==e||*k!=':')return false;auto*q=scanval(k+1,e);if(!q)return false;push16(o,d[8].add(string(p,q)+","));p=q;if(p<e){if(*p!=',')return false;++p;}}}
  push16(o,len);s=std::move(o);
 }
 return true;
}

static bool packid(const string&s,vector<uint8_t>&v){if(s.size()!=24)return false;uint32_t x=0,bits=0;for(unsigned i=1;i<23;i++){char c=s[i];int z=c>='A'&&c<='Z'?c-'A':c>='a'&&c<='z'?c-'a'+26:c>='0'&&c<='9'?c-'0'+52:c=='-'?62:c=='_'?63:-1;if(z<0)return false;x=(x<<6)|z;bits+=6;if(bits>=8){bits-=8;v.push_back(x>>bits);}}return bits==4&&(x&15)==0;}
static bool packnum(const string&s,vector<uint8_t>&v){if(s.empty()||s.size()>15)return false;for(size_t i=0;i<s.size();i+=2){unsigned c[2]={0,0};for(unsigned j=0;j<2&&i+j<s.size();j++){char a=s[i+j];c[j]=a>='0'&&a<='9'?a-'0':a=='.'?10:a=='-'?11:255;if(c[j]==255)return false;}v.push_back(c[0]|(c[1]<<4));}return true;}
extern "C" int64_t lab_encode(const uint8_t*raw,size_t n,uint8_t*out,size_t cap){try{Dict d[9];vector<Row>rows;vector<string>f;string tail;const char*p=(const char*)raw,*end=p+n;while(p<end){if(!fields(p,end,f,tail)||f.size()!=14)return -1;Row r;r.bid=f[0];r.lat=f[6];r.lon=f[7];r.ids[0]=d[0].add(f[1]);r.ids[1]=d[1].add(f[2]);r.ids[2]=d[2].add(f[3]+",\"state\":"+f[4]+",\"postal_code\":"+f[5]+",\"latitude\":");if(!attributes(f[11],d[3],r.attrs)||r.attrs.size()>254)return -1;string a=r.attrs.empty()?f[11]:"{";r.ids[4]=d[4].add(",\"stars\":"+f[8]+",\"review_count\":"+f[9]+",\"is_open\":"+f[10]+",\"attributes\":"+a);r.ids[5]=d[5].add(",\"categories\":"+f[12]+",\"hours\":");r.ids[6]=d[6].add(f[13]+tail);rows.push_back(std::move(r));}
combine_attributes(rows,d[3]);
if(!make_primitive(d))return -1;
Header h{};h.magic=MAGIC;h.raw=n;h.rows=rows.size();vector<uint8_t>dict;for(int j=0;j<9;j++){if(j<7)d[j].sort(j==3);h.counts[j]=d[j].words.size();if((j==2||j==4||j==6)&&h.counts[j]>65536)return -1;for(auto&s:d[j].words){if(j>=7){unsigned sz=j==7?64:32;if(s.size()>=sz)return -1;dict.push_back(s.size());dict.insert(dict.end(),s.begin(),s.end());dict.resize(dict.size()+sz-s.size()-1,0);}else{if(s.size()>65535)return -1;put(dict,s.size(),2);dict.insert(dict.end(),s.begin(),s.end());}}}
vector<uint8_t>stream;for(auto&r:rows){if(!packid(r.bid,stream))return -1;put(stream,d[0].remap[r.ids[0]],3);put(stream,d[1].remap[r.ids[1]],3);put(stream,d[2].remap[r.ids[2]],2);put(stream,d[4].remap[r.ids[4]],2);put(stream,d[5].remap[r.ids[5]],3);put(stream,d[6].remap[r.ids[6]],2);stream.push_back(r.lat.size()|(r.lon.size()<<4));if(!packnum(r.lat,stream)||!packnum(r.lon,stream))return -1;stream.push_back(r.attrs.size());for(auto id:r.attrs){id=d[3].remap[id];if(id>=65536)return -1;put(stream,id,2);}}
auto comp=strcodec::compress(dict.data(),dict.size());h.draw=dict.size();h.dcomp=comp.size();h.stream=stream.size();size_t total=sizeof(h)+comp.size()+stream.size()+64;if(total>cap)return -1;memcpy(out,&h,sizeof(h));memcpy(out+sizeof(h),comp.data(),comp.size());memcpy(out+sizeof(h)+comp.size(),stream.data(),stream.size());memset(out+total-64,0,64);fprintf(stderr,"rows=%u rawdict=%u compdict=%u stream=%u archive=%zu\n",h.rows,h.draw,h.dcomp,h.stream,total);for(int j=0;j<9;j++)fprintf(stderr,"dict%d count=%u\n",j,h.counts[j]);return total;}catch(...){return -1;}}
#else
struct Entry{uint32_t off,len;};struct State{Header h;const uint8_t*stream;uint8_t*dict;const uint8_t*catwords,*hourwords;Entry*all,*d[9];size_t allocation;};
static void* scratch_get(size_t n){void*p=mmap(nullptr,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);if(p==MAP_FAILED)return nullptr;madvise(p,n,MADV_HUGEPAGE);return p;}
static void scratch_put(void*p,size_t n){if(p)munmap(p,n);}

extern "C" void*lab_open(const uint8_t*ar,size_t n){if(!ar||n<sizeof(Header))return nullptr;Header h;memcpy(&h,ar,sizeof(h));if(h.magic!=MAGIC||h.raw>1000000000||h.draw>300000000||h.rows>10000000||uint64_t(sizeof(h))+h.dcomp+h.stream+64!=n)return nullptr;size_t cnt=0;for(auto x:h.counts){if(x>1000000)return nullptr;cnt+=x;}State*s=(State*)calloc(1,sizeof(State));if(!s)return nullptr;s->h=h;s->allocation=((size_t(h.draw)+127)&~size_t(63))+cnt*sizeof(Entry);s->dict=(uint8_t*)scratch_get(s->allocation);s->all=s->dict?(Entry*)(s->dict+((size_t(h.draw)+127)&~size_t(63))):nullptr;if(!s->dict||!s->all){scratch_put(s->dict,s->allocation);free(s);return nullptr;}if(!strcodec::decompress(ar+sizeof(h),h.dcomp,s->dict,h.draw)){scratch_put(s->dict,s->allocation);free(s);return nullptr;}memset(s->dict+h.draw,0,64);uint8_t*p=s->dict,*e=p+h.draw;Entry*it=s->all;for(int j=0;j<9;j++){s->d[j]=it;if(j>=7){size_t len=size_t(h.counts[j])*(j==7?64:32);if(size_t(e-p)<len)goto bad;if(j==7)s->catwords=p;else s->hourwords=p;for(size_t x=0;x<len;x+=(j==7?64:32))if(p[x]>=(j==7?64:32))goto bad;p+=len;continue;}for(unsigned i=0;i<h.counts[j];i++){if(e-p<2)goto bad;unsigned z=r16(p);p+=2;static constexpr unsigned minimum[9]={2,2,40,1,40,3,3,2,2};if(z<minimum[j]||z>size_t(e-p))goto bad;*it++={uint32_t(p-s->dict),z};p+=z;}}if(p!=e)goto bad;

for(unsigned j=5;j<=6;j++)for(unsigned i=0;i<h.counts[j];i++){
 Entry a=s->d[j][i];const uint8_t*t=s->dict+a.off;unsigned len=a.len;if(len<3)goto bad;unsigned tag=t[0];
 if(tag==1){if(len-3!=r16(t+len-2))goto bad;}else if((j==5&&tag!=0)||(j==6&&tag!=0&&tag!=2)||len<5||((len-3)&1))goto bad;
}
s->stream=ar+sizeof(h)+h.dcomp;return s;bad:scratch_put(s->dict,s->allocation);free(s);return nullptr;}
static inline uint8_t*cp(uint8_t*p,const uint8_t*s,unsigned n,uint8_t*bound){
uint8_t*e=p+n;
if(n<=64){if(bound-p>=64)_mm512_storeu_si512(p,_mm512_loadu_si512(s));else _mm512_mask_storeu_epi8(p,_bzhi_u64(~0ull,n),_mm512_loadu_si512(s));return e;}
while(n>128){_mm512_storeu_si512(p,_mm512_loadu_si512(s));p+=64;s+=64;n-=64;}
_mm512_storeu_si512(p,_mm512_loadu_si512(s));_mm512_storeu_si512(p+n-64,_mm512_loadu_si512(s+n-64));return e;}
extern "C" int64_t lab_decode(void*vs,uint8_t*out,size_t cap){if(!vs)return -1;State*s=(State*)vs;const Header&h=s->h;if(!out||cap<h.raw)return -1;const uint8_t*p=s->stream,*e=p+h.stream;uint8_t*q=out,*end=out+h.raw;const uint8_t*dict=s->dict;for(unsigned row=0;row<h.rows;row++){if(e-p<33||end-q<80)return -1;unsigned ni=r24(p+16),ai=r24(p+19),pi=r16(p+22),mi=r16(p+24),ci=r24(p+26),hi=r16(p+29);if(ni>=h.counts[0]||ai>=h.counts[1]||pi>=h.counts[2]||mi>=h.counts[4]||ci>=h.counts[5]||hi>=h.counts[6])return -1;Entry ne=s->d[0][ni],ad=s->d[1][ai],pl=s->d[2][pi],me=s->d[4][mi],ca=s->d[5][ci],ho=s->d[6][hi];unsigned lens=p[31],nl=lens&15,nr=lens>>4;if(!nl||!nr)return -1;const uint8_t*nums=p+32;const uint8_t*bid=p;p=nums+(nl+1)/2+(nr+1)/2;if(p>=e)return -1;unsigned ac=*p++;const uint8_t*caprog=dict+ca.off,*hoprog=dict+ho.off;if(ca.len<3||ho.len<3)return -1;unsigned catlen=r16(caprog+ca.len-2),hourslen=r16(hoprog+ho.len-2);uint64_t base=16+22+28+ne.len+ad.len+pl.len+nl+13+nr+me.len+catlen+hourslen;if(base>size_t(end-q))return -1;
memcpy(q,"{\"business_id\":\"",16);q+=16;yelp_id_decode(bid,(char*)q);q+=22;memcpy(q,"\",\"name\":",9);q+=9;q=cp(q,dict+ne.off,ne.len,end);memcpy(q,",\"address\":",11);q+=11;q=cp(q,dict+ad.off,ad.len,end);memcpy(q,",\"city\":",8);q+=8;q=cp(q,dict+pl.off,pl.len,end);yelp_bcd_decode(nums,(char*)q);q+=nl;memcpy(q,",\"longitude\":",13);q+=13;yelp_bcd_decode(nums+(nl+1)/2,(char*)q);q+=nr;q=cp(q,dict+me.off,me.len,end);
for(unsigned k=0;k<ac;k++){if(e-p<2)return -1;unsigned id=r16(p);p+=2;if(id>=h.counts[3])return -1;Entry a=s->d[3][id];if(a.len>size_t(end-q))return -1;q=cp(q,dict+a.off,a.len,end);}if(ac)q[-1]='}';if(uint64_t(catlen)+hourslen>size_t(end-q))return -1;
if(caprog[0]==1){if(ca.len-3>size_t(end-q))return -1;q=cp(q,caprog+1,ca.len-3,end);}else{if(end-q<15)return -1;memcpy(q,",\"categories\":\"",15);q+=15;for(unsigned i=1;i+2<ca.len;i+=2){unsigned id=r16(caprog+i);if(id>=h.counts[7])return -1;const uint8_t*t=s->catwords+(id<<6);unsigned n=*t++;if(n>size_t(end-q))return -1;if(end-q>=64)_mm512_storeu_si512(q,_mm512_loadu_si512(t));else _mm512_mask_storeu_epi8(q,_bzhi_u64(~0ull,n),_mm512_loadu_si512(t));q+=n;}q-=2;if(end-q<10)return -1;memcpy(q,"\",\"hours\":",10);q+=10;}
if(hoprog[0]==1){if(ho.len-3>size_t(end-q))return -1;q=cp(q,hoprog+1,ho.len-3,end);}else{if(end-q<1)return -1;*q++='{';for(unsigned i=1;i+2<ho.len;i+=2){unsigned id=r16(hoprog+i);if(id>=h.counts[8])return -1;const uint8_t*t=s->hourwords+(id<<5);unsigned n=*t++;if(n>size_t(end-q))return -1;if(end-q>=32)_mm256_storeu_si256((__m256i*)q,_mm256_loadu_si256((const __m256i*)t));else _mm256_mask_storeu_epi8(q,_bzhi_u32(~0u,n),_mm256_loadu_si256((const __m256i*)t));q+=n;}q[-1]='}';if(end-q<(hoprog[0]==0?2:1))return -1;*q++='}';if(hoprog[0]==0)*q++='\n';}
}return p==e&&q==end?q-out:-1;}
extern "C" void lab_close(void*vs){if(!vs)return;State*s=(State*)vs;scratch_put(s->dict,s->allocation);free(s);}
#endif
