
#include "codec.h"
#include "entropy.h"
#include "lit2.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#ifndef DECODE_ONLY
#include "lzparse.h"
#ifdef FITTED_PARSE
#include "fitted.h"
#endif
#include <vector>
#include <stdio.h>
#endif
#ifndef OF_CONTEXT
#define OF_CONTEXT 0
#endif
#ifndef LIT_CONTEXT
#define LIT_CONTEXT 0
#endif
#ifndef NREP
#define NREP 4
#endif
static uint32_t rd32(const uint8_t*p){uint32_t v;memcpy(&v,p,4);return v;}
static uint64_t rd64(const uint8_t*p){uint64_t v;memcpy(&v,p,8);return v;}
static void wr32(uint8_t*p,uint32_t v){memcpy(p,&v,4);}
static void wr64(uint8_t*p,uint64_t v){memcpy(p,&v,8);}
static uint64_t rot(uint64_t v,unsigned k){return(v<<k)|(v>>(64-k));}
static uint64_t check(const uint8_t*p,size_t n){
 uint64_t a=0x123456789abcdef0ULL,b=0x987654321fedcba0ULL,c=0xabcdef0123456789ULL,d=0x76543210fedcba98ULL;
 size_t z=n;
 while(n>=32){a=rot((a^rd64(p))*0x9e3779b185ebca87ULL,27);b=rot((b^rd64(p+8))*0xc2b2ae3d27d4eb4fULL,29);c=rot((c^rd64(p+16))*0x9e3779b185ebca87ULL,31);d=rot((d^rd64(p+24))*0xc2b2ae3d27d4eb4fULL,33);p+=32;n-=32;}
 a^=rot(b,7)^rot(c,19)^rot(d,41)^z;
 while(n--){a=(a^*p++)*0x100000001b3ULL;}
 a^=a>>29;a*=0x165667b19e3779f9ULL;a^=a>>32;return a;
}
struct BitR {
 const uint8_t *p,*end; uint64_t buf=0;unsigned have=0;bool ok=true;
 uint32_t get(unsigned k) {
  if(k>31){ok=false;return 0;}
  while(have<k){if(p==end){ok=false;return 0;}buf|=uint64_t(*p++)<<have;have+=8;}
  uint32_t v=uint32_t(buf)&((1u<<k)-1);buf>>=k;have-=k;return v;
 }
};
static uint32_t getlen(unsigned code,BitR&bits){
 if(code<16)return code;
 unsigned k=((code-16)>>2)+2;
 if(k>26){bits.ok=false;return 0;}
 return ((4u+((code-16)&3))<<k)+bits.get(k);
}
struct State {const uint8_t* a;size_t as;uint32_t n,ns,nl;uint32_t sz[5];uint32_t ctx;};
extern "C" void* lab_open(const uint8_t*a,size_t s){
 if(!a||s<64||memcmp(a,"PYLZ001",8)||rd64(a+8)!=s||check(a+24,s-24)!=rd64(a+16))return nullptr;
 uint32_t n=rd32(a+24),ns=rd32(a+28),nl=rd32(a+32);
 if(n>1000000000u||!ns||ns>n+1||nl>n||rd32(a+56)>7||((rd32(a+56)&4)&&!(rd32(a+56)&1)))return nullptr;
 size_t total=64; for(unsigned i=0;i<5;i++){uint32_t z=rd32(a+36+4*i);if(z>s-total)return nullptr;total+=z;}
 if(total!=s)return nullptr;
 State*t=(State*)malloc(sizeof(State));if(!t)return nullptr;
 t->a=a;t->as=s;t->n=n;t->ns=ns;t->nl=nl;t->ctx=rd32(a+56);
 for(unsigned i=0;i<5;i++)t->sz[i]=rd32(a+36+4*i);
 return t;
}
extern "C" void lab_close(void*v){free(v);}
extern "C" int64_t lab_decode(void*v,uint8_t*out,size_t cap){
 if(!v)return -1; State*t=(State*)v;
 if(cap<t->n||(!out&&t->n))return -1;
 size_t tmpn=size_t(t->ns)*3+t->nl;
 uint8_t*tmp=(uint8_t*)malloc(tmpn?tmpn:1);if(!tmp)return -1;
 uint8_t*ll=tmp,*ml=ll+t->ns,*oc=ml+t->ns,*lit=oc+t->ns;
 const uint8_t*p=t->a+64;
 bool ok=ent::decode(p,t->sz[0],ll,t->ns);p+=t->sz[0];
 ok=ok&&ent::decode(p,t->sz[1],ml,t->ns);p+=t->sz[1];
 uint8_t*op[64],*oe[64];
 if(ok&&(t->ctx&2)){
  if(t->sz[2]<512)ok=false;
  else{size_t ro=0,so=512;
   for(unsigned c=0;c<64&&ok;c++){
    uint32_t rn=rd32(p+c*8),sn=rd32(p+c*8+4);
    if(rn>t->ns-ro||sn>t->sz[2]-so){ok=false;break;}
    op[c]=oc+ro;oe[c]=oc+ro+rn;ok=ent::decode(p+so,sn,op[c],rn);ro+=rn;so+=sn;
   }
   if(ro!=t->ns||so!=t->sz[2])ok=false;
  }
 }else if(ok)ok=ent::decode(p,t->sz[2],oc,t->ns);
 p+=t->sz[2];
 uint8_t* cp[256],*ce[256];lit2::Decoder litdecoder;
 if(ok&&(t->ctx&4))ok=litdecoder.open(p,t->sz[3],lit,t->nl);
 else if(ok&&(t->ctx&1)){
  if(t->sz[3]<2048)ok=false;
  else {size_t ro=0,so=2048;
   for(unsigned c=0;c<256&&ok;c++){
    uint32_t rn=rd32(p+c*8),sn=rd32(p+c*8+4);
    if(rn>t->nl-ro||sn>t->sz[3]-so){ok=false;break;}
    cp[c]=lit+ro;ce[c]=lit+ro+rn;
    ok=ent::decode(p+so,sn,cp[c],rn);ro+=rn;so+=sn;
   }
   if(ro!=t->nl||so!=t->sz[3])ok=false;
  }
 }else if(ok)ok=ent::decode(p,t->sz[3],lit,t->nl);
 p+=t->sz[3];
 if(!ok){free(tmp);return -1;}
 BitR bits{p,p+t->sz[4]};
 uint32_t rep[NREP];for(unsigned j=0;j<NREP;j++)rep[j]=0;
 size_t pos=0,lp=0;
 for(uint32_t i=0;i<t->ns;i++){
  uint32_t l=getlen(ll[i],bits),m=getlen(ml[i],bits);
  if(!bits.ok||l>t->n-pos||l>t->nl-lp){ok=false;break;}
  if(t->ctx&4){
   for(uint32_t j=0;j<l;j++){unsigned c=pos?out[pos-1]:0;unsigned prev=pos>1?out[pos-2]:0;if(!litdecoder.get((prev<<8)|c,out[pos])){ok=false;break;}pos++;}
   if(!ok)break;
  }else if(t->ctx&1){
   for(uint32_t j=0;j<l;j++){unsigned c=pos?out[pos-1]:0;if(cp[c]==ce[c]){ok=false;break;}out[pos++]=*cp[c]++;}
   if(!ok)break;
  }else{if(l)memcpy(out+pos,lit+lp,l);pos+=l;}
  lp+=l;
  if(!m){if(i!=t->ns-1||pos!=t->n||lp!=t->nl)ok=false;if(t->ctx&2){unsigned c=(ml[i]>>2)*2+(ll[i]!=0);if(c>=64||op[c]==oe[c])ok=false;else op[c]++;}break;}
  if(m<3||m>t->n-pos){ok=false;break;}
  unsigned code;
  if(t->ctx&2){unsigned c=(ml[i]>>2)*2+(ll[i]!=0);if(c>=64||op[c]==oe[c]){ok=false;break;}code=*op[c]++;}else code=oc[i];
  uint32_t d;
  if(code<NREP){d=rep[code];for(unsigned j=code;j>0;j--)rep[j]=rep[j-1];}
  else {unsigned k=code-NREP;if(k>29){ok=false;break;}d=(1u<<k)+bits.get(k);for(unsigned j=NREP-1;j>0;j--)rep[j]=rep[j-1];}
  rep[0]=d;
  if(!bits.ok||!d||d>pos){ok=false;break;}
  uint32_t original_m=m;uint8_t*dst=out+pos;const uint8_t*src=dst-d;
  if(d>=m)memcpy(dst,src,m);
  else if(d==1)memset(dst,*src,m);
  else {while(m>=d){memcpy(dst,src,d);dst+=d;m-=d;}if(m)memcpy(dst,src,m);}
  pos+=original_m;
 }
 if(pos!=t->n||lp!=t->nl||bits.p!=bits.end)ok=false;
 if(t->ctx&2)for(unsigned c=0;c<64;c++)if(op[c]!=oe[c])ok=false;
 if((t->ctx&4)&&!litdecoder.finished())ok=false;
 free(tmp);return ok?(int64_t)t->n:-1;
}
#ifndef DECODE_ONLY
struct BitW {
 std::vector<uint8_t> v;uint64_t buf=0;unsigned have=0;
 void put(uint32_t x,unsigned k){buf|=uint64_t(x)<<have;have+=k;while(have>=8){v.push_back(uint8_t(buf));buf>>=8;have-=8;}}
 void finish(){if(have){v.push_back(uint8_t(buf));buf=0;have=0;}}
};
static uint8_t putlen(uint32_t n,BitW&b){
 if(n<16)return n;unsigned k=31-__builtin_clz(n),e=k-2;
 unsigned code=16+(k-4)*4+((n>>e)&3);b.put(n&((1u<<e)-1),e);return code;
}
extern "C" int64_t lab_encode(const uint8_t*raw,size_t n,uint8_t*out,size_t cap){
 try{
 if(n>1000000000u)return -1;
 #ifdef FITTED_PARSE
 auto seq=fitted_parse(raw,n);
#else
 auto seq=parse_lz(raw,n);
#endif
 if(seq.empty())return -1;
 std::vector<uint8_t> ll,ml,oc,lit;BitW bits;
 std::vector<uint8_t> groups[256];std::vector<uint16_t> lctx;
 ll.reserve(seq.size());ml.reserve(seq.size());oc.reserve(seq.size());lit.reserve(n/8);
 uint32_t rep[NREP]={};size_t pos=0;
 for(auto s:seq){
  ll.push_back(putlen(s.literals,bits));ml.push_back(putlen(s.length,bits));
  if(LIT_CONTEXT)for(unsigned j=0;j<s.literals;j++){unsigned c=pos+j?raw[pos+j-1]:0;
   if(LIT_CONTEXT==1)groups[c].push_back(raw[pos+j]);
   else {unsigned prev=pos+j>1?raw[pos+j-2]:0;lctx.push_back((prev<<8)|c);}
  }
  lit.insert(lit.end(),raw+pos,raw+pos+s.literals);pos+=s.literals;
  if(s.length){
   unsigned r=0;while(r<NREP&&rep[r]!=s.distance)r++;
   if(r<NREP){oc.push_back(r);for(unsigned j=r;j>0;j--)rep[j]=rep[j-1];}
   else {unsigned k=31-__builtin_clz(s.distance);oc.push_back(NREP+k);bits.put(s.distance-(1u<<k),k);for(unsigned j=NREP-1;j>0;j--)rep[j]=rep[j-1];}
   rep[0]=s.distance;pos+=s.length;
  }else oc.push_back(0);
 }
 if(pos!=n)return -1;bits.finish();
 std::vector<uint8_t> streams[5]={ent::encode(ll),ent::encode(ml),ent::encode(oc),ent::encode(lit),std::move(bits.v)};
 if(LIT_CONTEXT==2)streams[3]=lit2::encode(lit,lctx);
 else if(LIT_CONTEXT==1){std::vector<uint8_t> cl(2048);
  for(unsigned c=0;c<256;c++){auto e=ent::encode(groups[c]);wr32(cl.data()+8*c,groups[c].size());wr32(cl.data()+8*c+4,e.size());cl.insert(cl.end(),e.begin(),e.end());}streams[3]=std::move(cl);
 }
 if(OF_CONTEXT){std::vector<uint8_t> og[64];for(size_t i=0;i<oc.size();i++)og[(ml[i]>>2)*2+(ll[i]!=0)].push_back(oc[i]);std::vector<uint8_t> co(512);
  for(unsigned c=0;c<64;c++){auto e=ent::encode(og[c]);wr32(co.data()+8*c,og[c].size());wr32(co.data()+8*c+4,e.size());co.insert(co.end(),e.begin(),e.end());}streams[2]=std::move(co);
 }
 size_t sz=64;for(auto&s:streams)sz+=s.size();if(sz>cap)return -1;
 memset(out,0,64);memcpy(out,"PYLZ001",8);wr64(out+8,sz);wr32(out+24,n);wr32(out+28,seq.size());wr32(out+32,lit.size());wr32(out+56,(LIT_CONTEXT?1:0)|(OF_CONTEXT?2:0)|(LIT_CONTEXT==2?4:0));
 size_t off=64;for(unsigned i=0;i<5;i++){wr32(out+36+4*i,streams[i].size());memcpy(out+off,streams[i].data(),streams[i].size());off+=streams[i].size();}
 wr64(out+16,check(out+24,sz-24));
 return sz;
 }catch(...){return -1;}
}
#endif
