#include "interface/codec.h"
#include "special.h"
#include "location2.h"
#include "textbit.h"
#include "texthuff.h"
#include "url.h"
#ifdef ENCODER
extern "C" int64_t lab_encode(const uint8_t*raw,size_t n,uint8_t*out,size_t cap){try{std::vector<uint8_t>v;if(!sp::encode(raw,n,v)&&!loc2::encode(raw,n,v)&&!url::encode(raw,n,v) ){std::vector<uint8_t> h;if(!tb::encode(raw,n,v)||!th::encode(raw,n,h))return -1;if(h.size()<v.size())v.swap(h);}if(v.size()>cap)return -1;memcpy(out,v.data(),v.size());return v.size();}catch(...){return -1;}}
#else
struct Any {unsigned kind;void*p;};
extern "C" void*lab_open(const uint8_t*a,size_t n){try{if(auto*p=sp::open(a,n))return new Any{1,p};if(auto*p=loc2::open(a,n))return new Any{2,p};if(auto*p=url::open(a,n))return new Any{4,p};if(auto*p=th::open(a,n))return new Any{5,p};if(auto*p=tb::open(a,n))return new Any{3,p};return nullptr;}catch(...){return nullptr;}}
extern "C" int64_t lab_decode(void*v,uint8_t*out,size_t cap){if(!v)return -1;auto*s=(Any*)v;switch(s->kind){case 1:return sp::decode((sp::State*)s->p,out,cap);case 2:return loc2::decode((loc2::State*)s->p,out,cap);case 4:return url::decode((url::State*)s->p,out,cap);case 5:return th::decode((th::State*)s->p,out,cap);case 3:return tb::decode((tb::State*)s->p,out,cap);}return -1;}
extern "C" int64_t lab_rows(void*v,const uint64_t*ids,size_t n,uint8_t*out,size_t cap,uint64_t*off){if(!v)return -1;auto*s=(Any*)v;
 if((s->kind==3||s->kind==5)&&off){size_t nr=s->kind==3?((tb::State*)s->p)->h.rows:((th::State*)s->p)->h.rows;if(n==nr){bool all=true;for(size_t i=0;i<n;i++)if(ids[i]!=i){all=false;break;}if(all){int64_t used=lab_decode(v,out,cap);if(used<0)return -1;off[0]=0;size_t j=0,i=0;__m512i lf=_mm512_set1_epi8(10);for(;i+64<=size_t(used);i+=64){uint64_t mask=_mm512_cmpeq_epi8_mask(_mm512_loadu_si512(out+i),lf);while(mask){if(j==n)return -1;off[++j]=i+__builtin_ctzll(mask)+1;mask&=mask-1;}}for(;i<size_t(used);i++)if(out[i]==10){if(j==n)return -1;off[++j]=i+1;}if(j<n&&off[j]<size_t(used))off[++j]=used;return j==n?used:-1;}}}
switch(s->kind){case 1:return sp::rows((sp::State*)s->p,ids,n,out,cap,off);case 2:return loc2::rows((loc2::State*)s->p,ids,n,out,cap,off);case 4:return url::rows((url::State*)s->p,ids,n,out,cap,off);case 5:return th::rows((th::State*)s->p,ids,n,out,cap,off);case 3:return tb::rows((tb::State*)s->p,ids,n,out,cap,off);}return -1;}
extern "C" void lab_close(void*v){if(!v)return;auto*s=(Any*)v;switch(s->kind){case 1:delete(sp::State*)s->p;break;case 2:delete(loc2::State*)s->p;break;case 4:delete(url::State*)s->p;break;case 5:delete(th::State*)s->p;break;case 3:delete(tb::State*)s->p;break;}delete s;}
#endif
