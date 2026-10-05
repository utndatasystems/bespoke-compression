#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <queue>
#include <unordered_map>
#include <algorithm>
#include <stdexcept>
#include <cstdio>
#include <cmath>
namespace ctx {
static unsigned semantic_group(unsigned c,unsigned G){if(G==1)return 0;if(G==3)return c==10?0:c==32?1:2;if(G==5)return c==10?0:c==32?1:c>=65&&c<=90?2:c>=97&&c<=122?3:4;if(c==10)return 0;if(c==32)return 1;unsigned l=c|32;bool v=l=='a'||l=='e'||l=='i'||l=='o'||l=='u'||l=='y';if(c>='A'&&c<='Z')return v?2:3;if(c>='a'&&c<='z')return v?4:5;if(c>='0'&&c<='9')return 6;return 7;}
static uint32_t rd32(const uint8_t*p){uint32_t v;memcpy(&v,p,4);return v;}
static uint64_t rd64(const uint8_t*p){uint64_t v;memcpy(&v,p,8);return v;}
static void w32(std::vector<uint8_t>&o,uint32_t x){for(int i=0;i<4;i++)o.push_back(x>>(8*i));}
static void w64(std::vector<uint8_t>&o,uint64_t x){for(int i=0;i<8;i++)o.push_back(x>>(8*i));}
static void var(std::vector<uint8_t>&o,uint32_t x){while(x>=128){o.push_back((x&127)|128);x>>=7;}o.push_back(x);}
static uint32_t rev(uint32_t x,int n){uint32_t y=0;while(n--){y=y*2+(x&1);x>>=1;}return y;}
struct Hnode{uint64_t f;int l,r;};
static std::vector<uint8_t> makelens(const std::vector<uint64_t>&freq){
 uint64_t add=0;std::vector<uint8_t> lens;
 for(;;){std::vector<Hnode> ns;using P=std::pair<uint64_t,int>;std::priority_queue<P,std::vector<P>,std::greater<P>>q;
  for(int i=0;i<(int)freq.size();i++)if(freq[i]){ns.push_back({freq[i]+add,-i-1,-1});q.push({freq[i]+add,(int)ns.size()-1});}
  lens.assign(freq.size(),0);if(q.empty())return lens;if(q.size()==1){lens[-ns[q.top().second].l-1]=1;return lens;}
  while(q.size()>1){P a=q.top();q.pop();P b=q.top();q.pop();ns.push_back({a.first+b.first,a.second,b.second});q.push({a.first+b.first,(int)ns.size()-1});}
  std::vector<std::pair<int,int>>st{{q.top().second,0}};int mx=0;
  while(!st.empty()){auto [v,d]=st.back();st.pop_back();if(ns[v].l<0){lens[-ns[v].l-1]=d;mx=std::max(mx,d);}else{st.push_back({ns[v].l,d+1});st.push_back({ns[v].r,d+1});}}
  if(mx<=16)return lens;add=add?add*2:1;
 }
}
static std::vector<uint32_t> codes(const std::vector<uint8_t>&ls){uint32_t c[17]={},n[17]={};for(auto l:ls)if(l)c[l]++;for(int i=1;i<=16;i++)n[i]=(n[i-1]+c[i-1])<<1;std::vector<uint32_t>o(ls.size());for(int i=0;i<(int)ls.size();i++)if(ls[i])o[i]=rev(n[ls[i]]++,ls[i]);return o;}
#ifndef LAB_DECODER
struct PairEntry{uint64_t key;uint32_t count=0,heap=0;std::vector<int> occ;};
struct PQE{uint32_t n,id;bool operator<(const PQE&o)const{return n!=o.n?n<o.n:id>o.id;}};
struct Fit{std::vector<int>sy,nx,rs,dl;std::vector<std::pair<uint16_t,uint16_t>>dict;};
static Fit fit(const uint8_t*raw,size_t n,int maxdict,int minfreq){
 if(n>0x7ffffff0||!n)throw std::runtime_error("text input");
 std::vector<int>sy(n),pr(n),nx(n),rs;for(size_t i=0;i<n;i++){sy[i]=raw[i];pr[i]=(i&&raw[i-1]!=10)?i-1:-1;nx[i]=(i+1<n&&raw[i]!=10)?i+1:-1;if(pr[i]<0)rs.push_back(i);}
 std::unordered_map<uint64_t,uint32_t>map;map.reserve(262144);std::vector<PairEntry>pairs;pairs.reserve(262144);std::priority_queue<PQE>q;
 auto keyat=[&](int p)->uint64_t{return(uint64_t(uint32_t(sy[p]))<<32)|uint32_t(sy[nx[p]]);};
 auto inc=[&](int p){if(p<0||nx[p]<0)return;uint64_t k=keyat(p);auto it=map.find(k);uint32_t id;if(it==map.end()){id=pairs.size();map.emplace(k,id);pairs.push_back({k,0,0,{}});}else id=it->second;auto&e=pairs[id];e.count++;e.occ.push_back(p);if(e.count>e.heap){e.heap=e.count;q.push({e.count,id});}};
 auto dec=[&](int p){if(p<0||nx[p]<0)return;auto it=map.find(keyat(p));if(it==map.end()||!pairs[it->second].count)throw std::runtime_error("pair count");pairs[it->second].count--;};
 for(int p=0;p<(int)n;p++)inc(p);
 std::vector<std::pair<uint16_t,uint16_t>>dict;std::vector<int>dl(256,1);
 while((int)dict.size()+256<maxdict&&!q.empty()){
  PQE top=q.top();q.pop();if(top.n!=pairs[top.id].heap)continue;auto&e=pairs[top.id];if(e.count!=top.n){e.heap=e.count;if(e.count)q.push({e.count,top.id});continue;}if(e.count<(unsigned)minfreq)break;
  uint64_t k=e.key;uint32_t a=k>>32,b=(uint32_t)k;if(dl[a]+dl[b]>255){e.heap=0;continue;}
  std::vector<int>occ;occ.swap(e.occ);e.heap=0;int newS=dict.size()+256;dict.push_back({a,b});dl.push_back(dl[a]+dl[b]);int uses=0;
  for(int p:occ){if(sy[p]!=(int)a||nx[p]<0||sy[nx[p]]!=(int)b)continue;int r=nx[p],l=pr[p],rr=nx[r];dec(l);dec(p);dec(r);sy[p]=newS;sy[r]=-1;nx[p]=rr;if(rr>=0)pr[rr]=p;nx[r]=-1;pr[r]=-1;inc(l);inc(p);uses++;}
  if(!uses)throw std::runtime_error("zero uses");
 }


 return {std::move(sy),std::move(nx),std::move(rs),std::move(dl),std::move(dict)};
}
static std::vector<uint8_t> encode(const uint8_t*raw,size_t n,int maxdict=32768,int minfreq=9,unsigned mode=5,const Fit*trained=nullptr){
 bool learned=mode>=100;unsigned G=learned?mode-100:mode;
 if((learned?(G!=4&&G!=8&&G!=16):(G!=1&&G!=3&&G!=5&&G!=8))||n>0x7ffffff0||!n)throw std::runtime_error("text input");
 Fit own;if(!trained){own=fit(raw,n,maxdict,minfreq);trained=&own;}
 const auto&sy=trained->sy;const auto&nx=trained->nx;const auto&rs=trained->rs;const auto&dl=trained->dl;auto dict=trained->dict;
 size_t ns=256+dict.size();
 uint8_t grouping[256]{};
 if(learned&&G>1){
  using Counts=std::vector<std::pair<uint16_t,uint32_t>>;
  std::vector<std::unordered_map<uint16_t,uint32_t>> initial(256);
  for(int r:rs)for(int p=r;p>=0;p=nx[p])if(p!=r)initial[raw[p-1]][sy[p]]++;
  struct Cluster{Counts count;std::vector<unsigned> members;uint64_t total=0;unsigned version=0;bool alive=true;};std::vector<Cluster> cc;
  for(unsigned c=0;c<256;c++)if(c!=10&&!initial[c].empty()){Cluster cl;cl.members.push_back(c);for(auto kv:initial[c]){cl.count.push_back(kv);cl.total+=kv.second;}std::sort(cl.count.begin(),cl.count.end());cc.push_back(std::move(cl));}
  auto F=[](double x){return x?x*log2(x):0;};
  auto loss=[&](unsigned i,unsigned j){auto&a=cc[i];auto&b=cc[j];double z=F(a.total+b.total)-F(a.total)-F(b.total);size_t x=0,y=0;while(x<a.count.size()&&y<b.count.size()){if(a.count[x].first<b.count[y].first)x++;else if(a.count[x].first>b.count[y].first)y++;else{double av=a.count[x++].second,bv=b.count[y++].second;z-=F(av+bv)-F(av)-F(bv);}}return z;};
  struct Merge{double cost;unsigned a,b,av,bv;bool operator<(const Merge&o)const{return cost!=o.cost?cost>o.cost:a!=o.a?a>o.a:b>o.b;}};std::priority_queue<Merge> merges;
  for(unsigned i=0;i<cc.size();i++)for(unsigned j=i+1;j<cc.size();j++)merges.push({loss(i,j),i,j,0,0});
  size_t alive=cc.size();while(alive+1>G){if(merges.empty())break;auto m=merges.top();merges.pop();auto&a=cc[m.a];auto&b=cc[m.b];if(!a.alive||!b.alive||a.version!=m.av||b.version!=m.bv)continue;Counts merged;merged.reserve(a.count.size()+b.count.size());size_t x=0,y=0;while(x<a.count.size()||y<b.count.size()){if(y==b.count.size()||(x<a.count.size()&&a.count[x].first<b.count[y].first))merged.push_back(a.count[x++]);else if(x==a.count.size()||b.count[y].first<a.count[x].first)merged.push_back(b.count[y++]);else{auto p=a.count[x++];p.second+=b.count[y++].second;merged.push_back(p);}}a.count.swap(merged);a.total+=b.total;a.members.insert(a.members.end(),b.members.begin(),b.members.end());a.version++;b.alive=false;alive--;for(unsigned j=0;j<cc.size();j++)if(j!=m.a&&cc[j].alive){unsigned l=std::min(j,m.a),r=std::max(j,m.a);merges.push({loss(l,r),l,r,cc[l].version,cc[r].version});}}
  std::fill(grouping,grouping+256,1);grouping[10]=0;unsigned ng=1;for(auto&cl:cc)if(cl.alive){for(unsigned c:cl.members)grouping[c]=ng;ng++;}G=ng;if(G==1)std::fill(grouping,grouping+256,0);
 }
 if(!learned)for(unsigned c=0;c<256;c++)grouping[c]=semantic_group(c,G);
 auto group=[&](unsigned c,unsigned){return unsigned(grouping[c]);};
 std::vector<std::vector<uint64_t>> freq(G,std::vector<uint64_t>(ns));for(int r:rs)for(int p=r;p>=0;p=nx[p])freq[group(p==r?10:raw[p-1],G)][sy[p]]++;
 std::vector<std::vector<uint8_t>> lens;for(auto&f:freq)lens.push_back(makelens(f));
 struct Trie{int child=-1,sib=-1,sym=-1;uint8_t c=0;};std::vector<Trie>tr(1);std::vector<std::vector<uint8_t>>words(ns);for(int i=0;i<256;i++)words[i].push_back(i);for(size_t i=256;i<ns;i++){auto [a,b]=dict[i-256];words[i]=words[a];words[i].insert(words[i].end(),words[b].begin(),words[b].end());}
 std::vector<int> root(256,-1);for(size_t sym=0;sym<ns;sym++){int t=0;for(uint8_t c:words[sym]){int u;if(!t)u=root[c];else for(u=tr[t].child;u>=0&&tr[u].c!=c;u=tr[u].sib){}if(u<0){u=tr.size();tr.push_back({-1,tr[t].child,-1,c});tr[t].child=u;if(!t)root[c]=u;}t=u;}tr[t].sym=sym;}
 std::vector<std::vector<uint16_t>>tokens(rs.size());
 for(int iter=0;iter<3;iter++){
  for(auto&f:freq)std::fill(f.begin(),f.end(),0);
  for(size_t ri=0;ri<rs.size();ri++){int start=rs[ri],end=ri+1<rs.size()?rs[ri+1]:n,L=end-start;std::vector<uint32_t>cost(L+1);std::vector<uint16_t>choice(L);for(int j=L-1;j>=0;j--){auto&ls=lens[group(j?raw[start+j-1]:10,G)];uint32_t best=0x7fffffffu;int bs=-1,t=root[raw[start+j]];for(int k=j;t>=0;){int ss=tr[t].sym;if(ss>=0){uint32_t c=(ls[ss]?ls[ss]:16)+cost[k+1];if(c<=best){best=c;bs=ss;}}if(++k>=L)break;uint8_t c=raw[start+k];for(t=tr[t].child;t>=0&&tr[t].c!=c;t=tr[t].sib){}}cost[j]=best;choice[j]=bs;}auto&ts=tokens[ri];ts.clear();for(int j=0;j<L;){int ss=choice[j];ts.push_back(ss);freq[group(j?raw[start+j-1]:10,G)][ss]++;j+=dl[ss];}}
  for(unsigned g=0;g<G;g++)lens[g]=makelens(freq[g]);
 }
 std::vector<uint32_t>rlen;uint64_t nbits=0;for(auto&ts:tokens){uint32_t z=0,g=0;for(int sym:ts){z+=lens[g][sym];g=group(words[sym].back(),G);}rlen.push_back(z);nbits+=z;}
 std::vector<uint8_t>needed(ns);for(auto&f:freq)for(size_t i=0;i<ns;i++)if(f[i])needed[i]=1;for(int i=int(ns)-1;i>=256;i--)if(needed[i]){needed[dict[i-256].first]=1;needed[dict[i-256].second]=1;}
 std::vector<int>ren(ns);std::vector<std::pair<uint16_t,uint16_t>>ndict;std::vector<std::vector<uint8_t>>nlens;for(auto&ls:lens)nlens.emplace_back(ls.begin(),ls.begin()+256);std::vector<uint8_t>nextgroup(256);for(int i=0;i<256;i++){ren[i]=i;nextgroup[i]=group(i,G);}for(size_t i=256;i<ns;i++)if(needed[i]){ren[i]=256+ndict.size();auto [a,b]=dict[i-256];ndict.push_back({ren[a],ren[b]});for(unsigned g=0;g<G;g++)nlens[g].push_back(lens[g][i]);nextgroup.push_back(group(words[i].back(),G));}dict.swap(ndict);lens.swap(nlens);for(auto&ts:tokens)for(auto&v:ts)v=ren[v];std::vector<std::vector<uint32_t>> cs;for(auto&ls:lens)cs.push_back(codes(ls));
 std::vector<uint8_t>o;w32(o,0x37545854);w32(o,n);w32(o,rs.size());w32(o,dict.size());w64(o,nbits);o.push_back(G);if(G>1)o.insert(o.end(),grouping,grouping+256);uint64_t acc=0;int bits=0;auto put=[&](uint32_t v,int w){acc|=uint64_t(v)<<bits;bits+=w;while(bits>=8){o.push_back(acc);acc>>=8;bits-=8;}};auto flush=[&](){if(bits)o.push_back(acc);acc=0;bits=0;};for(size_t i=0;i<dict.size();i++){int w=32-__builtin_clz(uint32_t(i+255));put(dict[i].first,w);put(dict[i].second,w);}flush();
 for(auto&ls:lens){std::vector<std::pair<unsigned,unsigned>>seq;for(size_t i=0;i<ls.size();){size_t j=i+1;while(j<ls.size()&&ls[j]==ls[i])j++;size_t run=j-i;if(run>=8){seq.push_back({ls[i],0});seq.push_back({17,unsigned(run-1)});}else for(size_t k=i;k<j;k++)seq.push_back({ls[i],0});i=j;}std::vector<uint64_t>f(18);for(auto x:seq)f[x.first]++;auto ll=makelens(f);auto lc=codes(ll);for(auto l:ll)put(l,5);flush();for(auto x:seq){put(lc[x.first],ll[x.first]);if(x.first==17){unsigned k=31-__builtin_clz(x.second);put(1u<<k,k+1);put(x.second-(1u<<k),k);}}flush();}
 std::vector<uint32_t>deltas;uint32_t delta=0,dm=0;for(size_t i=0;i<rlen.size();i++){if(i&&(i&31)==0){deltas.push_back(delta);dm=std::max(dm,delta);delta=0;}delta+=rlen[i];}int dw=dm?32-__builtin_clz(dm):0;o.push_back(dw);for(uint32_t d:deltas)put(d,dw);flush();
 size_t start=o.size();o.resize(start+(nbits+7)/8+8,0);uint64_t pos=0;
 for(auto&ts:tokens){unsigned g=0;for(uint32_t sym:ts){uint32_t c=cs[g][sym];uint64_t v=uint64_t(c)<<(pos&7);size_t byte=start+(pos>>3);o[byte]|=v;o[byte+1]|=v>>8;o[byte+2]|=v>>16;pos+=lens[g][sym];g=nextgroup[sym];}}
 return o;
}
static std::vector<uint8_t> encode_best(const uint8_t*raw,size_t n,int maxdict=32768,int minfreq=9){
 Fit trained=fit(raw,n,maxdict,minfreq);std::vector<uint8_t> best;
 for(unsigned mode:{1u,3u,5u,8u,104u,108u,116u}){
  auto candidate=encode(raw,n,maxdict,minfreq,mode,&trained);
  if(best.empty()||candidate.size()<best.size())best.swap(candidate);
 }
 return best;
}

#endif
struct Tab{uint16_t sym;uint8_t bits,pad;};
struct State{
 bool valid=false;uint32_t rawsize=0,nrows=0;unsigned G=0;uint8_t grouping[256]{};uint64_t nbits=0;const uint8_t*stream=nullptr;
 std::vector<uint64_t>rowbits;std::vector<uint32_t>dp;std::vector<uint16_t>dl;std::vector<uint8_t>dict;std::vector<Tab>tab;
 State(const uint8_t*a,size_t n){try{if(n<25||rd32(a)!=0x37545854)return;rawsize=rd32(a+4);nrows=rd32(a+8);uint32_t nd=rd32(a+12);nbits=rd64(a+16);G=a[24];if(!G||G>64||nd>65279||nrows>rawsize||nbits>8ull*(n-25)||nrows>nbits)return;uint64_t dbits=0;for(uint32_t i=256;i<nd+256;i++)dbits+=2*(32-__builtin_clz(i-1));size_t dbytes=(dbits+7)/8;size_t header=G==1?25:281;if(header+dbytes+12*G+8>n)return;if(G>1)memcpy(grouping,a+25,256);for(unsigned c=0;c<256;c++)if(grouping[c]>=G)return;if(grouping[10])return;const uint8_t*p=a+header;uint32_t ns=nd+256;dp.resize(ns);dl.resize(ns);dict.reserve(nd*20+256);for(int i=0;i<256;i++){dp[i]=dict.size();dl[i]=1;dict.push_back(i);}uint64_t pos=0;for(uint32_t i=256;i<ns;i++){int w=32-__builtin_clz(i-1);uint32_t l=(rd64(p+(pos>>3))>>(pos&7))&((1u<<w)-1);pos+=w;uint32_t r=(rd64(p+(pos>>3))>>(pos&7))&((1u<<w)-1);pos+=w;if(l>=i||r>=i||dl[l]+dl[r]>255)return;dp[i]=dict.size();dl[i]=dl[l]+dl[r];for(int j=0;j<dl[l];j++)dict.push_back(dict[dp[l]+j]);for(int j=0;j<dl[r];j++)dict.push_back(dict[dp[r]+j]);}
  p+=dbytes;tab.resize(65536*G);std::vector<Tab>meta(65536);
  for(unsigned g=0;g<G;g++){if(size_t(a+n-p)<20)return;std::vector<uint8_t>ll(18);for(unsigned i=0;i<18;i++)ll[i]=(rd64(p+(5*i/8))>>((5*i)&7))&31;p+=12;for(auto l:ll)if(l>16)return;auto mc=codes(ll);std::fill(meta.begin(),meta.end(),Tab{});for(unsigned i=0;i<18;i++)if(ll[i])for(uint32_t c=mc[i];c<65536;c+=1u<<ll[i]){if(meta[c].bits)return;meta[c]={uint16_t(i),ll[i],0};}std::vector<uint8_t>ls(ns);uint64_t lb=0;for(uint32_t i=0;i<ns;){if((lb>>3)+8>size_t(a+n-p))return;auto h=meta[(rd64(p+(lb>>3))>>(lb&7))&65535];if(!h.bits)return;lb+=h.bits;if(h.sym<17){ls[i++]=h.sym;}else{if(!i||(lb>>3)+8>size_t(a+n-p))return;uint64_t x=rd64(p+(lb>>3))>>(lb&7);if(!x)return;unsigned k=__builtin_ctzll(x);if(k>16)return;lb+=k+1;if((lb>>3)+8>size_t(a+n-p))return;unsigned run=(1u<<k)|((rd64(p+(lb>>3))>>(lb&7))&((1u<<k)-1));lb+=k;if(run>ns-i)return;unsigned v=ls[i-1];while(run--)ls[i++]=v;}}p+=(lb+7)/8;auto cs=codes(ls);for(uint32_t i=0;i<ns;i++)if(ls[i])for(uint32_t c=cs[i];c<65536;c+=1u<<ls[i]){auto&t=tab[g*65536+c];if(t.bits)return;uint8_t last=dict[dp[i]+dl[i]-1];t={uint16_t(i),ls[i],uint8_t((grouping[last]<<1)|(last==10))};}}
  uint32_t blocks=(uint64_t(nrows)+31)/32;if(p>=a+n)return;int dw=*p++;if(dw>32)return;size_t idxbytes=((uint64_t)(blocks?blocks-1:0)*dw+7)/8;if(idxbytes>size_t(a+n-p)||8>size_t(a+n-p)-idxbytes)return;rowbits.resize(blocks);uint64_t rb=0;for(uint32_t i=0;i<blocks;i++){if(i){uint64_t pb=uint64_t(i-1)*dw;rb+=(rd64(p+(pb>>3))>>(pb&7))&((1ull<<dw)-1);}if(rb>nbits)return;rowbits[i]=rb;}p+=idxbytes;if(nbits>8ull*size_t(a+n-p)||((nbits+7)/8)+8>size_t(a+n-p))return;stream=p;valid=true;
 }catch(...){valid=false;}}
};
static int64_t decode(State*s,uint8_t*out,size_t cap){if(!s||!s->valid||cap<s->rawsize)return -1;uint64_t b=0;size_t at=0;unsigned g=0;auto*t=s->tab.data();while(b<s->nbits){uint64_t x=rd64(s->stream+(b>>3))>>(b&7);Tab h=t[g*65536+(x&65535)];if(!h.bits)return -1;b+=h.bits;g=h.pad>>1;uint32_t z=s->dl[h.sym];if(z>cap-at)return -1;memcpy(out+at,s->dict.data()+s->dp[h.sym],z);at+=z;}return b==s->nbits&&at==s->rawsize?at:-1;}
static int64_t row_seek(State*s,uint32_t r,uint8_t*out,size_t cap,uint64_t&b,uint32_t&current){if(!s||!s->valid||r>=s->nrows)return -1;auto*t=s->tab.data();unsigned g=0;uint32_t base=r&~uint32_t(31);if(current>r||current<base){current=base;b=s->rowbits[r>>5];}while(current<r){if(b>=s->nbits)return -1;Tab h=t[g*65536+((rd64(s->stream+(b>>3))>>(b&7))&65535)];if(!h.bits)return -1;b+=h.bits;g=h.pad>>1;current+=h.pad&1;}size_t at=0;for(;;){if(b>=s->nbits)return -1;Tab h=t[g*65536+((rd64(s->stream+(b>>3))>>(b&7))&65535)];if(!h.bits)return -1;b+=h.bits;g=h.pad>>1;uint32_t z=s->dl[h.sym];if(z>cap-at)return -1;memcpy(out+at,s->dict.data()+s->dp[h.sym],z);at+=z;if((h.pad&1)||b==s->nbits)break;}if(b>s->nbits)return -1;current++;return at;}
static int64_t rows(State*s,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offs){if(!s||!s->valid||!offs)return -1;offs[0]=0;size_t at=0;uint64_t b=0;uint32_t current=~uint32_t(0);for(size_t i=0;i<count;i++){if(ids[i]>=s->nrows||(i&&ids[i]<ids[i-1]))return -1;auto n=row_seek(s,ids[i],out+at,cap-at,b,current);if(n<0)return -1;at+=n;offs[i+1]=at;}return at;}
static int64_t row(State*s,uint32_t id,uint8_t*out,size_t cap){uint64_t r=id,o[2];return rows(s,&r,1,out,cap,o);}
}
