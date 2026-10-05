
#include "codec.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <utility>
using namespace std;
struct Pair { uint64_t key; int count=0, head=-1,prev=-1,next=-1; };

extern "C" int64_t lab_encode(const uint8_t* raw,size_t size,uint8_t* archive,size_t capacity) {
  if((size && !raw) || !archive || size>INT_MAX) return -1;
  try {
    const int maxsym=40000,maxlen=32;
    int n=(int)size;
    vector<int> pv(n),nx(n),op(n,-1),on(n,-1),pi(n,-1),symbol(n),bucket(n+1,-1);
    vector<int> len(256,1),left(256,-1),right(256,-1);
    vector<Pair> pairs;
    unordered_map<uint64_t,int> ids;
    ids.reserve(n/4);
    int best=0;
    auto unbucket=[&](int i) {
      auto&v=pairs[i]; if(v.count==0) return;
      if(v.prev<0) bucket[v.count]=v.next; else pairs[v.prev].next=v.next;
      if(v.next>=0) pairs[v.next].prev=v.prev;
      v.prev=v.next=-1;
    };
    auto inbucket=[&](int i) {
      auto&v=pairs[i]; if(v.count==0) return;
      uint32_t a=v.key>>32,b=v.key;
      if(len[a]+len[b]>maxlen) return;
      v.prev=-1; v.next=bucket[v.count];
      if(v.next>=0) pairs[v.next].prev=i;
      bucket[v.count]=i; best=max(best,v.count);
    };
    auto add=[&](int pos) {
      if(pos<0 || nx[pos]<0) return;
      uint64_t key=(uint64_t(uint32_t(symbol[pos]))<<32)|uint32_t(symbol[nx[pos]]);
      int id;
      auto it=ids.find(key);
      if(it==ids.end()) { id=(int)pairs.size(); ids.emplace(key,id); pairs.push_back({key}); }
      else id=it->second;
      auto&v=pairs[id];
      if(len[uint32_t(v.key>>32)]+len[uint32_t(v.key)]<=maxlen) unbucket(id);
      ++v.count; inbucket(id); pi[pos]=id; op[pos]=-1; on[pos]=v.head;
      if(v.head>=0) op[v.head]=pos;
      v.head=pos;
    };
    auto rem=[&](int pos) {
      if(pos<0 || pi[pos]<0) return;
      int id=pi[pos]; auto&v=pairs[id];
      if(len[uint32_t(v.key>>32)]+len[uint32_t(v.key)]<=maxlen) unbucket(id);
      --v.count; inbucket(id);
      if(op[pos]<0) v.head=on[pos]; else on[op[pos]]=on[pos];
      if(on[pos]>=0) op[on[pos]]=op[pos];
      op[pos]=on[pos]=pi[pos]=-1;
    };
    for(int i=0;i<n;i++) { pv[i]=i-1; nx[i]=i+1<n?i+1:-1; symbol[i]=raw[i]; }
    for(int i=0;i<n-1;i++) add(i);
    while((int)len.size()<maxsym) {
      while(best>1 && bucket[best]<0) --best;
      if(best<2) break;
      int id=bucket[best]; if(id<0) break;
      uint64_t key=pairs[id].key; uint32_t a=key>>32,b=key;
      int newid=(int)len.size();
      len.push_back(len[a]+len[b]); left.push_back(a); right.push_back(b);
      while(pairs[id].head>=0) {
        int p=pairs[id].head,q=nx[p],prev=pv[p],next=nx[q];
        rem(prev); rem(p); rem(q);
        symbol[p]=newid; nx[p]=next;
        if(next>=0) pv[next]=p;
        nx[q]=pv[q]=-1;
        add(prev); add(p);
      }
    }
    vector<int> counts(len.size(),0),stream;
    if(n) for(int p=0;p>=0;p=nx[p]) { stream.push_back(symbol[p]); ++counts[symbol[p]]; }
    vector<string> dict(len.size());
    for(int i=0;i<256;i++) dict[i].push_back((char)i);
    for(size_t i=256;i<len.size();i++) dict[i]=dict[left[i]]+dict[right[i]];
    vector<uint32_t> order;
    for(uint32_t i=0;i<len.size();i++) if(counts[i]) order.push_back(i);
    sort(order.begin(),order.end(),[&](uint32_t a,uint32_t b) {
      return counts[a]!=counts[b] ? counts[a]>counts[b] : a<b;
    });
    // Reparse the byte input optimally with the complete set of surviving phrases.
    // Each dynamic-programming edge emits one fixed-width direct descriptor.
    struct TrieNode { int head=-1,sibling=-1,id=-1; uint8_t ch=0; };
    vector<TrieNode> trie(1);
    for(uint32_t id:order) {
      int node=0;
      for(uint8_t ch:dict[id]) {
        int at=trie[node].head;
        while(at>=0 && trie[at].ch!=ch) at=trie[at].sibling;
        if(at<0) {
          TrieNode child; child.ch=ch; child.sibling=trie[node].head;
          at=(int)trie.size(); trie[node].head=at; trie.push_back(child);
        }
        node=at;
      }
      trie[node].id=(int)id;
    }
    const uint32_t unreachable=UINT32_MAX;
    vector<uint32_t> cost(size+1,unreachable);
    vector<uint16_t> choice(size);
    int first[256]; fill(first,first+256,-1);
    for(int at=trie[0].head;at>=0;at=trie[at].sibling) first[trie[at].ch]=at;
    cost[size]=0;
    for(int p=n-1;p>=0;--p) {
      int node=first[raw[p]],length=1;
      while(node>=0) {
        int id=trie[node].id;
        if(id>=0 && cost[(size_t)p+length]!=unreachable &&
           cost[(size_t)p+length]+1<cost[p]) {
          cost[p]=cost[(size_t)p+length]+1; choice[p]=(uint16_t)id;
        }
        if((size_t)p+length>=size) break;
        int at=trie[node].head;
        while(at>=0 && trie[at].ch!=raw[(size_t)p+length]) at=trie[at].sibling;
        node=at; ++length;
      }
    }
    if(cost[0]==unreachable) return -1;
    stream.clear(); fill(counts.begin(),counts.end(),0);
    for(size_t p=0;p<size;) {
      uint32_t id=choice[p]; stream.push_back((int)id); ++counts[id]; p+=len[id];
    }
    order.erase(remove_if(order.begin(),order.end(),[&](uint32_t id){return counts[id]==0;}),order.end());
    vector<uint32_t> renum(len.size(),0);
    for(uint32_t i=0;i<order.size();i++) renum[order[i]]=i;
    vector<uint32_t> packorder=order;
    sort(packorder.begin(),packorder.end(),[&](uint32_t a,uint32_t b) {
      return len[a]!=len[b] ? len[a]>len[b] : renum[a]<renum[b];
    });
    string pool;
    vector<string_view> seeds;
    for(uint32_t id:packorder) {
      if(pool.find(dict[id])==string::npos) { pool+=dict[id]; seeds.emplace_back(dict[id]); }
    }
    int ns=(int)seeds.size();
    vector<int> pre(ns,-1),nex(ns,-1),overlap(ns,0),root(ns);
    for(int i=0;i<ns;i++) root[i]=i;
    auto find=[&](int x) {
      while(root[x]!=x) { root[x]=root[root[x]]; x=root[x]; }
      return x;
    };
    for(int k=31;k>0;k--) {
      unordered_map<string_view,vector<int>> tab;
      for(int i=0;i<ns;i++)
        if(pre[i]<0 && (int)seeds[i].size()>=k)
          tab[seeds[i].substr(0,k)].push_back(i);
      for(int i=0;i<ns;i++) if(nex[i]<0 && (int)seeds[i].size()>=k) {
        auto it=tab.find(seeds[i].substr(seeds[i].size()-k));
        if(it==tab.end()) continue;
        for(int j:it->second) {
          if(pre[j]>=0 || find(i)==find(j)) continue;
          nex[i]=j; pre[j]=i; overlap[j]=k; root[find(j)]=find(i);
          break;
        }
      }
    }
    pool.clear();
    for(int i=0;i<ns;i++) if(pre[i]<0)
      for(int j=i;j>=0;j=nex[j]) pool.append(seeds[j].substr(overlap[j]));
    vector<uint32_t> desc(65536,0);
    for(uint32_t id:order) {
      size_t at=pool.find(dict[id]);
      if(at==string::npos) { at=pool.size(); pool+=dict[id]; }
      if(at>(UINT32_MAX>>6)) return -1;
      desc[renum[id]]=(uint32_t(at)<<6)|uint32_t(len[id]-1);
    }
    pool.append(64,'\0');
    if(pool.size()>(1u<<19)) return -1;
    size_t required=32+stream.size()*3+64+pool.size();
    if(required>capacity || required>(size_t)INT64_MAX) return -1;
    uint64_t header[4]={size,stream.size(),pool.size(),0x0034325443455244ULL};
    memcpy(archive,header,32);
    uint8_t* outputstream=archive+32;
    for(size_t i=0;i<stream.size();i++) {
      uint32_t old=desc[renum[stream[i]]];
      uint32_t v=((old>>6)<<5)|(old&31);
      outputstream[3*i]=(uint8_t)v;
      outputstream[3*i+1]=(uint8_t)(v>>8);
      outputstream[3*i+2]=(uint8_t)(v>>16);
    }
    memset(outputstream+3*stream.size(),0,64);
    memcpy(outputstream+3*stream.size()+64,pool.data(),pool.size());
    return (int64_t)required;
  } catch(...) { return -1; }
}