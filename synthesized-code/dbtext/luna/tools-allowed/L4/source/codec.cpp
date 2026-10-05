#include "interface/codec.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <new>
#include <queue>
#include <functional>
#include <utility>
#include <vector>
#include <emmintrin.h>

namespace {
constexpr uint32_t H=40, BR=64, NM=2048, ML=255, TS=2560;
enum { CNAME=1, DNA=2, HEX=3, UUID=4, LOC=5, BPE=6, LZ=7, RAW=8 };
const uint8_t MAGIC[8]={'D','B','T','X','R','O','W','1'};
uint32_t r32(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
uint16_t r16(const uint8_t*p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
void w32(uint8_t*p,uint32_t x){p[0]=uint8_t(x);p[1]=uint8_t(x>>8);p[2]=uint8_t(x>>16);p[3]=uint8_t(x>>24);}
void a16(std::vector<uint8_t>&v,uint16_t x){v.push_back(uint8_t(x));v.push_back(uint8_t(x>>8));}
void a32(std::vector<uint8_t>&v,uint32_t x){v.push_back(uint8_t(x));v.push_back(uint8_t(x>>8));v.push_back(uint8_t(x>>16));v.push_back(uint8_t(x>>24));}
void header(std::vector<uint8_t>&a,uint32_t mode,uint32_t raw,uint32_t rows,uint32_t x0,uint32_t x1,uint32_t x2,uint32_t x3,uint32_t x4){
 a.assign(H,0);std::memcpy(a.data(),MAGIC,8);w32(a.data()+8,mode);w32(a.data()+12,raw);w32(a.data()+16,rows);w32(a.data()+20,x0);w32(a.data()+24,x1);w32(a.data()+28,x2);w32(a.data()+32,x3);w32(a.data()+36,x4);
}
bool split(const uint8_t*p,size_t n,std::vector<std::pair<uint32_t,uint32_t>>&v){
 if(n>UINT32_MAX)return false;v.clear();uint32_t s=0;
 for(uint32_t i=0;i<n;i++)if(p[i]=='\n'){v.emplace_back(s,i+1);s=i+1;}
 if(s<n)v.emplace_back(s,(uint32_t)n);return true;
}
bool hx(uint8_t c){return (c>='0'&&c<='9')||(c>='A'&&c<='F')||(c>='a'&&c<='f');}
uint8_t hv(uint8_t c){return c<='9'?c-'0':(c<='F'?c-'A'+10:c-'a'+10);}
bool det_cname(const uint8_t*p,size_t n,uint32_t&nr,std::vector<uint8_t>&b){
 if(!n||n%19)return false;nr=n/19;b.clear();b.reserve(nr*3);
 for(uint32_t r=0;r<nr;r++){auto q=p+size_t(r)*19;if(std::memcmp(q,"Customer#",9)||q[18]!='\n'||q[9]!='0'||q[10]!='0'||q[11]!='0')return false;
 uint32_t x=0;for(int j=12;j<18;j++){if(q[j]<'0'||q[j]>'9')return false;x=x*10+q[j]-'0';}
 b.push_back(x);b.push_back(x>>8);b.push_back(x>>16);}return true;
}
bool det_dna(const uint8_t*p,size_t n,uint32_t&nr,std::vector<uint8_t>&b){
 if(!n||n%10)return false;nr=n/10;b.clear();b.reserve(n/4);uint64_t acc=0;unsigned used=0;
 for(uint32_t r=0;r<nr;r++){uint32_t x=0;auto q=p+size_t(r)*10;for(unsigned j=0;j<9;j++){uint32_t c;
 switch(q[j]){case'a':c=0;break;case'c':c=1;break;case'g':c=2;break;case't':c=3;break;default:return false;}x|=c<<(2*j);}
 if(q[9]!='\n')return false;acc|=uint64_t(x)<<used;used+=18;while(used>=8){b.push_back(acc);acc>>=8;used-=8;}}
 if(used)b.push_back(acc);b.insert(b.end(),4,0);return true;
}
bool det_hex(const uint8_t*p,size_t n,uint32_t&nr,std::vector<uint8_t>&b){
 std::vector<std::pair<uint32_t,uint32_t>>v;if(!split(p,n,v)||v.empty())return false;nr=v.size();b.clear();b.reserve(n/2);
 for(auto [s,e]:v){if(e<=s||p[e-1]!='\n')return false;unsigned l=e-s-1;if(l<1||l>8||(l>1&&p[s]=='0'))return false;uint32_t x=0;
 for(unsigned i=s;i<e-1;i++){if(!hx(p[i])||(p[i]>='a'&&p[i]<='f'))return false;x=(x<<4)|hv(p[i]);}
 b.push_back(x);b.push_back(x>>8);b.push_back(x>>16);b.push_back(x>>24);}return true;
}
bool det_uuid(const uint8_t*p,size_t n,uint32_t&nr,std::vector<uint8_t>&b){
 if(!n||n%37)return false;nr=n/37;b.clear();b.reserve(nr*11);
 for(uint32_t r=0;r<nr;r++){auto q=p+size_t(r)*37;if(q[36]!='\n')return false;for(unsigned j=0;j<36;j++){
 bool sep=j==8||j==13||j==18||j==23;if(sep){if(q[j]!='-')return false;}else if(!hx(q[j])||(q[j]>='A'&&q[j]<='F'))return false;}
 for(unsigned i=0;i<32;i+=2){unsigned j=i;if(j>=8)j++;if(j>=13)j++;if(j>=18)j++;if(j>=23)j++;uint8_t v=(hv(q[j])<<4)|hv(q[j+1]);unsigned k=i/2;
  if(k==0){if(v!=0x84)return false;}else if(k==4){if(v!=0x2d)return false;}else if(k==5){if(v!=0xa5)return false;}else if(k==6){if(v!=0x11)return false;}else if(k==7){if(v!=0xe8)return false;}else b.push_back(v);}}
 return true;
}
bool loc_number(const uint8_t*p,size_t end,size_t&at,uint8_t&meta,uint8_t*out){
 unsigned neg=0;if(at<end&&p[at]=='-'){neg=1;at++;}
 unsigned ip=0,ii=0;
 while(at<end&&p[at]>='0'&&p[at]<='9'){
  if(ii>=3)return false;unsigned d=p[at++]-'0';if((ii&1)==0)out[ii>>1]=uint8_t(d<<4);else out[ii>>1]|=uint8_t(d);ii++;ip++;
 }
 if(!ii||at>=end||p[at++]!='.')return false;
 unsigned nd=ii;
 while(at<end&&p[at]>='0'&&p[at]<='9'){
  if(nd>=18)return false;unsigned d=p[at++]-'0';if((nd&1)==0)out[nd>>1]=uint8_t(d<<4);else out[nd>>1]|=uint8_t(d);nd++;
 }
 if(nd==ii)return false;meta=uint8_t(nd|(ii<<5)|(neg<<7));return true;
}
bool det_location(const uint8_t*p,size_t n,uint32_t&nr,std::vector<uint8_t>&b,uint32_t&lastnl){
 if(n<4||!(p[0]=='('||std::memcmp(p,"NULL",4)==0))return false;
 std::vector<std::pair<uint32_t,uint32_t>> rows;if(!split(p,n,rows)||rows.empty())return false;
 nr=(uint32_t)rows.size();lastnl=(p[n-1]=='\n');b.clear();b.reserve(size_t(nr)*20);
 for(uint32_t r=0;r<nr;r++){
  auto [lo,hi]=rows[r];size_t end=hi;if(end>lo&&p[end-1]=='\n')end--;uint8_t rec[20]={};
  if(end-lo==4&&std::memcmp(p+lo,"NULL",4)==0){b.insert(b.end(),rec,rec+20);continue;}
  size_t at=lo;if(at>=end||p[at++]!='(')return false;
  if(!loc_number(p,end,at,rec[0],rec+2))return false;
  if(at+2>end||p[at++]!=','||p[at++]!=' ')return false;
  if(!loc_number(p,end,at,rec[1],rec+11))return false;
  if(at>=end||p[at++]!=')'||at!=end)return false;
  b.insert(b.end(),rec,rec+20);
 }
 return true;
}
unsigned bits(uint32_t x){unsigned b=0;do{b++;x>>=1;}while(x);return b;}
struct Merge{uint16_t a,b;};struct Desc{uint32_t data,idx;uint16_t bits,n;};struct HPair{uint64_t word;uint16_t info;};
struct HNode{uint64_t w;int left,right,sym;};
struct HChunk{uint64_t w[2];uint8_t bits,n;};
bool make_huffman(const std::vector<uint16_t>&src,std::array<uint8_t,TS>&lens,std::array<uint64_t,TS>&codes){
 std::array<uint64_t,TS> freq{};for(uint16_t c:src){if(c>=TS)return false;freq[c]++;}
 std::vector<HNode> nodes;nodes.reserve(2*TS-1);using Q=std::pair<uint64_t,int>;
 std::priority_queue<Q,std::vector<Q>,std::greater<Q>> q;
 for(unsigned i=0;i<TS;i++)if(freq[i]){int id=nodes.size();nodes.push_back({freq[i],-1,-1,(int)i});q.push({freq[i],id});}
 if(q.empty())return false;if(q.size()==1){lens.fill(0);lens[nodes[q.top().second].sym]=1;codes.fill(0);return true;}
 while(q.size()>1){auto a=q.top();q.pop();auto b=q.top();q.pop();int id=nodes.size();nodes.push_back({a.first+b.first,a.second,b.second,-1});q.push({a.first+b.first,id});}
 lens.fill(0);codes.fill(0);bool ok=true;
 auto walk=[&](auto&&self,int id,unsigned depth)->void{const HNode&x=nodes[id];if(x.sym>=0){if(depth==0)depth=1;if(depth>56){ok=false;return;}lens[x.sym]=(uint8_t)depth;}else{self(self,x.left,depth+1);self(self,x.right,depth+1);}};
 walk(walk,q.top().second,0);if(!ok)return false;
 std::array<uint16_t,TS> order{};unsigned count=0;for(unsigned i=0;i<TS;i++)if(lens[i])order[count++]=i;
 std::sort(order.begin(),order.begin()+count,[&](uint16_t a,uint16_t b){return lens[a]!=lens[b]?lens[a]<lens[b]:a<b;});
 uint64_t code=0;unsigned prev=0;for(unsigned i=0;i<count;i++){unsigned c=order[i],n=lens[c];code<<=(n-prev);if(n<64&&code>=(uint64_t(1)<<n))return false;codes[c]=code++;prev=n;}
 return true;
}
struct BitWriter{
 std::vector<uint8_t> bytes;uint64_t acc=0,total=0;unsigned have=0;
 bool put(uint64_t code,unsigned n){if(!n||n>56)return false;acc=(acc<<n)|code;have+=n;total+=n;while(have>=8){have-=8;bytes.push_back(uint8_t(acc>>have));if(have)acc&=(uint64_t(1)<<have)-1;else acc=0;}return true;}
 void finish(){if(have){bytes.push_back(uint8_t(acc<<(8-have)));acc=0;have=0;}bytes.insert(bytes.end(),4,0);}
};

bool make_bpe(const uint8_t*p,size_t n,std::vector<uint8_t>&a){
 std::vector<std::pair<uint32_t,uint32_t>> rows;if(!split(p,n,rows)||rows.empty())return false;uint32_t nr=rows.size();
 std::array<uint64_t,256> f{};for(size_t i=0;i<n;i++)f[p[i]]++;
 std::array<uint16_t,256> ord{};for(unsigned i=0;i<256;i++)ord[i]=i;
 std::sort(ord.begin(),ord.end(),[&](uint16_t x,uint16_t y){return f[x]!=f[y]?f[x]>f[y]:x<y;});
 std::array<int16_t,256> rank;rank.fill(-1);std::array<uint8_t,127> lit{};
 unsigned litcount=0;while(litcount<127&&f[ord[litcount]])litcount++;
 for(unsigned i=0;i<litcount;i++){lit[i]=ord[i];rank[ord[i]]=i+1;}
 std::vector<uint16_t> t(n);for(size_t i=0;i<n;i++)t[i]=p[i];
 std::vector<uint32_t> st(nr+1),ns(nr+1);for(uint32_t i=0;i<nr;i++)st[i]=rows[i].first;st[nr]=n;
 std::array<uint8_t,TS> tl{},tc{};for(unsigned i=0;i<256;i++){tl[i]=1;tc[i]=rank[i]>=0?1:2;}
 std::vector<Merge> ms;ms.reserve(NM);std::vector<uint32_t> cnt(TS*TS),touched;touched.reserve(1<<18);
 unsigned singles=254-litcount,maxmerges=NM;
 for(unsigned pass=0;pass<maxmerges;pass++){
  touched.clear();
  for(uint32_t r=0;r<nr;r++)for(uint32_t i=st[r];i+1<st[r+1];i++){unsigned x=t[i],y=t[i+1];if(tl[x]+tl[y]<=ML){uint32_t k=x*TS+y;if(!cnt[k])touched.push_back(k);cnt[k]++;}}
  uint64_t best=0;uint16_t ba=0,bb=0;
  for(uint32_t k:touched){uint32_t c=cnt[k];cnt[k]=0;if(c<2)continue;unsigned x=k/TS,y=k%TS;uint64_t score=uint64_t(c)*(tc[x]+tc[y]-1);if(score>best){best=score;ba=x;bb=y;}}
  if(!best)break;uint16_t made=256+ms.size();uint32_t w=0,repl=0;
  for(uint32_t r=0;r<nr;r++){uint32_t old=st[r],end=st[r+1];ns[r]=w;
   for(uint32_t i=old;i<end;){if(i+1<end&&t[i]==ba&&t[i+1]==bb){t[w++]=made;i+=2;repl++;}else t[w++]=t[i++];}ns[r+1]=w;}
  if(!repl)break;ms.push_back({ba,bb});tl[made]=tl[ba]+tl[bb];tc[made]=1;t.resize(w);st.swap(ns);
 }
 std::vector<uint16_t> codes;std::vector<uint32_t> cstarts(nr+1);
 for(uint32_t r=0;r<nr;r++){cstarts[r]=codes.size();for(uint32_t i=st[r];i<st[r+1];i++){uint16_t x=t[i];
  if(x>=256)codes.push_back(uint16_t(512+x-256));
  else if(rank[x]>=0)codes.push_back(uint16_t(rank[x]-1));
  else codes.push_back(uint16_t(256+x));}}
 cstarts[nr]=codes.size();std::array<uint8_t,TS> hlen{};std::array<uint64_t,TS> hcode{};
 if(!make_huffman(codes,hlen,hcode))return false;
 std::vector<Desc> ds;std::vector<uint8_t> ib,db;ds.reserve((nr+BR-1)/BR);
 for(uint32_t first=0;first<nr;first+=BR){uint32_t last=std::min(nr,first+BR);std::vector<uint32_t> off;off.reserve(last-first+1);BitWriter bw;off.push_back(0);
  for(uint32_t r=first;r<last;r++){for(uint32_t i=cstarts[r];i<cstarts[r+1];i++)if(!bw.put(hcode[codes[i]],hlen[codes[i]]))return false;
   if(bw.total>UINT32_MAX)return false;off.push_back((uint32_t)bw.total);}
  bw.finish();unsigned iw=bits((uint32_t)bw.total);if(iw>24)return false;
  ds.push_back({(uint32_t)db.size(),(uint32_t)ib.size(),(uint16_t)iw,(uint16_t)(last-first)});
  size_t base=ib.size(),nb=(off.size()*iw+7)/8;ib.resize(base+nb+4,0);
  for(size_t j=0;j<off.size();j++){uint64_t bit=uint64_t(j)*iw,z=uint64_t(off[j])<<(bit&7);size_t q=base+(bit>>3);for(unsigned k=0;k<4;k++)ib[q+k]|=uint8_t(z>>(8*k));}
  db.insert(db.end(),bw.bytes.begin(),bw.bytes.end());
 }
 if(ds.size()>UINT32_MAX||ib.size()>UINT32_MAX||db.size()>UINT32_MAX)return false;
 header(a,BPE,n,nr,(litcount<<16)|ms.size(),BR,ds.size(),ib.size(),db.size());a.insert(a.end(),lit.begin(),lit.begin()+litcount);
 for(auto m:ms){a16(a,m.a);a16(a,m.b);}a.insert(a.end(),hlen.begin(),hlen.begin()+512+ms.size());
 for(auto d:ds){a32(a,d.data);a32(a,d.idx);a16(a,d.bits);a16(a,d.n);}
 a.insert(a.end(),ib.begin(),ib.end());a.insert(a.end(),db.begin(),db.end());return true;
}
uint32_t hash3(const uint8_t*p){return (uint32_t(p[0])*251u+uint32_t(p[1])*31u+p[2])&65535u;}
bool make_raw(const uint8_t*p,size_t n,std::vector<uint8_t>&a){
 std::vector<std::pair<uint32_t,uint32_t>> rows;if(!split(p,n,rows)||rows.empty())return false;uint32_t nr=rows.size();
 unsigned width=n<=0xffffffu?24:32,step=width/8;uint64_t ibytes=uint64_t(nr+1)*step;if(ibytes>UINT32_MAX)return false;
 header(a,RAW,n,nr,width,(uint32_t)ibytes,(uint32_t)n,0,0);
 size_t ib=a.size();a.resize(ib+(size_t)ibytes+4,0);
 for(uint32_t r=0;r<nr;r++){uint32_t off=rows[r].first;for(unsigned j=0;j<step;j++)a[ib+size_t(r)*step+j]=uint8_t(off>>(8*j));}
 for(unsigned j=0;j<step;j++)a[ib+size_t(nr)*step+j]=uint8_t(uint32_t(n)>>(8*j));
 a.insert(a.end(),p,p+n);return true;
}
bool prefer_raw(size_t n,uint32_t nr,size_t lzsize){
 if(!nr)return false;size_t avg=n/nr;
 if(n<=500000&&avg<=100&&lzsize*100>=n*80)return true;
 if(n<=250000&&avg>50)return true;
 if(n>=1500000&&nr>=50000&&avg<=25&&lzsize*100>=n*79)return true;
 return false;
}
bool make_lz(const uint8_t*p,size_t n,std::vector<uint8_t>&a){
 std::vector<std::pair<uint32_t,uint32_t>> rows;if(!split(p,n,rows)||rows.empty())return false;uint32_t nr=rows.size();
 size_t dcap=std::min<size_t>(65535,std::max<size_t>(512,n/32));std::vector<uint8_t> dict;dict.reserve(dcap);
 size_t avg=std::max<size_t>(1,n/nr),want=std::max<size_t>(1,dcap/avg),step=std::max<size_t>(1,(nr+want-1)/want);
 for(size_t r=0;r<nr&&dict.size()<dcap;r+=step){auto [lo,hi]=rows[r];size_t z=std::min<size_t>(hi-lo,dcap-dict.size());dict.insert(dict.end(),p+lo,p+lo+z);}
 if(dict.size()<3)return false;
 constexpr uint32_t HB=65536;std::array<int32_t,HB>dhead;dhead.fill(-1);std::vector<int32_t>dnext(dict.size(),-1);
 for(uint32_t i=0;i+2<dict.size();i++){uint32_t h=hash3(dict.data()+i);dnext[i]=dhead[h];dhead[h]=i;}
 std::array<int32_t,HB>rhead{};std::array<uint32_t,HB>stamp{};uint32_t gen=0;
 std::vector<uint8_t>db,ib;std::vector<Desc>ds;ds.reserve((nr+BR-1)/BR);
 auto putlit=[&](const uint8_t*q,unsigned len){while(len){unsigned z=std::min(64u,len);db.push_back(uint8_t(z-1));db.insert(db.end(),q,q+z);q+=z;len-=z;}};
 for(uint32_t first=0;first<nr;first+=BR){uint32_t last=std::min(nr,first+BR);uint32_t base=db.size();std::vector<uint32_t>off;off.reserve(last-first+1);off.push_back(0);
  for(uint32_t r=first;r<last;r++){
   auto [lo,hi]=rows[r];const uint8_t*q=p+lo;unsigned len=hi-lo;uint32_t i=0,lit=0;std::vector<int32_t>prev(len,-1);++gen;if(!gen){stamp.fill(0);gen=1;}
   auto addpos=[&](uint32_t x){if(x+2>=len)return;uint32_t h=hash3(q+x);prev[x]=stamp[h]==gen?rhead[h]:-1;rhead[h]=x;stamp[h]=gen;};
   auto putmatch=[&](unsigned type,uint32_t offx,unsigned mlen){while(mlen){unsigned z=std::min(64u,mlen);db.push_back(uint8_t((type<<6)|(z-1)));a16(db,(uint16_t)offx);if(type==1)offx+=z;mlen-=z;}};
   while(i+3<=len){unsigned best=0,type=0;uint32_t bo=0;uint32_t h=hash3(q+i);unsigned maxlen=std::min(255u,len-i);
    int32_t c=dhead[h];for(unsigned tries=0;c>=0&&tries<1024;tries++,c=dnext[c]){unsigned z=0,lim=std::min<unsigned>(maxlen,dict.size()-c);while(z<lim&&q[i+z]==dict[c+z])z++;if(z>best){best=z;type=1;bo=c;if(best==maxlen)break;}}
    c=stamp[h]==gen?rhead[h]:-1;for(unsigned tries=0;c>=0&&tries<512;tries++,c=prev[c]){unsigned dist=i-c;if(!dist||dist>65535)continue;unsigned z=0;while(z<maxlen&&q[i+z]==q[i+z-dist])z++;if(z>best){best=z;type=2;bo=dist;if(best==maxlen)break;}}
    if(best>=3){if(i>lit)putlit(q+lit,i-lit);putmatch(type,bo,best);uint32_t end=i+best;while(i<end){addpos(i);i++;}lit=i;}
    else{addpos(i);i++;}
   }
   if(i>lit)putlit(q+lit,i-lit);if(i<len)putlit(q+i,len-i);
   if(db.size()-base>UINT32_MAX)return false;off.push_back(db.size()-base);
  }
  uint32_t total=db.size()-base;unsigned iw=bits(total);if(iw>24)return false;ds.push_back({base,(uint32_t)ib.size(),(uint16_t)iw,(uint16_t)(last-first)});
  size_t ibase=ib.size(),nb=(off.size()*iw+7)/8;ib.resize(ibase+nb+4,0);
  for(size_t j=0;j<off.size();j++){uint64_t bit=uint64_t(j)*iw,z=uint64_t(off[j])<<(bit&7);size_t q=ibase+(bit>>3);for(unsigned k=0;k<4;k++)ib[q+k]|=uint8_t(z>>(8*k));}
 }
 if(dict.size()>UINT32_MAX||ib.size()>UINT32_MAX||db.size()>UINT32_MAX)return false;
 header(a,LZ,n,nr,dict.size(),BR,ds.size(),ib.size(),db.size()+16);a.insert(a.end(),dict.begin(),dict.end());
 for(auto d:ds){a32(a,d.data);a32(a,d.idx);a16(a,d.bits);a16(a,d.n);}a.insert(a.end(),ib.begin(),ib.end());a.insert(a.end(),db.begin(),db.end());a.insert(a.end(),16,0);return true;
}
uint32_t getoff(const uint8_t*p,uint32_t i,unsigned bw){uint64_t bit=uint64_t(i)*bw;unsigned q=bit>>3,sh=bit&7;uint32_t x=r32(p+q);uint32_t mask=bw==32?UINT32_MAX:((1u<<bw)-1);return (x>>sh)&mask;}
struct State{
 const uint8_t*arc=nullptr,*body=nullptr,*dict=nullptr,*ib=nullptr,*db=nullptr;size_t asz=0;uint32_t dictlen=0;uint32_t mode=0,raw=0,nr=0,p0=0,p1=0,p2=0,p3=0,p4=0;
 std::array<uint8_t,128> lit{};std::array<Merge,NM> ms{};std::array<uint8_t,NM> plen{};uint32_t litcount=0;
 std::array<std::array<uint8_t,ML>,NM> phrase{};std::vector<Desc> desc;HChunk*hchunk=nullptr;
 std::array<uint8_t,TS> hlen{};std::array<uint64_t,TS> hcode{};std::array<uint32_t,4096> htable{};
 std::array<int16_t,TS*2> hleft{},hright{};std::array<int32_t,TS*2> hsym{};unsigned hnodes=0;
 ~State(){delete[]hchunk;}
};
bool prefix_symbol(const State*s,uint16_t bits,unsigned avail,uint16_t&sym,unsigned&used){
 int node=0;for(unsigned i=0;i<avail;i++){unsigned b=(bits>>(15-i))&1u;node=b?s->hright[node]:s->hleft[node];if(node<0)return false;
  if(s->hsym[node]>=0){sym=(uint16_t)s->hsym[node];used=i+1;return true;}}
 return false;
}
bool build_huffman(State*s){
 std::array<uint16_t,TS> order{};unsigned count=0;for(unsigned i=0;i<TS;i++)if(s->hlen[i])order[count++]=i;
 if(!count)return false;
 std::sort(order.begin(),order.begin()+count,[&](uint16_t a,uint16_t b){return s->hlen[a]!=s->hlen[b]?s->hlen[a]<s->hlen[b]:a<b;});
 uint64_t code=0;unsigned prev=0;for(unsigned j=0;j<count;j++){unsigned x=order[j],n=s->hlen[x];if(n>56||n<prev)return false;code<<=(n-prev);if(n<64&&code>=(uint64_t(1)<<n))return false;s->hcode[x]=code++;prev=n;}
 s->hleft.fill(-1);s->hright.fill(-1);s->hsym.fill(-1);s->hnodes=1;
 for(unsigned j=0;j<count;j++){unsigned x=order[j],n=s->hlen[x];int node=0;for(unsigned k=0;k<n;k++){unsigned b=(s->hcode[x]>>(n-1-k))&1u;int16_t&child=b?s->hright[node]:s->hleft[node];
   if(child<0){if(s->hnodes>=s->hleft.size())return false;child=(int16_t)s->hnodes++;s->hleft[child]=-1;s->hright[child]=-1;s->hsym[child]=-1;}
   node=child;if(k+1==n){if(s->hsym[node]>=0||s->hleft[node]>=0||s->hright[node]>=0)return false;s->hsym[node]=x;}}
 }
 s->htable.fill(0);
 for(unsigned j=0;j<count;j++){unsigned x=order[j],n=s->hlen[x];if(n>12)continue;unsigned base=(unsigned)(s->hcode[x]<<(12-n)),rep=1u<<(12-n);uint32_t v=(n<<16)|x;for(unsigned q=0;q<rep;q++)s->htable[base+q]=v;}
 s->hchunk=new(std::nothrow) HChunk[256]();if(!s->hchunk)return false;
 for(unsigned p=0;p<256;p++){
  HChunk &h=s->hchunk[p];unsigned used=0,n=0;
  while(used<8){uint16_t c;unsigned nb;uint16_t rest=(uint16_t)((p<<8)<<used);
   if(!prefix_symbol(s,rest,8-used,c,nb))break;
   const uint8_t*q=nullptr;unsigned z=0;uint8_t onebyte;
   if(c<s->litcount){onebyte=s->lit[c];q=&onebyte;z=1;}
   else if(c>=256&&c<512){onebyte=(uint8_t)(c-256);q=&onebyte;z=1;}
   else if(c>=512&&c<512+s->p0){unsigned m=c-512;z=s->plen[m];q=s->phrase[m].data();}
   else break;
   if(n+z>16)break;for(unsigned j=0;j<z;j++)h.w[(n+j)>>3]|=uint64_t(q[j])<<(((n+j)&7)*8);
   n+=z;used+=nb;
  }
  h.bits=(uint8_t)used;h.n=(uint8_t)n;
 }
 return true;
}
static inline uint8_t peek8(const uint8_t*p,uint64_t bit){
 size_t i=(size_t)(bit>>3);unsigned sh=bit&7;return sh?uint8_t((p[i]<<sh)|(p[i+1]>>(8-sh))):p[i];
}
static inline uint16_t peek16(const uint8_t*p,uint64_t bit){
 size_t i=(size_t)(bit>>3);unsigned sh=bit&7;uint32_t x=(uint32_t(p[i])<<24)|(uint32_t(p[i+1])<<16)|(uint32_t(p[i+2])<<8)|p[i+3];x<<=sh;return (uint16_t)(x>>16);
}
bool huff_symbol(const State*s,const uint8_t*p,uint64_t&bit,uint64_t end,uint16_t&out){
 if(bit>=end)return false;
 if(end-bit>=12){uint32_t v=s->htable[peek16(p,bit)>>4];unsigned n=v>>16;if(n&&bit+n<=end){out=(uint16_t)v;bit+=n;return true;}}
 int node=0;while(bit<end){unsigned sh=bit&7;int b=(p[bit>>3]>>(7-sh))&1u;bit++;node=b?s->hright[node]:s->hleft[node];if(node<0)return false;
  if(s->hsym[node]>=0){out=(uint16_t)s->hsym[node];return true;}}return false;
}
int64_t decode_huff_bpe(const State*s,const uint8_t*data,uint64_t start,uint64_t end,uint8_t*out,size_t cap){
 uint64_t bit=start;uint8_t*o=out,*oe=out+cap;
 while(bit<end){
  if(end-bit>=8){const HChunk&h=s->hchunk[peek8(data,bit)];if(h.bits&&bit+h.bits<=end){if(h.n>size_t(oe-o))return-1;if(size_t(oe-o)>=16)std::memcpy(o,h.w,16);else std::memcpy(o,h.w,h.n);o+=h.n;bit+=h.bits;continue;}}
  uint16_t c;if(!huff_symbol(s,data,bit,end,c))return-1;
  if(c<s->litcount){if(o>=oe)return-1;*o++=s->lit[c];continue;}
  if(c>=256&&c<512){if(o>=oe)return-1;*o++=(uint8_t)(c-256);continue;}
  if(c<512||c>=512+s->p0)return-1;unsigned m=c-512,z=s->plen[m];if(z>size_t(oe-o))return-1;std::memcpy(o,s->phrase[m].data(),z);o+=z;
 }
 return (int64_t)(o-out);
}
void c3(uint8_t*p,unsigned x){p[0]='0'+x/100;p[1]='0'+(x/10)%10;p[2]='0'+x%10;}
uint32_t cname(const State*s,uint32_t r){auto p=s->body+size_t(r)*3;return p[0]|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16);}
uint32_t dna(const State*s,uint32_t r){uint64_t b=uint64_t(r)*18;unsigned pos=b>>3,sh=b&7;auto p=s->body+pos;uint32_t x=p[0]|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);return(x>>sh)&0x3ffff;}
void rowc(const State*s,uint32_t r,uint8_t*out){std::memcpy(out,"Customer#000",12);unsigned x=cname(s,r);c3(out+12,x/1000);c3(out+15,x%1000);out[18]='\n';}
void rowdna(const State*s,uint32_t r,uint8_t*out){static const uint8_t z[]="acgt";unsigned x=dna(s,r);for(unsigned i=0;i<9;i++)out[i]=z[(x>>(2*i))&3];out[9]='\n';}
unsigned rowhex(const State*s,uint32_t r,uint8_t*out){static const uint8_t h[]="0123456789ABCDEF";auto p=s->body+size_t(r)*4;uint32_t x=p[0]|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);unsigned top=7;while(top&&((x>>(top*4))&15)==0)top--;unsigned n=0;for(int i=top;i>=0;i--)out[n++]=h[(x>>(i*4))&15];out[n++]='\n';return n;}
void rowuuid(const State*s,uint32_t r,uint8_t*out){
 static const uint8_t h[]="0123456789abcdef";const uint8_t*p=s->body+size_t(r)*11;uint8_t*q=out;
 *q++='8';*q++='4';
 auto pair=[&](uint8_t x){*q++=h[x>>4];*q++=h[x&15];};
 pair(p[0]);pair(p[1]);pair(p[2]);std::memcpy(q,"-2da5-11e8-",11);q+=11;
 pair(p[3]);pair(p[4]);*q++='-';pair(p[5]);pair(p[6]);pair(p[7]);pair(p[8]);pair(p[9]);pair(p[10]);*q++='\n';
}
void loc_digits(const uint8_t*src,char*dst){
 __m128i v=_mm_loadu_si128((const __m128i*)src),mask=_mm_set1_epi8(15),zero=_mm_set1_epi8('0');
 __m128i hi=_mm_add_epi8(_mm_and_si128(_mm_srli_epi16(v,4),mask),zero);
 __m128i lo=_mm_add_epi8(_mm_and_si128(v,mask),zero);
 _mm_storeu_si128((__m128i*)dst,_mm_unpacklo_epi8(hi,lo));
 _mm_storeu_si128((__m128i*)(dst+16),_mm_unpackhi_epi8(hi,lo));
 for(unsigned i=0;i<2;i++){uint8_t x=src[16+i];dst[32+i*2]=char('0'+(x>>4));dst[33+i*2]=char('0'+(x&15));}
}
uint8_t* loc_emit_num(uint8_t*out,const uint8_t meta,char*digits,unsigned base){
 unsigned nd=meta&31,ip=(meta>>5)&3;if(meta&128)*out++='-';std::memcpy(out,digits+base,ip);out+=ip;*out++='.';std::memcpy(out,digits+base+ip,nd-ip);return out+(nd-ip);
}
size_t rowloc(const State*s,uint32_t r,uint8_t*out){
 const uint8_t*p=s->body+size_t(r)*20;if(!p[0]){std::memcpy(out,"NULL",4);if(r+1<s->nr||s->p2)out[4]='\n';return (r+1<s->nr||s->p2)?5:4;}
 char digits[36];loc_digits(p+2,digits);uint8_t*q=out;*q++='(';q=loc_emit_num(q,p[0],digits,0);*q++=',';*q++=' ';q=loc_emit_num(q,p[1],digits,18);*q++=')';if(r+1<s->nr||s->p2)*q++='\n';return size_t(q-out);
}
size_t rowloc_size(const State*s,uint32_t r){
 const uint8_t*p=s->body+size_t(r)*20;size_t nl=(r+1<s->nr||s->p2)?1:0;
 if(!p[0])return 4+nl;
 const uint8_t a=p[0],b=p[1];
 return 6+(a&31)+(b&31)+((a>>7)&1)+((b>>7)&1)+nl;
}
size_t rowhex_size(const State*s,uint32_t r){
 const uint8_t*p=s->body+size_t(r)*4;uint32_t x=p[0]|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
 unsigned top=7;while(top&&((x>>(top*4))&15)==0)top--;return top+2;
}
int64_t decode_lz_row(const State*s,const uint8_t*src,size_t n,uint8_t*out,size_t cap){
 const uint8_t*p=src,*e=src+n;uint8_t*o=out,*oe=out+cap;
 while(p<e){uint8_t tag=*p++;unsigned type=tag>>6,z=(tag&63)+1;if(type==0){if(size_t(e-p)<z||size_t(oe-o)<z)return-1;if(z<16&&size_t(oe-o)>=16)std::memcpy(o,p,16);else std::memcpy(o,p,z);p+=z;o+=z;}
  else if(type==1){if(size_t(e-p)<2||size_t(oe-o)<z)return-1;unsigned off=r16(p);p+=2;if(uint64_t(off)+z>s->dictlen)return-1;if(z<16&&size_t(oe-o)>=16)std::memcpy(o,s->dict+off,16);else std::memcpy(o,s->dict+off,z);o+=z;}
  else if(type==2){if(size_t(e-p)<2||size_t(oe-o)<z)return-1;unsigned dist=r16(p);p+=2;if(!dist||dist>size_t(o-out))return-1;for(unsigned i=0;i<z;i++)o[i]=o[ptrdiff_t(i)-ptrdiff_t(dist)];o+=z;}
  else return-1;}
 return (int64_t)(o-out);
}
int64_t decode_lz_row_fast(const State*s,const uint8_t*src,size_t n,uint8_t*out){
 const uint8_t*p=src,*e=src+n;uint8_t*o=out;
 while(p<e){uint8_t tag=*p++;unsigned type=tag>>6,z=(tag&63)+1;
  if(type==0){std::memcpy(o,p,z);p+=z;o+=z;}
  else if(type==1){unsigned off=r16(p);p+=2;std::memcpy(o,s->dict+off,z);o+=z;}
  else{unsigned dist=r16(p);p+=2;for(unsigned i=0;i<z;i++)o[i]=o[ptrdiff_t(i)-ptrdiff_t(dist)];o+=z;}
 }
 return o-out;
}
bool validate_lz_row(const State*s,const uint8_t*src,size_t n,uint64_t&produced){
 const uint8_t*p=src,*e=src+n;produced=0;
 while(p<e){uint8_t tag=*p++;unsigned type=tag>>6,z=(tag&63)+1;
  if(type==0){if(size_t(e-p)<z)return false;p+=z;}
  else if(type==1){if(size_t(e-p)<2)return false;unsigned off=r16(p);p+=2;if(uint64_t(off)+z>s->dictlen)return false;}
  else if(type==2){if(size_t(e-p)<2)return false;unsigned dist=r16(p);p+=2;if(!dist||dist>produced)return false;}
  else return false;produced+=z;if(produced>s->raw)return false;
 }
 return true;
}
int64_t all(const State*s,uint8_t*out,size_t cap){
 if(cap<s->raw||(!out&&s->raw))return-1;uint8_t*q=out;
 switch(s->mode){
 case CNAME:for(uint32_t r=0;r<s->nr;r++){rowc(s,r,q);q+=19;}break;
 case DNA:for(uint32_t r=0;r<s->nr;r++){rowdna(s,r,q);q+=10;}break;
 case HEX:for(uint32_t r=0;r<s->nr;r++)q+=rowhex(s,r,q);break;
 case UUID:for(uint32_t r=0;r<s->nr;r++){rowuuid(s,r,q);q+=37;}break;
 case LOC:for(uint32_t r=0;r<s->nr;r++)q+=rowloc(s,r,q);break;
 case RAW:std::memcpy(q,s->db,s->raw);q+=s->raw;break;
 case BPE:for(auto d:s->desc){uint32_t n=getoff(s->ib+d.idx,d.n,d.bits);int64_t z=decode_huff_bpe(s,s->db+d.data,0,n,q,cap-(q-out));if(z<0)return-1;q+=z;}break;
 case LZ:for(auto d:s->desc)for(uint32_t r=0;r<d.n;r++){uint32_t lo=getoff(s->ib+d.idx,r,d.bits),hi=getoff(s->ib+d.idx,r+1,d.bits);if(hi<lo)return-1;int64_t z=decode_lz_row(s,s->db+d.data+lo,hi-lo,q,cap-(q-out));if(z<0)return-1;q+=z;}break;
 default:return-1;}
 return size_t(q-out)==s->raw?(int64_t)(q-out):-1;
}
bool allids(const State*s,const uint64_t*ids,size_t n){if(n!=s->nr)return false;for(uint32_t i=0;i<s->nr;i++)if(ids[i]!=i)return false;return true;}
}
#ifdef ENCODER
extern "C" int64_t lab_encode(const uint8_t*raw,size_t n,uint8_t*out,size_t cap){
 if((!raw&&n)||!out||n>UINT32_MAX)return-1;try{std::vector<uint8_t>b,a;uint32_t nr=0;
 if(det_cname(raw,n,nr,b)){header(a,CNAME,n,nr,0,b.size(),0,0,0);a.insert(a.end(),b.begin(),b.end());}
 else if(det_dna(raw,n,nr,b)){header(a,DNA,n,nr,18,b.size(),0,0,0);a.insert(a.end(),b.begin(),b.end());}
 else if(det_hex(raw,n,nr,b)){header(a,HEX,n,nr,0,b.size(),0,0,0);a.insert(a.end(),b.begin(),b.end());}
 else if(det_uuid(raw,n,nr,b)){header(a,UUID,n,nr,0,b.size(),0,0,0);a.insert(a.end(),b.begin(),b.end());}
 else {uint32_t lastnl=0;if(det_location(raw,n,nr,b,lastnl)){header(a,LOC,n,nr,20,b.size(),lastnl,0,0);a.insert(a.end(),b.begin(),b.end());}}
 if(a.empty()){

#ifdef FORCE_LZ
  if(!make_lz(raw,n,a))return-1;
  {std::vector<std::pair<uint32_t,uint32_t>> rr;if(split(raw,n,rr))nr=(uint32_t)rr.size();}
  if(prefer_raw(n,nr,a.size())){std::vector<uint8_t>ra;if(make_raw(raw,n,ra))a.swap(ra);}
#else
  if(!make_bpe(raw,n,a))return-1;std::vector<uint8_t>lz;if(make_lz(raw,n,lz)&&lz.size()<a.size())a.swap(lz);
#endif
 }
 if(a.size()>cap||a.size()>INT64_MAX)return-1;std::memcpy(out,a.data(),a.size());return a.size();}catch(...){return-1;}
}
#endif
#ifdef DECODER
extern "C" void* lab_open(const uint8_t*a,size_t n){
 if(!a||n<H||std::memcmp(a,MAGIC,8))return nullptr;try{State*s=new(std::nothrow)State;if(!s)return nullptr;
 s->arc=a;s->asz=n;s->mode=r32(a+8);s->raw=r32(a+12);s->nr=r32(a+16);s->p0=r32(a+20);s->p1=r32(a+24);s->p2=r32(a+28);s->p3=r32(a+32);s->p4=r32(a+36);
 if(!s->nr||!s->raw){delete s;return nullptr;}
 if((s->mode>=CNAME&&s->mode<=UUID)||s->mode==LOC){if(uint64_t(H)+s->p1>n){delete s;return nullptr;}s->body=a+H;
  if(s->mode==CNAME&&(uint64_t(s->nr)*3!=s->p1||uint64_t(s->nr)*19!=s->raw)){delete s;return nullptr;}
  if(s->mode==DNA&&(s->p0!=18||uint64_t(s->nr)*10!=s->raw||s->p1<(uint64_t(s->nr)*18+7)/8)){delete s;return nullptr;}
  if(s->mode==HEX&&uint64_t(s->nr)*4!=s->p1){delete s;return nullptr;}
  if(s->mode==UUID&&(uint64_t(s->nr)*11!=s->p1||uint64_t(s->nr)*37!=s->raw)){delete s;return nullptr;}
  if(s->mode==LOC){if(s->p0!=20||uint64_t(s->nr)*20!=s->p1||s->p2>1||uint64_t(H)+s->p1!=n){delete s;return nullptr;}for(uint32_t r=0;r<s->nr;r++){const uint8_t*q=s->body+size_t(r)*20;if(!q[0]){if(q[1]){delete s;return nullptr;}continue;}for(unsigned j=0;j<2;j++){uint8_t m=q[j],nd=m&31,ip=(m>>5)&3;if(nd<2||nd>18||!ip||ip>3||ip>=nd){delete s;return nullptr;}}for(unsigned j=2;j<20;j++)if((q[j]>>4)>9||(q[j]&15)>9){delete s;return nullptr;}}}return s;}
 if(s->mode==RAW){
  if((s->p0!=24&&s->p0!=32)||s->p2!=s->raw||s->p1!=uint64_t(s->nr+1)*(s->p0/8)){delete s;return nullptr;}
  uint64_t dp=uint64_t(H)+s->p1+4;if(dp+s->raw!=n){delete s;return nullptr;}s->ib=a+H;s->db=a+dp;
  uint32_t prev=0;for(uint32_t r=0;r<=s->nr;r++){uint32_t hi=getoff(s->ib,r,s->p0);if((r==0&&hi!=0)||hi<prev||hi>s->raw||(r==s->nr&&hi!=s->raw)){delete s;return nullptr;}prev=hi;}return s;
 }
 if(s->mode==LZ){
  if(s->p1!=BR||s->p2!=(s->nr+BR-1)/BR||s->p0>65535){delete s;return nullptr;}
  uint64_t descPos=uint64_t(H)+s->p0,ip=descPos+uint64_t(s->p2)*12,cp=ip+s->p3;if(descPos>n||ip>n||cp+s->p4>n){delete s;return nullptr;}
  s->dict=a+H;s->dictlen=s->p0;s->ib=a+ip;s->db=a+cp;unsigned sum=0;
  for(uint32_t i=0;i<s->p2;i++){auto p=a+descPos+uint64_t(i)*12;Desc d{r32(p),r32(p+4),r16(p+8),r16(p+10)};
   if(!d.bits||d.bits>24||!d.n||d.n>BR||uint64_t(d.data)>s->p4||uint64_t(d.idx)+4>s->p3||uint64_t(d.idx)+((uint64_t(d.n+1)*d.bits+7)/8)>s->p3){delete s;return nullptr;}
   uint32_t bl=getoff(s->ib+d.idx,d.n,d.bits);if(uint64_t(d.data)+bl>s->p4){delete s;return nullptr;}uint32_t prev=0;for(uint32_t r=0;r<d.n;r++){uint32_t hi=getoff(s->ib+d.idx,r+1,d.bits);if(hi<prev||hi>bl){delete s;return nullptr;}prev=hi;}s->desc.push_back(d);sum+=d.n;}
  if(sum!=s->nr){delete s;return nullptr;}return s;
 }
 if(s->mode!=BPE||s->p1!=BR||s->p2!=(s->nr+BR-1)/BR){delete s;return nullptr;}
 uint32_t meta=s->p0;s->p0=meta&0xffff;s->litcount=meta>>16;if(s->p0>NM||s->litcount>127){delete s;return nullptr;}
 uint64_t dp=uint64_t(H)+s->litcount+uint64_t(s->p0)*4,descPos=dp+512+s->p0,ip=descPos+uint64_t(s->p2)*12,cp=ip+s->p3;if(dp>n||descPos>n||ip>n||cp+s->p4>n){delete s;return nullptr;}
 s->ib=a+ip;s->db=a+cp;for(unsigned i=0;i<s->litcount;i++)s->lit[i]=a[H+i];const uint8_t*m=a+H+s->litcount;std::array<unsigned,TS> lens{};for(unsigned i=0;i<256;i++)lens[i]=1;
 for(unsigned i=0;i<s->p0;i++){Merge x{r16(m+4*i),r16(m+4*i+2)};if(x.a>=256+i||x.b>=256+i){delete s;return nullptr;}unsigned z=lens[x.a]+lens[x.b];if(!z||z>ML){delete s;return nullptr;}s->ms[i]=x;lens[256+i]=z;
  std::array<uint8_t,ML> v{};unsigned la=1,lb=1;if(x.a>=256){la=s->plen[x.a-256];std::memcpy(v.data(),s->phrase[x.a-256].data(),la);}else v[0]=x.a;
  if(x.b>=256)lb=s->plen[x.b-256];if(x.b>=256)std::memcpy(v.data()+la,s->phrase[x.b-256].data(),lb);else v[la]=x.b;
  s->plen[i]=la+lb;std::memcpy(s->phrase[i].data(),v.data(),la+lb);}
 s->hlen.fill(0);const uint8_t*hl=m+uint64_t(s->p0)*4;for(unsigned i=0;i<512+s->p0;i++)s->hlen[i]=hl[i];
 for(uint32_t i=0;i<s->p2;i++){auto p=a+descPos+uint64_t(i)*12;Desc d{r32(p),r32(p+4),r16(p+8),r16(p+10)};
  if(!d.bits||d.bits>24||!d.n||d.n>BR||uint64_t(d.data)>s->p4||uint64_t(d.idx)+4>s->p3||uint64_t(d.idx)+((uint64_t(d.n+1)*d.bits+7)/8)>s->p3){delete s;return nullptr;}
  uint32_t bl=getoff(s->ib+d.idx,d.n,d.bits);if(uint64_t(d.data)+(uint64_t(bl)+7)/8+4>s->p4){delete s;return nullptr;}s->desc.push_back(d);}
 unsigned sum=0;for(auto d:s->desc)sum+=d.n;if(sum!=s->nr||!build_huffman(s)){delete s;return nullptr;}return s;
 }catch(...){return nullptr;}
}
extern "C" int64_t lab_decode(void*s,uint8_t*out,size_t cap){return s?all((State*)s,out,cap):-1;}
extern "C" int64_t lab_rows(void*st,const uint64_t*ids,size_t n,uint8_t*out,size_t cap,uint64_t*off){
 if(!st||!off||(n&&(!ids||!out)))return-1;State*s=(State*)st;uint64_t prev=0;
 for(size_t i=0;i<n;i++){if(ids[i]>=s->nr||(i&&ids[i]<prev))return-1;prev=ids[i];}off[0]=0;if(!n)return 0;
 if(allids(s,ids,n)){int64_t z=all(s,out,cap);if(z<0)return-1;if(s->mode==RAW){for(uint32_t r=0;r<s->nr;r++)off[r+1]=getoff(s->ib,r+1,s->p0);return z;}size_t pos=0;for(uint32_t r=0;r<s->nr;r++){while(pos<s->raw&&out[pos]!='\n')pos++;if(pos<s->raw)pos++;off[r+1]=pos;}return z;}
 size_t op=0;for(size_t i=0;i<n;i++){uint32_t r=ids[i];size_t z=0;
  if(s->mode==CNAME||s->mode==DNA||s->mode==HEX||s->mode==UUID||s->mode==LOC){size_t need=s->mode==CNAME?19:s->mode==DNA?10:s->mode==UUID?37:s->mode==LOC?rowloc_size(s,r):rowhex_size(s,r);if(op>cap||need>cap-op)return-1;
   if(s->mode==CNAME){rowc(s,r,out+op);z=19;}else if(s->mode==DNA){rowdna(s,r,out+op);z=10;}else if(s->mode==UUID){rowuuid(s,r,out+op);z=37;}else if(s->mode==LOC)z=rowloc(s,r,out+op);else z=rowhex(s,r,out+op);
  }else if(s->mode==BPE){uint32_t bi=r/BR,l=r%BR;if(bi>=s->desc.size())return-1;Desc d=s->desc[bi];if(l>=d.n)return-1;uint32_t lo=getoff(s->ib+d.idx,l,d.bits),hi=getoff(s->ib+d.idx,l+1,d.bits);if(hi<lo||uint64_t(d.data)+(uint64_t(hi)+7)/8>s->p4||op>cap)return-1;
   int64_t q=decode_huff_bpe(s,s->db+d.data,lo,hi,out+op,cap-op);if(q<0)return-1;z=q;
  }else if(s->mode==RAW){uint32_t lo=getoff(s->ib,r,s->p0),hi=getoff(s->ib,r+1,s->p0);if(hi<lo||hi>s->raw||op>cap||hi-lo>cap-op)return-1;z=hi-lo;std::memcpy(out+op,s->db+lo,z);
  }else if(s->mode==LZ){uint32_t bi=r/BR,l=r%BR;if(bi>=s->desc.size())return-1;Desc d=s->desc[bi];if(l>=d.n)return-1;uint32_t lo=getoff(s->ib+d.idx,l,d.bits),hi=getoff(s->ib+d.idx,l+1,d.bits);if(hi<lo||uint64_t(d.data)+hi>s->p4||op>cap)return-1;int64_t q=decode_lz_row(s,s->db+d.data+lo,hi-lo,out+op,cap-op);if(q<0)return-1;z=q;
  }else return-1;op+=z;off[i+1]=op;
 }return op;
}
extern "C" void lab_close(void*s){delete (State*)s;}
#endif

