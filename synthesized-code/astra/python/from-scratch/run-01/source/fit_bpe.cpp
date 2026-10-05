#include <bits/stdc++.h>
using namespace std;
int main(int argc,char**argv){
ifstream f("/inputs/python-source.py",ios::binary); string s((istreambuf_iterator<char>(f)),{}); vector<string> dict;for(int c=0;c<256;c++)dict.emplace_back(1,char(c));
vector<uint32_t> a; size_t sample=argc>1?atol(argv[1]):10000000;
for(size_t p=0;p<s.size();p+=100000) for(size_t j=p;j<min(s.size(),p+sample/1000);j++) a.push_back((uint8_t)s[j]);
fprintf(stderr,"training %zu\n",a.size());
for(int round=0;dict.size()<65536;round++){
unordered_map<uint64_t,uint32_t> counts; counts.reserve(a.size()/2);
for(size_t i=0;i+1<a.size();i++){ if(dict[a[i]].size()+dict[a[i+1]].size()<=64) counts[(uint64_t(a[i])<<32)|a[i+1]]++;}
vector<pair<uint32_t,uint64_t>> ranks; ranks.reserve(counts.size());for(auto [key,n]:counts)if(n>3)ranks.push_back({n,key});
if(ranks.empty())break;size_t adds=min(size_t(1024),min(ranks.size(),65536-dict.size())); partial_sort(ranks.begin(),ranks.begin()+adds,ranks.end(),greater<>());
unordered_map<uint64_t,uint32_t> repl;
for(size_t i=0;i<adds;i++){ auto [n,key]=ranks[i]; repl[key]=dict.size();dict.push_back(dict[key>>32]+dict[uint32_t(key)]); }
size_t out=0;for(size_t i=0;i<a.size();i++){auto it=i+1<a.size()?repl.find((uint64_t(a[i])<<32)|a[i+1]):repl.end();if(it!=repl.end()){a[out++]=it->second;i++;}else a[out++]=a[i];}a.resize(out);
fprintf(stderr,"round %d vocab %zu seq %zu best %u last %u\n",round,dict.size(),a.size(),ranks[0].first,ranks[adds-1].first);
}
ofstream of(argc>2?argv[2]:"/work/grammar/fitting/dict.bin",ios::binary);for(auto &d:dict){uint8_t len=d.size();of.write((char*)&len,1);of.write(d.data(),len);} }
