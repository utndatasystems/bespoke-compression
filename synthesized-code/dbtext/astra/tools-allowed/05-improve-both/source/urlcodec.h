#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <string>
#include <string_view>
#include <array>
#include <unordered_map>
#include <algorithm>
#include <zstd.h>
#include <immintrin.h>
struct UrlTemplate {std::string base;uint16_t pos=0;uint8_t fields=0;};
struct UrlRow {uint32_t off;uint16_t size,raw;uint16_t special;};
struct UrlEntry {alignas(32) uint8_t b[32];};
struct UrlState {const uint8_t* data=nullptr;uint32_t n=0;std::vector<UrlRow> rows;std::vector<UrlEntry> dict;std::vector<uint8_t> lens;std::vector<std::string> pre;std::vector<UrlTemplate> temp;std::vector<uint32_t> ht,pt;unsigned hm=0,pm=0;};
struct UrlHeader {uint32_t n,nd,np,nt,dc,dr,ix,data,hb,pb;};
static inline void url_put16(std::vector<uint8_t>&v,unsigned x){v.push_back(x);v.push_back(x>>8);}
static inline unsigned url_get16(const uint8_t*p){return p[0]|(unsigned(p[1])<<8);}
static inline unsigned url_code(const uint8_t*p,unsigned i){unsigned j=(i>>1)*3;return i&1?(p[j+1]>>4)|(unsigned(p[j+2])<<4):p[j]|((unsigned(p[j+1])&15)<<8);}
#ifdef ENCODER
#include "phrase.h"
static bool url_time(std::string_view s,UrlTemplate& t,uint32_t& val){
 if(s.size()<44||s.substr(0,29)!="http://reference.data.gov.uk/")return false;
 auto p=s.find('/',29);if(p==s.npos)return false;p=s.find('/',p+1);if(p==s.npos)return false;p++;
 if(p+13>=s.size())return false;auto digit=[&](size_t i){return i<s.size()&&s[i]>='0'&&s[i]<='9';};
 for(unsigned i:{0u,1u,2u,3u,5u,6u,8u,9u,11u,12u})if(!digit(p+i))return false;
 if(s[p+4]!='-'||s[p+7]!='-'||s[p+10]!='T')return false;
 auto two=[&](unsigned i){return unsigned((s[p+i]-'0')*10+s[p+i+1]-'0');};
 unsigned year=two(0)*100+two(2),mon=two(5),day=two(8),hr=two(11),mi=0,se=0,f=0;
 if(year<1960||year>2023||mon>15||day>31||hr>31)return false;
 size_t e=p+13;if(e+3<s.size()&&s[e]==':'&&digit(e+1)&&digit(e+2)){f++;mi=two(14);e+=3;if(e+3<s.size()&&s[e]==':'&&digit(e+1)&&digit(e+2)){f++;se=two(17);e+=3;}}
 if(mi>63||se>63||s.size()>65535||p>65535)return false;
 val=(year-1960)|(mon<<6)|(day<<10)|(hr<<15)|(mi<<20)|(se<<26);
 t.base=std::string(s);t.base.replace(p,e-p,std::string("1960-00-00T00:00:00",e-p));t.pos=p;t.fields=f;return true;
}
static std::vector<uint8_t> url_encode(const uint8_t* raw,size_t size){
 std::vector<std::string_view> rows;size_t start=0;for(size_t i=0;i<size;i++)if(raw[i]=='\n'){rows.emplace_back((const char*)raw+start,i+1-start);start=i+1;}if(start!=size||rows.size()<100)return {};
 for(auto s:rows)if(s.substr(0,7)!="http://"&&s.substr(0,8)!="https://")return {};
 std::unordered_map<std::string,unsigned> tc;std::vector<UrlTemplate> parsed(rows.size());std::vector<uint32_t> vals(rows.size());
 for(size_t i=0;i<rows.size();i++)if(url_time(rows[i],parsed[i],vals[i]))tc[parsed[i].base]++;
 std::vector<std::pair<unsigned,std::string>> rank;for(auto&[s,n]:tc)if(n>=8)rank.emplace_back(n,s);std::sort(rank.begin(),rank.end(),[](auto&a,auto&b){return a.first!=b.first?a.first>b.first:a.second<b.second;});if(rank.size()>127)rank.resize(127);
 std::unordered_map<std::string,unsigned> tm;std::vector<UrlTemplate> templates;for(auto& [n,s]:rank){tm[s]=templates.size();UrlTemplate t;uint32_t v;url_time(s,t,v);templates.push_back(std::move(t));}
 if(templates.empty())return {};
 std::vector<int> spec(rows.size(),-1);std::vector<unsigned> generic;for(unsigned i=0;i<rows.size();i++){auto it=tm.find(parsed[i].base);if(it!=tm.end())spec[i]=it->second;else generic.push_back(i);}
 std::unordered_map<std::string,unsigned> pc;for(unsigned i:generic){auto s=rows[i];for(unsigned j=8;j<s.size()&&j<=180;j++)if(s[j]=='/')pc[std::string(s.substr(0,j+1))]++;}
 std::vector<std::string> prefs(rows.size());std::unordered_map<std::string,unsigned> pf;
 for(unsigned i:generic){auto s=rows[i];for(unsigned j=8;j<s.size()&&j<=180;j++)if(s[j]=='/'){std::string p(s.substr(0,j+1));if(pc[p]>=8)prefs[i]=std::move(p);}pf[prefs[i]]++;}
 rank.clear();for(auto&[s,n]:pf)rank.emplace_back(n,s);std::sort(rank.begin(),rank.end(),[](auto&a,auto&b){return a.first!=b.first?a.first>b.first:a.second<b.second;});if(rank.size()>32768)return {};
 std::vector<std::string> prefixes;std::unordered_map<std::string,unsigned> pm;for(auto&[n,s]:rank){pm[s]=prefixes.size();prefixes.push_back(s);}
 std::vector<std::string_view> tails;for(unsigned i:generic)tails.push_back(rows[i].substr(prefs[i].size()));
 auto r=phrase::train(tails,16384,64,32);phrase::reparse_huffman(tails,r,15,3);auto hf=phrase::huffman(r,15);phrase::PhraseEncoded pr;pr.dict=prefixes;for(unsigned i:generic)pr.tokens.push_back(pm[prefs[i]]);auto ph=phrase::huffman(pr,14);
 std::vector<uint8_t> dict;for(unsigned j=0;j<r.dict.size();j++){auto&s=r.dict[j];dict.push_back(s.size());dict.push_back(hf.lengths[j]);dict.insert(dict.end(),s.begin(),s.end());}for(unsigned j=0;j<prefixes.size();j++){auto&s=prefixes[j];url_put16(dict,s.size());dict.push_back(ph.lengths[j]);dict.insert(dict.end(),s.begin(),s.end());}for(auto&t:templates){url_put16(dict,t.base.size());url_put16(dict,t.pos);dict.push_back(t.fields);dict.insert(dict.end(),t.base.begin(),t.base.end());}
 for(auto row:rows){if(row.size()>65535)return {};if(row.size()<255)dict.push_back(row.size());else{dict.push_back(255);url_put16(dict,row.size());}}
 std::vector<uint8_t> cd(ZSTD_compressBound(dict.size()));size_t ds=ZSTD_compress(cd.data(),cd.size(),dict.data(),dict.size(),9);if(ZSTD_isError(ds))return {};cd.resize(ds);
 std::vector<uint8_t> ix,data;unsigned gi=0;uint64_t buf=0;unsigned used=0;uint32_t totalbits=0;
 auto put=[&](uint32_t code,unsigned bits){buf|=uint64_t(code)<<used;used+=bits;totalbits+=bits;while(used>=8){data.push_back(buf);buf>>=8;used-=8;}};
 for(unsigned i=0;i<rows.size();i++){
  if(spec[i]>=0){ix.push_back(128+spec[i]);put(vals[i],32);}
  else {unsigned start=totalbits,pr=pm[prefs[i]];put(ph.codes[pr],ph.lengths[pr]);
   unsigned j=r.row_token_offsets[gi],e=r.row_token_offsets[gi+1];for(;j<e;j++){unsigned c=r.tokens[j];put(hf.codes[c],hf.lengths[c]);}gi++;
   unsigned n=totalbits-start;if(n<127)ix.push_back(n);else{if(n>65535)return {};ix.push_back(127);url_put16(ix,n);}
  }
 }
 if(used)data.push_back(buf);
 UrlHeader h{uint32_t(rows.size()),uint32_t(r.dict.size()),uint32_t(prefixes.size()),uint32_t(templates.size()),uint32_t(cd.size()),uint32_t(dict.size()),uint32_t(ix.size()),uint32_t(data.size()),uint32_t(hf.maxbits),uint32_t(ph.maxbits)};std::vector<uint8_t> out(sizeof(h));memcpy(out.data(),&h,sizeof(h));out.insert(out.end(),cd.begin(),cd.end());out.insert(out.end(),ix.begin(),ix.end());out.insert(out.end(),data.begin(),data.end());out.resize(out.size()+8);return out;
}
#endif
static inline uint64_t url_window(const uint8_t* p,unsigned bit){uint64_t v;memcpy(&v,p+(bit>>3),8);return v>>(bit&7);}
static bool url_table(std::vector<uint32_t>&table,const std::vector<uint8_t>&bits,unsigned maxbits){
 if(maxbits<1||maxbits>16)return false;unsigned count[17]={},next[17]={};for(auto b:bits){if(!b||b>maxbits)return false;count[b]++;}unsigned code=0;
 for(unsigned b=1;b<=maxbits;b++){code=(code+count[b-1])<<1;next[b]=code;if(code+count[b]>(1u<<b))return false;}table.assign(1u<<maxbits,0);
 for(unsigned i=0;i<bits.size();i++){unsigned n=bits[i],c=next[n]++,rev=0;for(unsigned j=0;j<n;j++){rev=(rev<<1)|(c&1);c>>=1;}for(unsigned j=rev;j<table.size();j+=1u<<n)table[j]=i|(n<<16);}return true;
}
static bool url_open(UrlState& st,const uint8_t* p,size_t size){
 if(size<sizeof(UrlHeader))return false;UrlHeader h;memcpy(&h,p,sizeof(h));if(h.n>1000000||h.nd>16384||h.np>32768||h.nt>127||h.dr>16777216||uint64_t(sizeof(h))+h.dc+h.ix+h.data+8!=size)return false;
 std::vector<uint8_t>d(h.dr);size_t z=ZSTD_decompress(d.data(),d.size(),p+sizeof(h),h.dc);if(ZSTD_isError(z)||z!=h.dr)return false;size_t pos=0;std::vector<uint8_t> bits(h.nd),pbits(h.np);
 st.dict.resize(h.nd);st.lens.resize(h.nd);for(unsigned i=0;i<h.nd;i++){if(pos+2>d.size())return false;unsigned n=d[pos++];bits[i]=d[pos++];if(!n||n>32||pos+n>d.size())return false;st.lens[i]=n;memcpy(st.dict[i].b,d.data()+pos,n);pos+=n;}
 for(unsigned i=0;i<h.np;i++){if(pos+3>d.size())return false;unsigned n=url_get16(d.data()+pos);pbits[i]=d[pos+2];pos+=3;if(pos+n>d.size())return false;st.pre.emplace_back((const char*)d.data()+pos,n);pos+=n;}
 for(unsigned i=0;i<h.nt;i++){if(pos+5>d.size())return false;unsigned n=url_get16(d.data()+pos),dp=url_get16(d.data()+pos+2),f=d[pos+4];pos+=5;if(f>2||dp+13+3*f>n||pos+n>d.size())return false;st.temp.push_back({std::string((const char*)d.data()+pos,n),uint16_t(dp),uint8_t(f)});pos+=n;}
 std::vector<uint16_t> rawlens(h.n);for(unsigned i=0;i<h.n;i++){if(pos>=d.size())return false;unsigned n=d[pos++];if(n==255){if(pos+2>d.size())return false;n=url_get16(d.data()+pos);pos+=2;}rawlens[i]=n;}if(pos!=d.size()||!url_table(st.ht,bits,h.hb)||!url_table(st.pt,pbits,h.pb))return false;st.hm=st.ht.size()-1;st.pm=st.pt.size()-1;
 const uint8_t* ix=p+sizeof(h)+h.dc;st.data=ix+h.ix;st.n=h.n;st.rows.resize(h.n);pos=0;uint64_t off=0;
 for(unsigned i=0;i<h.n;i++){if(pos>=h.ix)return false;unsigned v=ix[pos++],n=0,raw=0,special=65535;
  if(v>=128){special=v-128;if(special>=h.nt)return false;n=32;raw=st.temp[special].base.size();}
  else {n=v;if(n==127){if(pos+2>h.ix)return false;n=url_get16(ix+pos);pos+=2;}if(!n||off+n>uint64_t(h.data)*8)return false;raw=rawlens[i];}

  if(off+n>uint64_t(h.data)*8||off+n>UINT32_MAX)return false;st.rows[i]={uint32_t(off),uint16_t(n),uint16_t(raw),uint16_t(special)};off+=n;
 }
 return pos==h.ix&&(off+7)/8==h.data;
}
static inline unsigned url_rowlen(const UrlState& st,uint32_t id){return st.rows[id].raw;}
static inline void url_two(uint8_t*p,unsigned x){p[0]='0'+x/10;p[1]='0'+x%10;}
static inline void url_copy(uint8_t*dst,const uint8_t*src,size_t n){
 if(n>=32){_mm256_storeu_si256((__m256i*)dst,_mm256_loadu_si256((const __m256i*)src));if(n>32){for(size_t i=32;i+32<n;i+=32)_mm256_storeu_si256((__m256i*)(dst+i),_mm256_loadu_si256((const __m256i*)(src+i)));_mm256_storeu_si256((__m256i*)(dst+n-32),_mm256_loadu_si256((const __m256i*)(src+n-32)));}}
 else if(n>=16){_mm_storeu_si128((__m128i*)dst,_mm_loadu_si128((const __m128i*)src));_mm_storeu_si128((__m128i*)(dst+n-16),_mm_loadu_si128((const __m128i*)(src+n-16)));}
 else if(n>=8){uint64_t a,b;memcpy(&a,src,8);memcpy(&b,src+n-8,8);memcpy(dst,&a,8);memcpy(dst+n-8,&b,8);}else memcpy(dst,src,n);
}
static inline unsigned url_row(const UrlState& st,uint32_t id,uint8_t* out,size_t capacity){
 const UrlRow&r=st.rows[id];if(capacity<r.raw)return UINT32_MAX;
 if(r.special!=65535){const auto&t=st.temp[r.special];url_copy(out,(const uint8_t*)t.base.data(),t.base.size());uint32_t v=url_window(st.data,r.off);uint8_t*q=out+t.pos;unsigned yr=1960+(v&63);url_two(q,yr/100);url_two(q+2,yr%100);url_two(q+5,(v>>6)&15);url_two(q+8,(v>>10)&31);url_two(q+11,(v>>15)&31);if(t.fields){url_two(q+14,(v>>20)&63);if(t.fields==2)url_two(q+17,v>>26);}return r.raw;}
 unsigned bit=r.off,end=bit+r.size;uint64_t buffer=url_window(st.data,bit);const uint8_t*next=st.data+(bit>>3)+8;unsigned have=64-(bit&7);
 unsigned pr=st.pt[buffer&st.pm],nb=pr>>16;if(!nb||nb>end-bit)return UINT32_MAX;bit+=nb;buffer>>=nb;have-=nb;const auto&prefix=st.pre[pr&65535];if(prefix.size()>capacity)return UINT32_MAX;url_copy(out,(const uint8_t*)prefix.data(),prefix.size());size_t pos=prefix.size();
 while(bit<end){if(have<16&&end-bit>have){uint64_t v;memcpy(&v,next,8);buffer|=v<<have;unsigned bytes=(64-have)>>3;have+=bytes*8;next+=bytes;}unsigned h=st.ht[buffer&st.hm];nb=h>>16;if(!nb||nb>end-bit)return UINT32_MAX;bit+=nb;buffer>>=nb;have-=nb;unsigned c=h&65535,n=st.lens[c];if(n>capacity-pos)return UINT32_MAX;if(capacity-pos>=32)_mm256_storeu_si256((__m256i*)(out+pos),_mm256_load_si256((const __m256i*)st.dict[c].b));else memcpy(out+pos,st.dict[c].b,n);pos+=n;}return pos==r.raw?r.raw:UINT32_MAX;
}
