#pragma once
#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <queue>
namespace phrase {
struct PhraseEncoded { std::vector<std::string> dict; std::vector<uint16_t> tokens; std::vector<uint32_t> row_token_offsets; };
inline PhraseEncoded train(const std::vector<std::string_view>& rows, int maxdict=4096,int batch=128,int maxlen=32) {
 PhraseEncoded r; for(int i=0;i<256;i++) r.dict.emplace_back(1,char(i));
 size_t total=0;for(auto s:rows)total+=s.size();r.tokens.reserve(total);
 r.row_token_offsets.push_back(0);for(auto s:rows){for(unsigned char c:s)r.tokens.push_back(c);r.row_token_offsets.push_back(r.tokens.size());}
 std::vector<uint16_t> next;std::vector<uint32_t> nidx(rows.size()+1);
 while(r.dict.size()<size_t(maxdict)){
  std::unordered_map<uint32_t,uint32_t> cnt;cnt.reserve(r.tokens.size()/3);
  for(size_t row=0;row<rows.size();row++)for(uint32_t j=r.row_token_offsets[row]+1;j<r.row_token_offsets[row+1];j++){
   uint16_t a=r.tokens[j-1],b=r.tokens[j];if(r.dict[a].size()+r.dict[b].size()<=size_t(maxlen))cnt[(uint32_t(a)<<16)|b]++;
  }
  std::vector<std::pair<uint32_t,uint32_t>> ranks;ranks.reserve(cnt.size());for(auto p:cnt)if(p.second>=3)ranks.emplace_back(p.second,p.first);
  int k=std::min({batch,maxdict-int(r.dict.size()),int(ranks.size())});if(!k)break;
  std::partial_sort(ranks.begin(),ranks.begin()+k,ranks.end(),std::greater<std::pair<uint32_t,uint32_t>>());
  std::unordered_map<uint32_t,uint16_t> merges;merges.reserve(k*2);
  for(int i=0;i<k;i++){auto key=ranks[i].second;merges[key]=r.dict.size();r.dict.push_back(r.dict[key>>16]+r.dict[key&65535]);}
  next.clear();next.reserve(r.tokens.size());nidx[0]=0;
  for(size_t row=0;row<rows.size();row++){
   uint32_t end=r.row_token_offsets[row+1];
   for(uint32_t j=r.row_token_offsets[row];j<end;j++){
    if(j+1<end){auto it=merges.find((uint32_t(r.tokens[j])<<16)|r.tokens[j+1]);if(it!=merges.end()){next.push_back(it->second);j++;continue;}}
    next.push_back(r.tokens[j]);
   }nidx[row+1]=next.size();
  }
  r.tokens.swap(next);r.row_token_offsets.swap(nidx);
 }
 std::vector<uint32_t> freq(r.dict.size());for(auto t:r.tokens)freq[t]++;
 std::vector<uint16_t> order;for(unsigned i=0;i<freq.size();i++)if(freq[i])order.push_back(i);
 std::sort(order.begin(),order.end(),[&](uint16_t a,uint16_t b){return freq[a]>freq[b];});
 std::vector<uint16_t> remap(r.dict.size());std::vector<std::string> dict;dict.reserve(order.size());
 for(unsigned i=0;i<order.size();i++){remap[order[i]]=i;dict.push_back(std::move(r.dict[order[i]]));}
 for(auto &t:r.tokens)t=remap[t];r.dict.swap(dict);return r;
}

inline unsigned context_byte(uint8_t c);
// Reparse the learned phrase alphabet with dynamic programming; format unchanged.
inline void reparse(const std::vector<std::string_view>& rows, PhraseEncoded& r, bool variable=false, const std::vector<uint8_t>* costs=nullptr, const std::vector<std::vector<uint8_t>>* ctxcosts=nullptr) {
 struct Node { std::unordered_map<uint8_t,uint32_t> child; int token=-1; };
 std::vector<Node> trie(1);
 for(unsigned t=0;t<r.dict.size();t++){uint32_t node=0;for(uint8_t c:r.dict[t]){auto it=trie[node].child.find(c);if(it==trie[node].child.end()){uint32_t n=trie.size();trie[node].child[c]=n;trie.emplace_back();node=n;}else node=it->second;}trie[node].token=t;}
 std::vector<uint16_t> tokens;tokens.reserve(r.tokens.size());std::vector<uint32_t> index(1,0);
 std::vector<int> cost,prev,code;std::vector<uint16_t> rev;
 for(auto row:rows){cost.assign(row.size()+1,1<<29);prev.resize(row.size()+1);code.resize(row.size()+1);cost[0]=0;
  for(size_t i=0;i<row.size();i++)if(cost[i]<(1<<29)){
   uint32_t node=0;for(size_t j=i;j<row.size();j++){auto it=trie[node].child.find(uint8_t(row[j]));if(it==trie[node].child.end())break;node=it->second;int t=trie[node].token;if(t>=0){int pc=costs?(*costs)[t]:1+(variable&&t>=128);if(ctxcosts){unsigned ctx=i?context_byte(uint8_t(row[i-1])):0;pc=(*ctxcosts)[ctx][t];if(!pc)pc=15;}int c=cost[i]+pc;if(c<cost[j+1]){cost[j+1]=c;prev[j+1]=i;code[j+1]=t;}}}
  }
  rev.clear();for(size_t i=row.size();i;i=prev[i])rev.push_back(code[i]);tokens.insert(tokens.end(),rev.rbegin(),rev.rend());index.push_back(tokens.size());
 }
 r.tokens.swap(tokens);r.row_token_offsets.swap(index);
 std::vector<uint32_t> freq(r.dict.size());for(auto t:r.tokens)freq[t]++;
 std::vector<uint16_t> order;for(unsigned i=0;i<freq.size();i++)if(freq[i])order.push_back(i);
 std::sort(order.begin(),order.end(),[&](uint16_t a,uint16_t b){return freq[a]>freq[b];});
 std::vector<uint16_t> remap(r.dict.size());std::vector<std::string> dict;dict.reserve(order.size());
 for(unsigned i=0;i<order.size();i++){remap[order[i]]=i;dict.push_back(std::move(r.dict[order[i]]));}
 for(auto &t:r.tokens)t=remap[t];r.dict.swap(dict);
}

struct Huffman { std::vector<uint8_t> lengths; std::vector<uint32_t> codes; int maxbits=0; };
// Canonical complete Huffman alphabet. The longest all-ones code lets row ends
// use one-bit padding without emitting an extra symbol from <=7 residual bits.
inline Huffman huffman_frequencies(const std::vector<uint64_t>& freq,int limit=15) {
 struct N { uint64_t freq; int left,right; };
 using P=std::pair<uint64_t,int>;
 std::vector<N> nodes;std::priority_queue<P,std::vector<P>,std::greater<P>> q;
 for(unsigned i=0;i<freq.size();i++){nodes.push_back({freq[i],-1,-1});q.emplace(freq[i],i);}
 Huffman h;h.lengths.resize(freq.size());h.codes.resize(freq.size());
 if(freq.size()==1){h.lengths[0]=1;h.maxbits=1;return h;}
 while(q.size()>1){auto a=q.top();q.pop();auto b=q.top();q.pop();int n=nodes.size();nodes.push_back({a.first+b.first,a.second,b.second});q.emplace(a.first+b.first,n);}
 std::vector<std::pair<int,int>> stack{{q.top().second,0}};std::vector<int> count(limit+1);int overflow=0;
 while(stack.size()){auto p=stack.back();stack.pop_back();auto n=nodes[p.first];if(n.left>=0){stack.emplace_back(n.left,p.second+1);stack.emplace_back(n.right,p.second+1);}else{int bits=p.second;if(bits>limit){bits=limit;overflow++;}count[bits]++;}}
 uint64_t kraft=0;for(int b=1;b<=limit;b++)kraft+=uint64_t(count[b])<<(limit-b);
 while(kraft>(uint64_t(1)<<limit)){int bits=limit-1;while(bits>0&&count[bits]==0)bits--;count[bits]--;count[bits+1]+=2;count[limit]--;kraft--;}
 std::vector<uint16_t> order(freq.size());for(unsigned i=0;i<order.size();i++)order[i]=i;
 std::sort(order.begin(),order.end(),[&](uint16_t a,uint16_t b){return freq[a]!=freq[b]?freq[a]<freq[b]:a<b;});
 size_t at=0;for(int b=limit;b>=1;b--)for(int k=0;k<count[b];k++)h.lengths[order[at++]]=b;
 uint32_t code=0;std::vector<uint32_t> next(limit+1);for(int b=1;b<=limit;b++){code=(code+count[b-1])<<1;next[b]=code;if(count[b])h.maxbits=b;}
 for(unsigned i=0;i<h.lengths.size();i++){int n=h.lengths[i];uint32_t c=next[n]++,rev=0;for(int j=0;j<n;j++){rev=(rev<<1)|(c&1);c>>=1;}h.codes[i]=rev;}
 return h;
}
inline Huffman huffman(const PhraseEncoded& r,int limit=15) {std::vector<uint64_t> freq(r.dict.size());for(auto t:r.tokens)freq[t]++;return huffman_frequencies(freq,limit);}
inline std::vector<uint8_t> huffman_bytes(const PhraseEncoded& r,const Huffman& h,std::vector<uint32_t>& offsets) {
 std::vector<uint8_t> out;offsets.clear();offsets.push_back(0);uint64_t bits=0;int used=0;
 for(size_t row=0;row+1<r.row_token_offsets.size();row++){
  for(uint32_t p=r.row_token_offsets[row];p<r.row_token_offsets[row+1];p++){uint16_t c=r.tokens[p];bits|=uint64_t(h.codes[c])<<used;used+=h.lengths[c];while(used>=8){out.push_back(uint8_t(bits));bits>>=8;used-=8;}}
  if(used){out.push_back(uint8_t(bits|(uint64_t(255)<<used)));bits=0;used=0;}offsets.push_back(out.size());
 }
 return out;
}

inline void reparse_huffman(const std::vector<std::string_view>& rows,PhraseEncoded& r,int limit=14,int iterations=3){for(int i=0;i<iterations;i++){auto h=huffman(r,limit);reparse(rows,r,false,&h.lengths);}}

inline unsigned context_byte(uint8_t c) { if(c==10)return 0;c|=32;return c>='a'&&c<='z'?c-'a'+1:27; }
struct ContextHuffman {std::vector<std::vector<uint8_t>> lengths;std::vector<std::vector<uint32_t>> codes;std::vector<uint8_t> next;std::vector<uint8_t> maxbits;};
inline ContextHuffman context_huffman(const PhraseEncoded& r,int limit=12){
 ContextHuffman out;out.lengths.resize(28);out.codes.resize(28);out.maxbits.resize(28);out.next.resize(r.dict.size());
 std::vector<std::vector<uint64_t>> freqs(28,std::vector<uint64_t>(r.dict.size()));
 for(unsigned i=0;i<r.dict.size();i++)out.next[i]=context_byte(uint8_t(r.dict[i].back()));
 for(unsigned row=0;row+1<r.row_token_offsets.size();row++){unsigned ctx=0;for(uint32_t j=r.row_token_offsets[row];j<r.row_token_offsets[row+1];j++){uint16_t t=r.tokens[j];freqs[ctx][t]++;ctx=out.next[t];}}
 for(unsigned c=0;c<28;c++){
  std::vector<uint64_t> freq;std::vector<uint16_t> ids;for(unsigned i=0;i<r.dict.size();i++)if(freqs[c][i]){ids.push_back(i);freq.push_back(freqs[c][i]);}
  out.lengths[c].resize(r.dict.size());out.codes[c].resize(r.dict.size());if(freq.empty())continue;
  int m=limit;while((1u<<m)<ids.size())m++;auto h=huffman_frequencies(freq,m);out.maxbits[c]=h.maxbits;
  for(unsigned i=0;i<ids.size();i++){out.lengths[c][ids[i]]=h.lengths[i];out.codes[c][ids[i]]=h.codes[i];}
 }
 return out;
}

inline void reparse_context(const std::vector<std::string_view>& rows,PhraseEncoded& r,int limit=12,int iterations=3){for(int i=0;i<iterations;i++){auto h=context_huffman(r,limit);reparse(rows,r,false,nullptr,&h.lengths);}}
}
