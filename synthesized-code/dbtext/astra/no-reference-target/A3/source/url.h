#pragma once
#include "phrase_fast.h"
#include <map>
#include <memory>
#include <immintrin.h>
namespace urlcodec {
constexpr uint32_t MAGIC=0x32524c55;
struct Header {uint32_t magic,raw,rows,nt,keysize,gsize,metasize,paysize;};
struct Field {uint32_t min;uint8_t bits,width,base,pad;uint32_t pos;};
struct Tmpl {uint32_t start,fields,nfields,bytes,raw;};
struct State {Header h;const uint8_t*ids;const uint8_t*index;const uint8_t*pay;phrasefast::State*gen;std::vector<Tmpl> tm;std::vector<Field> ff;std::vector<uint8_t> flat;};
static inline uint32_t rd32(const void*p){uint32_t v;memcpy(&v,p,4);return v;}
static inline uint16_t id_at(const State*s,unsigned row){uint64_t b=uint64_t(row)*13;uint32_t v;memcpy(&v,s->ids+(b>>3),4);return (v>>(b&7))&8191;}
static inline uint32_t index_at(const State*s,unsigned index){return rd32(s->index+4ull*index);}
#ifdef ENCODER
struct Sample {std::string key;std::vector<uint32_t> val;};
static Sample parse(const uint8_t*p,size_t n){Sample s;size_t hex=std::string((const char*)p,n).rfind("/int_");if(hex!=std::string::npos){hex+=5;for(size_t j=hex;j+1<n;j++)if(!((p[j]>='0'&&p[j]<='9')||(p[j]>='a'&&p[j]<='f'))){hex=n;break;}if(n-1-hex>8)hex=n;}else hex=n;
 for(size_t j=0;j<n;){unsigned base=10;size_t end=j;if(j==hex){base=16;end=n-1;}else if(p[j]>='0'&&p[j]<='9'){while(end<n&&p[end]>='0'&&p[end]<='9')end++;}if(end>j&&end-j<=8){uint32_t v=0;for(size_t k=j;k<end;k++)v=v*base+(p[k]<='9'?p[k]-'0':p[k]-'a'+10);s.key.push_back(1);s.key.push_back(base==10?'d':'h');s.key.push_back('0'+end-j);s.val.push_back(v);j=end;}else{s.key.push_back(p[j++]);}}
 return s;}
inline bool encode(const uint8_t*raw,size_t n,std::vector<uint8_t>&out){if(n<1000||memcmp(raw,"http",4))return false;
 std::vector<Sample> samples;std::vector<std::string> strings;std::map<std::string,std::vector<uint32_t>> groups;size_t start=0;for(size_t i=0;i<n;i++)if(raw[i]==10){strings.emplace_back((const char*)raw+start,i+1-start);samples.push_back(parse(raw+start,i+1-start));groups[samples.back().key].push_back(samples.size()-1);start=i+1;}if(start!=n)return false;
 std::map<std::string,uint16_t> selected;std::vector<std::vector<Field>> fields(1);std::vector<uint32_t> widths(1);std::vector<uint8_t> keyraw,meta;unsigned tid=0;
 for(auto&kv:groups){auto&key=kv.first;auto&rr=kv.second;if(rr.size()<2||key.find(char(1))==std::string::npos)continue;std::vector<Field> f;size_t hole=0;unsigned bits=0;for(size_t j=0;j<key.size();j++)if(key[j]==1){uint32_t lo=UINT32_MAX,hi=0;for(auto r:rr){lo=std::min(lo,samples[r].val[hole]);hi=std::max(hi,samples[r].val[hole]);}unsigned b=0;while((uint64_t(1)<<b)<=hi-lo)b++;f.push_back({lo,(uint8_t)b,(uint8_t)(key[j+2]-'0'),(uint8_t)(key[j+1]=='d'?10:16),0});bits+=b;hole++;j+=2;}
 unsigned bytes=(bits+7)/8;size_t saved=0;for(auto r:rr)saved+=strings[r].size();if(saved<=rr.size()*(2+bytes)+key.size()+f.size()*5+8)continue;if(tid==8191)break;selected[key]=++tid;fields.push_back(f);widths.push_back(bytes);keyraw.insert(keyraw.end(),key.begin(),key.end());for(auto x:f){for(int b=0;b<4;b++)meta.push_back(x.min>>(b*8));meta.push_back(x.bits);}}
 if(tid<2)return false;
 std::vector<uint8_t> keyar,genraw,genar,pay;std::vector<uint16_t> ids;std::vector<uint32_t> index;unsigned gr=0;
 for(size_t r=0;r<samples.size();r++){if(r%32==0){index.push_back(pay.size());index.push_back(gr);}auto it=selected.find(samples[r].key);unsigned id=it==selected.end()?0:it->second;ids.push_back(id);if(!id){genraw.insert(genraw.end(),strings[r].begin(),strings[r].end());gr++;}else {size_t at=pay.size();pay.resize(at+widths[id],0);unsigned bit=0;for(size_t j=0;j<fields[id].size();j++){uint64_t v=samples[r].val[j]-fields[id][j].min;for(unsigned k=0;k<fields[id][j].bits;k++)if(v&(1ULL<<k))pay[at+((bit+k)>>3)]|=1u<<((bit+k)&7);bit+=fields[id][j].bits;}}}
 if(!phrase::encode(keyraw.data(),keyraw.size(),keyar)||!phrase::encode(genraw.data(),genraw.size(),genar))return false;
 Header h{MAGIC,(uint32_t)n,(uint32_t)samples.size(),tid,(uint32_t)keyar.size(),(uint32_t)genar.size(),(uint32_t)meta.size(),(uint32_t)pay.size()};out.resize(sizeof(h));memcpy(out.data(),&h,sizeof(h));out.insert(out.end(),keyar.begin(),keyar.end());out.insert(out.end(),genar.begin(),genar.end());out.insert(out.end(),meta.begin(),meta.end());while(out.size()%4)out.push_back(0);{size_t base=out.size();out.resize(base+(ids.size()*13+7)/8,0);for(size_t i=0;i<ids.size();i++){uint64_t b=i*13;uint32_t v=uint32_t(ids[i])<<(b&7);for(unsigned j=0;j<3&&base+(b>>3)+j<out.size();j++)out[base+(b>>3)+j]|=v>>(8*j);}}while(out.size()%4)out.push_back(0);for(auto v:index){for(int j=0;j<4;j++)out.push_back(v>>(j*8));}out.insert(out.end(),pay.begin(),pay.end());out.resize(out.size()+8,0);
return true;
}
#endif
static inline void valueout(uint32_t v,unsigned width,unsigned base,uint8_t*out){
 static const char pairs[]="00010203040506070809101112131415161718192021222324252627282930313233343536373839404142434445464748495051525354555657585960616263646566676869707172737475767778798081828384858687888990919293949596979899";
 if(base==10){while(width>=2){width-=2;unsigned pair=v%100;memcpy(out+width,pairs+pair*2,2);v/=100;}if(width)out[0]='0'+v;}
 else {__m128i x=_mm_cvtsi32_si128((int)__builtin_bswap32(v)),m=_mm_set1_epi8(15);__m128i nib=_mm_unpacklo_epi8(_mm_and_si128(_mm_srli_epi16(x,4),m),_mm_and_si128(x,m));__m128i ascii=_mm_shuffle_epi8(_mm_setr_epi8('0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'),nib);uint64_t bytes=_mm_cvtsi128_si64(ascii);bytes>>=(8-width)*8;if(width==8)memcpy(out,&bytes,8);else memcpy(out,&bytes,width);}
}
inline void close(void*v){State*s=(State*)v;if(s){phrasefast::close(s->gen);delete s;}}
inline void*open(const uint8_t*a,size_t n){if(n<sizeof(Header))return nullptr;Header h;memcpy(&h,a,sizeof(h));if(h.magic!=MAGIC||h.nt>8191||h.rows>1000000||h.raw>100000000)return nullptr;size_t at=sizeof(h);if(uint64_t(at)+h.keysize+h.gsize+h.metasize>n)return nullptr;std::unique_ptr<State>s(new State);s->h=h;s->gen=nullptr;s->tm.resize(h.nt+1);auto*k=(phrasefast::State*)phrasefast::open(a+at,h.keysize);if(!k)return nullptr;std::vector<uint8_t> keys(k->h.nraw);if(phrasefast::decode(k,keys.data(),keys.size())!=(int64_t)keys.size()){phrasefast::close(k);return nullptr;}phrasefast::close(k);at+=h.keysize;s->gen=(phrasefast::State*)phrasefast::open(a+at,h.gsize);if(!s->gen)return nullptr;at+=h.gsize;const uint8_t*m=a+at;size_t mi=0;at+=h.metasize;s->ff.reserve(h.metasize/5);s->flat.reserve(keys.size()+h.metasize);
 size_t pos=0;for(unsigned id=1;id<=h.nt;id++){Tmpl&t=s->tm[id];t.start=s->flat.size();t.fields=s->ff.size();while(pos<keys.size()){uint8_t c=keys[pos++];if(c==1){if(pos+2>keys.size()||mi+5>h.metasize){close(s.release());return nullptr;}uint8_t base=keys[pos++]=='d'?10:16,w=keys[pos++]-'0',bits=m[mi+4];if(!w||w>8||bits>32){close(s.release());return nullptr;}uint32_t min=rd32(m+mi);mi+=5;if(bits)s->ff.push_back({min,bits,w,base,0,t.raw});size_t old=s->flat.size();s->flat.resize(old+w);valueout(min,w,base,s->flat.data()+old);t.raw+=w;t.bytes+=bits;}else{size_t begin=pos-1,end=keys.size();const uint8_t*p=keys.data()+begin;const uint8_t*mark=(const uint8_t*)memchr(p,1,end-begin);const uint8_t*nl=(const uint8_t*)memchr(p,10,end-begin);if(mark)end=mark-keys.data();if(nl&&size_t(nl-keys.data())<end)end=nl-keys.data()+1;size_t dest=s->flat.size();s->flat.resize(dest+end-begin);memcpy(s->flat.data()+dest,keys.data()+begin,end-begin);t.raw+=end-begin;pos=end;if(end>begin&&keys[end-1]==10)break;}}t.nfields=s->ff.size()-t.fields;t.bytes=(t.bytes+7)/8;}
 if(pos!=keys.size()||mi!=h.metasize){close(s.release());return nullptr;}at=(at+3)&~size_t(3);if(at+(uint64_t(h.rows)*13+7)/8>n){close(s.release());return nullptr;}s->ids=a+at;at+=(uint64_t(h.rows)*13+7)/8;at=(at+3)&~size_t(3);size_t idxsize=((h.rows+31)/32)*8;if(at+idxsize+h.paysize+8!=n){close(s.release());return nullptr;}s->index=a+at;at+=idxsize;s->pay=a+at;
 uint64_t pay=0,gen=0,raw=0;for(unsigned r=0;r<h.rows;r++){if(r%32==0&&(index_at(s.get(),r/32*2)!=pay||index_at(s.get(),r/32*2+1)!=gen)){close(s.release());return nullptr;}unsigned id=id_at(s.get(),r);if(id>h.nt){close(s.release());return nullptr;}if(id){pay+=s->tm[id].bytes;raw+=s->tm[id].raw;}else gen++;}if(pay!=h.paysize||gen!=s->gen->h.nrow||raw+s->gen->h.nraw!=h.raw){close(s.release());return nullptr;}return s.release();}
inline uint8_t*special(const State*s,const Tmpl&t,const uint8_t*p,uint8_t*out){memcpy(out,s->flat.data()+t.start,t.raw);unsigned bit=0;for(size_t j=0;j<t.nfields;j++){const Field&f=s->ff[t.fields+j];uint64_t v;memcpy(&v,p+(bit>>3),8);v=(v>>(bit&7))&((1ULL<<f.bits)-1);v+=f.min;bit+=f.bits;valueout(v,f.width,f.base,out+f.pos);}return out+t.raw;}
template<unsigned B,bool OFF=false> inline int64_t decode_bits(State*s,uint8_t*out,size_t cap,uint64_t*offsets=nullptr){if(!s||cap<s->h.raw||(!out&&s->h.raw))return -1;uint8_t*o=out;const uint8_t*p=s->pay;unsigned gr=0,tok=0;if constexpr(OFF)offsets[0]=0;for(unsigned r=0;r<s->h.rows;r++){unsigned id=id_at(s,r);if(id){auto&t=s->tm[id];if(t.raw>cap-size_t(o-out))return -1;o=special(s,t,p,o);p+=t.bytes;}else {unsigned ct=s->gen->lens[gr++];o=phrasefast::unpack<B>(s->gen,tok,ct,o,out+cap);if(!o)return -1;tok+=ct;}if constexpr(OFF)offsets[r+1]=o-out;}return size_t(o-out)==s->h.raw?o-out:-1;}
inline int64_t decode(void*v,uint8_t*out,size_t cap){State*s=(State*)v;if(!s)return -1;
#define URL_D(B) case B:return decode_bits<B>(s,out,cap);
switch(s->gen->h.bits){URL_D(13)}
#undef URL_D
return -1;}
template<unsigned B> inline int64_t rows_bits(State*s,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets){uint8_t*o=out;unsigned block=UINT32_MAX,cursor=0,p=0,gr=0,gblock=UINT32_MAX,gcursor=0,tok=0;
if(count==s->h.rows){bool all=true;for(size_t q=0;q<count;q++)if(ids[q]!=q){all=false;break;}if(all)return decode_bits<B,true>(s,out,cap,offsets);}
for(size_t q=0;q<count;q++){uint64_t r=ids[q];if(r>=s->h.rows||(q&&r<ids[q-1]))return -1;if(r/32!=block||r<cursor){block=r/32;cursor=block*32;p=index_at(s,block*2);gr=index_at(s,block*2+1);}while(cursor<r){unsigned id=id_at(s,cursor++);if(id)p+=s->tm[id].bytes;else gr++;}unsigned id=id_at(s,r);if(id){auto&t=s->tm[id];if(t.raw>cap-size_t(o-out))return -1;o=special(s,t,s->pay+p,o);p+=t.bytes;}else{if(gr/32!=gblock||gr<gcursor){gblock=gr/32;gcursor=gblock*32;tok=phrasefast::r32(s->gen->index+gblock*4);}while(gcursor<gr){if(tok>s->gen->h.ntok||s->gen->lens[gcursor]>s->gen->h.ntok-tok)return -1;tok+=s->gen->lens[gcursor++];}unsigned ct=s->gen->lens[gr];o=phrasefast::unpack<B>(s->gen,tok,ct,o,out+cap);if(!o)return -1;tok+=ct;gcursor=++gr;}cursor=r+1;offsets[q+1]=o-out;}return o-out;}
inline int64_t rows(void*v,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets){State*s=(State*)v;if(!s||!offsets||(!ids&&count)||(!out&&count))return -1;offsets[0]=0;if(!count)return 0;
#define URL_R(B) case B:return rows_bits<B>(s,ids,count,out,cap,offsets);
switch(s->gen->h.bits){URL_R(13)}
#undef URL_R
return -1;}
}
