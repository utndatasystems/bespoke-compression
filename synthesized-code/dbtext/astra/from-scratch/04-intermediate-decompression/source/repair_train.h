
#pragma once
#include <queue>
static void repair_train(std::vector<std::vector<uint16_t>>& rows,std::vector<std::string>&dict,unsigned target){
 struct Node{int prev,next;uint16_t sym;};
 struct P{int count=0;std::vector<int>occ;};
 struct H{int count;uint32_t key;bool operator<(const H&o)const{return count<o.count||(count==o.count&&key>o.key);}};
 std::vector<Node>nodes;std::vector<int>starts;size_t n=0;for(auto&r:rows)n+=r.size();nodes.reserve(n);
 for(auto&r:rows){int st=nodes.size();starts.push_back(st);for(unsigned i=0;i<r.size();i++)nodes.push_back({i?int(nodes.size())-1:-1,i+1<r.size()?int(nodes.size())+1:-1,r[i]});}
 std::unordered_map<uint32_t,P>ps;ps.reserve(n/2);
 auto valid=[&](int pos){return pos>=0&&nodes[pos].prev!=-2&&nodes[pos].next>=0&&dict[nodes[pos].sym].size()+dict[nodes[nodes[pos].next].sym].size()<=16;};
 auto keyat=[&](int pos){return (uint32_t(nodes[pos].sym)<<16)|nodes[nodes[pos].next].sym;};
 for(int i=0;i<int(nodes.size());i++)if(valid(i)){auto&p=ps[keyat(i)];p.count++;p.occ.push_back(i);}
 std::priority_queue<H>heap;for(auto&kv:ps)if(kv.second.count>=3)heap.push({kv.second.count,kv.first});
 std::vector<uint32_t>affected;
 while(dict.size()<target&&!heap.empty()){
  auto h=heap.top();heap.pop();auto found=ps.find(h.key);if(found==ps.end()||found->second.count!=h.count||h.count<3)continue;
  std::vector<int>occ;occ.swap(found->second.occ);uint16_t newsym=dict.size();dict.push_back(dict[h.key>>16]+dict[h.key&65535]);affected.clear();
  auto remove=[&](int pos){if(valid(pos)){uint32_t k=keyat(pos);ps[k].count--;affected.push_back(k);}};
  auto add=[&](int pos){if(valid(pos)){uint32_t k=keyat(pos);auto&p=ps[k];p.count++;p.occ.push_back(pos);affected.push_back(k);}};
  for(int pos:occ){if(!valid(pos)||keyat(pos)!=h.key)continue;int a=nodes[pos].prev,b=nodes[pos].next,c=nodes[b].next;remove(a);remove(pos);remove(b);nodes[pos].sym=newsym;nodes[pos].next=c;if(c>=0)nodes[c].prev=pos;nodes[b].prev=-2;nodes[b].next=-1;add(a);add(pos);}
  std::sort(affected.begin(),affected.end());affected.erase(std::unique(affected.begin(),affected.end()),affected.end());
  for(uint32_t k:affected){auto&p=ps[k];if(p.count>=3)heap.push({p.count,k});}
 }
 for(unsigned i=0;i<rows.size();i++){auto&r=rows[i];r.clear();for(int p=starts[i];p>=0;p=nodes[p].next)r.push_back(nodes[p].sym);}
}
