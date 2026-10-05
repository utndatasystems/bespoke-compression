#pragma once
#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <array>
#include <unordered_map>
#include <queue>
#include <algorithm>
struct Train {std::vector<std::array<uint16_t,2>> parents;std::vector<std::vector<uint16_t>> rows;};
static Train train(const uint8_t*raw,size_t n,size_t dictlimit=8192,size_t maxlen=32){
 Train result;if(!n)return result;if(dictlimit>65536)dictlimit=65536;if(dictlimit<256)dictlimit=256;
 struct Node{int32_t prev,next;uint16_t sym;};
 struct Pair{uint32_t key,count=0;bool dirty=false;std::vector<uint32_t> pos;};
 struct Item{uint32_t count,key,index;};
 struct Compare{bool operator()(const Item&a,const Item&b)const{return a.count!=b.count?a.count<b.count:a.key>b.key;}};
 std::vector<Node> nodes(n);std::vector<uint32_t> starts;std::vector<uint16_t> lens(256,1);lens.reserve(dictlimit);
 std::unordered_map<uint32_t,uint32_t> lookup;lookup.reserve(std::min<size_t>(n/2,1000000));
 std::vector<Pair> pairs;pairs.reserve(std::min<size_t>(n/2,1000000));std::vector<uint32_t> dirty;
 std::priority_queue<Item,std::vector<Item>,Compare> heap;
 auto touch=[&](uint32_t k){if(!pairs[k].dirty){pairs[k].dirty=true;dirty.push_back(k);}};
 auto add=[&](int32_t p){if(p<0||nodes[p].next<0)return;uint16_t a=nodes[p].sym,b=nodes[nodes[p].next].sym;if(size_t(lens[a])+lens[b]>maxlen)return;uint32_t key=(uint32_t(a)<<16)|b;auto it=lookup.find(key);uint32_t k;if(it==lookup.end()){k=pairs.size();lookup.emplace(key,k);pairs.push_back(Pair{key,0,false,{}});}else k=it->second;pairs[k].count++;pairs[k].pos.push_back(p);touch(k);};
 auto remove=[&](int32_t p){if(p<0||nodes[p].next<0)return;uint16_t a=nodes[p].sym,b=nodes[nodes[p].next].sym;if(size_t(lens[a])+lens[b]>maxlen)return;uint32_t key=(uint32_t(a)<<16)|b;auto it=lookup.find(key);if(it==lookup.end())return;uint32_t k=it->second;if(pairs[k].count)--pairs[k].count;touch(k);};
 for(size_t i=0;i<n;i++){bool first=i==0||raw[i-1]=='\n';bool last=i+1==n||raw[i]=='\n';nodes[i]={first?-1:int32_t(i-1),last?-1:int32_t(i+1),raw[i]};if(first)starts.push_back(i);}
 for(size_t i=0;i<n;i++)add(i);
 for(uint32_t k:dirty){if(pairs[k].count>1)heap.push(Item{pairs[k].count,pairs[k].key,k});pairs[k].dirty=false;}dirty.clear();
 while(lens.size()<dictlimit&&!heap.empty()){
  Item top=heap.top();heap.pop();if(pairs[top.index].count!=top.count||top.count<2)continue;
  uint16_t a=top.key>>16,b=top.key;uint16_t sym=lens.size();lens.push_back(lens[a]+lens[b]);result.parents.push_back({a,b});
  std::vector<uint32_t> positions;positions.swap(pairs[top.index].pos);
  for(uint32_t p:positions){if(nodes[p].next<0||nodes[p].sym!=a)continue;int32_t q=nodes[p].next;if(nodes[q].sym!=b)continue;
   int32_t previous=nodes[p].prev,next=nodes[q].next;remove(previous);remove(p);remove(q);
   nodes[p].sym=sym;nodes[p].next=next;if(next>=0)nodes[next].prev=p;nodes[q].next=-2;nodes[q].prev=-2;
   add(previous);add(p);
  }
  for(uint32_t k:dirty){if(pairs[k].count>1)heap.push(Item{pairs[k].count,pairs[k].key,k});pairs[k].dirty=false;}dirty.clear();
 }
 result.rows.reserve(starts.size());for(uint32_t start:starts){std::vector<uint16_t> row;for(int32_t p=start;p>=0;p=nodes[p].next)row.push_back(nodes[p].sym);result.rows.push_back(std::move(row));}
 return result;
}
