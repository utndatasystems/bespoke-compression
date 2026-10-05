#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <immintrin.h>
#ifdef ENCODER
#include <vector>
#include <algorithm>
#endif
namespace {
constexpr uint32_t MAGIC=0x31504353;
struct Header { uint32_t magic; uint16_t type,version; uint64_t raw,rows,aux; };
static_assert(sizeof(Header)==32);
static constexpr uint64_t pow10[16]={1,10,100,1000,10000,100000,1000000,10000000,100000000,1000000000,10000000000ULL,100000000000ULL,1000000000000ULL,10000000000000ULL,100000000000000ULL,1000000000000000ULL};
static inline uint64_t decround(uint64_t m,unsigned n,unsigned bits){return (uint64_t)(((__uint128_t)m*pow10[n]+(1ULL<<(bits-1)))>>bits);}
static inline uint32_t rd32(const void*p){uint32_t v;memcpy(&v,p,4);return v;}
static inline uint64_t rd64(const void*p){uint64_t v;memcpy(&v,p,8);return v;}
static inline void wr32(void*p,uint32_t v){memcpy(p,&v,4);}
static inline void wr64(void*p,uint64_t v){memcpy(p,&v,8);}
static inline int unhex(uint8_t c,bool upper=false){if(c>='0'&&c<='9')return c-'0';if(c>=(upper?'A':'a')&&c<=(upper?'F':'f'))return c-(upper?'A':'a')+10;return -1;}
#ifdef ENCODER
static bool number(const uint8_t*p,size_t n,uint64_t&v){v=0;for(size_t j=0;j<n;++j){if(p[j]<'0'||p[j]>'9')return false;v=v*10+p[j]-'0';}return true;}
static void put18(uint8_t*p,uint64_t i,uint32_t v){uint64_t bit=i*18;unsigned sh=bit&7;size_t at=bit>>3;uint32_t w=v<<sh;p[at]|=w;p[at+1]|=w>>8;p[at+2]|=w>>16;p[at+3]|=w>>24;}
struct LocationFit { uint64_t a,b; uint8_t lens; };
static bool fitlocation(const uint8_t*raw,size_t size,uint64_t rows,uint64_t&base,std::vector<uint8_t>&payload){
 std::vector<LocationFit> rec;rec.reserve(rows);uint32_t counts[256]={};uint64_t lonbase=UINT64_MAX;base=UINT64_MAX;size_t pos=0;
 for(uint64_t i=0;i<rows;++i){size_t end=pos;while(end<size&&raw[end]!=10)++end;if(end==size)return false;const uint8_t*r=raw+pos;size_t n=end-pos;
  if(n==4&&!memcmp(r,"NULL",4)){rec.push_back({0,0,255});pos=end+1;continue;}
  if(n<20||memcmp(r,"(40.",4)||r[n-1]!=')')return false;size_t comma=4;while(comma<n&&r[comma]!=',')++comma;
  if(comma+6>=n||memcmp(r+comma,", -7",4)||(r[comma+4]!='3'&&r[comma+4]!='4')||r[comma+5]!='.')return false;
  unsigned a=comma-4,b=n-comma-7;if(a<9||a>15||b<10||b>14)return false;uint64_t av,bv;if(!number(r+4,a,av)||!number(r+comma+6,b,bv))return false;
  char*ae,*be;double da=strtod((const char*)r+1,&ae),db=-strtod((const char*)r+comma+2,&be);if(ae!=(const char*)r+comma||be!=(const char*)r+n-1)return false;
  uint64_t ab,bb;memcpy(&ab,&da,8);memcpy(&bb,&db,8);
  if(decround(ab&((1ULL<<47)-1),a,47)!=av||decround(bb&((1ULL<<46)-1),b,46)!=bv)return false;
  base=std::min(base,ab);lonbase=std::min(lonbase,bb);uint8_t lens=a|(b<<4);counts[lens]++;rec.push_back({ab,bb,lens});pos=end+1;
 }
 if(pos!=size||base==UINT64_MAX)return false;
 std::vector<unsigned> ranked;for(unsigned i=0;i<255;++i)if(counts[i])ranked.push_back(i);
 std::sort(ranked.begin(),ranked.end(),[&](unsigned a,unsigned b){return counts[a]!=counts[b]?counts[a]>counts[b]:a<b;});
 uint8_t map[256];memset(map,15,256);uint8_t lens[16];memset(lens,255,16);for(unsigned i=0;i<15&&i<ranked.size();++i){map[ranked[i]]=i;lens[i]=ranked[i];}
 uint32_t ex=0;for(auto&r:rec)ex+=map[r.lens]==15;payload.resize(32+size_t(ex)*8+rows*12);memset(payload.data(),0,payload.size());
 wr64(payload.data(),lonbase);wr32(payload.data()+8,ex);memcpy(payload.data()+12,lens,16);uint8_t*ep=payload.data()+32;uint8_t*out=ep+ex*8;
 for(uint64_t i=0;i<rows;++i){auto&r=rec[i];unsigned tag=map[r.lens];uint64_t a=0,b=0;if(r.lens!=255){a=r.a-base;b=r.b-lonbase;if(a>=(1ULL<<46)||b>=(1ULL<<46))return false;}
  if(tag==15){wr32(ep,i);ep[4]=r.lens;ep+=8;}wr64(out+12*i,a|(b<<46));wr32(out+12*i+8,(b>>18)|(uint32_t(tag)<<28));
 }
 return true;
}

#endif
#ifdef DECODER
struct State { Header h; const uint8_t*p; const uint32_t*quad; uint64_t lonbase; const uint8_t*loclens;const uint8_t*ex;uint32_t nex; };
struct PairTable { uint16_t x[128]; constexpr PairTable():x{}{for(unsigned i=0;i<128;++i)x[i]=uint16_t('0'+i/10)|uint16_t('0'+i%10)<<8;} };
static constexpr PairTable pairs;

struct GenomeTable { uint32_t x[256]; constexpr GenomeTable():x{}{const char*a="acgt";for(unsigned i=0;i<256;++i)for(unsigned j=0;j<4;++j)x[i]|=uint32_t(a[(i>>(2*j))&3])<<(j*8);} };
static constexpr GenomeTable genome;
static inline uint32_t get18(const uint8_t*p,uint64_t i){size_t bit=i*18;return (rd32(p+(bit>>3))>>(bit&7))&0x3ffff;}
static inline void customer(uint8_t*o,uint32_t v){unsigned a=v&15,b=(v>>4)&127,c=v>>11;wr64(o,0x72656d6f74737543ULL);wr32(o+8,0x30303023);uint64_t d=uint64_t(pairs.x[a])|(uint64_t(pairs.x[b])<<16)|(uint64_t(pairs.x[c])<<32)|(uint64_t(10)<<48);memcpy(o+12,&d,7);}
static inline void kmer(uint8_t*o,uint32_t v){uint64_t a=uint64_t(genome.x[v&255])|uint64_t(genome.x[(v>>8)&255])<<32;wr64(o,a);o[8]="acgt"[v>>16];o[9]=10;}
static inline __m256i hex32(__m128i bytes){__m256i w=_mm256_cvtepu8_epi16(bytes);w=_mm256_or_si256(_mm256_srli_epi16(w,4),_mm256_slli_epi16(_mm256_and_si256(w,_mm256_set1_epi16(15)),8));return _mm256_shuffle_epi8(_mm256_setr_epi8('0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f','0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'),w);}
struct UUIDMap { uint8_t idx[64],literal[64]; constexpr UUIDMap():idx{},literal{}{for(unsigned j=0;j<64;++j)idx[j]=64+j;const char*t="84xxxxxx-2da5-11e8-xxxx-xxxxxxxxxxxx\n";for(unsigned j=0;j<37;++j)literal[j]=t[j];for(unsigned j=2;j<8;++j)idx[j]=j-2;for(unsigned j=19;j<23;++j)idx[j]=j-13;for(unsigned j=24;j<36;++j)idx[j]=j-14;} };
alignas(64) static constexpr UUIDMap uuidmap;
static inline void uuid(uint8_t*o,const uint8_t*p,const uint8_t*tailp,uint64_t i,uint32_t base){uint32_t delta=get18(p,i);uint32_t first=__builtin_bswap32(base+delta*10)>>8;size_t tb=i*60;uint64_t t=(rd64(tailp+(tb>>3))>>(tb&7))&((1ULL<<60)-1);t=_pdep_u64(t,0xfffffffffffcff3fULL)|0x30080;__m128i bytes=_mm_set_epi64x(t>>40,uint64_t(first)|(t<<24));__m256i ascii=hex32(bytes);__m512i result=_mm512_permutex2var_epi8(_mm512_castsi256_si512(ascii),_mm512_loadu_si512(uuidmap.idx),_mm512_loadu_si512(uuidmap.literal));_mm512_mask_storeu_epi8(o,(1ULL<<37)-1,result);}
static inline unsigned hexlen(uint32_t v){return v?8-(_lzcnt_u32(v)>>2):1;}
static inline unsigned hexadecimal(uint8_t*o,uint32_t v){unsigned n=hexlen(v);__m128i w=_mm_cvtepu8_epi16(_mm_cvtsi32_si128(__builtin_bswap32(v)));w=_mm_or_si128(_mm_srli_epi16(w,4),_mm_slli_epi16(_mm_and_si128(w,_mm_set1_epi16(15)),8));__m128i a=_mm_shuffle_epi8(_mm_setr_epi8('0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'),w);uint64_t word=uint64_t(_mm_cvtsi128_si64(a))>>((8-n)*8);_mm_mask_storeu_epi8(o,(1u<<n)-1,_mm_cvtsi64_si128(word));o[n]=10;return n+1;}
static inline uint64_t digits8(uint32_t v,const uint32_t*quad){uint32_t a=v/10000,b=v%10000;return uint64_t(quad[a])|(uint64_t(quad[b])<<32);}
static inline __m128i digits15(uint64_t v,const uint32_t*quad){uint64_t a=digits8(v/100000000,quad)>>8,b=digits8(v%100000000,quad);return _mm_set_epi64x(b>>8,a|(b<<56));}
static inline __m128i digits14(uint64_t v,const uint32_t*quad){uint64_t a=digits8(v/100000000,quad)>>16,b=digits8(v%100000000,quad);return _mm_set_epi64x(b>>16,a|(b<<48));}
static inline uint8_t locmeta(const State*s,const uint8_t*p,uint64_t row){unsigned tag=p[11]>>4;if(tag<15)return s->loclens[tag];uint32_t lo=0,hi=s->nex;while(lo<hi){uint32_t mid=(lo+hi)/2;uint32_t v=rd32(s->ex+mid*8);if(v<row)lo=mid+1;else hi=mid;}return lo<s->nex&&rd32(s->ex+lo*8)==row?s->ex[lo*8+4]:255;}
static inline unsigned loclen(uint8_t m){return m==255?5:12+(m&15)+(m>>4);}
static inline unsigned location(uint8_t*o,const uint8_t*p,const State*s,uint8_t meta){if(meta==255){wr32(o,0x4c4c554e);o[4]=10;return 5;}unsigned a=meta&15,b=meta>>4;uint64_t lat=s->h.aux+(rd64(p)&((1ULL<<46)-1));uint64_t lon=s->lonbase+((rd64(p+4)>>14)&((1ULL<<46)-1));uint64_t lv=decround(lat&((1ULL<<47)-1),a,47)*pow10[15-a],rv=decround(lon&((1ULL<<46)-1),b,46)*pow10[14-b];wr32(o,0x2e303428);_mm_mask_storeu_epi8(o+4,(1u<<a)-1,digits15(lv,s->quad));uint8_t*t=o+4+a;wr32(t,0x372d202c);t[4]='3'+(((lon>>46)&63)==10);t[5]='.';_mm_mask_storeu_epi8(t+6,(1u<<b)-1,digits14(rv,s->quad));t[6+b]=')';t[7+b]=10;return 12+a+b;}
#endif
}
#ifdef ENCODER
extern "C" int64_t spec_encode(const uint8_t*raw,size_t size,uint8_t*out,size_t cap){
 uint16_t type=0;uint64_t rows=0;
 if(size%19==0&&size>=19&&!memcmp(raw,"Customer#",9))type=1,rows=size/19;
 else if(size%10==0&&size>=10&&raw[9]==10&&(raw[0]=='a'||raw[0]=='c'||raw[0]=='g'||raw[0]=='t'))type=2,rows=size/10;
 else if(size%37==0&&size>=37&&!memcmp(raw,"84",2)&&raw[8]=='-'&&raw[36]==10)type=3,rows=size/37;
 else if(size>=4&&raw[0]=='('&&raw[1]=='4'&&raw[2]=='0'&&raw[3]=='.')type=5;
 else {bool okay=size>0&&raw[size-1]==10;size_t start=0;for(size_t i=0;okay&&i<size;++i)if(raw[i]==10){if(i-start<1||i-start>8||(i-start>1&&raw[start]=='0'))okay=false;start=i+1;++rows;}else if(unhex(raw[i],true)<0)okay=false;if(okay)type=4;}
 if(!type)return -2;
 if(type==5)for(size_t i=0;i<size;++i)rows+=raw[i]==10;
 uint64_t base=0;std::vector<uint8_t> locpayload;if(type==5&&!fitlocation(raw,size,rows,base,locpayload))return -2;
 if(type==3){uint32_t mn=UINT32_MAX,mx=0;for(uint64_t i=0;i<rows;++i){const uint8_t*r=raw+37*i;uint32_t v=0;for(unsigned j=0;j<8;++j){int c=unhex(r[j]);if(c<0)return -2;v=(v<<4)|c;}if(v&1)return -2;int a=unhex(r[19]),b=unhex(r[25]);if(a<8||a>11||b<0||(b&3)!=3)return -2;mn=v<mn?v:mn;mx=v>mx?v:mx;}if((mx-mn)/10>0x3ffff)return -2;base=mn;}
 size_t bytes=type<=2?(rows*18+7)/8:type==3?(rows*18+7)/8+(rows*60+7)/8:type==4?rows*4:locpayload.size();
 if(bytes>SIZE_MAX-sizeof(Header)-32||cap<sizeof(Header)+bytes+32)return -1;
 memset(out,0,sizeof(Header)+bytes+32);Header h{MAGIC,type,5,size,rows,base};memcpy(out,&h,sizeof h);uint8_t*p=out+sizeof h;
 if(type==1){for(uint64_t i=0;i<rows;++i){const uint8_t*r=raw+19*i;uint64_t v;if(memcmp(r,"Customer#",9)||r[18]!=10||!number(r+9,9,v)||v>=1000000)return -2;if(v>=160000)return -2;uint32_t code=(v/10000)|(((v/100)%100)<<4)|((v%100)<<11);put18(p,i,code);}}
 else if(type==2){for(uint64_t i=0;i<rows;++i){const uint8_t*r=raw+10*i;if(r[9]!=10)return -2;uint32_t v=0;for(unsigned j=0;j<9;++j){unsigned c=r[j]=='a'?0:r[j]=='c'?1:r[j]=='g'?2:r[j]=='t'?3:4;if(c==4)return -2;v|=c<<(j*2);}put18(p,i,v);}}
 else if(type==3){for(uint64_t i=0;i<rows;++i){const uint8_t*r=raw+37*i;uint8_t q[11];if(memcmp(r,"84",2)||memcmp(r+8,"-2da5-11e8-",11)||r[23]!='-'||r[36]!=10)return -2;for(unsigned j=0;j<11;++j){unsigned at=j<3?2+2*j:j<5?19+2*(j-3):24+2*(j-5);int a=unhex(r[at]),b=unhex(r[at+1]);if(a<0||b<0)return -2;q[j]=(a<<4)|b;}uint32_t f=0x84000000|uint32_t(q[0])<<16|uint32_t(q[1])<<8|q[2];if((f-base)%10)return -2;uint32_t d=(f-base)/10;put18(p,i,d);uint64_t t=_pext_u64(rd64(q+3),0xfffffffffffcff3fULL);uint8_t*tp=p+(rows*18+7)/8;size_t tb=i*60;wr64(tp+(tb>>3),rd64(tp+(tb>>3))|(t<<(tb&7)));}}
 else if(type==4){size_t at=0;for(uint64_t i=0;i<rows;++i){uint32_t v=0;while(at<size&&raw[at]!=10)v=(v<<4)|unhex(raw[at++],true);++at;wr32(p+4*i,v);}}
 else memcpy(p,locpayload.data(),locpayload.size());
 return sizeof(Header)+bytes+32;
}
#endif
#ifdef DECODER
extern "C" void* spec_open(const uint8_t*data,size_t size){if(size<64)return nullptr;Header h;memcpy(&h,data,32);if(h.magic!=MAGIC||h.version!=5||h.type<1||h.type>5||h.rows>UINT32_MAX)return nullptr;uint64_t bytes=h.type<=2?(h.rows*18+7)/8:h.type==3?(h.rows*18+7)/8+(h.rows*60+7)/8:h.type==4?h.rows*4:32+uint64_t(rd32(data+40))*8+h.rows*12;if(bytes>size-64||size!=bytes+64)return nullptr;if((h.type==1&&h.raw!=h.rows*19)||(h.type==2&&h.raw!=h.rows*10)||(h.type==3&&h.raw!=h.rows*37))return nullptr;State*s=(State*)malloc(sizeof(State)+(h.type==5?40000:0));if(!s)return nullptr;s->h=h;s->p=data+32;s->quad=nullptr;if(h.type==5){uint32_t*q=(uint32_t*)(s+1);for(unsigned a=0;a<100;++a)for(unsigned b=0;b<100;++b)q[a*100+b]=uint32_t(pairs.x[a])|(uint32_t(pairs.x[b])<<16);s->quad=q;s->lonbase=rd64(data+32);s->nex=rd32(data+40);s->loclens=data+44;s->ex=data+64;s->p=s->ex+s->nex*8;for(unsigned i=0;i<15;++i){unsigned z=s->loclens[i];if(z!=255&&((z&15)<9||(z>>4)<10||(z>>4)>14)){free(s);return nullptr;}}for(uint32_t i=0;i<s->nex;++i){unsigned z=s->ex[i*8+4];if(z!=255&&((z&15)<9||(z>>4)<10||(z>>4)>14)){free(s);return nullptr;}}}return s;}
extern "C" int64_t spec_decode(void*state,uint8_t*out,size_t cap){State*s=(State*)state;if(!s||cap<s->h.raw)return -1;const uint8_t*p=s->p;uint64_t n=s->h.rows;uint8_t*o=out;
 if(s->h.type==1){uint64_t i=0;for(;i+4<=n;i+=4){const uint8_t*q=p+(i/4)*9;uint64_t v=rd64(q);customer(o,v&0x3ffff);customer(o+19,(v>>18)&0x3ffff);customer(o+38,(v>>36)&0x3ffff);customer(o+57,(v>>54)|(uint32_t(q[8])<<10));o+=76;}for(;i<n;++i){customer(o,get18(p,i));o+=19;}}
 else if(s->h.type==2){uint64_t i=0;for(;i+4<=n;i+=4){const uint8_t*q=p+(i/4)*9;uint64_t v=rd64(q);kmer(o,v&0x3ffff);kmer(o+10,(v>>18)&0x3ffff);kmer(o+20,(v>>36)&0x3ffff);kmer(o+30,(v>>54)|(uint32_t(q[8])<<10));o+=40;}for(;i<n;++i){kmer(o,get18(p,i));o+=10;}}
 else if(s->h.type==3){const uint8_t*tp=p+(n*18+7)/8;for(uint64_t i=0;i<n;++i){uuid(o,p,tp,i,s->h.aux);o+=37;}}
 else if(s->h.type==4){for(uint64_t i=0;i<n;++i){unsigned z=hexlen(rd32(p+4*i))+1;if(size_t(o-out)+z>cap)return -1;o+=hexadecimal(o,rd32(p+4*i));}}
 else {for(uint64_t i=0;i<n;++i){uint8_t meta=locmeta(s,p+12*i,i);unsigned z=loclen(meta);if(size_t(o-out)+z>cap)return -1;o+=location(o,p+12*i,s,meta);}}
 return o-out;
}
extern "C" int64_t spec_rows(void*state,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets){State*s=(State*)state;if(!s||!offsets)return -1;offsets[0]=0;const uint8_t*p=s->p;uint64_t n=s->h.rows;size_t off=0;
 if(s->h.type==1){if(count>cap/19)return -1;for(size_t j=0;j<count;++j){uint64_t i=ids[j];if(i>=n)return -1;customer(out+j*19,get18(p,i));offsets[j+1]=(j+1)*19;}return count*19;}
 if(s->h.type==2){if(count>cap/10)return -1;for(size_t j=0;j<count;++j){uint64_t i=ids[j];if(i>=n)return -1;kmer(out+j*10,get18(p,i));offsets[j+1]=(j+1)*10;}return count*10;}
 if(s->h.type==3){if(count>cap/37)return -1;const uint8_t*tp=p+(n*18+7)/8;for(size_t j=0;j<count;++j){uint64_t i=ids[j];if(i>=n)return -1;uuid(out+j*37,p,tp,i,s->h.aux);offsets[j+1]=(j+1)*37;}return count*37;}
 if(s->h.type==4){for(size_t j=0;j<count;++j){uint64_t i=ids[j];if(i>=n)return -1;uint32_t v=rd32(p+i*4);unsigned z=hexlen(v)+1;if(z>cap-off)return -1;off+=hexadecimal(out+off,v);offsets[j+1]=off;}}
 else{for(size_t j=0;j<count;++j){uint64_t i=ids[j];if(i>=n)return -1;const uint8_t*q=p+i*12;uint8_t meta=locmeta(s,q,i);unsigned z=loclen(meta);if(z>cap-off)return -1;off+=location(out+off,q,s,meta);offsets[j+1]=off;}}
 return off;
}
extern "C" void spec_close(void*state){free(state);}
#endif
