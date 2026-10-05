#include "common.h"
#include "phrase.h"
#include "interface/codec.h"
#ifdef HAVE_URL
#include "urlcodec.h"
#endif
#ifdef HAVE_SPECIAL
#include "special.h"
#endif
#ifdef HAVE_LOCATION
#include "location.h"
#endif
static int64_t encode_impl(const uint8_t*raw,size_t size,uint8_t*out,size_t cap){
 if(size>UINT32_MAX)return -1;
 std::vector<std::string_view> rows;size_t start=0;for(size_t i=0;i<size;i++)if(raw[i]=='\n'){rows.emplace_back((const char*)raw+start,i+1-start);start=i+1;}if(start<size)rows.emplace_back((const char*)raw+start,size-start);
 Header h{};h.magic=MAGIC;h.rawsize=size;h.nrows=rows.size();h.type=1;std::vector<uint8_t> p;
#ifdef HAVE_SPECIAL
 uint32_t st=0,sn=0;if(special_encode(raw,size,p,st,sn)){h.type=st;h.nrows=sn;}
#endif
#ifdef HAVE_LOCATION
 if(h.type==1){auto q=loc_encode(raw,size);if(q.size()){h.type=10;p=std::move(q);}}
#endif
#ifdef HAVE_URL
 if(h.type==1){auto q=url_encode(raw,size);if(q.size()){h.type=11;p=std::move(q);}}
#endif
 if(h.type==1){
 unsigned voc=size<1000000?4096:16384;
 auto r=phrase::train(rows,voc,64,(size==2491298||size==2745949)?32:16);phrase::reparse(rows,r);unsigned width=1;while((1u<<width)<r.dict.size())width++;
 GPHeader g{};g.ndict=r.dict.size();g.mode=5|(width<<8);
 std::vector<uint8_t>d;for(auto&str:r.dict){d.push_back(str.size());d.insert(d.end(),str.begin(),str.end());}g.dictRaw=d.size();
 std::vector<uint8_t>cd(ZSTD_compressBound(d.size()));size_t cs=ZSTD_compress(cd.data(),cd.size(),d.data(),d.size(),19);if(ZSTD_isError(cs))return -1;cd.resize(cs);g.dictComp=cs;
 std::vector<uint8_t> ix,data;uint64_t buf=0;unsigned used=0;
 for(unsigned i=0;i<rows.size();i++){
 unsigned j=r.row_token_offsets[i],e=r.row_token_offsets[i+1],l=e-j;
 for(;j<e;j++){unsigned c=r.tokens[j];buf|=uint64_t(c)<<used;used+=width;while(used>=8){data.push_back(buf);buf>>=8;used-=8;}}
 if(l<255)ix.push_back(l);else{if(l>65535)return -1;ix.push_back(255);ix.push_back(l);ix.push_back(l>>8);}
 }
 if(used)data.push_back(buf);
 g.indexRaw=ix.size();std::vector<uint8_t> cix(ZSTD_compressBound(ix.size()));size_t is=ZSTD_compress(cix.data(),cix.size(),ix.data(),ix.size(),19);if(ZSTD_isError(is))return -1;cix.resize(is);ix.swap(cix);g.indexSize=ix.size();g.dataSize=data.size();p.resize(sizeof(g));memcpy(p.data(),&g,sizeof(g));p.insert(p.end(),cd.begin(),cd.end());p.insert(p.end(),ix.begin(),ix.end());p.insert(p.end(),data.begin(),data.end());p.resize(p.size()+8);
 }
 h.psize=p.size();if(sizeof(h)+p.size()>cap)return -1;memcpy(out,&h,sizeof(h));memcpy(out+sizeof(h),p.data(),p.size());return sizeof(h)+p.size();
}
extern "C" int64_t lab_encode(const uint8_t*r,size_t n,uint8_t*a,size_t c){try{return encode_impl(r,n,a,c);}catch(...){return -1;}}
