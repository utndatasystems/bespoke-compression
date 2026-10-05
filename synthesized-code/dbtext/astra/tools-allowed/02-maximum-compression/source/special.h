#pragma once
#include <algorithm>
#include <vector>
#include <cstdint>
#include <cstring>
#include <charconv>
#include <numeric>
#include <string>
#include <limits>
#include <memory>
#include <zstd.h>
#include "front.h"

// Archive: 48-byte LE header, followed by an optionally compressed typed payload.
// Types: customer decimal IDs, two-bit DNA, uint32 hex, UUID timestamp tables, binary64 coordinates.
// The mode word selects a shrinking-alphabet rank stream or coordinate byte planes.
static constexpr uint64_t SP_MAGIC=0x3150535442584442ULL;
static uint32_t sp_u32(const uint8_t*p){uint32_t x; memcpy(&x,p,4);return x;}
static uint64_t sp_u64(const uint8_t*p){uint64_t x; memcpy(&x,p,8);return x;}
static void sp_put32(std::vector<uint8_t>&v,uint32_t x){auto n=v.size();v.resize(n+4);memcpy(v.data()+n,&x,4);}
static void sp_put64(std::vector<uint8_t>&v,uint64_t x){auto n=v.size();v.resize(n+8);memcpy(v.data()+n,&x,8);}
static void sp_var(std::vector<uint8_t>&v,uint32_t x){while(x>=128){v.push_back((x&127)|128);x>>=7;}v.push_back(x);}
static bool sp_getvar(const uint8_t*&p,const uint8_t*e,uint32_t&x){x=0;for(int s=0;s<=28;s+=7){if(p==e)return false;uint8_t c=*p++;if(s==28&&c>15)return false;x|=uint32_t(c&127)<<s;if(!(c&128))return true;}return false;}
static unsigned sp_width(uint64_t x){return x?64-__builtin_clzll(x):0;}
static void sp_pack(std::vector<uint8_t>&v,const std::vector<uint64_t>&x,unsigned bits){size_t st=v.size(),nb=(x.size()*bits+7)/8;v.resize(st+nb+8,0);size_t off=0;for(uint64_t val:x){size_t j=off>>3;unsigned s=off&7;uint64_t q=sp_u64(v.data()+st+j);q|=val<<s;memcpy(v.data()+st+j,&q,8);off+=bits;}v.resize(st+nb);}
static uint64_t sp_bit(const uint8_t*p,size_t i,unsigned bits){size_t off=i*bits;uint64_t q=sp_u64(p+(off>>3))>>(off&7);return q&((uint64_t(1)<<bits)-1);}
static int sp_hex(uint8_t c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
static bool sp_readhex(const uint8_t*p,size_t n,uint64_t&v){v=0;for(size_t i=0;i<n;i++){int c=sp_hex(p[i]);if(c<0)return false;v=(v<<4)|c;}return true;}
static bool sp_is_magic(const uint8_t*p,size_t n){return n>=8&&sp_u64(p)==SP_MAGIC;}

// Only transformed integers, permutations and compressed syntax live in decoder state.
// Neither setup nor repeated row calls materialize/cache the original string column.
struct SpState {
 uint32_t type=0,n=0;uint64_t raw=0;bool arith=false;std::vector<uint32_t> rank;std::vector<uint64_t> nums;std::vector<uint8_t> data;
 const uint8_t* a=nullptr;const uint8_t*b=nullptr;unsigned w=0,w2=0;uint32_t base=0,nil=0;
 uint64_t base64=0,base642=0;std::vector<uint32_t> times;std::vector<uint16_t> seq;
};

static SpState* sp_open(const uint8_t*src,size_t size){
 if(size<48||sp_u64(src)!=SP_MAGIC)return nullptr;
 uint32_t ty=sp_u32(src+8),n=sp_u32(src+12),coding=sp_u32(src+40),mode=sp_u32(src+44);
 uint64_t raw=sp_u64(src+16),usize=sp_u64(src+24),csize=sp_u64(src+32);
 if(ty<1||ty>5||!n||n>1000000||raw>16000000||usize>32000000||csize!=size-48||(coding!=0&&coding!=1&&coding!=2&&coding!=4&&coding!=5&&coding!=6)||(mode>1&&(mode!=2||ty!=5)))return nullptr;
 std::unique_ptr<SpState> s(new SpState);s->type=ty;s->n=n;s->raw=raw;s->arith=(mode==1);s->data.resize(usize+16,0);
 if(coding){if(!FC::decompress(coding-1,src+48,csize,s->data.data(),usize)){return nullptr;}}
 else {if(csize!=usize){return nullptr;}memcpy(s->data.data(),src+48,usize);}
 const uint8_t*p=s->data.data();bool ok=false;
 if(ty==1){if(usize>=15){s->w=p[10];s->base=sp_u32(p+11);s->a=p+15;ok=s->w>0&&s->w<=30&&raw==uint64_t(n)*19;if(mode){if(usize<19)ok=false;else {uint32_t u=sp_u32(p+15);s->rank.resize(n);ok=ok&&u>=n&&u<=1000000&&FC::permutation_decode(p+19,usize-19,s->rank,u);}}else ok=ok&&usize==15+(uint64_t(n)*s->w+7)/8;}}
 if(ty==2){if(usize>=6){s->w=p[4]*2;s->a=p+6;ok=p[4]==9&&raw==uint64_t(n)*10;if(mode){s->rank.resize(n);ok=ok&&n<=(1u<<18)&&FC::permutation_decode(p+6,usize-6,s->rank,1u<<18);}else ok=ok&&usize==6+(uint64_t(n)*s->w+7)/8;}}
 if(ty==3){s->a=p;ok=usize==uint64_t(n)*4&&raw>=uint64_t(n)*2&&raw<=uint64_t(n)*9;}
 if(ty==4&&usize>=57){
  s->w=p[36];s->base=sp_u32(p+37);uint32_t step=sp_u32(p+41),nt=sp_u32(p+45),ns=sp_u32(p+49),np=sp_u32(p+53);
  uint64_t nn=(uint64_t(n)*46+7)/8;
  if(s->w==sp_width(n-1)&&step&&(mode||np==(uint64_t(n)*s->w+7)/8)&&uint64_t(57)+nt+ns+np+nn==usize&&raw==uint64_t(n)*37){
   const uint8_t*q=p+57,*qe=q+nt;uint64_t t=s->base;ok=true;s->times.resize(n);s->seq.resize(n);
   for(uint32_t i=0;i<n;i++){uint32_t d;if(!sp_getvar(q,qe,d)){ok=false;break;}t+=uint64_t(d)*step;if(t>UINT32_MAX){ok=false;break;}s->times[i]=t;}if(q!=qe)ok=false;
   q=qe;qe+=ns;uint16_t seq=0;
   for(uint32_t i=0;ok&&i<n;i++){uint32_t d;if(!sp_getvar(q,qe,d)||d>65535){ok=false;break;}seq=uint16_t(seq+d);s->seq[i]=seq;}if(q!=qe)ok=false;
   s->a=qe;s->b=qe+np;
   if(ok&&mode){s->rank.resize(n);ok=FC::permutation_decode(s->a,np,s->rank);}else if(ok)for(uint32_t i=0;i<n;i++)if(sp_bit(s->a,i,s->w)>=n){ok=false;break;}
  }
 }
 if(ty==5&&usize>=22){s->w=p[0];s->w2=p[1];s->nil=sp_u32(p+2);s->base64=sp_u64(p+6);s->base642=sp_u64(p+14);uint64_t an=mode==2?uint64_t(n)*((s->w+7)/8):(uint64_t(n)*s->w+7)/8,bn=mode==2?uint64_t(n)*((s->w2+7)/8):(uint64_t(n)*s->w2+7)/8;s->a=p+22;s->b=s->a+an;ok=s->w>0&&s->w<=52&&s->w2>0&&s->w2<=52&&usize==22+an+bn&&(s->nil==UINT32_MAX||s->nil<n)&&raw<=uint64_t(n)*100;if(ok&&mode==2){s->nums.resize(size_t(n)*2);for(uint32_t i=0;i<n;i++){uint64_t a=0,b=0;for(unsigned j=0;j<(s->w+7)/8;j++)a|=uint64_t(s->a[j*n+i])<<(j*8);for(unsigned j=0;j<(s->w2+7)/8;j++)b|=uint64_t(s->b[j*n+i])<<(j*8);s->nums[i]=a;s->nums[n+i]=b;}}}
 if(!ok){return nullptr;}return s.release();
}

static int sp_one(const SpState*s,uint32_t id,uint8_t*out){
 if(s->type==1){uint32_t val=s->base+(s->arith?s->rank[id]:sp_bit(s->a,id,s->w));if(val>999999999)return -1;memcpy(out,s->data.data(),9);for(int i=17;i>=9;i--){out[i]='0'+val%10;val/=10;}out[18]=s->data[9];return 19;}
 if(s->type==2){uint64_t val=s->arith?s->rank[id]:sp_bit(s->a,id,s->w);for(int i=0;i<9;i++){out[i]=s->data[val&3];val>>=2;}out[9]=s->data[5];return 10;}
 if(s->type==3){static const char h[]="0123456789ABCDEF";uint32_t v=sp_u32(s->a+id*4);int len=v?(32-__builtin_clz(v)+3)/4:1;for(int i=len-1;i>=0;i--){out[i]=h[v&15];v>>=4;}out[len]='\n';return len+1;}
 if(s->type==4){static const char h[]="0123456789abcdef";uint32_t k=s->arith?s->rank[id]:sp_bit(s->a,id,s->w);if(k>=s->n)return -1;memcpy(out,s->data.data(),36);uint32_t t=s->times[k];for(int j=7;j>=0;j--){out[j]=h[t&15];t>>=4;}uint16_t seq=s->seq[k];for(int j=22;j>=19;j--){out[j]=h[seq&15];seq>>=4;}uint64_t nd=sp_bit(s->b,id,46);nd=(nd&((uint64_t(1)<<40)-1))|((nd>>40)<<42)|(uint64_t(3)<<40);for(int j=35;j>=24;j--){out[j]=h[nd&15];nd>>=4;}out[36]='\n';return 37;}
 if(s->type==5){if(id==s->nil){memcpy(out,"NULL\n",5);return 5;}uint64_t la=s->base64+(s->nums.empty()?sp_bit(s->a,id,s->w):s->nums[id]),lo=s->base642+(s->nums.empty()?sp_bit(s->b,id,s->w2):s->nums[s->n+id]);double a,b;memcpy(&a,&la,8);memcpy(&b,&lo,8);out[0]='(';auto x=std::to_chars((char*)out+1,(char*)out+48,a);if(x.ec!=std::errc())return -1;*x.ptr++=',';*x.ptr++=' ';auto y=std::to_chars(x.ptr,(char*)out+96,b);if(y.ec!=std::errc())return -1;*y.ptr++=')';*y.ptr++='\n';return y.ptr-(char*)out;}
 return -1;
}
static int64_t sp_decode(SpState*s,uint8_t*out,size_t cap){if(!s||!out||cap<s->raw)return -1;size_t pos=0;uint8_t tmp[128];for(uint32_t i=0;i<s->n;i++){int n=sp_one(s,i,tmp);if(n<0||size_t(n)>cap-pos)return -1;memcpy(out+pos,tmp,n);pos+=n;}return pos==s->raw?int64_t(pos):-1;}
static int64_t sp_rows(SpState*s,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offs){if(!s||!offs||(count&&(!ids||!out)))return -1;size_t pos=0;offs[0]=0;uint8_t tmp[128];for(size_t j=0;j<count;j++){if(ids[j]>=s->n||(j&&ids[j]<ids[j-1]))return -1;int n=sp_one(s,ids[j],tmp);if(n<0||size_t(n)>cap-pos)return -1;memcpy(out+pos,tmp,n);pos+=n;offs[j+1]=pos;}return pos;}
static void sp_close(SpState*s){delete s;}

#ifdef ENCODER
static bool sp_encode(const uint8_t*raw,size_t size,std::vector<uint8_t>&archive){
 if(size==0)return false;uint32_t ty=0,n=0,mode=0;std::vector<uint8_t> p;std::vector<uint64_t>a,b;
 if(size%19==0&&size>=19&&memcmp(raw,"Customer#",9)==0){n=size/19;a.reserve(n);uint32_t lo=UINT32_MAX,hi=0;bool ok=true;for(uint32_t i=0;i<n;i++){const uint8_t*q=raw+i*19;if(memcmp(q,raw,9)||q[18]!='\n'){ok=false;break;}uint32_t v=0;for(int j=9;j<18;j++){if(q[j]<'0'||q[j]>'9'){ok=false;break;}v=v*10+q[j]-'0';}if(!ok)break;a.push_back(v);lo=std::min(lo,v);hi=std::max(hi,v);}if(ok){ty=1;p.insert(p.end(),raw,raw+9);p.push_back('\n');unsigned w=std::max(1u,sp_width(hi-lo));p.push_back(w);sp_put32(p,lo);for(auto&v:a)v-=lo;std::vector<uint32_t>r(a.begin(),a.end()),c=r;std::sort(c.begin(),c.end());bool unique=std::adjacent_find(c.begin(),c.end())==c.end();if(unique){auto z=FC::permutation_encode(r,hi-lo+1);if(z.size()+4<(uint64_t(n)*w+7)/8){mode=1;sp_put32(p,hi-lo+1);p.insert(p.end(),z.begin(),z.end());}}if(!mode)sp_pack(p,a,w);}}
 if(!ty&&size%10==0&&size>=10){n=size/10;a.clear();a.reserve(n);bool ok=true;const char*alphabet="acgt";for(uint32_t i=0;i<n;i++){const uint8_t*q=raw+i*10;if(q[9]!='\n'){ok=false;break;}uint64_t v=0;for(int j=0;j<9;j++){const char*t=strchr(alphabet,q[j]);if(!t||!q[j]){ok=false;break;}v|=uint64_t(t-alphabet)<<(j*2);}if(!ok)break;a.push_back(v);}if(ok){ty=2;p.assign(alphabet,alphabet+4);p.push_back(9);p.push_back('\n');std::vector<uint32_t>r(a.begin(),a.end()),c=r;std::sort(c.begin(),c.end());bool unique=std::adjacent_find(c.begin(),c.end())==c.end();if(unique){auto z=FC::permutation_encode(r,1u<<18);if(z.size()<(uint64_t(n)*18+7)/8){mode=1;p.insert(p.end(),z.begin(),z.end());}}if(!mode)sp_pack(p,a,18);}}
 if(!ty&&size%37==0&&size>=37&&raw[8]=='-'&&raw[13]=='-'&&raw[18]=='-'&&raw[23]=='-'){
  n=size/37;std::vector<uint32_t>ts(n),sq(n),ix(n);a.assign(n,0);bool ok=true;uint32_t base=UINT32_MAX,step=0;
  for(uint32_t i=0;i<n;i++){const uint8_t*q=raw+i*37;uint64_t t,c,nd;if(memcmp(q+8,raw+8,11)||q[23]!='-'||q[36]!='\n'||!sp_readhex(q,8,t)||!sp_readhex(q+19,4,c)||!sp_readhex(q+24,12,nd)||((nd>>40)&3)!=3){ok=false;break;}for(int j=0;j<36;j++)if(q[j]>='A'&&q[j]<='F')ok=false;ts[i]=t;sq[i]=c;ix[i]=i;a[i]=(nd&((uint64_t(1)<<40)-1))|((nd>>42)<<40);base=std::min(base,uint32_t(t));}
  if(ok){for(auto t:ts)step=std::gcd(step,t-base);if(!step)step=1;std::sort(ix.begin(),ix.end(),[&](uint32_t x,uint32_t y){return ts[x]<ts[y];});std::vector<uint8_t>dt,ds;std::vector<uint64_t>perm(n);uint32_t pt=base;uint16_t ps=0;for(uint32_t i=0;i<n;i++){uint32_t k=ix[i];sp_var(dt,(ts[k]-pt)/step);pt=ts[k];sp_var(ds,uint16_t(sq[k]-ps));ps=sq[k];perm[k]=i;}ty=4;unsigned w=sp_width(n-1);p.assign(raw,raw+36);p.push_back(w);sp_put32(p,base);sp_put32(p,step);sp_put32(p,dt.size());sp_put32(p,ds.size());std::vector<uint32_t>r(perm.begin(),perm.end());auto pr=FC::permutation_encode(r);mode=1;sp_put32(p,pr.size());p.insert(p.end(),dt.begin(),dt.end());p.insert(p.end(),ds.begin(),ds.end());p.insert(p.end(),pr.begin(),pr.end());sp_pack(p,a,46);}
 }
 if(!ty){a.clear();const uint8_t*q=raw,*end=raw+size;bool ok=true;while(q<end){uint32_t v=0;unsigned k=0;const uint8_t*st=q;while(q<end&&*q!='\n'){int c=sp_hex(*q);if(c<0||(*q>='a'&&*q<='f')||k>=8){ok=false;break;}v=v*16+c;q++;k++;}if(!ok||!k||q==end||(k>1&&*st=='0')){ok=false;break;}q++;a.push_back(v);}if(ok&&a.size()){ty=3;n=a.size();p.clear();for(auto v:a)sp_put32(p,v);}}
 if(!ty&&(raw[0]=='('||(size>=5&&memcmp(raw,"NULL\n",5)==0))){a.clear();b.clear();const uint8_t*q=raw,*end=raw+size;bool ok=true;uint32_t nil=UINT32_MAX;uint64_t ca=0,cb=0,ma=0,mb=0;bool first=true;
  while(q<end){if(size_t(end-q)>=5&&memcmp(q,"NULL\n",5)==0){if(nil!=UINT32_MAX){ok=false;break;}nil=a.size();a.push_back(0);b.push_back(0);q+=5;continue;}if(*q!='('){ok=false;break;}const uint8_t*st=q++;double x,y;auto xres=std::from_chars((const char*)q,(const char*)end,x);if(xres.ec!=std::errc()||end-(const uint8_t*)xres.ptr<2||memcmp(xres.ptr,", ",2)){ok=false;break;}auto yres=std::from_chars(xres.ptr+2,(const char*)end,y);if(yres.ec!=std::errc()||end-(const uint8_t*)yres.ptr<2||memcmp(yres.ptr,")\n",2)){ok=false;break;}q=(const uint8_t*)yres.ptr+2;uint8_t tmp[128];tmp[0]='(';auto tx=std::to_chars((char*)tmp+1,(char*)tmp+48,x);if(tx.ec!=std::errc()){ok=false;break;}*tx.ptr++=',';*tx.ptr++=' ';auto t=std::to_chars(tx.ptr,(char*)tmp+96,y);if(t.ec!=std::errc()){ok=false;break;}*t.ptr++=')';*t.ptr++='\n';if(size_t(t.ptr-(char*)tmp)!=size_t(q-st)||memcmp(st,tmp,q-st)){ok=false;break;}uint64_t u,v;memcpy(&u,&x,8);memcpy(&v,&y,8);if(first){ca=u;cb=v;first=false;}ma|=u^ca;mb|=v^cb;a.push_back(u);b.push_back(v);}
  if(ok&&!first){ca=UINT64_MAX;cb=UINT64_MAX;ma=mb=0;for(size_t i=0;i<a.size();i++)if(i!=nil){ca=std::min(ca,a[i]);cb=std::min(cb,b[i]);ma=std::max(ma,a[i]);mb=std::max(mb,b[i]);}unsigned wa=std::max(1u,sp_width(ma-ca)),wb=std::max(1u,sp_width(mb-cb));if(wa<=52&&wb<=52){ty=5;n=a.size();for(size_t i=0;i<a.size();i++){a[i]=i==nil?0:a[i]-ca;b[i]=i==nil?0:b[i]-cb;}p.clear();p.push_back(wa);p.push_back(wb);sp_put32(p,nil);sp_put64(p,ca);sp_put64(p,cb);mode=2;for(unsigned j=0;j<(wa+7)/8;j++)for(auto v:a)p.push_back(v>>(j*8));for(unsigned j=0;j<(wb+7)/8;j++)for(auto v:b)p.push_back(v>>(j*8));}}
 }
 if(!ty)return false;
 unsigned codec=0;std::vector<uint8_t>z=FC::compress(p,codec);size_t zn=z.size();if(!zn)return false;bool use=zn<p.size();archive.clear();sp_put64(archive,SP_MAGIC);sp_put32(archive,ty);sp_put32(archive,n);sp_put64(archive,size);sp_put64(archive,p.size());sp_put64(archive,use?zn:p.size());sp_put32(archive,use?codec+1:0);sp_put32(archive,mode);if(use)archive.insert(archive.end(),z.begin(),z.begin()+zn);else archive.insert(archive.end(),p.begin(),p.end());return true;
}
#endif
