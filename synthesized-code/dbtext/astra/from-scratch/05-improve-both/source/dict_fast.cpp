#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <vector>
#include <algorithm>
#include <immintrin.h>
#include "defpack.h"
#ifdef ENCODER
#include <unordered_map>
#include <queue>
#include <stdio.h>
#endif
static uint16_t rd16(const void* p){uint16_t x;memcpy(&x,p,2);return x;}
static uint32_t rd32(const void* p){uint32_t x;memcpy(&x,p,4);return x;}
static uint64_t rd64(const void* p){uint64_t x;memcpy(&x,p,8);return x;}
static void wr16(void* p,uint16_t x){memcpy(p,&x,2);}
static void wr32(void* p,uint32_t x){memcpy(p,&x,4);}
static void wr64(void* p,uint64_t x){memcpy(p,&x,8);}
static constexpr uint32_t MAGIC=0x31544344;
struct DH { uint32_t magic; uint16_t alphabet,ndict; uint8_t bits,reserved[7]; uint64_t rawsize; uint32_t rows,tokens,extra; uint32_t padding;};
static_assert(sizeof(DH)==40,"header");
#ifdef ENCODER
struct Node {int32_t prev,next; uint16_t sym; uint8_t len,end;};
struct Pair {uint32_t key,count;std::vector<int32_t> occ;bool dirty=false;};
struct QueueEntry {uint32_t count,id;bool operator<(const QueueEntry& b)const{return count!=b.count?count<b.count:id>b.id;}};
struct DictTrainer {
 const uint8_t* raw;size_t n;unsigned maxphrase=16;std::vector<Node> nodes;std::vector<uint16_t> left,right;std::vector<uint8_t> lens,alphabet;std::vector<Pair> pairs;std::unordered_map<uint32_t,uint32_t> map;std::priority_queue<QueueEntry> pq;std::vector<uint32_t> dirty;std::vector<uint16_t> bestcodes,bestcounts; uint32_t ntokens,rows=0,bestdict=0,bestbits=0;size_t bestsize=~size_t(0);
 DictTrainer(const uint8_t* p,size_t z):raw(p),n(z),ntokens(z){
  bool used[256]={};for(size_t i=0;i<n;i++)used[p[i]]=1;
  uint16_t remap[256]={};for(int i=0;i<256;i++)if(used[i]){remap[i]=alphabet.size();alphabet.push_back(i);left.push_back(i);right.push_back(0);lens.push_back(1);}
  if(alphabet.size()<=40&&n>1000000)maxphrase=32;
  nodes.resize(n);for(size_t i=0;i<n;i++){bool end=p[i]==10||i+1==n; nodes[i]={(int32_t)i-1,i+1<n?(int32_t)i+1:-1,remap[p[i]],1,(uint8_t)end};if(end)rows++;}
  map.reserve(n/4+1024);
  for(size_t i=0;i+1<n;i++)if(!nodes[i].end)add(i);
  for(uint32_t id:dirty){pairs[id].dirty=false;pq.push({pairs[id].count,id});}dirty.clear();
 }
 uint32_t get(uint32_t key){auto it=map.find(key);if(it!=map.end())return it->second;uint32_t id=pairs.size();map.emplace(key,id);pairs.push_back({key,0,{}});return id;}
 void touch(uint32_t id){if(!pairs[id].dirty){pairs[id].dirty=true;dirty.push_back(id);}}
 void add(int32_t x){if(x<0)return;Node& a=nodes[x];int32_t y=a.next;if(y<0||a.end||a.len+nodes[y].len>maxphrase)return;uint32_t id=get((uint32_t(a.sym)<<16)|nodes[y].sym);pairs[id].count++;pairs[id].occ.push_back(x);touch(id);}
 void sub(int32_t x){if(x<0)return;Node& a=nodes[x];int32_t y=a.next;if(y<0||a.end||a.len+nodes[y].len>maxphrase)return;uint32_t id=map.find((uint32_t(a.sym)<<16)|nodes[y].sym)->second;if(pairs[id].count)pairs[id].count--;touch(id);}
 struct TrieNode {uint32_t child[256]{};uint16_t code=65535;};
 void snapshot(unsigned bits){
  std::vector<std::vector<uint8_t>> words(lens.size());
  for(size_t i=0;i<alphabet.size();i++)words[i].push_back(alphabet[i]);
  for(size_t i=alphabet.size();i<lens.size();i++){words[i]=words[left[i]];words[i].insert(words[i].end(),words[right[i]].begin(),words[right[i]].end());}
  std::vector<TrieNode> trie(1);
  for(uint32_t i=0;i<words.size();i++){uint32_t node=0;for(uint8_t b:words[i]){uint32_t next=trie[node].child[b];if(!next){next=trie.size();trie[node].child[b]=next;trie.emplace_back();}node=next;}trie[node].code=i;}
  std::vector<uint16_t> codes,counts;codes.reserve(ntokens);counts.reserve(rows);
  size_t start=0;
  while(start<n){size_t stop=start;while(stop<n&&raw[stop]!=10)stop++;if(stop<n)stop++;
   size_t nn=stop-start;std::vector<uint32_t> dp(nn+1,~0u),choice(nn);dp[nn]=0;
   for(size_t i=nn;i--;){uint32_t node=0;for(size_t j=i;j<nn&&j<i+maxphrase;j++){node=trie[node].child[raw[start+j]];if(!node)break;uint16_t code=trie[node].code;if(code!=65535&&dp[j+1]+1<=dp[i]){dp[i]=dp[j+1]+1;choice[i]=code;}}}
   counts.push_back(dp[0]);for(size_t i=0;i<nn;){unsigned code=choice[i];codes.push_back(code);i+=lens[code];}start=stop;
  }
  size_t extra=0,rc=0;
  for(auto count:counts)if(count>=15)extra++;
  size_t size=sizeof(DH)+alphabet.size()+defpack::size(alphabet.size(),lens.size())+(rows+1)/2+extra*2+(uint64_t(codes.size())*bits+7)/8+8;
  if(size>=bestsize)return;
  bestsize=size;bestdict=lens.size();bestbits=bits;bestcodes=std::move(codes);bestcounts=std::move(counts);
 }
 void train(){
  unsigned firstbits=1;while((1u<<firstbits)<lens.size())firstbits++;snapshot(firstbits);
  while(lens.size()<65535&&!pq.empty()){
   QueueEntry e=pq.top();pq.pop();if(e.count!=pairs[e.id].count||e.count<2)continue;
   uint32_t key=pairs[e.id].key;uint16_t a=key>>16,b=key;uint16_t sym=lens.size();left.push_back(a);right.push_back(b);lens.push_back(lens[a]+lens[b]);
   auto occ=std::move(pairs[e.id].occ);pairs[e.id].occ.clear();
   for(int32_t x:occ){
    Node& xn=nodes[x];int32_t y=xn.next;if(xn.len==0||y<0||xn.sym!=a||nodes[y].sym!=b||xn.end)continue;
    Node& yn=nodes[y];int32_t prev=xn.prev,next=yn.next;sub(prev);sub(x);sub(y);
    xn.sym=sym;xn.len+=yn.len;xn.end=yn.end;xn.next=next;yn.len=0;yn.next=-1;if(next>=0)nodes[next].prev=x;
    add(prev);add(x);ntokens--;
   }
   for(uint32_t id:dirty){pairs[id].dirty=false;if(pairs[id].count>=2)pq.push({pairs[id].count,id});}dirty.clear();
   if((lens.size()&(lens.size()-1))==0){unsigned bits=0;while((1u<<bits)<lens.size())bits++;snapshot(bits);}
  }
  snapshot(16);
 }
 int64_t output(uint8_t* dst,size_t cap){
  if(bestsize>cap)return -1;DH h{};h.magic=MAGIC;h.alphabet=alphabet.size();h.ndict=bestdict;h.bits=bestbits;h.rawsize=n;h.rows=rows;h.tokens=bestcodes.size();h.reserved[4]=1;h.reserved[5]=maxphrase==32;
  for(auto c:bestcounts)if(c>=15)h.extra++;
  memcpy(dst,&h,sizeof h);uint8_t* p=dst+sizeof h;memcpy(p,alphabet.data(),alphabet.size());p+=alphabet.size();
  defpack::encode(left.data(),right.data(),alphabet.size(),bestdict,p);p+=defpack::size(alphabet.size(),bestdict);
  memset(p,0,(rows+1)/2);uint8_t* countp=p;p+=(rows+1)/2;uint8_t* ex=p;p+=h.extra*2;
  for(uint32_t i=0;i<rows;i++){unsigned c=bestcounts[i];countp[i>>1]|=std::min(c,15u)<<((i&1)*4);if(c>=15){wr16(ex,c);ex+=2;}}
  memset(p,0,(uint64_t(h.tokens)*h.bits+7)/8+8);
  uint64_t bit=0;for(auto c:bestcodes){uint64_t v=uint64_t(c)<<(bit&7);for(unsigned j=0;j<(h.bits+(bit&7)+7)/8;j++)p[(bit>>3)+j]|=v>>(8*j);bit+=h.bits;}
  return bestsize;
 }
};
extern "C" int64_t dict_encode(const uint8_t* raw,size_t size,uint8_t* archive,size_t capacity){
 if(!raw||!archive||size==0||size>0x7fffffff)return -1;
 try{DictTrainer t(raw,size);t.train();return t.output(archive,capacity);}catch(...){return -1;}
}
#endif
#ifdef DECODER
struct alignas(16) Entry{uint8_t bytes[16];};
struct DState {DH h;const uint8_t* codes;uint32_t* offsets;uint8_t* entries;uint8_t* lengths;uint32_t mask;};
static void destroy(DState* s){if(!s)return;free(s->offsets);free(s->entries);free(s->lengths);free(s);}
extern "C" void* dict_open(const uint8_t* archive,size_t size){
 if(!archive||size<sizeof(DH))return nullptr;DH h;memcpy(&h,archive,sizeof h);
 if(h.magic!=MAGIC||!h.alphabet||h.alphabet>256||h.ndict<h.alphabet||h.bits<1||h.bits>16||h.ndict>(1u<<h.bits)||!h.rows||h.rows>h.tokens||h.tokens>h.rawsize||h.rawsize>0x7fffffff||h.extra>h.rows||h.reserved[5]>1)return nullptr;
 uint64_t need=sizeof h+h.alphabet+(h.reserved[4]?defpack::size(h.alphabet,h.ndict):uint64_t(h.ndict-h.alphabet)*4)+(uint64_t(h.rows)+1)/2+uint64_t(h.extra)*2+(uint64_t(h.tokens)*h.bits+7)/8+8;
 if(need!=size)return nullptr;
 DState* s=(DState*)calloc(1,sizeof(DState));if(!s)return nullptr;s->h=h;s->mask=(1u<<h.bits)-1;
 s->offsets=(uint32_t*)malloc((size_t(h.rows)+1)*4);unsigned stride=h.reserved[5]?32:16;s->entries=(uint8_t*)calloc((1u<<h.bits)+1,stride);s->lengths=(uint8_t*)calloc(1u<<h.bits,1);
 if(!s->offsets||!s->entries||!s->lengths){destroy(s);return nullptr;}
 const uint8_t* p=archive+sizeof h;
 for(unsigned i=0;i<h.alphabet;i++){s->entries[i*stride]=*p++;s->lengths[i]=1;}
 defpack::Reader dr(p);
 for(unsigned i=h.alphabet;i<h.ndict;i++){unsigned a,b;if(h.reserved[4])dr.pair(i,a,b);else {a=rd16(p);b=rd16(p+2);p+=4;}if(a>=i||b>=i||s->lengths[a]+s->lengths[b]>stride){destroy(s);return nullptr;}unsigned la=s->lengths[a],lb=s->lengths[b];if(stride==16){_mm_storeu_si128((__m128i*)(s->entries+i*stride),_mm_loadu_si128((const __m128i*)(s->entries+a*stride)));_mm_storeu_si128((__m128i*)(s->entries+i*stride+la),_mm_loadu_si128((const __m128i*)(s->entries+b*stride)));}else{_mm256_storeu_si256((__m256i*)(s->entries+i*stride),_mm256_loadu_si256((const __m256i*)(s->entries+a*stride)));_mm256_storeu_si256((__m256i*)(s->entries+i*stride+la),_mm256_loadu_si256((const __m256i*)(s->entries+b*stride)));}s->lengths[i]=la+lb;}
 if(h.reserved[4])p+=defpack::size(h.alphabet,h.ndict);
 const uint8_t* cnt=p;const uint8_t* ex=cnt+(h.rows+1)/2;p=ex+h.extra*2;uint64_t total=0;uint32_t nex=0;
 for(uint32_t i=0;i<h.rows;i++){s->offsets[i]=total;unsigned c=(cnt[i>>1]>>((i&1)*4))&15;if(c==15){if(nex>=h.extra){destroy(s);return nullptr;}c=rd16(ex+nex*2);nex++;if(c<15){destroy(s);return nullptr;}}if(!c){destroy(s);return nullptr;}total+=c;if(total>h.tokens){destroy(s);return nullptr;}}
 s->offsets[h.rows]=total;if(nex!=h.extra||total!=h.tokens){destroy(s);return nullptr;}s->codes=p;return s;
}
static inline unsigned getcode(const DState* s,uint64_t i){uint64_t bit=i*s->h.bits;return (rd32(s->codes+(bit>>3))>>(bit&7))&s->mask;}
template<unsigned Stride> static inline bool emit(const DState* s,unsigned code,uint8_t*& dst,uint8_t* end){
 unsigned len=s->lengths[code];if(!len||size_t(end-dst)<len)return false;
 const uint8_t* src=s->entries+code*Stride;
 if(size_t(end-dst)>=Stride){if constexpr(Stride==16)_mm_storeu_si128((__m128i*)dst,_mm_loadu_si128((const __m128i*)src));else _mm256_storeu_si256((__m256i*)dst,_mm256_loadu_si256((const __m256i*)src));}
 else memcpy(dst,src,len);dst+=len;return true;
}
template<unsigned Stride> static int64_t decode_impl(DState* s,uint8_t* output){
 uint8_t* dst=output,*end=output+s->h.rawsize;
 uint32_t i=0;
 for(;i+4<=s->h.tokens;i+=4){
  unsigned a=getcode(s,i),b=getcode(s,i+1),c=getcode(s,i+2),d=getcode(s,i+3);
  unsigned la=s->lengths[a],lb=s->lengths[b],lc=s->lengths[c],ld=s->lengths[d],total=la+lb+lc+ld;
  if(!la||!lb||!lc||!ld||size_t(end-dst)<total)return -1;
  if(size_t(end-dst)>=total+Stride){
   if constexpr(Stride==16){
    _mm_storeu_si128((__m128i*)dst,_mm_loadu_si128((const __m128i*)(s->entries+a*Stride)));
    _mm_storeu_si128((__m128i*)(dst+la),_mm_loadu_si128((const __m128i*)(s->entries+b*Stride)));
    _mm_storeu_si128((__m128i*)(dst+la+lb),_mm_loadu_si128((const __m128i*)(s->entries+c*Stride)));
    _mm_storeu_si128((__m128i*)(dst+la+lb+lc),_mm_loadu_si128((const __m128i*)(s->entries+d*Stride)));
   }else{
    _mm256_storeu_si256((__m256i*)dst,_mm256_loadu_si256((const __m256i*)(s->entries+a*Stride)));
    _mm256_storeu_si256((__m256i*)(dst+la),_mm256_loadu_si256((const __m256i*)(s->entries+b*Stride)));
    _mm256_storeu_si256((__m256i*)(dst+la+lb),_mm256_loadu_si256((const __m256i*)(s->entries+c*Stride)));
    _mm256_storeu_si256((__m256i*)(dst+la+lb+lc),_mm256_loadu_si256((const __m256i*)(s->entries+d*Stride)));
   }dst+=total;
  }else {if(!emit<Stride>(s,a,dst,end)||!emit<Stride>(s,b,dst,end)||!emit<Stride>(s,c,dst,end)||!emit<Stride>(s,d,dst,end))return -1;}
 }
 for(;i<s->h.tokens;i++)if(!emit<Stride>(s,getcode(s,i),dst,end))return -1;
 return dst==end?s->h.rawsize:-1;
}
extern "C" int64_t dict_decode(void* state,uint8_t* output,size_t capacity){
 DState* s=(DState*)state;if(!s||!output||capacity<s->h.rawsize)return -1;
 return s->h.reserved[5]?decode_impl<32>(s,output):decode_impl<16>(s,output);
}
template<unsigned Stride> static int64_t rows_impl(DState* s,const uint64_t* ids,size_t count,uint8_t* output,size_t capacity,uint64_t* offsets){
 uint8_t* dst=output,*end=output+capacity;uint64_t previous=0;
 for(size_t i=0;i<count;i++){uint64_t id=ids[i];if(id>=s->h.rows||(i&&id<previous))return -1;previous=id;for(uint32_t j=s->offsets[id],stop=s->offsets[id+1];j<stop;j++)if(!emit<Stride>(s,getcode(s,j),dst,end))return -1;offsets[i+1]=dst-output;}
 return dst-output;
}
extern "C" int64_t dict_rows(void* state,const uint64_t* ids,size_t count,uint8_t* output,size_t capacity,uint64_t* offsets){
 DState* s=(DState*)state;if(!s||!offsets||(count&&(!ids||!output)))return -1;offsets[0]=0;if(!count)return 0;
 return s->h.reserved[5]?rows_impl<32>(s,ids,count,output,capacity,offsets):rows_impl<16>(s,ids,count,output,capacity,offsets);
}
extern "C" void dict_close(void* state){destroy((DState*)state);}
#endif
