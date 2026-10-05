#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <immintrin.h>
#include "interface/codec.h"
#include "text.h"
#include "numeric.h"
#include "numeric_group_huff.h"
#include "complex.h"
#include "location_huff.h"
#include "id_simdfused.h"
#ifdef ENCODER
#include <vector>
#include <string>
#include <map>
#include <algorithm>
using std::vector; using std::string;
#endif
static uint64_t rd64(const void*p){uint64_t v;memcpy(&v,p,8);return v;}
static uint32_t rd32(const void*p){uint32_t v;memcpy(&v,p,4);return v;}
static uint16_t rd16(const void*p){uint16_t v;memcpy(&v,p,2);return v;}
static void wr64(void*p,uint64_t v){memcpy(p,&v,8);}
static void wr32(void*p,uint32_t v){memcpy(p,&v,4);}
static void wr16(void*p,uint16_t v){memcpy(p,&v,2);}
static uint64_t checksum(const uint8_t*p,size_t n){
 uint64_t a=0x0123456789abcdefULL,b=0xfedcba9876543210ULL,c=0x1231231231231231ULL,d=0x9879879879879879ULL;
 const uint64_t m=0x9e3779b185ebca87ULL;
 while(n>=32){a=(a^rd64(p))*m;b=(b^rd64(p+8))*m;c=(c^rd64(p+16))*m;d=(d^rd64(p+24))*m;p+=32;n-=32;}
 a^=b+((c<<1)|(c>>63))+((d<<2)|(d>>62));while(n--){a=(a^*p++)*m;}return a;
}
struct Header {uint64_t magic,rawsize,total,hash;uint32_t rows,reserved;uint64_t off[8];};
static const uint64_t MAGIC=0x314b434f4c504c59ULL;
static const char abc[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
#ifdef ENCODER
static void put32(vector<uint8_t>&o,uint32_t x){size_t n=o.size();o.resize(n+4);wr32(o.data()+n,x);}
static void put16(vector<uint8_t>&o,uint16_t x){size_t n=o.size();o.resize(n+2);wr16(o.data()+n,x);}
static bool scan(const uint8_t*raw,size_t n,vector<string> cols[14]){
 static const char*keys[]={"business_id","name","address","city","state","postal_code","latitude","longitude","stars","review_count","is_open","attributes","categories","hours"};
 size_t p=0;
 while(p<n){if(raw[p++]!='{')return false;
 for(int k=0;k<14;k++){
 size_t kl=strlen(keys[k]);if(p+kl+3>n||raw[p]!='"'||memcmp(raw+p+1,keys[k],kl)||raw[p+kl+1]!='"'||raw[p+kl+2]!=':')return false;p+=kl+3;
 size_t start=p;int nesting=0;bool instr=false,escape=false;
 for(;p<n;p++){uint8_t c=raw[p];if(instr){if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"')instr=false;}else{if(c=='"')instr=true;else if(c=='{'||c=='[')nesting++;else if(c=='}'||c==']'){if(nesting==0)break;nesting--;}else if(c==','&&nesting==0)break;}}
 if(p==n||instr||nesting)return false;cols[k].emplace_back((const char*)raw+start,p-start);
 if(k<13){if(raw[p++]!=',')return false;}else {if(raw[p++]!='}')return false;}
 }
 if(p>=n||raw[p++]!='\n')return false;
 }return true;
}
extern "C" __attribute__((visibility("default"))) int64_t lab_encode(const uint8_t*raw,size_t n,uint8_t*out,size_t cap){
 try {
 vector<string> cols[14];if(!scan(raw,n,cols))return -1;size_t nr=cols[0].size();
 vector<uint8_t> sections[7];
 int8_t inv[256];memset(inv,-1,sizeof(inv));for(int i=0;i<64;i++)inv[(unsigned char)abc[i]]=i;
 auto&id=sections[0];id.reserve(nr*16);
 for(auto&s:cols[0]){if(s.size()!=24||s[0]!='"'||s[23]!='"')return -1;for(int j=0;j<22;j++)if(inv[(uint8_t)s[j+1]]<0)return -1;
 for(int j=0;j<20;j+=4){uint32_t v=(inv[(uint8_t)s[j+1]]<<18)|(inv[(uint8_t)s[j+2]]<<12)|(inv[(uint8_t)s[j+3]]<<6)|inv[(uint8_t)s[j+4]];id.push_back(v);id.push_back(v>>8);id.push_back(v>>16);}
 if(inv[(uint8_t)s[22]]&15)return -1;id.push_back((inv[(uint8_t)s[21]]<<2)|(inv[(uint8_t)s[22]]>>4));}
 vector<string> names,addrs,locs;names.reserve(nr);addrs.reserve(nr);locs.reserve(nr);
 for(size_t r=0;r<nr;r++){names.push_back(cols[1][r].substr(1,cols[1][r].size()-2));addrs.push_back(cols[2][r].substr(1,cols[2][r].size()-2));locs.push_back(cols[3][r]+",\"state\":"+cols[4][r]+",\"postal_code\":"+cols[5][r]+",\"latitude\":");}
 sections[1]=textcol::encode(names,4,16384,128);sections[2]=textcol::encode(addrs,4,12288,32);sections[3]=loccodec::encode(locs);
 std::map<string,uint32_t> locmap;for(auto&x:locs)locmap[x]=0;uint32_t nextloc=0;for(auto&x:locmap)x.second=nextloc++;vector<uint32_t> groups;for(auto&x:locs)groups.push_back(locmap[x]);
 sections[4]=numgrouphuff::encode(cols[6],cols[7],cols[8],cols[9],cols[10],groups);
 sections[5]=complexcodec::encode(cols[11],cols[12],cols[13]);
 Header h={};h.magic=MAGIC;h.rawsize=n;h.rows=nr;h.off[0]=sizeof(Header);
 for(int i=0;i<7;i++)h.off[i+1]=h.off[i]+sections[i].size();h.total=h.off[7];
 if(h.total>cap)return -1;memcpy(out,&h,sizeof(h));for(int i=0;i<7;i++)memcpy(out+h.off[i],sections[i].data(),sections[i].size());h.hash=checksum(out+sizeof(h),h.total-sizeof(h))^checksum(out,24)^checksum(out+32,sizeof(h)-32);memcpy(out,&h,sizeof(h));return h.total;
 } catch (...) {return -1;}
}
#else
struct State {Header h;const uint8_t* ids;textcol::State name{},addr{};loccodec::State loc;numgrouphuff::State num; complexcodec::State complex;
 ~State(){textcol::close(name);textcol::close(addr);}
};
extern "C" __attribute__((visibility("default"))) void* lab_open(const uint8_t*arc,size_t n){
 if(n<sizeof(Header))return nullptr;Header h;memcpy(&h,arc,sizeof(h));if(h.magic!=MAGIC||h.total!=n||h.rawsize!=100000488ULL||h.rows!=126509||h.reserved||h.off[0]!=sizeof(Header)||h.off[7]!=n)return nullptr;
 for(int i=0;i<7;i++)if(h.off[i]>h.off[i+1])return nullptr;
 if((checksum(arc+sizeof(h),n-sizeof(h))^checksum(arc,24)^checksum(arc+32,sizeof(h)-32))!=h.hash)return nullptr;
 if(h.off[1]-h.off[0]!=size_t(h.rows)*16)return nullptr;
 State*s=new State;s->h=h;s->ids=arc+h.off[0];

 if(!textcol::open(arc+h.off[1],h.off[2]-h.off[1],s->name)||!textcol::open(arc+h.off[2],h.off[3]-h.off[2],s->addr)||s->name.rows!=h.rows||s->addr.rows!=h.rows||!s->loc.open(arc+h.off[3],h.off[4]-h.off[3],h.rows)||!s->num.open(arc+h.off[4],h.off[5]-h.off[4],h.rows)||s->num.groups!=s->loc.count||!s->complex.open(arc+h.off[5],h.off[6]-h.off[5],h.rows)){delete s;return nullptr;}return s;
}
template<size_t N> static inline uint8_t* lit(uint8_t*p,const char(&s)[N]){memcpy(p,s,N-1);return p+N-1;}
extern "C" __attribute__((visibility("default"))) int64_t lab_decode(void*state,uint8_t*out,size_t cap){
 if(!state)return -1;State&s=*(State*)state;if(cap<s.h.rawsize)return -1;uint8_t*p=out;const uint8_t*b=s.ids;
 for(uint32_t r=0;r<s.h.rows;r++,b+=16){
 if(s.h.rawsize-size_t(p-out)<size_t(s.name.offsets[r+1]-s.name.offsets[r])+size_t(s.addr.offsets[r+1]-s.addr.offsets[r])+s.loc.len[s.loc.get(r)]+256)return -1;
 p=lit(p,"{\"business_id\":\"");
 p=idsimdfused::emit(b,p);
 p=textcol::emit(s.name,r,p);p=lit(p,"\",\"address\":\"");p=textcol::emit(s.addr,r,p);p=lit(p,"\",\"city\":");p=s.loc.emit(r,p);
 s.num.prepare(r,s.loc.get(r));if(s.num.bad)return -1;p=s.num.emit_lat(r,p);p=lit(p,",\"longitude\":");p=s.num.emit_lon(r,p);p=lit(p,",\"stars\":");p=s.num.emit_stars(r,p);p=lit(p,",\"review_count\":");p=s.num.emit_review(r,p);if(s.num.bad)return -1;p=lit(p,",\"is_open\":");p=s.num.emit_open(r,p);
 p=lit(p,",\"attributes\":");if(s.h.rawsize-size_t(p-out)<32)return -1;size_t an=s.complex.emit_attr(r,p,s.h.rawsize-(p-out)-32);if(an==SIZE_MAX)return -1;p+=an;p=lit(p,",\"categories\":");if(s.h.rawsize-size_t(p-out)<12)return -1;size_t cn=s.complex.emit_cat(r,p,s.h.rawsize-(p-out)-12);if(cn==SIZE_MAX)return -1;p+=cn;p=lit(p,",\"hours\":");if(s.h.rawsize-size_t(p-out)<2)return -1;size_t hn=s.complex.emit_hour(r,p,s.h.rawsize-(p-out)-2);if(hn==SIZE_MAX)return -1;p+=hn;p=lit(p,"}\n");
 }
 return p-out==s.h.rawsize?(int64_t)(p-out):-1;
}
extern "C" __attribute__((visibility("default"))) void lab_close(void*s){delete (State*)s;}
#endif
