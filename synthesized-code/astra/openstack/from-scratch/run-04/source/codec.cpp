#include <new>
#include <immintrin.h>
#include "timestamp.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <vector>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <stdio.h>
#include "codec.h"
struct Field {uint32_t off,len,kind,bw; uint64_t base,mx; bool vary; uint32_t prev;};
struct Template {std::string line; std::vector<Field> fields; uint32_t freq=0;};
static bool digit(char c){return c>='0'&&c<='9';}
static bool hex(char c){return digit(c)||(c>='a'&&c<='f');}
static uint64_t num(const char*p,int n){uint64_t v=0;for(int i=0;i<n;i++)v=v*10+p[i]-'0';return v;}
static uint64_t tmnum(const char*p){return ((num(p,2)*60+num(p+3,2))*60+num(p+6,2))*1000+num(p+9,3);}
static uint64_t val(const std::string&s,const Field&f){return f.kind==2?tmnum(s.data()+f.off):num(s.data()+f.off,f.len);}
#ifndef SPLIT_MAX
#define SPLIT_MAX 32
#endif
#ifdef ENCODER
static void put(std::vector<uint8_t>&o,uint64_t v,unsigned n){for(unsigned i=0;i<n;i++){o.push_back(v);v>>=8;}}
static void vint(std::vector<uint8_t>&o,uint64_t v){while(v>127){o.push_back((v&127)|128);v>>=7;}o.push_back(v);}
static void scan(const std::string&s,std::string&key,std::vector<Field>&fs){
 key=s;for(size_t i=0;i<s.size();){size_t n=0;unsigned kind=0;
 if(i+36<=s.size()&&s[i+8]=='-'&&s[i+13]=='-'&&s[i+18]=='-'&&s[i+23]=='-'){bool yes=true;for(int j=0;j<36;j++)if(j!=8&&j!=13&&j!=18&&j!=23&&!hex(s[i+j])){yes=false;break;}if(yes){n=36;kind=1;}}
 if(!n&&hex(s[i])){size_t j=i;while(j<s.size()&&hex(s[j]))j++;if((j-i==32||j-i==40)&&(i==0||!hex(s[i-1]))){n=j-i;kind=1;}}
 if(!n&&i+12<=s.size()&&s[i+2]==':'&&s[i+5]==':'&&s[i+8]=='.'&&digit(s[i])&&digit(s[i+1])&&digit(s[i+3])&&digit(s[i+4])&&digit(s[i+6])&&digit(s[i+7])&&digit(s[i+9])&&digit(s[i+10])&&digit(s[i+11])){n=12;kind=2;}
 if(!n&&digit(s[i])){size_t j=i;while(j<s.size()&&digit(s[j]))j++;if(j-i<=18){n=j-i;kind=0;}}
 if(!n){i++;continue;}for(size_t j=0;j<n;j++)key[i+j]=char(kind+1);Field f{};f.off=i;f.len=n;f.kind=kind;f.base=f.mx=kind==1?0:val(s,f);fs.push_back(f);i+=n;
 }
}
#include "split.h"
extern "C" int64_t lab_encode(const uint8_t*raw,size_t size,uint8_t*archive,size_t cap){try{
 std::vector<Template> ts;std::vector<std::string> lines;std::vector<uint32_t> ids;std::unordered_map<std::string,uint32_t> map;
 for(size_t p=0;p<size;){size_t q=p;while(q<size&&raw[q]!='\n')q++;if(q<size)q++;std::string s((const char*)raw+p,q-p),key;std::vector<Field>fs;scan(s,key,fs);auto r=map.emplace(key,ts.size());uint32_t id=r.first->second;
 if(r.second){Template t;t.line=s;t.fields=fs;ts.push_back(std::move(t));}
 Template&t=ts[id];t.freq++;for(Field&f:t.fields){if(s.compare(f.off,f.len,t.line,f.off,f.len)!=0)f.vary=true;if(f.kind!=1){uint64_t v=val(s,f);f.base=std::min(f.base,v);f.mx=std::max(f.mx,v);}}
 ids.push_back(id);lines.push_back(std::move(s));p=q;
 }

 split_templates(ts,ids,lines);
 std::vector<uint32_t> ord(ts.size()),remap(ts.size());for(uint32_t i=0;i<ts.size();i++)ord[i]=i;std::stable_sort(ord.begin(),ord.end(),[&](uint32_t a,uint32_t b){return ts[a].freq>ts[b].freq;});for(uint32_t i=0;i<ts.size();i++)remap[ord[i]]=i;
 std::unordered_map<std::string,uint32_t> um;std::vector<std::string>us;
 for(size_t i=0;i<lines.size();i++)for(Field&f:ts[ids[i]].fields)if(f.vary&&f.kind==1){std::string v=lines[i].substr(f.off,f.len);auto r=um.emplace(v,us.size());if(r.second)us.push_back(v);}
 unsigned ub=us.size()<=65536?2:3;
 std::vector<uint8_t>out;put(out,0x314c504d54ULL,8);put(out,size,8);put(out,lines.size(),4);put(out,ts.size(),4);put(out,us.size(),4);put(out,ub,4);
 for(auto&u:us){put(out,u.size(),1);int high=-1;for(char c:u)if(c!='-'){int v=c<='9'?c-'0':c-'a'+10;if(high<0)high=v;else{put(out,high*16+v,1);high=-1;}}}
 unsigned fcount=0;for(uint32_t id:ord){Template&t=ts[id];std::vector<Field>keep;for(Field f:t.fields)if(f.vary){uint64_t range=f.mx-f.base;f.bw=1;while(f.bw<8&&(range>>(f.bw*8)))f.bw++;if(f.kind==1)f.bw=ub;f.prev=UINT32_MAX;keep.push_back(f);}t.fields=std::move(keep);fcount+=t.fields.size();put(out,t.line.size(),4);put(out,t.fields.size(),2);std::string base=t.line;for(Field&f:t.fields)if(f.kind!=2)memset(&base[f.off],0,f.len);else {for(unsigned k=0;k<f.len;++k)if(digit(base[f.off+k]))base[f.off+k]=0;}out.insert(out.end(),base.begin(),base.end());for(Field&f:t.fields){put(out,f.off,2);put(out,f.len,1);put(out,f.kind,1);put(out,f.bw,1);put(out,f.base,8);}}
 size_t metadata=out.size();int64_t lasttime=0;uint32_t nextuuid=0;for(size_t i=0;i<lines.size();i++){uint32_t id=remap[ids[i]];if(id<255)put(out,id,1);else{put(out,255,1);put(out,id,2);}for(Field&f:ts[ids[i]].fields){if(f.kind==1){uint32_t u=um[lines[i].substr(f.off,f.len)];uint32_t code;if(u==nextuuid){code=0;++nextuuid;}else code=nextuuid-u;vint(out,code);f.prev=u;}else if(f.kind==2){int64_t v=val(lines[i],f),d=v-lasttime;lasttime=v;vint(out,(uint64_t(d)<<1)^uint64_t(d>>63));}else put(out,val(lines[i],f)-f.base,f.bw);}}
 fprintf(stderr,"tmpl raw=%zu lines=%zu templates=%zu fields=%u uuid=%zu metadata=%zu archive=%zu\n",size,lines.size(),ts.size(),fcount,us.size(),metadata,out.size());if(out.size()>cap)return -1;memcpy(archive,out.data(),out.size());return out.size();
 }catch(...){return -1;}}
#else
struct P {uint16_t off;uint8_t len,kind,bw;uint64_t base;uint32_t prev;};
struct T {const uint8_t*line;uint32_t len;uint16_t nf;P*f;};
struct S {const uint8_t*data,*end;uint64_t size;uint32_t nl,nt,nu,ub;T*ts;char*us;ostime::Small*fmt;};
static inline uint16_t ld16(const uint8_t*p){uint16_t v;memcpy(&v,p,2);return v;}
static inline uint32_t ld32(const uint8_t*p){uint32_t v;memcpy(&v,p,4);return v;}
static inline uint64_t ld64(const uint8_t*p){uint64_t v;memcpy(&v,p,8);return v;}
static uint64_t get(const uint8_t*&p,unsigned n){uint64_t v;switch(n){case 1:v=*p;break;case 2:v=ld16(p);break;case 3:v=ld16(p)|uint32_t(p[2])<<16;break;case 4:v=ld32(p);break;case 5:v=ld32(p)|uint64_t(p[4])<<32;break;case 6:v=ld32(p)|uint64_t(ld16(p+4))<<32;break;case 7:v=ld32(p)|uint64_t(ld16(p+4))<<32|uint64_t(p[6])<<48;break;default:v=ld64(p);}p+=n;return v;}
static inline __m256i tohex(const uint8_t*p){__m128i b=_mm_loadu_si128((const __m128i*)p),mask=_mm_set1_epi8(15),chars=_mm_setr_epi8('0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f');__m128i lo=_mm_shuffle_epi8(chars,_mm_and_si128(b,mask)),hi=_mm_shuffle_epi8(chars,_mm_and_si128(_mm_srli_epi16(b,4),mask));return _mm256_set_m128i(_mm_unpackhi_epi8(hi,lo),_mm_unpacklo_epi8(hi,lo));}
static inline void uuidhex(char*u,const uint8_t*p,unsigned n){__m256i x=tohex(p);if(n==36){alignas(64)static const uint8_t ix[64]={0,1,2,3,4,5,6,7,0,8,9,10,11,0,12,13,14,15,0,16,17,18,19,0,20,21,22,23,24,25,26,27,28,29,30,31};__m512i z=_mm512_permutexvar_epi8(_mm512_load_si512(ix),_mm512_castsi256_si512(x));z=_mm512_mask_mov_epi8(z,(1ull<<8)|(1ull<<13)|(1ull<<18)|(1ull<<23),_mm512_set1_epi8('-'));_mm512_mask_storeu_epi8(u,(1ull<<36)-1,z);}else{_mm256_storeu_si256((__m256i*)u,x);if(n==40){uint32_t b;memcpy(&b,p+16,4);for(int j=0;j<4;j++){u[32+j*2]="0123456789abcdef"[(b>>(j*8+4))&15];u[33+j*2]="0123456789abcdef"[(b>>(j*8))&15];}}}}

static uint64_t gv(const uint8_t*&p){uint64_t v=0;unsigned k=0;while(*p&128){v|=uint64_t(*p++&127)<<k;k+=7;}return v|(uint64_t(*p++)<<k);}
static const char pairs[]="00010203040506070809101112131415161718192021222324252627282930313233343536373839404142434445464748495051525354555657585960616263646566676869707172737475767778798081828384858687888990919293949596979899";
static inline void dec(char*p,uint64_t v,unsigned n){while(n>=2){uint64_t q=v/100;unsigned r=v-q*100;n-=2;memcpy(p+n,pairs+r*2,2);v=q;}if(n)*p='0'+v;}
extern "C" void lab_close(void*state){S*s=(S*)state;if(s){if(s->ts)for(uint32_t i=0;i<s->nt;i++)free(s->ts[i].f);free(s->ts);free(s->us);delete s->fmt;free(s);}}
static inline bool space(const uint8_t*p,const uint8_t*end,size_t n){return p<=end&&n<=size_t(end-p);}
extern "C" void* lab_open(const uint8_t*a,size_t size){
 if(!a||size<32)return nullptr;const uint8_t*p=a,*end=a+size;if(get(p,8)!=0x314c504d54ULL)return nullptr;
 S*s=(S*)calloc(1,sizeof(S));if(!s)return nullptr;
 s->size=get(p,8);s->nl=get(p,4);s->nt=get(p,4);s->nu=get(p,4);s->ub=get(p,4);s->end=end;
 if(s->size>SIZE_MAX||s->nl>s->size||s->nt>s->nl||s->nt>65536||s->nt>size/6||s->nu>size/17||(s->ub!=2&&s->ub!=3)||s->nu>(1u<<(s->ub*8))){lab_close(s);return nullptr;}
 s->ts=(T*)calloc(s->nt,sizeof(T));s->us=(char*)malloc((size_t(s->nu)+1)*40);s->fmt=new(std::nothrow) ostime::Small;
 if((s->nt&&!s->ts)||!s->us||!s->fmt){lab_close(s);return nullptr;}
 for(uint32_t i=0;i<s->nu;i++){
  if(!space(p,end,1)){lab_close(s);return nullptr;}unsigned n=*p++;
  if((n!=32&&n!=36&&n!=40)||!space(p,end,n==40?20:16)){lab_close(s);return nullptr;}
  uuidhex(s->us+i*40,p,n);p+=n==40?20:16;
 }
 for(uint32_t i=0;i<s->nt;i++){
  if(!space(p,end,6)){lab_close(s);return nullptr;}T&t=s->ts[i];t.len=get(p,4);t.nf=get(p,2);
  if(!t.len||t.len>s->size||!space(p,end,t.len)){lab_close(s);return nullptr;}t.line=p;p+=t.len;
  if(!space(p,end,size_t(t.nf)*13)){lab_close(s);return nullptr;}t.f=(P*)malloc(t.nf*sizeof(P));if(t.nf&&!t.f){lab_close(s);return nullptr;}
  for(unsigned j=0;j<t.nf;j++){
   P&f=t.f[j];f.off=get(p,2);f.len=get(p,1);f.kind=get(p,1);f.bw=get(p,1);f.base=get(p,8);
   if(!f.len||f.off>t.len||f.len>t.len-f.off||f.kind>2||f.bw<1||f.bw>8||(f.kind==0&&f.len>18)||(f.kind==1&&(f.bw!=s->ub||(f.len!=32&&f.len!=36&&f.len!=40)))||(f.kind==2&&f.len!=12)){lab_close(s);return nullptr;}
  }
 }
 s->data=p;return s;
}
extern "C" int64_t lab_decode(void*state,uint8_t*out,size_t cap){
 S*s=(S*)state;if(!s||cap<s->size||(!out&&s->size))return -1;const uint8_t*p=s->data;uint8_t*o=out;int64_t time=0;size_t remaining=s->size;uint32_t nextuuid=0;
 for(uint32_t i=0;i<s->nl;i++){
  if(!space(p,s->end,1))return -1;uint32_t id=*p++;if(id==255){if(!space(p,s->end,2))return -1;id=get(p,2);}if(id>=s->nt)return -1;
  const T&t=s->ts[id];if(t.len>remaining)return -1;remaining-=t.len;memcpy(o,t.line,t.len);
  for(unsigned j=0;j<t.nf;j++){
   P&f=t.f[j];char*d=(char*)o+f.off;
   if(f.kind==1){
    uint32_t code=0,shift=0;while(true){if(!space(p,s->end,1)||shift>=28)return -1;unsigned b=*p++;code|=uint32_t(b&127)<<shift;if(!(b&128))break;shift+=7;}if(code>nextuuid)return -1;uint32_t v=nextuuid-code;nextuuid+=(code==0);if(v>=s->nu)return -1;
    const char*u=s->us+v*40;if(f.len==36){_mm256_storeu_si256((__m256i*)d,_mm256_loadu_si256((const __m256i*)u));memcpy(d+32,u+32,4);}else memcpy(d,u,f.len);
   }else if(f.kind==2){
    uint64_t z=0;unsigned shift=0;while(true){if(!space(p,s->end,1)||shift>=35)return -1;uint32_t v=*p++;z|=uint64_t(v&127)<<shift;if(!(v&128))break;shift+=7;}if(z>172800000)return -1;
    time+=(z>>1)^-(z&1);if(uint64_t(time)>=86400000)return -1;s->fmt->time(d,time);
   }else{
    if(!space(p,s->end,f.bw))return -1;uint64_t v=get(p,f.bw);if(v>UINT64_MAX-f.base)return -1;dec(d,v+f.base,f.len);
   }
  }
  o+=t.len;
 }
 if(remaining||p!=s->end)return -1;return s->size;
}
#endif
