
#include <bits/stdc++.h>
using namespace std;
struct P {int a,b,head=-1,cnt=0,bound=0;};
struct Node{int s,pr,nx,op=-1,on=-1,pi=-1;};
vector<P> ps; vector<Node> ns; unordered_map<uint64_t,int> dict; priority_queue<pair<int,int>> pq;
void rem(int x){if(x<0)return;auto &n=ns[x];if(n.pi<0)return;auto &p=ps[n.pi];
if(n.op>=0)ns[n.op].on=n.on;else p.head=n.on;
if(n.on>=0)ns[n.on].op=n.op;
p.cnt--;n.pi=-1;n.op=n.on=-1;}
void add(int x){if(x<0||ns[x].nx<0)return;auto &n=ns[x];uint64_t key=((uint64_t)n.s<<32)|ns[n.nx].s;
auto it=dict.find(key);int id;
if(it==dict.end()){id=ps.size();dict[key]=id;ps.push_back({n.s,ns[n.nx].s});}else id=it->second;
auto &p=ps[id];n.pi=id;n.op=-1;n.on=p.head;if(p.head>=0)ns[p.head].op=x;p.head=x;p.cnt++;
if(p.cnt>max(1,p.bound)){p.bound=p.cnt;pq.emplace(p.cnt,id);}
}
int main(int argc,char**argv){int maxsym=argc>1?atoi(argv[1]):65536;bool lex=argc>2?atoi(argv[2]):false;
ifstream f("/inputs/queries.nul",ios::binary);string s((istreambuf_iterator<char>(f)),{});
vector<string> words;unordered_map<string,int> wi;
if(lex){
 for(int i=0;i<s.size();){int st=i;unsigned char c=s[i];if(isalpha(c)||c=='_'){i++;while(i<s.size()&&(isalnum((unsigned char)s[i])||s[i]=='_'))i++;}
 else if(isdigit(c)){i++;while(i<s.size()&&isdigit((unsigned char)s[i]))i++;}
 else if(isspace(c)){i++;while(i<s.size()&&isspace((unsigned char)s[i]))i++;}
 else i++;
 string w=s.substr(st,i-st);int v;auto it=wi.find(w);if(it==wi.end()){v=words.size();wi[w]=v;words.push_back(w);}else v=it->second;
 ns.push_back({v,(int)ns.size()-1,(int)ns.size()+1});
 }
}else{for(int i=0;i<256;i++)words.push_back(string(1,i));for(unsigned char c:s)ns.push_back({c,(int)ns.size()-1,(int)ns.size()+1});}
ns.back().nx=-1;dict.reserve(2000000); ps.reserve(2000000);
cerr<<"initial "<<ns.size()<<" symbols "<<words.size()<<"\n";
vector<pair<int,int>> rules;
vector<int> lens;for(auto &w:words)lens.push_back(w.size());
for(int i=0;i<(int)ns.size()-1;i++)add(i);
int total=ns.size();
while(words.size()+rules.size()<maxsym&&!pq.empty()){
 auto [cnt,id]=pq.top();pq.pop();auto &p=ps[id];if(cnt!=p.bound)continue;
 p.bound=0;if(p.cnt!=cnt){if(p.cnt>=2){p.bound=p.cnt;pq.emplace(p.cnt,id);}continue;}
 if(cnt<2)break;
 int a=p.a,b=p.b,newid=words.size()+rules.size();
 rules.emplace_back(a,b);lens.push_back(lens[a]+lens[b]);int merges=0;
 while(ps[id].head>=0){int x=ps[id].head,y=ns[x].nx,pr=ns[x].pr,nx=ns[y].nx;
  rem(pr);rem(x);rem(y);ns[x].s=newid;ns[x].nx=nx;if(nx>=0)ns[nx].pr=x;ns[y].nx=ns[y].pr=-2;
  add(pr);add(x);total--;merges++;
 }
 if(newid%4096==0){cerr<<"sym "<<newid<<" count "<<cnt<<" merges "<<merges<<" remaining "<<total<<" pairs "<<ps.size()<<" heap "<<pq.size()<<"\n";}
}
vector<int> out;for(int x=0;x>=0;x=ns[x].nx)out.push_back(ns[x].s);
vector<int> freq(lens.size());long dictbytes=0;for(auto &w:words)dictbytes+=w.size();for(int x:out)freq[x]++;
double ent=0;int active=0;long expanded=0;for(int i=0;i<freq.size();i++){if(freq[i]){ent+=freq[i]*log2(double(out.size())/freq[i]);active++;}expanded+=lens[i];}
cout<<"initialdict "<<words.size()<<" dictbytes "<<dictbytes<<" rules "<<rules.size()<<" stream "<<out.size()<<" active "<<active<<" entropybytes "<<ent/8<<" expanded "<<expanded<<" avglen "<<double(s.size())/out.size()<<"\n";
cout<<"fixed16 "<<2*out.size()+4*rules.size()+dictbytes<<" entropy+4rules "<<ent/8+4*rules.size()+dictbytes<<"\n";
ofstream o(argc>3?argv[3]:"/work/analysis_repair.bin",ios::binary);uint32_t hdr[]={uint32_t(words.size()),uint32_t(rules.size()),uint32_t(out.size())};o.write((char*)hdr,sizeof hdr);
for(auto&w:words){uint32_t len=w.size();o.write((char*)&len,4);o.write(w.data(),w.size());}
for(auto[a,b]:rules){uint32_t x[2]={uint32_t(a),uint32_t(b)};o.write((char*)x,8);}o.write((char*)out.data(),4*out.size());
}
