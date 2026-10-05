#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <queue>
#include <functional>
#include <algorithm>
// Encoder-only optimal row parsing against the complete learned dictionary.
// Stable symbol IDs and stable priority ties make all passes deterministic.
static inline void dp_reparse(const uint8_t* raw,size_t size,
 const std::vector<std::string>& phrases,std::vector<int>& sym,
 std::vector<int>& next,const std::vector<int>& starts,unsigned passes=3) {
 struct Trie {int symbol=-1;};
 std::vector<Trie> trie(1);std::unordered_map<uint64_t,int> edges;
 edges.reserve(phrases.size()*4);
 for(unsigned v=0;v<phrases.size();v++){
  int st=0;for(uint8_t c:phrases[v]){
   uint64_t key=(uint64_t(st)<<8)|c;auto it=edges.find(key);
   if(it==edges.end()){int ns=trie.size();trie.emplace_back();edges.emplace(key,ns);st=ns;}else st=it->second;
  }
  trie[st].symbol=v;
 }
 std::vector<uint32_t> freq(phrases.size()),nb(phrases.size());
 for(unsigned pass=0;pass<passes;pass++) {
  std::fill(freq.begin(),freq.end(),0);
  for(int st:starts)for(int p=st;p>=0;p=next[p])freq[sym[p]]++;
  struct Node{uint64_t freq;int left,right;};
  std::vector<Node>nodes;nodes.reserve(phrases.size()*2);
  using Q=std::pair<uint64_t,int>;
  std::priority_queue<Q,std::vector<Q>,std::greater<Q>>q;
  for(unsigned v=0;v<freq.size();v++){uint64_t f=std::max(1u,freq[v]);nodes.push_back({f,-1,-1});q.push({f,v});}
  while(q.size()>1){auto a=q.top();q.pop();auto b=q.top();q.pop();q.push({a.first+b.first,(int)nodes.size()});nodes.push_back({a.first+b.first,a.second,b.second});}
  std::vector<std::pair<int,unsigned>> stack={{q.top().second,0}};
  while(!stack.empty()){auto [v,d]=stack.back();stack.pop_back();if(nodes[v].left<0)nb[v]=std::max(d,1u);else{stack.push_back({nodes[v].left,d+1});stack.push_back({nodes[v].right,d+1});}}
  std::vector<uint32_t>cost;std::vector<int>best;
  for(unsigned r=0;r<starts.size();r++) {
   size_t st=starts[r],end=r+1<starts.size()?starts[r+1]:size;unsigned n=end-st;
   cost.assign(n+1,0x3fffffff);best.resize(n);cost[n]=0;
   for(int i=n-1;i>=0;i--) {
    int state=0;for(unsigned j=i;j<n&&j<unsigned(i)+32;j++) {
     auto it=edges.find((uint64_t(state)<<8)|raw[st+j]);if(it==edges.end())break;state=it->second;int v=trie[state].symbol;
     if(v>=0&&nb[v]+cost[j+1]<cost[i]){cost[i]=nb[v]+cost[j+1];best[i]=v;}
    }
   }
   for(unsigned i=0;i<n;){int v=best[i];unsigned j=i+phrases[v].size();sym[st+i]=v;next[st+i]=j<n?st+j:-1;i=j;}
  }
 }
}
