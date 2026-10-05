#pragma once
#include "entropy.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#ifndef DECODE_ONLY
#include <vector>
#endif
namespace indbest {
static constexpr unsigned CX=729;
inline bool ws(uint8_t c){return c==' '||(c>=9&&c<=13);}
inline bool eq(const uint8_t*p,size_t n,const char*s,size_t z){return n==z&&!memcmp(p,s,z);}
inline unsigned classify(const uint8_t*p,size_t n,unsigned&tail){
 size_t end=n;while(end&&ws(p[end-1]))end--;if(!end){tail=4;return 5;}
 tail=p[end-1]==':'?1:p[end-1]==','?2:p[end-1]=='('?3:p[end-1]=='='?5:p[end-1]==')'?6:p[end-1]==']'?7:(p[end-1]=='\''||p[end-1]=='\"')?8:0;
 size_t a=0;while(a<end&&ws(p[a]))a++;size_t b=a;while(b<end&&!ws(p[b]))b++;
 const uint8_t*w=p+a;size_t z=b-a;
 if(eq(w,z,"def",3)||eq(w,z,"async",5))return 1;
 if(n&&p[0]=='@')return 2;
 if(eq(w,z,"else:",5)||eq(w,z,"elif",4)||eq(w,z,"except",6)||eq(w,z,"except:",7)||eq(w,z,"finally:",8))return 3;
 if(eq(w,z,"return",6)||eq(w,z,"raise",5)||eq(w,z,"pass",4)||eq(w,z,"break",5)||eq(w,z,"continue",8))return 4;
 if(n&&(p[0]==')'||p[0]==']'||p[0]=='}'))return 6;
 if(eq(w,z,"if",2)||eq(w,z,"for",3)||eq(w,z,"while",5)||eq(w,z,"try:",4))return 7;
 if(n>=5&&!memcmp(p,"self.",5))return 8;
 return 0;
}
inline uint32_t rd(const uint8_t*p){uint32_t v;memcpy(&v,p,4);return v;}
inline bool decode(const uint8_t*body,size_t size,const uint8_t*meta,size_t ms,uint8_t*out,size_t n){
 if(!meta||(!body&&size)||(!out&&n)||ms<8+CX*8||memcmp(meta,"INDCTX03",8))return false;
 size_t count=0,total=8+CX*8;uint32_t counts[CX],sizes[CX];for(unsigned j=0;j<CX;j++){counts[j]=rd(meta+8+8*j);sizes[j]=rd(meta+12+8*j);if(sizes[j]>ms-total||counts[j]>n-count)return false;total+=sizes[j];count+=counts[j];}if(total!=ms)return false;
 uint8_t*tmp=(uint8_t*)malloc(count?count:1);if(!tmp)return false;uint8_t*ptr[CX],*end[CX];size_t at=8+CX*8,off=0;bool ok=true;
 for(unsigned j=0;j<CX;j++){ptr[j]=tmp+off;end[j]=ptr[j]+counts[j];if(!ent::decode(meta+at,sizes[j],ptr[j],counts[j])){free(tmp);return false;}at+=sizes[j];off+=counts[j];}
 size_t p=0,pos=0;unsigned prev=0,tail=0;
 while(ok&&p<size){const uint8_t*nl=(const uint8_t*)memchr(body+p,'\n',size-p);size_t q=nl?size_t(nl-body)+1:size;unsigned nexttail=0,kind=classify(body+p,q-p,nexttail);unsigned ctx=(tail*9+kind)*9+(prev/4<8?prev/4:8);unsigned delta=0,shift=0;for(;;){if(ptr[ctx]==end[ctx]||shift>28){ok=false;break;}unsigned c=*ptr[ctx]++;if(shift==28&&(c&127)>15){ok=false;break;}delta|=(c&127)<<shift;if(!(c&128))break;shift+=7;}if(!ok)break;int64_t v=int64_t(prev)+((delta&1)?-int64_t((uint64_t(delta)+1)/2):int64_t(delta/2));if(v<0||uint64_t(v)>n-pos||q-p>n-pos-size_t(v)){ok=false;break;}if(v){if(v<=32&&n-pos>=32)memset(out+pos,' ',32);else memset(out+pos,' ',v);}pos+=v;if(q-p<=64&&size-p>=64&&n-pos>=64)memcpy(out+pos,body+p,64);else memcpy(out+pos,body+p,q-p);pos+=q-p;p=q;prev=v;tail=nexttail;
 }
 // A final line containing spaces has no body bytes. Its archive remains in the blank context.
 if(ok&&pos<n){unsigned ctx=(tail*9+5)*9+(prev/4<8?prev/4:8);unsigned delta=0,shift=0;for(;;){if(ptr[ctx]==end[ctx]||shift>28){ok=false;break;}unsigned c=*ptr[ctx]++;if(shift==28&&(c&127)>15){ok=false;break;}delta|=(c&127)<<shift;if(!(c&128))break;shift+=7;}int64_t v=int64_t(prev)+((delta&1)?-int64_t((uint64_t(delta)+1)/2):int64_t(delta/2));if(ok&&v>=0&&uint64_t(v)==n-pos){memset(out+pos,' ',v);pos+=v;}else ok=false;}
 for(unsigned j=0;j<CX;j++)if(ptr[j]!=end[j])ok=false;free(tmp);return ok&&pos==n&&p==size;
}
#ifndef DECODE_ONLY
struct Encoded{std::vector<uint8_t>body,archive;};
inline Encoded encode(const uint8_t*raw,size_t n){
 Encoded result;std::vector<uint8_t>stream[CX];result.body.reserve(n);unsigned prev=0,tail=0;size_t p=0;
 while(p<n){size_t a=p;while(p<n&&raw[p]==' ')p++;unsigned v=p-a;const uint8_t*nl=(const uint8_t*)memchr(raw+p,'\n',n-p);size_t q=nl?size_t(nl-raw)+1:n;unsigned nexttail=0,kind=classify(raw+p,q-p,nexttail);unsigned ctx=(tail*9+kind)*9+(prev/4<8?prev/4:8);unsigned delta=v>=prev?2*(v-prev):2*(prev-v)-1;while(delta>=128){stream[ctx].push_back((delta&127)|128);delta>>=7;}stream[ctx].push_back(delta);result.body.insert(result.body.end(),raw+p,raw+q);p=q;prev=v;tail=nexttail;
 }
 result.archive.resize(8+CX*8);memcpy(result.archive.data(),"INDCTX03",8);for(unsigned j=0;j<CX;j++){auto c=ent::encode(stream[j]);uint32_t x=stream[j].size(),z=c.size();memcpy(result.archive.data()+8+8*j,&x,4);memcpy(result.archive.data()+12+8*j,&z,4);result.archive.insert(result.archive.end(),c.begin(),c.end());}return result;
}
#endif
}
