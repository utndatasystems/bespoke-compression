#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <algorithm>
using namespace std;
static void put(vector<uint8_t>&v,uint32_t x){for(int k=0;k<4;k++)v.push_back(x>>(k*8));}
static uint32_t get(const uint8_t*&p){uint32_t x;memcpy(&x,p,4);p+=4;return x;}
static int ctx(int v){return v<4?v:v<8?4:v<16?5:v<32?6:7;}
static constexpr int LANES=16;
struct Enc{uint64_t low=0;uint32_t range=~0u;int cache=0;uint64_t nc=1;vector<uint8_t>o;void shift(){uint32_t lo=low;if(lo<0xff000000u||(low>>32)){int c=cache;do{o.push_back(c+(low>>32));c=255;}while(--nc);cache=lo>>24;}nc++;low=uint64_t(lo<<8);}void bit(uint16_t&p,int b,int rate){uint32_t bound=(range>>12)*p;if(!b){range=bound;p+=(4096-p)>>rate;}else{low+=bound;range-=bound;p-=p>>rate;}while(range<(1<<24)){range<<=8;shift();}}void end(){for(int i=0;i<5;i++)shift();}};
struct Dec{uint32_t code=0,range=~0u;const uint8_t*p,*end;bool bad=false;uint8_t read(){if(p==end){bad=true;return 0;}return *p++;}Dec(const uint8_t*q,const uint8_t*e):p(q),end(e){if(read()!=0)bad=true;for(int i=0;i<4;i++)code=(code<<8)|read();}int bit(uint16_t&pr,int rate){uint32_t bound=(range>>12)*pr;int b;if(code<bound){b=0;range=bound;pr+=(4096-pr)>>rate;}else{b=1;code-=bound;range-=bound;pr-=pr>>rate;}while(range<(1<<24)){range<<=8;code=(code<<8)|read();}return b;}};

static void update(uint16_t&p,int b,int r){if(!b)p+=(4096-p)>>r;else p-=p>>r;}
#ifdef ENCODER
static vector<uint8_t> bwt(const uint8_t*s,int n,uint32_t*primary){
 vector<int> p(n),c(n),pn(n),cn(n),cnt(max(n,256),0);
 for(int i=0;i<n;i++)cnt[s[i]]++;
 for(int i=1;i<256;i++)cnt[i]+=cnt[i-1];
 for(int i=0;i<n;i++)p[--cnt[s[i]]]=i;
 c[p[0]]=0;int classes=1;
 for(int i=1;i<n;i++){classes+=s[p[i]]!=s[p[i-1]];c[p[i]]=classes-1;}
 for(int h=1;h<n;h<<=1){
  for(int i=0;i<n;i++){pn[i]=p[i]-h;if(pn[i]<0)pn[i]+=n;}
  fill(cnt.begin(),cnt.begin()+classes,0);
  for(int i=0;i<n;i++)cnt[c[pn[i]]]++;
  for(int i=1;i<classes;i++)cnt[i]+=cnt[i-1];
  for(int i=n-1;i>=0;i--)p[--cnt[c[pn[i]]]]=pn[i];
  cn[p[0]]=0;int cl=1;
  for(int i=1;i<n;i++){int a=p[i]+h,b=p[i-1]+h;if(a>=n)a-=n;if(b>=n)b-=n;cl+=c[p[i]]!=c[p[i-1]]||c[a]!=c[b];cn[p[i]]=cl-1;}
  c.swap(cn);classes=cl;if(classes==n)break;
 }
 vector<uint8_t>v(n);for(int i=0;i<n;i++){v[i]=s[p[i]?p[i]-1:n-1];int q=n/LANES;if(p[i]%q==0 && p[i]/q<LANES)primary[p[i]/q]=i;}return v;
}
extern "C" __attribute__((visibility("default"))) int64_t lab_encode(const uint8_t*raw,size_t size,uint8_t*out,size_t capacity) try {
 if((size && !raw) || !out || size>UINT32_MAX)return -1;
 const int BLOCK=32<<20;vector<uint16_t> syms;vector<uint8_t>fronts;vector<uint32_t>lens,prim,scount;
 for(size_t off=0;off<size;off+=BLOCK){int n=min(size-off,size_t(BLOCK));if(n<LANES)return -1;uint32_t primary[LANES]={};auto v=bwt(raw+off,n,primary);lens.push_back(n);for(int z=0;z<LANES;z++)prim.push_back(primary[z]);size_t sb=syms.size();
  uint8_t mtf[256];for(int i=0;i<256;i++)mtf[i]=i;int run=0,prev=0;
  auto flush=[&](){while(run){run--;syms.push_back(run&1);fronts.push_back(mtf[0]);run>>=1;}};
  for(uint8_t ch:v){int rank=0;while(mtf[rank]!=ch)rank++;if(!rank){run++;prev=0;continue;}flush();syms.push_back(rank+1);fronts.push_back(mtf[0]);int dest=rank>1?rank/2:(prev==0?1:0);memmove(mtf+dest+1,mtf+dest,rank-dest);mtf[dest]=ch;prev=rank;}flush();scount.push_back(syms.size()-sb);
 }
 vector<uint16_t>model((512+256+8)*1024,2048);Enc en;int c=0;size_t si=0;
 for(auto sym:syms){uint16_t*pr=model.data()+c*1024,*pf=model.data()+(512+fronts[si++])*1024,*pl=model.data()+(512+256+(c&7))*1024;
  auto bit=[&](int node,int b){uint16_t z=(pr[node]*3+pf[node]*3+pl[node]*2)>>3;en.bit(z,b,5);update(pr[node],b,5);update(pf[node],b,4);update(pl[node],b,4);};
  if(sym<2){bit(0,0);bit(1,sym);}else{bit(0,1);unsigned x=sym-1;int k=31-__builtin_clz(x);for(int j=0;j<k;j++)bit(2+j,0);bit(2+k,1);for(int j=k-1,nd=1;j>=0;j--){int b=(x>>j)&1;bit(16+(1<<k)+nd,b);nd=nd*2+b;}}
  c=((c<<3)+ctx(sym))&511;
 }en.end();
 vector<uint8_t>a;put(a,0x42575437);put(a,size);put(a,lens.size());put(a,syms.size());
 for(int j=0;j<(int)lens.size();j++){put(a,lens[j]);put(a,scount[j]);for(int z=0;z<LANES;z++)put(a,prim[j*LANES+z]);}a.insert(a.end(),en.o.begin(),en.o.end());if(a.size()>capacity)return -1;memcpy(out,a.data(),a.size());return a.size();
} catch(...) {return -1;}
#else
struct FreeMem {void* p;~FreeMem(){free(p);}};
struct State{const uint8_t*p,*end;uint32_t size,blocks,nsyms;};
extern "C" __attribute__((visibility("default"))) void* lab_open(const uint8_t*a,size_t z) {
 if(!a || z<16)return nullptr;
 const uint8_t*p=a;if(get(p)!=0x42575437)return nullptr;
 uint32_t size=get(p),blocks=get(p),nsyms=get(p);
 const uint64_t fixed=uint64_t(blocks)*(8+4*LANES)+5;
 if(fixed>z-16 || nsyms>size || blocks>size/LANES || (!blocks && (size || nsyms)))return nullptr;
 State*s=(State*)malloc(sizeof(State));if(!s)return nullptr;*s=State{p,a+z,size,blocks,nsyms};return s;
}
extern "C" __attribute__((visibility("default"))) int64_t lab_decode(void*ss,uint8_t*out,size_t cap) {
 if(!ss)return -1;const State&s=*(State*)ss;if(cap<s.size || (s.size && !out))return -1;
 const uint8_t*p=s.p;
 struct Block{uint32_t n,syms,primary[LANES];};Block*bl=(Block*)malloc(size_t(s.blocks)*sizeof(Block));if(!bl && s.blocks)return -1;FreeMem freebl{bl};
 uint32_t biggest=0;uint64_t total=0,ns=0;
 for(uint32_t bi=0;bi<s.blocks;bi++){auto&b=bl[bi];
  b.n=get(p);b.syms=get(p);
  if(b.n<LANES || b.n>(1u<<25) || !b.syms || b.syms>b.n)return -1;
  total+=b.n;ns+=b.syms;if(total>s.size || ns>s.nsyms)return -1;
  for(int z=0;z<LANES;z++){b.primary[z]=get(p);if(b.primary[z]>=b.n)return -1;}
  biggest=max(biggest,b.n);
 }
 if(total!=s.size || ns!=s.nsyms)return -1;
 Dec de(p,s.end);uint16_t*model=(uint16_t*)malloc(size_t((512+256+8)*1024)*2);if(!model)return -1;FreeMem freemodel{model};fill(model,model+(512+256+8)*1024,2048);int c=0;uint8_t*bwtv=(uint8_t*)malloc(biggest);FreeMem freebwt{bwtv};uint32_t*tt=(uint32_t*)malloc(size_t(biggest)*4);FreeMem freett{tt};if(biggest && (!bwtv || !tt))return -1;size_t off=0;
 for(uint32_t bi=0;bi<s.blocks;bi++){const auto&b=bl[bi];
  uint8_t mtf[256];for(int i=0;i<256;i++)mtf[i]=i;
  int prev=0;uint32_t pos=0;uint64_t run=0,step=1;uint32_t count[256]={};
  auto flush=[&](){if(run){memset(bwtv+pos,mtf[0],size_t(run));pos+=run;count[mtf[0]]+=run;run=0;step=1;}};
  for(uint32_t i=0;i<b.syms;i++){
   uint16_t*pr=model+c*1024,*pf=model+(512+mtf[0])*1024,*pl=model+(512+256+(c&7))*1024;
   auto bit=[&](int node){uint16_t z=(pr[node]*3+pf[node]*3+pl[node]*2)>>3;int v=de.bit(z,5);update(pr[node],v,5);update(pf[node],v,4);update(pl[node],v,4);return v;};
   int sym;if(!bit(0)){sym=bit(1);}else{int k=0;while(!bit(2+k)){if(++k>7)return -1;}unsigned x=1;for(int j=k-1;j>=0;j--)x=x*2+bit(16+(1<<k)+x);sym=x+1;}
   if(sym>256||de.bad)return -1;c=((c<<3)+ctx(sym))&511;
   if(sym<2){prev=0;run+=(sym+1)*step;if(run>b.n-pos)return -1;step<<=1;continue;}
   flush();if(pos>=b.n)return -1;
   const int rank=sym-1;const uint8_t ch=mtf[rank];int dest=rank>1?rank/2:(prev==0?1:0);memmove(mtf+dest+1,mtf+dest,rank-dest);mtf[dest]=ch;prev=rank;bwtv[pos++]=ch;count[ch]++;
  }
  flush();if(pos!=b.n)return -1;
  uint8_t code[256],rev[128];int alphabet=0;uint32_t sum=0;
  for(int i=0;i<256;i++){
   const uint32_t t=count[i];
   if(t){if(alphabet>=128)return -1;code[i]=alphabet;rev[alphabet++]=i;}
   count[i]=sum;sum+=t;
  }
  for(uint32_t i=0;i<b.n;i++){const uint8_t ch=bwtv[i];tt[count[ch]++]=(i<<7)|code[ch];}
  uint32_t t[LANES],q=b.n/LANES;for(int j=0;j<LANES;j++)t[j]=tt[b.primary[j]];
  for(uint32_t i=0;i<q;i++){
   #pragma GCC unroll 1
   for(int j=0;j<LANES;j++){out[off+j*q+i]=rev[t[j]&127];t[j]=tt[t[j]>>7];}
  }
  for(uint32_t i=q*LANES;i<b.n;i++){out[off+i]=rev[t[LANES-1]&127];t[LANES-1]=tt[t[LANES-1]>>7];}
  off+=b.n;
 }
 if(de.bad || de.p!=s.end)return -1;
 return off;
}
extern "C" __attribute__((visibility("default"))) void lab_close(void*s){free(s);}
#endif
