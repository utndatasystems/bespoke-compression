#include "common.h"
#include <memory>
#include "hufffast.h"
#include "fastfixed.h"
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
struct Entry {alignas(32) uint8_t b[32];};
struct HalfEntry {alignas(16) uint8_t b[16];};
struct State {
#ifdef HAVE_URL
 UrlState url;
#endif
 Header h; const uint8_t *p;size_t plen;
 std::vector<uint32_t> off;
 std::vector<Entry> dict;std::vector<HalfEntry> dict16;bool short16=false;
 std::vector<uint8_t> lens;
 HuffFast hf;std::vector<uint32_t> htable;unsigned hmask=0;
 const uint8_t *data=nullptr;
 uint32_t mode=0,ndict=0,fixed_width=0;
 void *sp=nullptr;
#ifdef HAVE_LOCATION
 LocState loc;
#endif
};
static void cleanup(State*s){
#ifdef HAVE_SPECIAL
 if(s->sp)special_close(s->sp);
#endif
 delete s;}
static void* open_impl(const uint8_t*a,size_t z){
 if(!a||z<sizeof(Header))return nullptr;
 Header h;memcpy(&h,a,sizeof(h));if(h.magic!=MAGIC||h.psize!=z-sizeof(h)||h.rawsize>10000000||h.nrows>1000000||h.nrows>h.rawsize+1)return nullptr;
 auto owner=std::make_unique<State>();auto*s=owner.get();s->h=h;s->p=a+sizeof(h);s->plen=h.psize;
#ifdef HAVE_SPECIAL
 if(h.type>=101&&h.type<=104){s->sp=special_open(h.type,s->p,s->plen,h.nrows,h.rawsize);if(!s->sp){return nullptr;}return owner.release();}
#endif
#ifdef HAVE_LOCATION
 if(h.type==10){if(!loc_open(s->loc,s->p,s->plen)||s->loc.n!=h.nrows){return nullptr;}return owner.release();}
#endif
#ifdef HAVE_URL
 if(h.type==11){if(!url_open(s->url,s->p,s->plen)||s->url.n!=h.nrows){return nullptr;}return owner.release();}
#endif
 if(h.type!=1||h.psize<sizeof(GPHeader)){return nullptr;}
 GPHeader g;memcpy(&g,s->p,sizeof(g));
 if(!g.ndict||g.ndict>32768||(g.mode&255)>5||g.dictRaw>g.ndict*61||g.dictRaw<g.ndict*2||h.nrows>g.indexRaw||g.indexRaw>uint64_t(h.nrows)*3||uint64_t(sizeof(g))+g.dictComp+g.indexSize+g.dataSize+8!=h.psize){return nullptr;}
 if((g.mode&255)==2&&g.ndict>4096){return nullptr;}
 std::vector<uint8_t>d(g.dictRaw);size_t r=ZSTD_decompress(d.data(),d.size(),s->p+sizeof(g),g.dictComp);
 if(ZSTD_isError(r)||r!=g.dictRaw){return nullptr;}
 s->dict.resize(g.ndict);s->lens.resize(g.ndict);s->ndict=g.ndict;s->mode=g.mode&255;
 size_t pos=0;std::vector<uint8_t> bits(g.ndict);unsigned maxbits=g.mode>>8;
 s->fixed_width=maxbits;if(s->mode==5&&(maxbits<1||maxbits>15||g.ndict>(1u<<maxbits))){return nullptr;}
 if(s->mode==3&&(maxbits<1||maxbits>16)){return nullptr;}
 for(unsigned i=0;i<g.ndict;i++){if(pos>=d.size()){return nullptr;}unsigned len=d[pos++];if(s->mode==3){if(pos>=d.size()){return nullptr;}bits[i]=d[pos++];if(!bits[i]||bits[i]>maxbits){return nullptr;}}if(!len||len>32||pos+len>d.size()){return nullptr;}memcpy(s->dict[i].b,d.data()+pos,len);s->lens[i]=len;pos+=len;}
 if(s->mode==4){
 if(maxbits<1||maxbits>15||pos+28*g.ndict!=d.size()){return nullptr;}s->hmask=(1u<<maxbits)-1;s->htable.resize(28u<<maxbits);
 for(unsigned ctx=0;ctx<28;ctx++){
 unsigned counts[16]={},next[16]={};auto*cb=d.data()+pos+ctx*g.ndict;
 for(unsigned i=0;i<g.ndict;i++){if(cb[i]>maxbits){return nullptr;}if(cb[i])counts[cb[i]]++;}
 unsigned code=0;for(unsigned b=1;b<=maxbits;b++){code=(code+(b>1?counts[b-1]:0))<<1;next[b]=code;if(code+counts[b]>(1u<<b)){return nullptr;}}
 for(unsigned i=0;i<g.ndict;i++)if(cb[i]){unsigned n=cb[i],c=next[n]++,rev=0;for(unsigned j=0;j<n;j++){rev=(rev<<1)|(c&1);c>>=1;}unsigned last=s->dict[i].b[s->lens[i]-1],cc=last|32,nctx=last==10?0:(cc>='a'&&cc<='z'?cc-'a'+1:27);uint32_t value=i|(n<<16)|(nctx<<20)|(unsigned(s->lens[i])<<25);for(unsigned j=rev;j<=s->hmask;j+=1u<<n)s->htable[(ctx<<maxbits)+j]=value;}
 }pos=d.size();
 }
 if(pos!=d.size()){return nullptr;}
 if(s->mode==3){
 unsigned counts[17]={},next[17]={};for(auto b:bits)counts[b]++;unsigned code=0;
 for(unsigned b=1;b<=maxbits;b++){code=(code+counts[b-1])<<1;next[b]=code;if(code+counts[b]>(1u<<b)){return nullptr;}}
 s->hmask=(1u<<maxbits)-1;s->htable.resize(1u<<maxbits);
 for(unsigned i=0;i<g.ndict;i++){unsigned n=bits[i],c=next[n]++,rev=0;for(unsigned j=0;j<n;j++){rev=(rev<<1)|(c&1);c>>=1;}for(unsigned j=rev;j<=s->hmask;j+=1u<<n)s->htable[j]=i|(n<<16);}
 }
 if(s->mode==3&&!huff_fast_build(s->hf,s->htable,s->dict,s->lens)){return nullptr;}
 std::vector<uint8_t> idx(g.indexRaw);size_t iz=ZSTD_decompress(idx.data(),idx.size(),s->p+sizeof(g)+g.dictComp,g.indexSize);if(ZSTD_isError(iz)||iz!=g.indexRaw){return nullptr;}
 const uint8_t*ix=idx.data(); const uint8_t*ie=ix+idx.size();
 s->off.resize(size_t(h.nrows)+1);uint64_t off=0;
 for(unsigned i=0;i<h.nrows;i++){s->off[i]=off;if(ix==ie){return nullptr;}unsigned n=*ix++;if(n==255){if(ie-ix<2){return nullptr;}n=ix[0]|(unsigned(ix[1])<<8);ix+=2;}off+=s->mode==5?n*s->fixed_width:n;if(off>UINT32_MAX){return nullptr;}if(off>uint64_t(g.dataSize)*(s->mode>=3?8:1)){return nullptr;}}
 if(ix!=ie||(s->mode>=3?(off+7)/8:off)!=g.dataSize){return nullptr;}s->off[h.nrows]=off;s->data=s->p+sizeof(g)+g.dictComp+g.indexSize;
 if(s->mode==5&&*std::max_element(s->lens.begin(),s->lens.end())<=16){s->short16=true;s->dict16.resize(g.ndict);for(unsigned i=0;i<g.ndict;i++)memcpy(s->dict16[i].b,s->dict[i].b,16);std::vector<Entry>().swap(s->dict);}
 return owner.release();
}
static inline bool emit(State*s,unsigned code,uint8_t*out,size_t cap,size_t&pos){
 if(code>=s->ndict)return false;unsigned l=s->lens[code];if(cap-pos<32){if(l>cap-pos)return false;memcpy(out+pos,s->dict[code].b,l);}else{_mm256_storeu_si256((__m256i*)(out+pos),_mm256_load_si256((const __m256i*)s->dict[code].b));}pos+=l;return true;
}
static inline bool row(State*s,unsigned rid,uint8_t*out,size_t cap,size_t&pos){
 if(s->mode==5)return fastfixed_row(s,rid,out,cap,pos);
 if(s->mode==4){unsigned bit=s->off[rid],end=s->off[rid+1],ctx=0,width=__builtin_ctz(s->hmask+1);uint64_t buffer=0;unsigned avail=0;
 while(bit<end){if(avail<width){memcpy(&buffer,s->data+(bit>>3),8);buffer>>=bit&7;avail=64-(bit&7);}unsigned h=s->htable[(ctx<<width)+(buffer&s->hmask)],nb=(h>>16)&15,code=h&65535,len=h>>25;
 if(!nb||nb>end-bit||code>=s->ndict||len>cap-pos)return false;
 if(cap-pos>=32)_mm256_storeu_si256((__m256i*)(out+pos),_mm256_load_si256((const __m256i*)s->dict[code].b));else memcpy(out+pos,s->dict[code].b,len);
 pos+=len;ctx=(h>>20)&31;bit+=nb;buffer>>=nb;avail-=nb;
 }return true;}

 if(s->mode==3){
 unsigned bit=s->off[rid],end=s->off[rid+1];
 while(bit<end){unsigned v=get32(s->data+(bit>>3))>>(bit&7);unsigned fb=huff_fast_emit(s->hf,v,end-bit,out,cap,pos);if(fb==UINT32_MAX)return false;if(fb){bit+=fb;continue;}unsigned h=s->htable[v&s->hmask];unsigned nb=h>>16;if(!nb||nb>end-bit||!emit(s,h&65535,out,cap,pos))return false;bit+=nb;}return true;
 }
 const uint8_t*p=s->data+s->off[rid],*e=s->data+s->off[rid+1];
 if(s->mode==0){if((e-p)&1)return false;for(;p<e;p+=2){unsigned c=p[0]|unsigned(p[1])<<8;if(!emit(s,c,out,cap,pos))return false;}}
 else if(s->mode==1){while(p<e){unsigned c=*p++;if(c>=128){if(p==e)return false;c=128+(c&127)+(unsigned(*p++)<<7);}if(!emit(s,c,out,cap,pos))return false;}}
 else {while(e-p>=3){unsigned v=p[0]|unsigned(p[1])<<8|unsigned(p[2])<<16;p+=3;if(!emit(s,v&4095,out,cap,pos)||!emit(s,v>>12,out,cap,pos))return false;}if(e-p==2){unsigned c=p[0]|unsigned(p[1])<<8;if(c>=4096||!emit(s,c,out,cap,pos))return false;}else if(e-p)return false;}
 return true;
}
extern "C" int64_t lab_decode(void*v,uint8_t*out,size_t cap){
 auto*s=(State*)v;if(!s||cap<s->h.rawsize)return -1;
#ifdef HAVE_SPECIAL
 if(s->sp)return special_decode(s->sp,out,cap);
#endif
#ifdef HAVE_LOCATION
 if(s->h.type==10){size_t pos=0;for(unsigned i=0;i<s->h.nrows;i++){unsigned n=loc_rowlen(s->loc,i);if(n>cap-pos)return -1;pos+=loc_row(s->loc,i,out+pos);}return pos==s->h.rawsize?pos:-1;}
#endif
#ifdef HAVE_URL
 if(s->h.type==11){size_t pos=0;for(unsigned i=0;i<s->h.nrows;i++){unsigned n=url_row(s->url,i,out+pos,cap-pos);if(n==UINT32_MAX)return -1;pos+=n;}return pos==s->h.rawsize?pos:-1;}
#endif
 if(s->mode==5){size_t pos=0;if(!fastfixed_bulk(s,out,cap,pos))return -1;return pos==s->h.rawsize?pos:-1;}
 size_t pos=0;for(unsigned i=0;i<s->h.nrows;i++)if(!row(s,i,out,cap,pos))return -1;return pos==s->h.rawsize?pos:-1;
}
extern "C" int64_t lab_rows(void*v,const uint64_t*ids,size_t n,uint8_t*out,size_t cap,uint64_t*offsets){
 auto*s=(State*)v;if(!s||!offsets||(n&&!ids))return -1;
#ifdef HAVE_SPECIAL
 if(s->sp)return special_rows(s->sp,ids,n,out,cap,offsets);
#endif
 size_t pos=0;offsets[0]=0;
#ifdef HAVE_LOCATION
 if(s->h.type==10){for(size_t i=0;i<n;i++){if(ids[i]>=s->h.nrows)return -1;unsigned len=loc_rowlen(s->loc,ids[i]);if(len>cap-pos)return -1;pos+=loc_row(s->loc,ids[i],out+pos);offsets[i+1]=pos;}return pos;}
#endif
#ifdef HAVE_URL
 if(s->h.type==11){for(size_t i=0;i<n;i++){if(ids[i]>=s->h.nrows)return -1;unsigned len=url_row(s->url,ids[i],out+pos,cap-pos);if(len==UINT32_MAX)return -1;pos+=len;offsets[i+1]=pos;}return pos;}
#endif
 if(s->mode==5&&n==s->h.nrows){
  bool all=true;for(size_t i=0;i<n;i++)if(ids[i]!=i){all=false;break;}
  if(all){if(cap<s->h.rawsize)return -1;size_t bytes=0;if(!fastfixed_bulk(s,out,cap,bytes)||bytes!=s->h.rawsize)return -1;
   size_t p=0,r=0;offsets[0]=0;__m256i lf=_mm256_set1_epi8('\n');
   for(;p+32<=bytes;p+=32){unsigned mask=_mm256_movemask_epi8(_mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i*)(out+p)),lf));while(mask){unsigned bit=__builtin_ctz(mask);if(r==n)return -1;offsets[++r]=p+bit+1;mask&=mask-1;}}
   for(;p<bytes;p++)if(out[p]=='\n'){if(r==n)return -1;offsets[++r]=p+1;}
   if(bytes&&out[bytes-1]!='\n'){if(r==n)return -1;offsets[++r]=bytes;}
   return r==n?bytes:-1;
  }
 }
 if(s->mode==5)return fastfixed_rows(s,ids,n,out,cap,offsets);
 for(size_t i=0;i<n;i++){if(ids[i]>=s->h.nrows)return -1;if(!row(s,ids[i],out,cap,pos))return -1;offsets[i+1]=pos;}return pos;
}
extern "C" void lab_close(void*v){if(v)cleanup((State*)v);}

extern "C" void* lab_open(const uint8_t*a,size_t z){try{return open_impl(a,z);}catch(...){return nullptr;}}
