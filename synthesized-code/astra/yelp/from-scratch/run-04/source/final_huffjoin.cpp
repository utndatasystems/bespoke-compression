#include <interface/codec.h>
#include "inspect_pack.h"
#include "final_lz.h"
#include "final_huff8.h"
#ifndef STRUCT_LZ
#define STRUCT_LZ 3
#endif
#include <algorithm>
#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept>
#include <cstring>
#include <cstdio>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
using U=uint8_t;
struct H {uint64_t magic,rawsize,nrows,lastnl; uint64_t dictpos[6],dictn[6],idspos,litpos,coordpos,rowpos,total,flags,dictcpos,dictcsize,dictusize,litcsize,litusize,rowcsize,rowusize;};
struct D {uint32_t off; uint16_t len,pad;};
static constexpr uint64_t MAGIC=0x7374727563740009ull;
static uint16_t get16(const U*&p){uint16_t x;memcpy(&x,p,2);p+=2;return x;}
#ifndef STRUCT_DECODER
static void put16(std::vector<U>&v,uint16_t x){v.push_back(x);v.push_back(x>>8);}
static void app(std::vector<U>&v,const std::string&s){v.insert(v.end(),s.begin(),s.end());}
struct Dict {
 std::unordered_map<std::string,uint32_t> m;std::vector<std::string> a;std::vector<unsigned>f;
 unsigned id(const std::string&s){auto it=m.find(s);if(it!=m.end()){f[it->second]++;return it->second;}unsigned id=a.size();if(id>=65535)throw std::runtime_error("dict overflow");m.emplace(s,id);a.push_back(s);f.push_back(1);return id;}
};
static std::vector<std::string> tokens(const std::string&s){std::vector<std::string>r;size_t start=1;bool q=false;for(size_t i=1;i+1<s.size();++i){if(s[i]=='\\'&&q){++i;continue;}if(s[i]=='"')q=!q;if(s[i]==','&&!q){r.push_back(s.substr(start,i-start)+",");start=i+1;}}if(start<s.size()-1)r.push_back(s.substr(start,s.size()-1-start)+",");return r;}
static const char* keys[14]={"business_id","name","address","city","state","postal_code","latitude","longitude","stars","review_count","is_open","attributes","categories","hours"};
struct Row {std::string v[14];};
static std::vector<Row> parse(const U*raw,size_t sz){std::vector<Row>r;const char*b=(const char*)raw,*e=b+sz;while(b<e){const char*z=(const char*)memchr(b,'\n',e-b);if(!z)z=e;std::string s(b,z-b);if(s.empty())throw std::runtime_error("empty");Row row;size_t start=16; // len of {"business_id":
 start=std::string("{\"business_id\":").size();
 for(unsigned i=0;i<13;i++){std::string d=",\""+std::string(keys[i+1])+"\":";size_t end=s.find(d,start);if(end==std::string::npos)throw std::runtime_error("parse");row.v[i]=s.substr(start,end-start);start=end+d.size();}
 row.v[13]=s.substr(start,s.size()-1-start);r.push_back(std::move(row));b=z+(z<e);}
 return r;}
extern "C" int64_t lab_encode(const U*raw,size_t sz,U*archive,size_t cap){try{
 if(!sz)return -1;
 auto rr=parse(raw,sz); Dict ds[6]; // location, ratings, attrs, cats, common hours, rare hours
 std::unordered_map<std::string,unsigned> hfreq;for(auto&r:rr)hfreq[r.v[13]]++;
 std::vector<U>ids,lit,coord,row;
 for(auto&r:rr){U ib[16];if(r.v[0].size()!=24||!inspect_pack_id((const U*)r.v[0].data()+1,ib))return -1;ids.insert(ids.end(),ib,ib+16);
 if(r.v[1].size()>255||r.v[2].size()>255)return -1;row.push_back(r.v[1].size());row.push_back(r.v[2].size());app(lit,r.v[1]);app(lit,r.v[2]);
 size_t latdot=r.v[6].find('.'),londot=r.v[7].find('.');
 if(latdot==std::string::npos||londot==std::string::npos)return -1;
 unsigned n=r.v[6].size()-latdot-1,m=r.v[7].size()-londot-1;
 if(!n||n>15||!m||m>15)return -1;
 row.push_back(n|(m<<4));U co[16];
 std::string fractions=r.v[6].substr(latdot+1)+r.v[7].substr(londot+1);
 unsigned nc=inspect_pack_coord((const U*)fractions.data(),n+m,co);coord.insert(coord.end(),co,co+nc);
 std::string location=",\"city\":"+r.v[3]+",\"state\":"+r.v[4]+",\"postal_code\":"+r.v[5]+",\"latitude\":"+r.v[6].substr(0,latdot+1);
 location.push_back('\0');location+=",\"longitude\":"+r.v[7].substr(0,londot+1);
 put16(row,ds[0].id(location));
 put16(row,ds[1].id(",\"stars\":"+r.v[8]+",\"review_count\":"+r.v[9]+",\"is_open\":"+r.v[10]+",\"attributes\":"));
 if(r.v[11]=="null")row.push_back(0);else{auto t=tokens(r.v[11]);if(t.empty()||t.size()>255)return -1;row.push_back(t.size());for(auto&s:t)put16(row,ds[2].id(s));}
 if(r.v[12]=="null")row.push_back(0);else{std::string s=r.v[12].substr(1,r.v[12].size()-2);std::vector<std::string>ts;size_t p=0;for(;;){size_t q=s.find(", ",p);if(q==std::string::npos){ts.push_back(s.substr(p)+", ");break;}ts.push_back(s.substr(p,q-p)+", ");p=q+2;}if(ts.size()>255)return -1;row.push_back(ts.size());for(auto&s:ts)put16(row,ds[3].id(s));}
 if(hfreq[r.v[13]]>=4||r.v[13]=="null"){if(ds[4].a.size()>=32768)return -1;put16(row,ds[4].id(",\"hours\":"+r.v[13]+"}\n"));}else{auto t=tokens(r.v[13]);put16(row,0x8000|t.size());for(auto&s:t)put16(row,ds[5].id(s));}
 }
 std::vector<unsigned>remap[6];for(unsigned k:{2u,3u,5u}){std::vector<unsigned>order(ds[k].a.size());for(unsigned i=0;i<order.size();i++)order[i]=i;std::stable_sort(order.begin(),order.end(),[&](unsigned x,unsigned y){return ds[k].f[x]>ds[k].f[y];});std::vector<std::string>sorted;remap[k].resize(order.size());for(unsigned id:order){remap[k][id]=sorted.size();sorted.push_back(std::move(ds[k].a[id]));}ds[k].a=std::move(sorted);}
 std::vector<U>vr;const U*rp=row.data();auto pv=[&](unsigned x,unsigned th){if(x<th)vr.push_back(x);else{unsigned y=x-th;vr.push_back(th+(y>>8));vr.push_back(y);}};
 for(size_t i=0;i<rr.size();i++){vr.insert(vr.end(),rp,rp+7);rp+=7;unsigned n=*rp++;vr.push_back(n);while(n--)pv(remap[2][get16(rp)],240);n=*rp++;vr.push_back(n);while(n--)pv(remap[3][get16(rp)],240);n=get16(rp);put16(vr,n);if(n&0x8000){n&=0x7fff;while(n--)pv(remap[5][get16(rp)],128);}}
 row=std::move(vr);
 H h{};h.magic=MAGIC;h.rawsize=sz;h.nrows=rr.size();h.lastnl=raw[sz-1]=='\n';std::vector<U>out;
 for(unsigned k=0;k<6;k++){h.dictpos[k]=out.size();h.dictn[k]=ds[k].a.size();size_t p=out.size();out.resize(p+ds[k].a.size()*sizeof(D));for(unsigned j=0;j<ds[k].a.size();j++){D d{};d.off=out.size()-p;d.len=ds[k].a[j].size();if(d.len!=ds[k].a[j].size())return -1;if(k==0){size_t split=ds[k].a[j].find('\0');if(split==std::string::npos||split>=65535)return -1;d.pad=split;}app(out,ds[k].a[j]);memcpy(out.data()+p+j*sizeof(D),&d,sizeof(d));}out.resize(out.size()+64);}
 std::vector<U>db=std::move(out);for(unsigned k=0;k<6;k++)for(size_t j=0;j<h.dictn[k];j++){uint32_t zero=0;memcpy(db.data()+h.dictpos[k]+j*sizeof(D),&zero,4);}out.resize(sizeof(H));h.flags=STRUCT_LZ;h.dictcpos=out.size();h.dictusize=db.size();if(h.flags&2)db=rootlz::encode(db.data(),db.size());h.dictcsize=db.size();out.insert(out.end(),db.begin(),db.end());
 h.idspos=out.size();out.insert(out.end(),ids.begin(),ids.end());out.resize(out.size()+64);
 h.litpos=out.size();h.litusize=lit.size();if(h.flags&1)lit=rootlz::encode(lit.data(),lit.size());h.litcsize=lit.size();out.insert(out.end(),lit.begin(),lit.end());out.resize(out.size()+64);
 h.coordpos=out.size();out.insert(out.end(),coord.begin(),coord.end());out.resize(out.size()+64);
 h.rowpos=out.size();h.rowusize=row.size();if(h.flags&4){row=rowhuff::encode(row.data(),row.size());if(row.empty())return -1;}h.rowcsize=row.size();out.insert(out.end(),row.begin(),row.end());out.resize(out.size()+64);h.total=out.size();memcpy(out.data(),&h,sizeof(h));
 fprintf(stderr,"STRUCT rows %zu archive %zu ids %zu lit %zu coord %zu row %zu dictionaries",rr.size(),out.size(),ids.size(),lit.size(),coord.size(),row.size());for(unsigned k=0;k<6;k++){size_t bytes=0;for(auto&s:ds[k].a)bytes+=s.size();fprintf(stderr," %u:%zu/%zu",k,ds[k].a.size(),bytes);}fprintf(stderr,"\n");
 if(out.size()>cap)return -1;memcpy(archive,out.data(),out.size());return out.size();
 }catch(...){return -1;}}
#endif
#ifndef STRUCT_ENCODER
struct S {const U*a;H h;const U*dict,*lit,*row;std::unique_ptr<U[]>db,lb,rb,scratch;size_t rowbound;};
static inline unsigned gv(const U*&p,unsigned th){unsigned x=*p++;return x<th?x:th+((x-th)<<8)+*p++;}
static unsigned gvs(const U*&p,const U*e,unsigned th){if(p==e)return ~0u;unsigned x=*p++;if(x<th)return x;if(p==e)return ~0u;return th+((x-th)<<8)+*p++;}
static bool valid(S*s){
 const H&h=s->h;unsigned mx[6]={};
 for(unsigned k=0;k<6;k++){
  uint64_t start=h.dictpos[k],end=k==5?h.dictusize:h.dictpos[k+1];
  if(start>end||end>h.dictusize||h.dictn[k]>=65536||h.dictn[k]*sizeof(D)>end-start)return false;
  const U*d=s->dict+start;
  for(uint64_t j=0;j<h.dictn[k];j++){
   D e;memcpy(&e,d+j*sizeof(D),sizeof e);
   if(e.off<h.dictn[k]*sizeof(D)||e.off>end-start||!e.len||std::max<unsigned>(e.len,64)>end-start-e.off)return false;
   if(k==0&&(!e.pad||unsigned(e.pad)+1>=e.len))return false;
   mx[k]=std::max<unsigned>(mx[k],e.len);
  }
 }
 // Every row has at most 255 name/address bytes, 15 coordinate bytes,
 // 255 attribute/category tokens and 7 individual hours tokens. This bound
 // includes every fixed byte and allows safe unchecked wide stores in a
 // row whenever that much output capacity plus 64 bytes remains.
 s->rowbound=16+22+9+255+11+255+mx[0]+15+13+15+mx[1]
   +std::max<unsigned>(4,1+255*mx[2])+14+std::max<unsigned>(4,255*mx[3])
   +std::max<unsigned>(mx[4],10+7*mx[5]+2);
 s->scratch.reset(new U[s->rowbound+64]);
 return true;
}
extern "C" void*lab_open(const U*a,size_t n){try{
 if(n<sizeof(H))return nullptr;H h;memcpy(&h,a,sizeof h);
 if(h.magic!=MAGIC||h.total!=n||h.flags>7||h.lastnl>1||h.litcsize>n||h.rowcsize>n||!h.nrows||h.rawsize>(1ull<<32)||h.nrows>h.rawsize/32)return nullptr;
 auto range=[&](uint64_t p,uint64_t z){return p<=n&&z<=n-p;};
 if(h.dictcpos!=sizeof(H)||!range(h.dictcpos,h.dictcsize)||h.idspos!=h.dictcpos+h.dictcsize||!range(h.idspos,h.nrows*16+64)||h.litpos!=h.idspos+h.nrows*16+64||!range(h.litpos,h.litcsize+64)||h.coordpos!=h.litpos+h.litcsize+64||h.rowpos<h.coordpos+64||!range(h.rowpos,h.rowcsize+64)||h.total!=h.rowpos+h.rowcsize+64||h.dictusize>h.rawsize||h.litusize>h.rawsize||h.rowusize>h.rawsize)return nullptr;
 if(!(h.flags&1)&&h.litcsize!=h.litusize)return nullptr;if(!(h.flags&2)&&h.dictcsize!=h.dictusize)return nullptr;if(!(h.flags&4)&&h.rowcsize!=h.rowusize)return nullptr;
 S*s=new S{};s->a=a;s->h=h;s->dict=a+h.dictcpos;s->lit=a+h.litpos;s->row=a+h.rowpos;
 if(h.flags&2){s->db.reset(new U[h.dictusize+64]);if(!rootlz::decode(s->dict,h.dictcsize,s->db.get(),h.dictusize)){delete s;return nullptr;}s->dict=s->db.get();}
 if(h.flags&1){s->lb.reset(new U[h.litusize+64]);if(!rootlz::decode(s->lit,h.litcsize,s->lb.get(),h.litusize)){delete s;return nullptr;}s->lit=s->lb.get();}
 if(h.flags&4){s->rb.reset(new U[h.rowusize+64]);if(!rowhuff::decode(s->row,h.rowcsize,s->rb.get(),h.rowusize)){delete s;return nullptr;}s->row=s->rb.get();}
 if(!(h.flags&2)){s->db.reset(new U[h.dictusize+64]);memcpy(s->db.get(),s->dict,h.dictusize);s->dict=s->db.get();}
 for(unsigned k=0;k<6;k++){
  uint64_t start=h.dictpos[k],end=k==5?h.dictusize:h.dictpos[k+1];
  if(start>end||end>h.dictusize||h.dictn[k]>=65536||h.dictn[k]*sizeof(D)>end-start){delete s;return nullptr;}
  uint64_t off=h.dictn[k]*sizeof(D);
  for(unsigned j=0;j<h.dictn[k];j++){D e;memcpy(&e,s->dict+start+j*sizeof(D),sizeof e);
   if(e.off||!e.len||off>end-start||std::max<unsigned>(e.len,64)>end-start-off){delete s;return nullptr;}
   e.off=off;off+=e.len;memcpy(s->db.get()+start+j*sizeof(D),&e,sizeof e);
  }
  if(off+64!=end-start){delete s;return nullptr;}
 }
 if(!valid(s)){delete s;return nullptr;}return s;
 }catch(...){return nullptr;}}
static inline void cp(U*&o,const U*s,unsigned n){if(n<=64){_mm512_storeu_si512(o,_mm512_loadu_si512(s));}else{unsigned x=0;for(;x+64<n;x+=64)_mm512_storeu_si512(o+x,_mm512_loadu_si512(s+x));_mm512_storeu_si512(o+n-64,_mm512_loadu_si512(s+n-64));}o+=n;}
template<size_t N>static inline void fixed(U*&o,const char(&s)[N]){memcpy(o,s,N-1);o+=N-1;}
static inline void dict(U*&o,const U*d,unsigned id){D e;memcpy(&e,d+id*sizeof(D),sizeof e);cp(o,d+e.off,e.len);}
template<unsigned TH>static inline bool checked_tokens(U*&o,const U*&r,const U*re,const U*d,unsigned count,unsigned ndict){
 if((size_t)(re-r)>=2*count){
  do{unsigned id=gv(r,TH);if(id>=ndict)return false;dict(o,d,id);}while(--count);
 }else{
  do{unsigned id=gvs(r,re,TH);if(id>=ndict)return false;dict(o,d,id);}while(--count);
 }
 return true;
}
extern "C" int64_t lab_decode(void*state,U*output,size_t cap){
 if(!state)return -1;S*s=(S*)state;if(cap<s->h.rawsize)return -1;
 const U*a=s->a;const H&h=s->h;const U*ids=a+h.idspos,*lit=s->lit,*co=a+h.coordpos,*r=s->row;
 const U*le=lit+h.litusize,*ce=a+h.rowpos-64,*re=r+h.rowusize;
 const U*d[6];for(unsigned j=0;j<6;j++)d[j]=s->dict+h.dictpos[j];U*o=output;
 for(uint64_t i=0;i<h.nrows;i++){
  if(re-r<7)return -1;
  unsigned nn=*r++,na=*r++,cl=*r++;unsigned lat=cl&15,lon=cl>>4;
  unsigned loc=get16(r),rating=get16(r);
  unsigned nco=(lat+lon+1)/2;
  if(!lat||!lon||loc>=h.dictn[0]||rating>=h.dictn[1]||(size_t)(le-lit)<nn+na||(size_t)(ce-co)<nco)return -1;
  size_t remain=h.rawsize-(o-output);U*dest=o;
  const bool scratch=remain<s->rowbound+64;if(scratch)o=s->scratch.get();
  fixed(o,"{\"business_id\":\"");inspect_unpack_id(ids,o);ids+=16;o+=22;
  fixed(o,"\",\"name\":");cp(o,lit,nn);lit+=nn;fixed(o,",\"address\":");cp(o,lit,na);lit+=na;
  D location;memcpy(&location,d[0]+loc*sizeof(D),sizeof location);
  cp(o,d[0]+location.off,location.pad);inspect_unpack_coord_wide(co,o);o+=lat;
  cp(o,d[0]+location.off+location.pad+1,location.len-location.pad-1);
  uint64_t longitude;memcpy(&longitude,co+lat/2,8);longitude>>=(lat&1)*4;
  _mm_storeu_si128((__m128i*)o,inspect_coord_vector(longitude));co+=nco;o+=lon;dict(o,d[1],rating);
  if(r==re)return -1;unsigned n=*r++;
  if(!n){fixed(o,"null");}else{*o++='{';if(!checked_tokens<240>(o,r,re,d[2],n,h.dictn[2]))return -1;o[-1]='}';}
  fixed(o,",\"categories\":");if(r==re)return -1;n=*r++;
  if(!n){fixed(o,"null");}else{*o++='"';if(!checked_tokens<240>(o,r,re,d[3],n,h.dictn[3]))return -1;o-=2;*o++='"';}
  if(re-r<2)return -1;n=get16(r);
  if(!(n&0x8000)){if(n>=h.dictn[4])return -1;dict(o,d[4],n);}
  else{n&=0x7fff;if(!n||n>7)return -1;fixed(o,",\"hours\":{");if(!checked_tokens<128>(o,r,re,d[5],n,h.dictn[5]))return -1;o[-1]='}';fixed(o,"}\n");}
  if(i+1==h.nrows&&!h.lastnl)--o;
  if(scratch){size_t z=o-s->scratch.get();if(z>remain)return -1;memcpy(dest,s->scratch.get(),z);o=dest+z;}
 }
 if(r!=re||lit!=le||co!=ce||(size_t)(o-output)!=h.rawsize)return -1;
 return o-output;
}
extern "C" void lab_close(void*s){delete (S*)s;}
#endif
#ifdef STRUCT_MAIN
int main(){std::ifstream f("/inputs/yelp-business.jsonl",std::ios::binary);std::string raw((std::istreambuf_iterator<char>(f)),{});std::vector<U>a(raw.size()*2),o(raw.size()+64);auto t=std::chrono::steady_clock::now();auto n=lab_encode((const U*)raw.data(),raw.size(),a.data(),a.size());std::cerr<<"encoded "<<n<<" time "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-t).count()<<"\n";if(n<0)return 1;a.resize(n);std::ofstream z("/work/final_huffjoin_archive.bin",std::ios::binary);z.write((char*)a.data(),a.size());z.close();for(int i=0;i<12;i++){t=std::chrono::steady_clock::now();void*s=lab_open(a.data(),a.size());auto m=lab_decode(s,o.data(),raw.size());lab_close(s);double dt=std::chrono::duration<double>(std::chrono::steady_clock::now()-t).count();if(m!=(int64_t)raw.size()||memcmp(raw.data(),o.data(),raw.size())){size_t j=0;while(j<raw.size()&&raw[j]==o[j])j++;std::cerr<<"BAD size "<<m<<" diff "<<j<<" expected "<<raw.substr(j,100)<<" got "<<std::string((char*)o.data()+j,100)<<"\n";return 2;}std::cout<<"trial "<<i<<" secs "<<dt<<" MBps "<<raw.size()/dt/1e6<<"\n";}}
#endif
