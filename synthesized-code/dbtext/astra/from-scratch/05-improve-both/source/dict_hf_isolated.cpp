#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <vector>
#include <algorithm>
#include <immintrin.h>
#ifdef ENCODER
#include <unordered_map>
#include <queue>
#include <stdio.h>
#endif
namespace hfspace {
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
 const uint8_t* raw;size_t n;std::vector<Node> nodes;std::vector<uint16_t> left,right;std::vector<uint8_t> lens,alphabet;std::vector<Pair> pairs;std::unordered_map<uint32_t,uint32_t> map;std::priority_queue<QueueEntry> pq;std::vector<uint32_t> dirty;std::vector<uint16_t> bestcodes,bestcounts; std::vector<uint8_t> besthuff;unsigned bestcountbits=4,bestmode=0;uint32_t bestbitcount=0;uint32_t ntokens,rows=0,bestdict=0,bestbits=0;size_t bestsize=~size_t(0);
 DictTrainer(const uint8_t* p,size_t z):raw(p),n(z),ntokens(z){
  bool used[256]={};for(size_t i=0;i<n;i++)used[p[i]]=1;
  uint16_t remap[256]={};for(int i=0;i<256;i++)if(used[i]){remap[i]=alphabet.size();alphabet.push_back(i);left.push_back(i);right.push_back(0);lens.push_back(1);}
  nodes.resize(n);for(size_t i=0;i<n;i++){bool end=p[i]==10||i+1==n; nodes[i]={(int32_t)i-1,i+1<n?(int32_t)i+1:-1,remap[p[i]],1,(uint8_t)end};if(end)rows++;}
  map.reserve(n/4+1024);
  for(size_t i=0;i+1<n;i++)if(!nodes[i].end)add(i);
  for(uint32_t id:dirty){pairs[id].dirty=false;pq.push({pairs[id].count,id});}dirty.clear();
 }
 uint32_t get(uint32_t key){auto it=map.find(key);if(it!=map.end())return it->second;uint32_t id=pairs.size();map.emplace(key,id);pairs.push_back({key,0,{}});return id;}
 void touch(uint32_t id){if(!pairs[id].dirty){pairs[id].dirty=true;dirty.push_back(id);}}
 void add(int32_t x){if(x<0)return;Node& a=nodes[x];int32_t y=a.next;if(y<0||a.end||a.len+nodes[y].len>16)return;uint32_t id=get((uint32_t(a.sym)<<16)|nodes[y].sym);pairs[id].count++;pairs[id].occ.push_back(x);touch(id);}
 void sub(int32_t x){if(x<0)return;Node& a=nodes[x];int32_t y=a.next;if(y<0||a.end||a.len+nodes[y].len>16)return;uint32_t id=map.find((uint32_t(a.sym)<<16)|nodes[y].sym)->second;if(pairs[id].count)pairs[id].count--;touch(id);}
 std::vector<uint8_t> huffman(const std::vector<uint16_t>& codes) {
  std::vector<uint32_t> freq(lens.size());for(auto x:codes)freq[x]++;
  for(unsigned floor=1;;floor*=2){
   struct HN{uint64_t f;int a,b;};std::vector<HN> hn;hn.reserve(freq.size()*2);
   struct HC{uint64_t f;int id;bool operator>(const HC& b)const{return f!=b.f?f>b.f:id>b.id;}};
   std::priority_queue<HC,std::vector<HC>,std::greater<HC>> q;
   for(unsigned i=0;i<freq.size();i++){hn.push_back({freq[i]+floor,-1,-1});q.push({freq[i]+floor,int(i)});}
   while(q.size()>1){auto a=q.top();q.pop();auto b=q.top();q.pop();int id=hn.size();hn.push_back({a.f+b.f,a.id,b.id});q.push({a.f+b.f,id});}
   std::vector<uint8_t> hl(freq.size());std::vector<std::pair<int,unsigned>> stack{{q.top().id,0}};unsigned maxlen=0;
   while(!stack.empty()){auto x=stack.back();stack.pop_back();if(hn[x.first].a<0){hl[x.first]=std::max(x.second,1u);maxlen=std::max(maxlen,x.second);}else{stack.push_back({hn[x.first].a,x.second+1});stack.push_back({hn[x.first].b,x.second+1});}}
   if(maxlen<=17)return hl;
  }
 }
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
   for(size_t i=nn;i--;){uint32_t node=0;for(size_t j=i;j<nn&&j<i+16;j++){node=trie[node].child[raw[start+j]];if(!node)break;uint16_t code=trie[node].code;if(code!=65535&&dp[j+1]+1<=dp[i]){dp[i]=dp[j+1]+1;choice[i]=code;}}}
   counts.push_back(dp[0]);for(size_t i=0;i<nn;){unsigned code=choice[i];codes.push_back(code);i+=lens[code];}start=stop;
  }
  size_t extra=0,rc=0;
  for(auto count:counts)if(count>=15)extra++;
  size_t size=sizeof(DH)+alphabet.size()+(lens.size()-alphabet.size())*4+(rows+1)/2+extra*2+(uint64_t(codes.size())*bits+7)/8+8;
  if(size<bestsize){bestsize=size;bestdict=lens.size();bestbits=bits;bestcodes=codes;bestcounts=counts;bestmode=0;besthuff.clear();}
  // Minimize total Huffman bit cost using the phrase trie; refit twice.
  std::vector<uint8_t> hl;
  for(unsigned pass=0;pass<3;pass++){
   hl=huffman(codes);
   if(pass==2)break;
   codes.clear();counts.clear();size_t start=0;
   while(start<n){size_t stop=start;while(stop<n&&raw[stop]!=10)stop++;if(stop<n)stop++;
    size_t nn=stop-start;std::vector<uint32_t> dp(nn+1,~0u),choice(nn);dp[nn]=0;unsigned nc=0;
    for(size_t i=nn;i--;){uint32_t node=0;for(size_t j=i;j<nn&&j<i+16;j++){node=trie[node].child[raw[start+j]];if(!node)break;uint16_t code=trie[node].code;if(code!=65535&&dp[j+1]+hl[code]<=dp[i]){dp[i]=dp[j+1]+hl[code];choice[i]=code;}}}
    for(size_t i=0;i<nn;){unsigned code=choice[i];codes.push_back(code);i+=lens[code];nc++;}counts.push_back(nc);start=stop;
   }
  }
  uint64_t totalbits=0;unsigned maxhl=0,minhl=255;
  for(auto x:hl){maxhl=std::max(maxhl,unsigned(x));minhl=std::min(minhl,unsigned(x));}
  unsigned lb=maxhl-minhl<15?4:5;
  std::vector<uint16_t> bitcounts;bitcounts.reserve(rows);size_t ci=0;bool okay=true;
  for(auto c:counts){unsigned bl=0;for(unsigned j=0;j<c;j++)bl+=hl[codes[ci++]];if(bl>=65536)okay=false;bitcounts.push_back(bl);totalbits+=bl;}
  if(!okay||totalbits>0xffffffffu)return;
  for(unsigned cb=4;cb<=10;cb++){
   unsigned escape=(1u<<cb)-1,ex=0;for(auto c:bitcounts)if(c>=escape)ex++;
   uint64_t hs=sizeof(DH)+alphabet.size()+(lens.size()-alphabet.size())*4+(lens.size()*lb+7)/8+(uint64_t(rows)*cb+7)/8+ex*2+(totalbits+7)/8+8;
   if(hs<bestsize){bestsize=hs;bestdict=lens.size();bestbits=maxhl;bestcodes=codes;bestcounts=bitcounts;bestmode=1;besthuff=hl;bestcountbits=cb;bestbitcount=totalbits;}
  }
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
  if(bestsize>cap)return -1;DH h{};h.magic=MAGIC;h.alphabet=alphabet.size();h.ndict=bestdict;h.bits=bestbits;h.rawsize=n;h.rows=rows;h.tokens=bestcodes.size();
  unsigned cb=bestmode?bestcountbits:4,escape=(1u<<cb)-1;for(auto c:bestcounts)if(c>=escape)h.extra++;
  h.reserved[0]=bestmode;h.reserved[1]=cb;h.padding=bestmode?bestbitcount:0;
  unsigned minhl=255,maxhl=0,lb=0;
  if(bestmode){for(auto x:besthuff){minhl=std::min(minhl,unsigned(x));maxhl=std::max(maxhl,unsigned(x));}lb=maxhl-minhl<15?4:5;h.reserved[2]=minhl;h.reserved[3]=lb;}
  memcpy(dst,&h,sizeof h);uint8_t* p=dst+sizeof h;memcpy(p,alphabet.data(),alphabet.size());p+=alphabet.size();
  for(unsigned i=alphabet.size();i<bestdict;i++){wr16(p,left[i]);wr16(p+2,right[i]);p+=4;}
  auto pack=[](uint8_t* p,uint64_t bit,uint32_t value,unsigned bits){uint32_t v=value<<(bit&7);for(unsigned j=0;j<(bits+(bit&7)+7)/8;j++)p[(bit>>3)+j]|=v>>(8*j);};
  if(bestmode){size_t sz=(bestdict*lb+7)/8;memset(p,0,sz);for(unsigned i=0;i<bestdict;i++)pack(p,uint64_t(i)*lb,besthuff[i]-minhl+1,lb);p+=sz;}
  size_t csz=(uint64_t(rows)*cb+7)/8;memset(p,0,csz);uint8_t* countp=p;p+=csz;uint8_t* ex=p;p+=h.extra*2;
  for(uint32_t i=0;i<rows;i++){unsigned c=bestcounts[i];pack(countp,uint64_t(i)*cb,std::min(c,escape),cb);if(c>=escape){wr16(ex,c);ex+=2;}}
  uint64_t total=bestmode?bestbitcount:uint64_t(h.tokens)*h.bits;memset(p,0,(total+7)/8+8);
  uint64_t bit=0;
  if(bestmode){
   uint32_t ct[18]={},next[18]={};for(auto x:besthuff)ct[x]++;uint32_t code=0;for(unsigned i=1;i<=17;i++){code=(code+ct[i-1])<<1;next[i]=code;}
   std::vector<uint32_t> values(bestdict);for(unsigned i=0;i<bestdict;i++){unsigned len=besthuff[i],x=next[len]++,v=0;for(unsigned k=0;k<len;k++){v=(v<<1)|(x&1);x>>=1;}values[i]=v;}
   for(auto c:bestcodes){unsigned len=besthuff[c];pack(p,bit,values[c],len);bit+=len;}
  }else for(auto c:bestcodes){pack(p,bit,c,h.bits);bit+=h.bits;}
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
struct DState {DH h;const uint8_t* codes;uint32_t* offsets;Entry* entries;uint8_t* lengths;uint32_t mask;uint32_t* htable;};
static void destroy(DState* s){if(!s)return;free(s->offsets);free(s->entries);free(s->lengths);free(s->htable);free(s);}
extern "C" void* dict_open(const uint8_t* archive,size_t size){
 if(!archive||size<sizeof(DH))return nullptr;DH h;memcpy(&h,archive,sizeof h);
 unsigned mode=h.reserved[0],cb=mode?h.reserved[1]:4,lb=h.reserved[3],minhl=h.reserved[2];
 if(h.magic!=MAGIC||!h.alphabet||h.alphabet>256||h.ndict<h.alphabet||h.bits<1||h.bits>(mode?17:16)||h.ndict>(1u<<h.bits)||!h.rows||h.rows>h.tokens||h.tokens>h.rawsize||h.rawsize>0x7fffffff||h.extra>h.rows||mode>1||cb<4||cb>10||(mode&&(lb!=4&&lb!=5))||(mode&&(!minhl||minhl>h.bits||!h.padding)))return nullptr;
 uint64_t totalbits=mode?h.padding:uint64_t(h.tokens)*h.bits;
 uint64_t need=sizeof h+h.alphabet+uint64_t(h.ndict-h.alphabet)*4+(mode?(uint64_t(h.ndict)*lb+7)/8:0)+(uint64_t(h.rows)*cb+7)/8+uint64_t(h.extra)*2+(totalbits+7)/8+8;
 if(need!=size)return nullptr;
 DState* s=(DState*)calloc(1,sizeof(DState));if(!s)return nullptr;s->h=h;s->mask=(1u<<h.bits)-1;
 s->offsets=(uint32_t*)malloc((size_t(h.rows)+1)*4);s->entries=(Entry*)calloc(mode?h.ndict:1u<<h.bits,sizeof(Entry));s->lengths=(uint8_t*)calloc(mode?h.ndict:1u<<h.bits,1);
 if(!s->offsets||!s->entries||!s->lengths){destroy(s);return nullptr;}
 const uint8_t* p=archive+sizeof h;
 for(unsigned i=0;i<h.alphabet;i++){s->entries[i].bytes[0]=*p++;s->lengths[i]=1;}
 for(unsigned i=h.alphabet;i<h.ndict;i++){unsigned a=rd16(p),b=rd16(p+2);p+=4;if(a>=i||b>=i||s->lengths[a]+s->lengths[b]>16){destroy(s);return nullptr;}unsigned la=s->lengths[a],ll=s->lengths[b];memcpy(s->entries[i].bytes,s->entries[a].bytes,la);memcpy(s->entries[i].bytes+la,s->entries[b].bytes,ll);s->lengths[i]=la+ll;}
 if(mode){
  s->htable=(uint32_t*)calloc(1u<<h.bits,4);if(!s->htable){destroy(s);return nullptr;}uint32_t ct[18]={},next[18]={};std::vector<uint8_t> hl(h.ndict);
  for(unsigned i=0;i<h.ndict;i++){uint64_t bit=uint64_t(i)*lb;unsigned x=(rd16(p+(bit>>3))>>(bit&7))&((1u<<lb)-1);if(!x||x+minhl-1>h.bits){destroy(s);return nullptr;}hl[i]=x+minhl-1;ct[hl[i]]++;}
  uint32_t code=0;for(unsigned i=1;i<=h.bits;i++){code=(code+ct[i-1])<<1;next[i]=code;if(code+ct[i]>(1u<<i)){destroy(s);return nullptr;}}
  if(code+ct[h.bits]!=(1u<<h.bits)){destroy(s);return nullptr;}
  for(unsigned i=0;i<h.ndict;i++){unsigned len=hl[i],x=next[len]++,v=0;for(unsigned k=0;k<len;k++){v=(v<<1)|(x&1);x>>=1;}for(unsigned j=v;j<(1u<<h.bits);j+=1u<<len)s->htable[j]=i|(len<<16);}
  p+=(uint64_t(h.ndict)*lb+7)/8;
 }
 const uint8_t* cnt=p;const uint8_t* ex=cnt+(uint64_t(h.rows)*cb+7)/8;p=ex+h.extra*2;uint64_t total=0;uint32_t nex=0;unsigned escape=(1u<<cb)-1;
 for(uint32_t i=0;i<h.rows;i++){s->offsets[i]=total;uint64_t bit=uint64_t(i)*cb;unsigned c=(rd32(cnt+(bit>>3))>>(bit&7))&escape;if(c==escape){if(nex>=h.extra){destroy(s);return nullptr;}c=rd16(ex+nex*2);nex++;if(c<escape){destroy(s);return nullptr;}}if(!c){destroy(s);return nullptr;}total+=c;if(total>(mode?totalbits:h.tokens)){destroy(s);return nullptr;}}
 s->offsets[h.rows]=total;if(nex!=h.extra||total!=(mode?totalbits:h.tokens)){destroy(s);return nullptr;}s->codes=p;return s;
}
static inline unsigned getcode(const DState* s,uint64_t i){uint64_t bit=i*s->h.bits;return (rd32(s->codes+(bit>>3))>>(bit&7))&s->mask;}
static inline bool emit(const DState* s,unsigned code,uint8_t*& dst,uint8_t* end){
 unsigned len=s->lengths[code];if(!len||size_t(end-dst)<len)return false;
 if(size_t(end-dst)>=16){_mm_storeu_si128((__m128i*)dst,_mm_load_si128((const __m128i*)s->entries[code].bytes));}else memcpy(dst,s->entries[code].bytes,len);dst+=len;return true;
}
struct BitReader {
 const uint8_t* p;uint64_t word;unsigned avail;
 BitReader(const uint8_t* src,uint64_t bit){p=src+(bit>>3);word=rd64(p)>>(bit&7);avail=64-(bit&7);p+=8;}
 inline uint32_t next(const DState* s){if(avail<s->h.bits){word|=uint64_t(rd32(p))<<avail;p+=4;avail+=32;}uint32_t e=s->htable[word&s->mask];unsigned len=e>>16;word>>=len;avail-=len;return e;}
};
extern "C" int64_t dict_decode(void* state,uint8_t* output,size_t capacity){
 DState* s=(DState*)state;if(!s||!output||capacity<s->h.rawsize)return -1;uint8_t* dst=output,*end=output+s->h.rawsize;
 if(s->h.reserved[0]){uint64_t bit=0;BitReader br(s->codes,0);for(uint32_t i=0;i<s->h.tokens;i++){if(bit>=s->h.padding)return -1;uint32_t e=br.next(s);bit+=e>>16;if(!emit(s,e&65535,dst,end))return -1;}if(bit!=s->h.padding)return -1;}
 else for(uint32_t i=0;i<s->h.tokens;i++)if(!emit(s,getcode(s,i),dst,end))return -1;
 return dst==end?s->h.rawsize:-1;
}
extern "C" int64_t dict_rows(void* state,const uint64_t* ids,size_t count,uint8_t* output,size_t capacity,uint64_t* offsets){
 DState* s=(DState*)state;if(!s||!offsets||(count&&(!ids||!output)))return -1;offsets[0]=0;if(!count)return 0;uint8_t* dst=output,*end=output+capacity;uint64_t previous=0;
 for(size_t i=0;i<count;i++){uint64_t id=ids[i];if(id>=s->h.rows||(i&&id<previous))return -1;previous=id;if(s->h.reserved[0]){uint64_t bit=s->offsets[id],stop=s->offsets[id+1];BitReader br(s->codes,bit);while(bit<stop){uint32_t e=br.next(s);bit+=e>>16;if(bit>stop||!emit(s,e&65535,dst,end))return -1;}}else for(uint32_t j=s->offsets[id],stop=s->offsets[id+1];j<stop;j++)if(!emit(s,getcode(s,j),dst,end))return -1;offsets[i+1]=dst-output;}
 return dst-output;
}
extern "C" void dict_close(void* state){destroy((DState*)state);}
#endif

}
