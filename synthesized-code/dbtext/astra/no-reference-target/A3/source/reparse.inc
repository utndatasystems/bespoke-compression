 // Reparse rows optimally using the learned phrase dictionary.
 struct Trie { uint32_t child[256]{}; int id=-1; };std::vector<Trie> trie(1);
 for(unsigned id=0;id<dict.size();id++){unsigned t=0;for(uint8_t c:dict[id]){unsigned k=trie[t].child[c];if(!k){k=trie.size();trie[t].child[c]=k;trie.emplace_back();}t=k;}trie[t].id=id;}
 seq.clear(); std::vector<uint32_t> cost;std::vector<uint16_t> choice;size_t begin=0;
 for(size_t end=0;end<=n;end++)if(end==n||(end&&raw[end-1]==10)){
  size_t len=end-begin;if(!len)continue;cost.assign(len+1,0);choice.resize(len);
  for(size_t j=len;j-->0;){unsigned t=0,best=~0u,bid=raw[begin+j];for(size_t k=j;k<len&&k<j+24;k++){t=trie[t].child[raw[begin+k]];if(!t)break;int id=trie[t].id;if(id>=0&&cost[k+1]+1<best){best=cost[k+1]+1;bid=id;}}cost[j]=best;choice[j]=bid;}
  for(size_t j=0;j<len;){seq.push_back(choice[j]);j+=dict[choice[j]].size();}seq.push_back(65535);begin=end;
 }
