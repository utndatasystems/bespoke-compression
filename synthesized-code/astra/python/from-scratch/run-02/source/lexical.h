#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#ifndef DECODE_ONLY
#include <vector>
#include <string_view>
#include <unordered_map>
#include <algorithm>
#endif
namespace lexical {
inline uint16_t r16(const uint8_t*p){return unsigned(p[0])|(unsigned(p[1])<<8);}
inline uint32_t r32(const uint8_t*p){return uint32_t(r16(p))|(uint32_t(r16(p+2))<<16);}
inline bool word(uint8_t c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';}
inline bool decode(const uint8_t*src,size_t size,const uint8_t*dict,size_t ds,uint8_t*out,size_t n){
 if(ds<8)return false;unsigned mode=dict[0],single=r16(dict+2),count=r32(dict+4);if((mode!=1&&mode!=2)||single>127||count>65280)return false;
 if(mode==1&&count>single+(127-single)*256)return false;
 const uint8_t**words=(const uint8_t**)malloc((count?count:1)*sizeof(uint8_t*));uint16_t*lens=(uint16_t*)malloc((count?count:1)*sizeof(uint16_t));if(!words||!lens){free(words);free(lens);return false;}
 const uint8_t*p=dict+8,*de=dict+ds;bool ok=true;for(unsigned i=0;i<count;i++){if(size_t(de-p)<2){ok=false;break;}unsigned l=r16(p);p+=2;if(!l||l>size_t(de-p)){ok=false;break;}words[i]=p;lens[i]=l;p+=l;}if(p!=de)ok=false;
 size_t pos=0,k=0;while(ok&&k<size){unsigned id,c=src[k++];if(mode==1){if(c<128){if(pos==n){ok=false;break;}out[pos++]=c;continue;}if(c==255){if(k==size||pos==n){ok=false;break;}out[pos++]=src[k++];continue;}if(c<128+single)id=c-128;else{if(k==size){ok=false;break;}id=single+(c-128-single)*256+src[k++];}}else{if(k==size){ok=false;break;}id=c|(unsigned(src[k++])<<8);if(id<256){if(pos==n){ok=false;break;}out[pos++]=id;continue;}id-=256;}if(id>=count||lens[id]>n-pos){ok=false;break;}memcpy(out+pos,words[id],lens[id]);pos+=lens[id];}
 free(words);free(lens);return ok&&pos==n&&k==size;
}
#ifndef DECODE_ONLY
struct Encoded {std::vector<uint8_t> transformed,dictionary;};
inline void w16(std::vector<uint8_t>&v,unsigned x){v.push_back(x);v.push_back(x>>8);}inline void w32(std::vector<uint8_t>&v,unsigned x){w16(v,x);w16(v,x>>16);}
inline Encoded encode(const uint8_t*raw,size_t n,unsigned mode=1,unsigned limit=8192,unsigned single=64){
 if(mode==1)limit=std::min(limit,single+(127-single)*256);else limit=std::min(limit,65280u);
 std::unordered_map<std::string_view,uint32_t> freq;freq.reserve(1000000);
 for(size_t p=0;p<n;){if(!word(raw[p])){p++;continue;}size_t q=p+1;while(q<n&&word(raw[q]))q++;if(q-p>=2&&q-p<=65535)freq[std::string_view((const char*)raw+p,q-p)]++;p=q;}
 struct Entry{std::string_view text;uint32_t count;};std::vector<Entry> entries;entries.reserve(freq.size());
 for(auto x:freq)if(x.second>=2)entries.push_back({x.first,x.second});
 auto gain=[mode](const Entry&a){return ((mode==2?2:1)*int64_t(a.text.size())-2)*a.count-int64_t(a.text.size()+2);};
 auto byfreq=[](const Entry&a,const Entry&b){return a.count==b.count?a.text<b.text:a.count>b.count;};
 std::vector<Entry> selected;
 if(mode==1&&single){
  std::sort(entries.begin(),entries.end(),byfreq);
  unsigned ns=std::min({single,limit,unsigned(entries.size())});
  selected.insert(selected.end(),entries.begin(),entries.begin()+ns);entries.erase(entries.begin(),entries.begin()+ns);
 }
 std::sort(entries.begin(),entries.end(),[&](const Entry&a,const Entry&b){auto x=gain(a),y=gain(b);return x==y?a.text<b.text:x>y;});
 while(!entries.empty()&&gain(entries.back())<=0)entries.pop_back();
 if(entries.size()>limit-selected.size())entries.resize(limit-selected.size());
 std::sort(entries.begin(),entries.end(),byfreq);
 selected.insert(selected.end(),entries.begin(),entries.end());entries=std::move(selected);
 std::unordered_map<std::string_view,uint32_t> ids;ids.reserve(entries.size()*2);Encoded result;result.dictionary.push_back(mode);result.dictionary.push_back(0);w16(result.dictionary,single);w32(result.dictionary,entries.size());
 for(unsigned i=0;i<entries.size();i++){ids[entries[i].text]=i;w16(result.dictionary,entries[i].text.size());result.dictionary.insert(result.dictionary.end(),entries[i].text.begin(),entries[i].text.end());}
 result.transformed.reserve(n);
 auto emitraw=[&](uint8_t c){if(mode==2)w16(result.transformed,c);else{if(c>=128)result.transformed.push_back(255);result.transformed.push_back(c);}};
 for(size_t p=0;p<n;){if(!word(raw[p])){emitraw(raw[p++]);continue;}size_t q=p+1;while(q<n&&word(raw[q]))q++;auto it=ids.find(std::string_view((const char*)raw+p,q-p));if(it==ids.end()){while(p<q)emitraw(raw[p++]);continue;}unsigned id=it->second;if(mode==2)w16(result.transformed,256+id);else if(id<single)result.transformed.push_back(128+id);else{id-=single;result.transformed.push_back(128+single+(id>>8));result.transformed.push_back(id);}p=q;}
 return result;
}
#endif
}
