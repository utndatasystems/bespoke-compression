#pragma once
#include "numeric.h"
#ifdef ENCODER
#include <algorithm>
#endif
namespace numgrouphuff {
static inline uint32_t reverse_bits(uint32_t x,unsigned n){uint32_t r=0;while(n--){r=(r<<1)|(x&1);x>>=1;}return r;}
template<unsigned N,unsigned B,typename T> static inline bool make_table(const uint8_t*lengths,T*table){
 uint32_t counts[B+1]={},next[B+1]={};
 for(unsigned s=0;s<N;++s){if(!lengths[s]||lengths[s]>B)return false;++counts[lengths[s]];}
 unsigned code=0;for(unsigned i=1;i<=B;++i){code=(code+counts[i-1])*2;next[i]=code;if(code+counts[i]>(1u<<i))return false;}
 if(code+counts[B]!=(1u<<B))return false;
 for(unsigned s=0;s<N;++s){unsigned len=lengths[s],v=reverse_bits(next[len]++,len);T entry=T(s)|(T(len)<<(sizeof(T)==4?16:8));for(unsigned i=v;i<(1u<<B);i+=1u<<len)table[i]=entry;}return true;
}
struct State {
 const uint8_t *table=nullptr,*stream=nullptr,*lat_tail=nullptr,*lon_tail=nullptr,*reviews=nullptr,*review_cur=nullptr;
 uint32_t lat_idx=0,lon_idx=0,n=0,groups=0,cur_lat=0,cur_lon=0,cur_meta=0,lat_count=0,lon_count=0;
 bool bad=false;size_t stream_limit=0,review_limit=0;
 size_t bitpos=0,review_bitpos=0;uint16_t review_table[8192];uint16_t meta_table[256];
 bool open(const uint8_t*p,size_t len,size_t rows){
  if(len<24+274)return false;
  n=numcodec::rd32(p);groups=numcodec::rd32(p+4);const uint32_t nl=numcodec::rd32(p+8),no=numcodec::rd32(p+12),nr=numcodec::rd32(p+16),bs=numcodec::rd32(p+20);
  if(n!=rows||!groups||groups>rows||nl>n||no>n||nr<16)return false;
  const size_t ll=((size_t)nl*10+7)/8+8,lo=((size_t)no*10+7)/8+8;
  if(24+274+(size_t)groups*10+bs+16+ll+lo+nr!=len)return false;
  if(!make_table<256,13>(p+24+18,review_table)||!make_table<18,8>(p+24,meta_table))return false;
  table=p+24+274;stream=table+(size_t)groups*10;lat_tail=stream+bs+16;lon_tail=lat_tail+ll;reviews=lon_tail+lo;lat_count=nl;lon_count=no;stream_limit=size_t(bs)*8;review_limit=size_t(nr-16)*8;
  for(uint32_t g=0;g<groups;++g){const uint8_t*t=table+size_t(g)*10;unsigned a=t[8],b=t[9];uint64_t al=numcodec::rd32(t),bl=numcodec::rd32(t+4);if(a>31||b>31||a+b+7>64||al<270000000||al>540000000||bl<740000000||bl>1210000000||al+((uint64_t(1)<<a)-1)>=2000000000||bl+((uint64_t(1)<<b)-1)>=2000000000)return false;}
  reset();return true;
 }
 void reset(){lat_idx=lon_idx=0;review_cur=reviews;bitpos=review_bitpos=0;bad=false;}
 inline void prepare(size_t row,uint32_t group){
  if(row==0)reset();
  if(group>=groups||bitpos>stream_limit){bad=true;cur_lat=cur_lon=270000000;cur_meta=0;return;}
  const uint8_t*t=table+(size_t)group*10;
  const uint32_t a=t[8],b=t[9];
  const uint32_t shift=bitpos&7;const uint8_t*p=stream+(bitpos>>3);
  uint64_t v=numcodec::rd64(p)>>shift;
  const uint16_t me=meta_table[v&255];const uint32_t ml=me>>8,width=a+b+2+ml;
  if(width>64||bitpos+width>stream_limit){bad=true;cur_lat=cur_lon=270000000;cur_meta=0;return;}
  if(shift+width>64)v|=uint64_t(p[8])<<(64-shift);
  bitpos+=width;v>>=ml;
  cur_lat=numcodec::rd32(t)+(v&((uint64_t(1)<<a)-1));v>>=a;
  cur_lon=numcodec::rd32(t+4)+(v&((uint64_t(1)<<b)-1));v>>=b;
  cur_meta=(v&3)|((me&255)<<2);
 }
 static inline uint32_t get_tail(const uint8_t*p,uint32_t idx){return numcodec::State::get_tail(p,idx);}
 inline uint8_t*emit_lat(size_t row,uint8_t*p){uint32_t tail=0;if(cur_meta&1){if(lat_idx>=lat_count){bad=true;return p;}tail=get_tail(lat_tail,lat_idx++);}return numcodec::coordinate(p,cur_lat,tail,false);}
 inline uint8_t*emit_lon(size_t row,uint8_t*p){uint32_t tail=0;if(cur_meta&2){if(lon_idx>=lon_count){bad=true;return p;}tail=get_tail(lon_tail,lon_idx++);}return numcodec::coordinate(p,cur_lon,tail,true);}
 inline uint8_t*emit_stars(size_t row,uint8_t*p){uint32_t m=cur_meta>>3;p[0]='1'+m/2;p[1]='.';p[2]=(m&1)?'5':'0';return p+3;}
 inline uint8_t*emit_review(size_t row,uint8_t*p){
  if(review_bitpos>review_limit){bad=true;return p;}
  uint64_t v=numcodec::rd64(reviews+(review_bitpos>>3))>>(review_bitpos&7);uint32_t e=review_table[v&8191],x=e&255,l=e>>8;review_bitpos+=l;
  if(x==255){x=(v>>l)&8191;review_bitpos+=13;}else x+=5;if(review_bitpos>review_limit){bad=true;return p;}return numcodec::digits(p,x);
 }
 inline uint8_t*emit_open(size_t row,uint8_t*p){*p++='0'+((cur_meta>>2)&1);return p;}
};
#ifdef ENCODER
struct HEnc {std::vector<uint8_t>len;std::vector<uint32_t>code;};
static inline HEnc make_huffman(const std::vector<uint64_t>&freq){
 struct Node{uint64_t f;int a,b,s;};std::vector<Node>tree;std::vector<int>active;const unsigned n=freq.size();
 for(unsigned i=0;i<n;++i){tree.push_back({freq[i],-1,-1,int(i)});active.push_back(i);}
 while(active.size()>1){
  std::sort(active.begin(),active.end(),[&](int a,int b){return tree[a].f!=tree[b].f?tree[a].f>tree[b].f:a>b;});
  int a=active.back();active.pop_back();int b=active.back();active.pop_back();active.push_back(tree.size());tree.push_back({tree[a].f+tree[b].f,a,b,-1});
 }
 HEnc out;out.len.resize(n);out.code.resize(n);std::vector<std::pair<int,unsigned>>todo={{active[0],0}};
 while(!todo.empty()){auto v=todo.back();todo.pop_back();const auto&t=tree[v.first];if(t.s>=0)out.len[t.s]=v.second;else{todo.push_back({t.a,v.second+1});todo.push_back({t.b,v.second+1});}}
 uint32_t counts[32]={},next[32]={};for(auto l:out.len){if(l>=32)throw std::runtime_error("huff lengths");counts[l]++;}
 unsigned code=0;for(unsigned i=1;i<32;++i){code=(code+counts[i-1])*2;next[i]=code;}
 for(unsigned i=0;i<n;++i)out.code[i]=reverse_bits(next[out.len[i]]++,out.len[i]);return out;
}
static inline void emit_bits(std::vector<uint8_t>&dst,size_t&pos,uint64_t v,unsigned bits){__uint128_t word=(__uint128_t)v<<(pos&7);size_t off=pos>>3;for(unsigned j=0;j<9;++j)dst[off+j]|=word>>(j*8);pos+=bits;}
struct GroupEnc {uint32_t al=UINT32_MAX,ah=0,bl=UINT32_MAX,bh=0,a=0,b=0;};
static inline std::vector<uint8_t> encode(const std::vector<std::string>&lat,const std::vector<std::string>&lon,const std::vector<std::string>&stars,const std::vector<std::string>&review,const std::vector<std::string>&isopen,const std::vector<uint32_t>&groupid){
 const size_t n=lat.size();std::vector<uint16_t>lt,lo;
 std::vector<uint64_t>mf(18),rf(256);std::vector<unsigned>mvals(n),rvals(n);std::vector<uint8_t>rsyms(n);
 for(size_t i=0;i<n;++i){unsigned st=(stars[i][0]-'1')*2+(stars[i][2]=='5'),op=isopen[i][0]-'0',r=std::stoul(review[i]);if(st>8||op>1||r<5||r>8191)throw std::runtime_error("meta range");mvals[i]=(st<<1)|op;rvals[i]=r;rsyms[i]=r<260?r-5:255;mf[mvals[i]]++;rf[rsyms[i]]++;}
 HEnc mh=make_huffman(mf),rh=make_huffman(rf);for(auto l:mh.len)if(l>8)throw std::runtime_error("meta huff");for(auto l:rh.len)if(l>13)throw std::runtime_error("review huff");
 size_t rvbits=0;for(size_t i=0;i<n;++i)rvbits+=rh.len[rsyms[i]]+(rsyms[i]==255?13:0);std::vector<uint8_t>rv((rvbits+7)/8+16,0);size_t rpos=0;
 for(size_t i=0;i<n;++i){emit_bits(rv,rpos,rh.code[rsyms[i]],rh.len[rsyms[i]]);if(rsyms[i]==255)emit_bits(rv,rpos,rvals[i],13);}
 std::vector<uint64_t>av(n),bv(n);uint32_t ng=0;for(auto g:groupid)if(g>=ng)ng=g+1;std::vector<GroupEnc>groups(ng);
 for(size_t i=0;i<n;++i){av[i]=numcodec::decimal(lat[i]);bv[i]=numcodec::decimal(lon[i]);auto&g=groups[groupid[i]];uint32_t a=av[i]/1000,b=bv[i]/1000;g.al=std::min(g.al,a);g.ah=std::max(g.ah,a);g.bl=std::min(g.bl,b);g.bh=std::max(g.bh,b);}
 for(auto&g:groups){uint32_t a=g.ah-g.al,b=g.bh-g.bl;g.a=a?32-__builtin_clz(a):0;g.b=b?32-__builtin_clz(b):0;if(g.a+g.b+7>64)throw std::runtime_error("group bit range");}
 size_t nbits=0;for(size_t i=0;i<n;++i){auto id=groupid[i];nbits+=groups[id].a+groups[id].b+2+mh.len[mvals[i]];}std::vector<uint8_t>packed((nbits+7)/8+16,0);size_t pos=0;
 for(size_t i=0;i<n;++i){
  uint64_t a=av[i],b=bv[i];uint32_t at=a%1000,bt=b%1000;const auto&g=groups[groupid[i]];
  if(at)lt.push_back(at);if(bt)lo.push_back(bt);
  uint32_t st=(stars[i][0]-'1')*2+(stars[i][2]=='5');uint32_t op=isopen[i][0]-'0';
  if(st>8||op>1||lat[i][0]=='-'||lon[i][0]!='-')throw std::runtime_error("metadata range");
  uint32_t flags=(at!=0)|((bt!=0)<<1);unsigned ml=mh.len[mvals[i]];
  uint64_t v=(a/1000-g.al)|((b/1000-g.bl)<<g.a)|(uint64_t(flags)<<(g.a+g.b));
  emit_bits(packed,pos,mh.code[mvals[i]],ml);emit_bits(packed,pos,v,g.a+g.b+2);
 }
 auto ltp=numcodec::packed_tails(lt),lop=numcodec::packed_tails(lo);std::vector<uint8_t>out;out.reserve(24+274+ng*10+packed.size()+ltp.size()+lop.size()+rv.size());
 numcodec::append32(out,n);numcodec::append32(out,ng);numcodec::append32(out,lt.size());numcodec::append32(out,lo.size());numcodec::append32(out,rv.size());numcodec::append32(out,packed.size()-16);
 out.insert(out.end(),mh.len.begin(),mh.len.end());out.insert(out.end(),rh.len.begin(),rh.len.end());
 for(const auto&g:groups){numcodec::append32(out,g.al);numcodec::append32(out,g.bl);out.push_back(g.a);out.push_back(g.b);}
 out.insert(out.end(),packed.begin(),packed.end());out.insert(out.end(),ltp.begin(),ltp.end());out.insert(out.end(),lop.begin(),lop.end());out.insert(out.end(),rv.begin(),rv.end());return out;
}
#endif
}
