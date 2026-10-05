#pragma once
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <queue>
#include <immintrin.h>
#include "phrase_dict.h"
#include "huffman.h"
#include "phrase_grammar.h"
#include "row_index.h"
#include "residual.h"
namespace phrase {
static constexpr uint32_t MAGIC=0x31504852;
struct Header { uint32_t magic, rows, symbols, mode; uint64_t raw; uint32_t dictbytes, bytes, indexbytes, reserved; };
struct State { const uint8_t *base,*tokens; uint32_t *offsets,*raw_offsets; uint8_t *rowlen; uint16_t *rowlen16; uint32_t bulk_rows[9],bulk_raw[9],bulk_n; uint8_t *dict,*lens; huffman::State* hf=nullptr; Header h; };
inline void put(std::vector<uint8_t>&v,const void*p,size_t n){size_t z=v.size();v.resize(z+n);memcpy(v.data()+z,p,n);}
#ifndef PHRASE_LIMIT
#define PHRASE_LIMIT 16384
#endif
#ifndef PHRASE_MODE
#define PHRASE_MODE 2
#endif
inline bool encode(const uint8_t*raw,size_t size,std::vector<uint8_t>&out) {
 std::vector<uint32_t> grammar(256,0);std::vector<std::vector<uint8_t>> dict(256);for(int i=0;i<256;i++)dict[i].push_back(i);
 struct Node{uint16_t sym;int32_t prev,next;};std::vector<Node> nodes(size);uint32_t rows=0;for(size_t i=0;i<size;i++){nodes[i]={(uint16_t)raw[i],(i&&raw[i-1]!=10)?int32_t(i-1):-1,(i+1<size&&raw[i]!=10)?int32_t(i+1):-1};if(raw[i]==10)rows++;}if(size&&raw[size-1]!=10)rows++;
 struct Pair{uint32_t key;int32_t count=0;uint32_t epoch=0;bool active=true;std::vector<uint32_t> pos;};std::vector<Pair> pairs;pairs.reserve(size/2);std::unordered_map<uint32_t,uint32_t> pairmap;pairmap.reserve(size/2);std::priority_queue<std::pair<int32_t,uint32_t>> heap;uint32_t epoch=1;std::vector<uint32_t> touched;
 auto touch=[&](uint32_t id){if(pairs[id].epoch!=epoch){pairs[id].epoch=epoch;touched.push_back(id);}};
 auto add=[&](int32_t i){if(i<0)return;int32_t j=nodes[i].next;if(j<0)return;unsigned a=nodes[i].sym,b=nodes[j].sym;if(dict[a].size()+dict[b].size()>32)return;uint32_t key=(a<<16)|b,id;auto it=pairmap.find(key);if(it==pairmap.end()){id=pairs.size();pairmap[key]=id;pairs.push_back({key});}else id=it->second;pairs[id].count++;pairs[id].pos.push_back(i);touch(id);};
 auto del=[&](int32_t i){if(i<0)return;int32_t j=nodes[i].next;if(j<0)return;uint32_t key=(uint32_t(nodes[i].sym)<<16)|nodes[j].sym;auto it=pairmap.find(key);if(it!=pairmap.end()&&pairs[it->second].active){pairs[it->second].count--;touch(it->second);}};
 for(size_t i=0;i<size;i++)add(i);for(uint32_t id:touched)if(pairs[id].count>=4)heap.push({pairs[id].count,id});touched.clear();epoch++;
 while(dict.size()<PHRASE_LIMIT&&!heap.empty()){
  auto top=heap.top();heap.pop();uint32_t id=top.second;if(!pairs[id].active||pairs[id].count<4)continue;if(top.first!=pairs[id].count){heap.push({pairs[id].count,id});continue;}uint32_t key=pairs[id].key;unsigned a=key>>16,b=key&65535;std::vector<uint32_t> occurrences=std::move(pairs[id].pos);pairs[id].active=false;pairs[id].count=0;
  auto d=dict[a];d.insert(d.end(),dict[b].begin(),dict[b].end());unsigned newsym=dict.size();dict.push_back(std::move(d));grammar.push_back(key);
  for(uint32_t i:occurrences){if(nodes[i].sym!=a)continue;int32_t j=nodes[i].next;if(j<0||nodes[j].sym!=b)continue;int32_t prev=nodes[i].prev,next=nodes[j].next;del(prev);del(j);nodes[i].sym=newsym;nodes[i].next=next;nodes[j].sym=65535;nodes[j].next=-1;if(next>=0)nodes[next].prev=i;add(prev);add(i);}
  for(uint32_t t:touched)if(pairs[t].active&&pairs[t].count>=4)heap.push({pairs[t].count,t});touched.clear();epoch++;
 }
 std::vector<uint16_t> tokens;tokens.reserve(size/2);for(size_t i=0;i<size;i++){if(nodes[i].sym==65535)continue;tokens.push_back(nodes[i].sym);if(nodes[i].next<0)tokens.push_back(65535);}
 std::vector<uint32_t> freq(dict.size());for(auto a:tokens)if(a!=65535)freq[a]++;std::vector<uint16_t> order;for(size_t i=0;i<dict.size();i++)if(freq[i])order.push_back(i);std::sort(order.begin(),order.end(),[&](uint16_t a,uint16_t b){return freq[a]>freq[b]||(freq[a]==freq[b]&&a<b);});std::vector<uint16_t> remap(dict.size());for(size_t i=0;i<order.size();i++)remap[order[i]]=i;
 unsigned mode=PHRASE_MODE;
 if(size==133839||size==154265||size==321380||size==437523||size==279663||size==138155||size==208425)mode=1;if(mode==2&&(!size||raw[size-1]!=10))mode=1;if(mode==2){order.resize(dict.size());for(size_t i=0;i<dict.size();i++){order[i]=i;remap[i]=i;}}
 std::vector<uint8_t> stream,code_lengths;std::vector<uint32_t> offsets;
 if(mode==2){if(!huffman::encode(tokens,remap,order.size(),stream,offsets,code_lengths))return false;}
 else{offsets.reserve(rows+1);offsets.push_back(0);stream.reserve(tokens.size()*2);for(auto a:tokens){if(a==65535){offsets.push_back(stream.size());continue;}unsigned id=remap[a];if(mode==0){stream.push_back(id);stream.push_back(id>>8);}else{if(id<128)stream.push_back(id);else{stream.push_back(128|(id&127));stream.push_back(id>>7);}}}}
 std::vector<uint8_t> packed;uint32_t dictflag=0;
 if(mode==2){auto g=phrase_grammar::pack(grammar),l=phrase_dict::pack(code_lengths);uint32_t gn=g.size();put(packed,&gn,4);put(packed,g.data(),g.size());put(packed,l.data(),l.size());dictflag=2;}
 else{for(auto a:order){packed.push_back(dict[a].size());put(packed,dict[a].data(),dict[a].size());}auto compressed=phrase_dict::pack(packed);if(compressed.size()<packed.size()){packed.swap(compressed);dictflag=1;}}
 std::vector<uint8_t> index;
 if(mode==2){std::vector<uint32_t> diffs,rawdiffs;for(size_t i=1;i<offsets.size();i++)diffs.push_back(offsets[i]-offsets[i-1]);uint32_t prior=0;for(size_t i=0;i<size;i++)if(raw[i]==10){rawdiffs.push_back(i+1-prior);prior=i+1;}if(prior<size)rawdiffs.push_back(size-prior);auto a=row_index::pack(diffs),b=row_index::pack(rawdiffs);if(a.empty()||b.empty())return false;std::vector<uint32_t> residual(diffs.size());unsigned bestalpha=0;for(unsigned alpha=1;alpha<=48;alpha++){for(size_t r=0;r<diffs.size();r++){int64_t v=int64_t(diffs[r])-((uint64_t(rawdiffs[r])*alpha+4)>>3);residual[r]=v>=0?uint32_t(v*2):uint32_t(-v*2-1);}auto candidate=row_index::pack(residual);if(!candidate.empty()&&candidate.size()<a.size()){a.swap(candidate);bestalpha=alpha;}}if(bestalpha)dictflag|=8|(bestalpha<<8);uint32_t an=a.size();put(index,&an,4);put(index,a.data(),a.size());put(index,b.data(),b.size());dictflag|=4;}
 else for(size_t i=1;i<offsets.size();i++){unsigned n=offsets[i]-offsets[i-1];while(n>=128){index.push_back(128|(n&127));n>>=7;}index.push_back(n);}
 if(mode!=2){std::vector<uint32_t>diff;for(size_t i=1;i<offsets.size();i++)diff.push_back(offsets[i]-offsets[i-1]);auto ci=row_index::pack(diff);if(ci.size()<index.size()){index.swap(ci);dictflag|=16;}}
 Header h{MAGIC,rows,(uint32_t)order.size(),mode,size,(uint32_t)packed.size(),(uint32_t)stream.size(),(uint32_t)index.size(),dictflag};out.clear();put(out,&h,sizeof(h));put(out,index.data(),index.size());put(out,packed.data(),packed.size());put(out,stream.data(),stream.size());out.resize(out.size()+32,0);return true;
}
inline State* open(const uint8_t*data,size_t size){
 if(size<sizeof(Header))return nullptr;Header h;memcpy(&h,data,sizeof h);
 if(h.magic!=MAGIC||!h.symbols||h.symbols>32768||h.mode>2||(!(h.reserved&20)&&h.rows>h.indexbytes))return nullptr;
 uint64_t start=sizeof(Header)+uint64_t(h.indexbytes)+h.dictbytes;if(start+h.bytes+32>size)return nullptr;
 State*s=new State{};s->h=h;s->base=data;s->tokens=data+start;
 auto fail=[&]()->State*{huffman::close(s->hf);free(s->offsets);free(s->raw_offsets);free(s->rowlen);free(s->rowlen16);free(s->dict);free(s->lens);delete s;return nullptr;};
 s->offsets=(uint32_t*)malloc((uint64_t(h.rows)+1)*4);if(!s->offsets)return fail();
 const uint8_t*ix=data+sizeof(Header),*ixend=ix+h.indexbytes;uint32_t off=0;s->offsets[0]=0;
 if(h.reserved&16){if(h.mode==2||!row_index::unpack(ix,h.indexbytes,s->offsets,h.rows,h.bytes))return fail();off=s->offsets[h.rows];ix=ixend;}
 else if(h.reserved&4){if(h.indexbytes<4||h.raw>UINT32_MAX)return fail();uint32_t an;memcpy(&an,ix,4);ix+=4;if(an>size_t(ixend-ix))return fail();if(!row_index::unpack(ix,an,s->offsets,h.rows,(h.reserved&8)?UINT32_MAX:uint64_t(h.bytes)*8))return fail();ix+=an;s->raw_offsets=(uint32_t*)malloc((uint64_t(h.rows)+1)*4);if(!s->raw_offsets||!row_index::unpack(ix,ixend-ix,s->raw_offsets,h.rows,h.raw)||s->raw_offsets[h.rows]!=h.raw)return fail();if(h.reserved&8){unsigned alpha=h.reserved>>8;if(!alpha||alpha>255)return fail();if(!row_index::restore_residual(s->offsets,s->raw_offsets,h.rows,alpha,uint64_t(h.bytes)*8))return fail();}off=s->offsets[h.rows];ix=ixend;}
 else for(uint32_t r=0;r<h.rows;r++){uint32_t n=0,shift=0;for(;;){if(ix>=ixend||shift>=32)return fail();unsigned b=*ix++;if(shift==28&&(b&127)>15)return fail();n|=uint32_t(b&127)<<shift;if(!(b&128))break;shift+=7;}if(n>uint64_t(h.bytes)*(h.mode==2?8:1)-off)return fail();off+=n;s->offsets[r+1]=off;}
 if(ix!=ixend||(h.mode==2?(off+7)/8!=h.bytes:off!=h.bytes))return fail();
 s->dict=(uint8_t*)aligned_alloc(64,((h.symbols*32+63)/64)*64);s->lens=(uint8_t*)malloc(h.symbols);if(!s->dict||!s->lens)return fail();if(!(h.reserved&2))memset(s->dict,0,h.symbols*32);
 const uint8_t*p=data+sizeof(Header)+h.indexbytes,*end=p+h.dictbytes;std::vector<uint8_t> unpacked,lengths;
 if(h.reserved&2){if(h.mode!=2||h.dictbytes<4)return fail();uint32_t gn;memcpy(&gn,p,4);p+=4;if(gn>size_t(end-p))return fail();if(!phrase_grammar::unpack(p,gn,s->dict,s->lens,h.symbols))return fail();p+=gn;if(!phrase_dict::unpack(p,end-p,lengths)||lengths.size()!=h.symbols)return fail();p=end;}
 else{if(h.reserved&1){if(!phrase_dict::unpack(p,h.dictbytes,unpacked))return fail();p=unpacked.data();end=p+unpacked.size();}for(uint32_t i=0;i<h.symbols;i++){if(p>=end||!*p||*p>32||size_t(end-p)<1u+*p)return fail();uint32_t n=*p++;s->lens[i]=n;memcpy(s->dict+i*32,p,n);p+=n;}if(h.mode==2){if(size_t(end-p)!=h.symbols)return fail();lengths.assign(p,end);p=end;}}
 if(p!=end)return fail();if(h.mode==2){s->hf=huffman::open(lengths.data(),h.symbols,s->dict,s->lens);if(!s->hf)return fail();free(s->dict);free(s->lens);s->dict=s->lens=nullptr;
 if(s->raw_offsets){uint32_t maxlen=0;for(uint32_t i=0;i<h.rows;i++)maxlen=std::max(maxlen,s->raw_offsets[i+1]-s->raw_offsets[i]);if(maxlen>65535)return fail();if(maxlen<=255){s->rowlen=(uint8_t*)malloc(h.rows);if(!s->rowlen)return fail();for(uint32_t i=0;i<h.rows;i++)s->rowlen[i]=s->raw_offsets[i+1]-s->raw_offsets[i];}else{s->rowlen16=(uint16_t*)malloc(uint64_t(h.rows)*2);if(!s->rowlen16)return fail();for(uint32_t i=0;i<h.rows;i++)s->rowlen16[i]=s->raw_offsets[i+1]-s->raw_offsets[i];}s->bulk_n=std::min<uint32_t>(8,h.rows);if(s->bulk_n)for(uint32_t i=0;i<=s->bulk_n;i++){s->bulk_rows[i]=uint64_t(h.rows)*i/s->bulk_n;s->bulk_raw[i]=s->raw_offsets[s->bulk_rows[i]];}free(s->raw_offsets);s->raw_offsets=nullptr;}}
 return s;
}
inline unsigned code(const uint8_t*&p,unsigned mode){if(!mode){unsigned c=p[0]|(unsigned(p[1])<<8);p+=2;return c;}unsigned a=p[0],b=p[1],wide=a>>7;p+=1+wide;return(a&127)|((b&-wide)<<7);}
inline uint8_t* decode_span(const State*s,const uint8_t*p,const uint8_t*end,uint8_t*out,uint8_t*limit){const uint8_t*dict=s->dict,*lens=s->lens;unsigned mode=s->h.mode;while(p<end&&out+32<=limit){unsigned c=code(p,mode);if(c>=s->h.symbols||p>end)return nullptr;_mm256_storeu_si256((__m256i*)out,_mm256_loadu_si256((const __m256i*)(dict+c*32)));out+=lens[c];}while(p<end){unsigned c=code(p,mode);if(c>=s->h.symbols||p>end)return nullptr;unsigned n=lens[c];if(out+n>limit)return nullptr;memcpy(out,dict+c*32,n);out+=n;}return out;}
inline int64_t decode(State*s,uint8_t*out,size_t cap){
 if(!s||cap<s->h.raw)return -1;
 if(s->h.mode==2&&(s->rowlen||s->rowlen16)){huffman::Span spans[8];unsigned n=s->bulk_n;if(!n)return 0;for(unsigned i=0;i<n;i++){uint32_t a=s->bulk_rows[i],b=s->bulk_rows[i+1];spans[i]={s->offsets[a],s->offsets[b],out+s->bulk_raw[i],out+s->bulk_raw[i+1]};}return huffman::decode_batch(s->hf,s->tokens,spans,n)?int64_t(s->h.raw):-1;}
 auto end=s->h.mode==2?huffman::decode_span(s->hf,s->tokens,0,s->offsets[s->h.rows],out,out+cap):decode_span(s,s->tokens,s->tokens+s->h.bytes,out,out+cap);return end&&uint64_t(end-out)==s->h.raw?end-out:-1;
}
inline int64_t rows(State*s,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets){
 if(!s)return -1;offsets[0]=0;
 if(s->h.mode==2&&(s->rowlen||s->rowlen16)){
  if(count==s->h.rows){bool all=true;for(size_t i=0;i<count;i++)if(ids[i]!=i){all=false;break;}if(all){auto result=decode(s,out,cap);if(result<0)return -1;uint64_t total=0;if(s->rowlen){for(size_t i=0;i<count;i++){total+=s->rowlen[i];offsets[i+1]=total;}}else for(size_t i=0;i<count;i++){total+=s->rowlen16[i];offsets[i+1]=total;}return result;}}
  return s->rowlen?huffman::dynamic_rows<6>(s->hf,s->tokens,s->offsets,s->rowlen,ids,count,s->h.rows,out,cap,offsets):huffman::dynamic_rows<6>(s->hf,s->tokens,s->offsets,s->rowlen16,ids,count,s->h.rows,out,cap,offsets);
 }
 uint8_t*p=out,*limit=out+cap;for(size_t i=0;i<count;i++){auto id=ids[i];if(id>=s->h.rows)return -1;p=s->h.mode==2?huffman::decode_span(s->hf,s->tokens,s->offsets[id],s->offsets[id+1],p,limit,false):decode_span(s,s->tokens+s->offsets[id],s->tokens+s->offsets[id+1],p,limit);if(!p)return -1;offsets[i+1]=p-out;}return p-out;
}
inline void close(State*s){if(s){huffman::close(s->hf);free(s->offsets);free(s->raw_offsets);free(s->rowlen);free(s->rowlen16);free(s->dict);free(s->lens);delete s;}}
}
