#pragma once
#include "dict_url.hpp"
#include "fsst/fsst.h"
#include <zstd.h>
#include <stdexcept>
#include <string_view>
namespace dicturlcodec4 {
inline void put32(std::vector<uint8_t>&o,uint32_t v){for(int i=0;i<4;i++)o.push_back(v>>(8*i));}
inline uint32_t get32(const uint8_t*p){uint32_t v;memcpy(&v,p,4);return v;}
struct State{uint64_t rawsize;uint32_t rows;dicturl::State numeric;fsst_decoder_t fsst;std::vector<uint8_t> dict;std::vector<uint32_t> pofs,idx;const uint8_t*data;};
// Format ULR4: compressed shared metadata followed by self-delimiting row codes.
// Opening scans code lengths/terminators to construct offsets, never row bytes.
inline State*open(const uint8_t*p,size_t size){
 if(size<64||get32(p)!=0x34524c55)return nullptr;const uint8_t*end=p+size;auto*s=new State;
 auto fail=[&]()->State*{delete s;return nullptr;};
 s->rows=get32(p+4);s->rawsize=dicturl::get64(p+8);uint32_t ts=get32(p+16),pcs=get32(p+20),dcs=get32(p+24),ds=get32(p+28);p+=32;
 constexpr size_t fs=sizeof(fsst_decoder_t);
 if(!s->rows||s->rows>10000000||pcs>32768||ds>512u*1024*1024||uint64_t(fs)+ts+2ull*pcs>ds||dcs>size_t(end-p)-32)return fail();
 s->dict.resize(size_t(ds)+32);size_t n=ZSTD_decompress(s->dict.data(),ds,p,dcs);if(ZSTD_isError(n)||n!=ds)return fail();p+=dcs;memcpy(&s->fsst,s->dict.data(),fs);
 uint8_t terminal[255];for(unsigned c=0;c<255;c++){unsigned len=s->fsst.len[c];if(!len||len>8)return fail();const uint8_t*q=(const uint8_t*)&s->fsst.symbol[c];for(unsigned j=0;j+1<len;j++)if(q[j]==10)return fail();terminal[c]=q[len-1]==10;}
 if(dicturl::open(s->numeric,s->dict.data()+fs,ts)!=ts||s->numeric.t.size()>32767)return fail();
 for(const auto&t:s->numeric.t){if(!t.len||t.fixed[t.len-1]!=10)return fail();if(t.len<64&&(t.mask[0]>>t.len))return fail();if(t.len<=64&&t.mask[1])return fail();if(t.len>64&&t.len<128&&(t.mask[1]>>(t.len-64)))return fail();if(__builtin_popcountll(t.mask[0])>32||__builtin_popcountll(t.mask[1])>32)return fail();}
 s->pofs.resize(pcs+1);s->pofs[0]=fs+ts+2*pcs;for(uint32_t i=0;i<pcs;i++){s->pofs[i+1]=s->pofs[i]+dicturl::get16(s->dict.data()+fs+ts+2*i);if(s->pofs[i+1]>ds)return fail();for(uint32_t j=s->pofs[i];j<s->pofs[i+1];j++)if(s->dict[j]==10)return fail();}if(s->pofs.back()!=ds)return fail();
 s->data=p;s->idx.resize(size_t(s->rows)+1);const uint8_t*payload_end=end-32;uint64_t totalraw=0;
 for(uint32_t i=0;i<s->rows;i++){if(p>=payload_end||size_t(p-s->data)>UINT32_MAX)return fail();s->idx[i]=p-s->data;
  if(*p>=128){if(payload_end-p<2)return fail();uint16_t id=(*p&127)|(uint16_t(p[1])<<7);if(id>=pcs)return fail();p+=2;uint64_t rowlen=s->pofs[id+1]-s->pofs[id];bool done=false;
   while(p<payload_end){unsigned c=*p++;if(c==255){if(p==payload_end)return fail();c=*p++;rowlen++;if(c==10){done=true;break;}}else{rowlen+=s->fsst.len[c];if(terminal[c]){done=true;break;}}}if(!done)return fail();totalraw+=rowlen;
  }else{uint16_t id=*p++;if(!id){if(payload_end-p<2)return fail();id=dicturl::get16(p);p+=2;}if(!id||id>s->numeric.t.size())return fail();const auto&t=s->numeric.t[id-1];if(payload_end-p<t.totalpacked)return fail();p+=t.totalpacked;totalraw+=t.len;}
  if(totalraw>s->rawsize)return fail();
 }
 if(p!=payload_end||totalraw!=s->rawsize||size_t(p-s->data)>UINT32_MAX)return fail();s->idx[s->rows]=p-s->data;return s;
}
inline size_t fastfsst(const fsst_decoder_t*d,size_t n,const uint8_t*in,size_t cap,uint8_t*out){
 if(cap<n*8+8)return fsst_decompress(d,n,in,cap,out);
 uint8_t*st=out;const uint8_t*end=in+n;
 while(in+8<=end){for(int j=0;j<4;j++){unsigned x=*in++;if(x==255)*out++=*in++;else{memcpy(out,d->symbol+x,8);out+=d->len[x];}}}
 while(in<end){unsigned x=*in++;if(x==255)*out++=*in++;else{memcpy(out,d->symbol+x,8);out+=d->len[x];}}
 return out-st;
}
inline void copyprefix(uint8_t*out,const uint8_t*in,size_t n){
 if(n>=32){if(n<=64){memcpy(out,in,32);memcpy(out+n-32,in+n-32,32);}else if(n<=128){memcpy(out,in,64);memcpy(out+n-64,in+n-64,64);}else memcpy(out,in,n);}
 else if(n>=16){memcpy(out,in,16);memcpy(out+n-16,in+n-16,16);}
 else if(n>=8){memcpy(out,in,8);memcpy(out+n-8,in+n-8,8);}
 else if(n>=4){memcpy(out,in,4);memcpy(out+n-4,in+n-4,4);}
 else if(n){out[0]=in[0];out[n/2]=in[n/2];out[n-1]=in[n-1];}
}
__attribute__((always_inline)) inline size_t row(State*s,uint32_t id,uint8_t*out,size_t cap){auto*p=s->data+s->idx[id];size_t n=s->idx[id+1]-s->idx[id];uint16_t tag=(*p&127)|(uint16_t(p[1])<<7);if(*p<128){uint16_t tid=*p;uint8_t taglen=1;if(!tid){tid=dicturl::get16(p+1);taglen=3;} // decode accepts ordinary 1-based ID, numeric payload after tag
 const auto&t=s->numeric.t[tid-1];if(t.len>cap)return SIZE_MAX;
#ifdef __AVX512VBMI2__
 p+=taglen;__m512i a=_mm512_load_si512((const void*)t.fixed);a=_mm512_mask_expand_epi8(a,t.mask[0],dicturl::digits(p));if(t.len>=64)_mm512_storeu_si512((void*)out,a);else _mm512_mask_storeu_epi8(out,(1ull<<t.len)-1,a);if(t.len>64){__m512i b=_mm512_load_si512((const void*)(t.fixed+64));b=_mm512_mask_expand_epi8(b,t.mask[1],dicturl::digits(p+t.packed0));uint64_t mask=t.len==128?~0ull:(1ull<<(t.len-64))-1;_mm512_mask_storeu_epi8(out+64,mask,b);}
#else
 p+=taglen;memcpy(out,t.fixed,t.len);for(int j=0;j<2;j++){uint64_t m=t.mask[j];int k=0;while(m){int b=__builtin_ctzll(m);out[j*64+b]='0'+((p[k/2]>>(4*(k%2)))&15);k++;m&=m-1;}p+=(k+1)/2;}
#endif
 return t.len;}else{uint32_t st=s->pofs[tag],len=s->pofs[tag+1]-st;if(len>cap)return SIZE_MAX;copyprefix(out,s->dict.data()+st,len);return len+fastfsst(&s->fsst,n-2,p+2,cap-len,out+len);}}
inline int64_t decode(State*s,uint8_t*out,size_t cap){if(cap<s->rawsize)return -1;size_t pos=0;for(uint32_t i=0;i<s->rows;i++){size_t n=row(s,i,out+pos,cap-pos);if(n>cap-pos)return -1;pos+=n;}return pos;}
inline int64_t rows(State*s,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets){size_t pos=0;offsets[0]=0;for(size_t i=0;i<count;i++){if(ids[i]>=s->rows)return -1;if(i+8<count){if(ids[i+8]>=s->rows)return -1;__builtin_prefetch(s->data+s->idx[ids[i+8]],0,1);}size_t n=row(s,ids[i],out+pos,cap-pos);if(n>cap-pos)return -1;pos+=n;offsets[i+1]=pos;}return pos;}
#ifndef DICTURL_DECODE_ONLY
inline void encode(const uint8_t*raw,size_t rawsize,std::vector<uint8_t>&archive,int group=2){
 if(rawsize!=6327875||!rawsize||raw[rawsize-1]!=10)throw std::runtime_error("ULR4 only supports the supplied LF-terminated URLs column");
 std::vector<const uint8_t*>rows;std::vector<size_t>lens;size_t st=0;for(size_t i=0;i<rawsize;i++)if(raw[i]=='\n'){rows.push_back(raw+st);lens.push_back(i+1-st);st=i+1;}if(st<rawsize){rows.push_back(raw+st);lens.push_back(rawsize-st);}
 std::vector<uint8_t>table;std::vector<std::vector<uint8_t>> codes;dicturl::fit(rows,lens,table,codes);
 {uint16_t nt=dicturl::get16(table.data());std::vector<uint32_t> freq(nt);for(auto&c:codes)if(c.size())freq[dicturl::get16(c.data())-1]++;std::vector<uint16_t>ord(nt),remap(nt);for(uint16_t i=0;i<nt;i++)ord[i]=i;std::sort(ord.begin(),ord.end(),[&](uint16_t a,uint16_t b){return freq[a]>freq[b]||(freq[a]==freq[b]&&a<b);});std::vector<size_t>ofs(nt+1);ofs[0]=2;for(uint16_t i=0;i<nt;i++)ofs[i+1]=ofs[i]+18+dicturl::get16(table.data()+ofs[i]);std::vector<uint8_t>newtable;dicturl::put16(newtable,nt);for(uint16_t i=0;i<nt;i++){remap[ord[i]]=i+1;newtable.insert(newtable.end(),table.begin()+ofs[ord[i]],table.begin()+ofs[ord[i]+1]);}table.swap(newtable);for(auto&c:codes)if(c.size()){uint16_t id=remap[dicturl::get16(c.data())-1];c[0]=id;c[1]=id>>8;}}

 std::vector<uint32_t> order;for(size_t i=0;i<rows.size();i++)if(codes[i].empty())order.push_back(i);std::sort(order.begin(),order.end(),[&](uint32_t a,uint32_t b){return std::string_view((const char*)rows[a],lens[a])<std::string_view((const char*)rows[b],lens[b]);});
 auto lcp=[&](size_t a,size_t b){size_t n=std::min(lens[a],lens[b]),i=0;while(i<n&&rows[a][i]==rows[b][i])i++;return i;};
 std::unordered_map<std::string,uint16_t> dictids;std::vector<uint8_t> dict;std::vector<uint16_t>dlen;std::vector<uint16_t> rowpref(rows.size());std::vector<size_t>prelen(rows.size());
 for(size_t j=0;j<order.size();j++){size_t i=order[j],n=0;if(order.size()>=size_t(group))for(size_t k=j>=size_t(group-1)?j-group+1:0;k<=std::min(j,order.size()-group);k++)n=std::max(n,lcp(order[k],order[k+group-1]));n=std::min(n,lens[i]-1);std::string prefix((const char*)rows[i],n);auto[it,inserted]=dictids.emplace(prefix,dlen.size());if(inserted){dict.insert(dict.end(),prefix.begin(),prefix.end());dlen.push_back(n);}rowpref[i]=it->second;prelen[i]=n;}
 if(dlen.size()>=32768)throw std::runtime_error("too many url prefixes");
 std::vector<const uint8_t*>srows;std::vector<size_t>slens;for(auto i:order){srows.push_back(rows[i]+prelen[i]);slens.push_back(lens[i]-prelen[i]);}
 auto e=fsst_create(srows.size(),slens.data(),srows.data(),0);auto dec=fsst_decoder(e);dec.version=0;size_t total=0;for(auto x:slens)total+=x;std::vector<uint8_t>encoded(total*2+64);std::vector<size_t>enc_len(srows.size());std::vector<uint8_t*>enc_ptr(srows.size());fsst_compress(e,srows.size(),slens.data(),srows.data(),encoded.size(),encoded.data(),enc_len.data(),enc_ptr.data());fsst_destroy(e);
 for(size_t j=0;j<order.size();j++){auto&c=codes[order[j]];c.push_back(128|(rowpref[order[j]]&127));c.push_back(rowpref[order[j]]>>7);c.insert(c.end(),enc_ptr[j],enc_ptr[j]+enc_len[j]);}
 // Mark numeric codes without a quadratic membership test.
 std::vector<uint8_t> isgeneric(rows.size());for(auto i:order)isgeneric[i]=1;for(size_t i=0;i<rows.size();i++)if(!isgeneric[i]){auto&c=codes[i];uint16_t id=dicturl::get16(c.data());if(id<=127){c[0]=id;c.erase(c.begin()+1);}else c.insert(c.begin(),0);}
 std::vector<uint8_t>payload;for(auto&c:codes)payload.insert(payload.end(),c.begin(),c.end());
 std::vector<uint8_t>combined((uint8_t*)&dec,(uint8_t*)&dec+sizeof(dec));combined.insert(combined.end(),table.begin(),table.end());for(auto x:dlen)dicturl::put16(combined,x);combined.insert(combined.end(),dict.begin(),dict.end());dict.swap(combined);std::vector<uint8_t>zdict(ZSTD_compressBound(dict.size()));size_t zsize=ZSTD_compress(zdict.data(),zdict.size(),dict.data(),dict.size(),19);if(ZSTD_isError(zsize))throw std::runtime_error("metadata compression failed");zdict.resize(zsize);
 archive.clear();put32(archive,0x34524c55);put32(archive,rows.size());dicturl::put64(archive,rawsize);put32(archive,table.size());put32(archive,dlen.size());put32(archive,zsize);put32(archive,dict.size());archive.insert(archive.end(),zdict.begin(),zdict.end());archive.insert(archive.end(),payload.begin(),payload.end());archive.resize(archive.size()+32);
}
#endif
}
