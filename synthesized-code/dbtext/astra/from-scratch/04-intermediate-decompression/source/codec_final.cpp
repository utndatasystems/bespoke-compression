
#include <stdint.h>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "interface/codec.h"
#include "structured.h"
#include "location.h"
#include "textcodec_packed.h"
#include "urls.h"
#ifdef ENCODER
extern "C" int64_t lab_encode(const uint8_t*r,size_t n,uint8_t*a,size_t c) {
 try {std::vector<uint8_t>v;if(!dbs::encode(v,r,n)&&!location_encode(v,r,n)&&!urls_encode(v,r,n)&&!txt::encode(v,r,n))return -1;
 if(v.size()>c||(!a&&c))return -1;memcpy(a,v.data(),v.size());return v.size();} catch(...){return -1;}
}
#else
struct Wrap {int type;void*s;};
static void destroy(int type,void*s){if(type==1)dbs::close((dbs::State*)s);else if(type==2)location_close((LocationState*)s);else if(type==4)urls_close((UrlsState*)s);else txt::close((txt::State*)s);}
extern "C" void*lab_open(const uint8_t*p,size_t n){
 if(!p)return nullptr;
 try {void*s=nullptr;int type=0;if(dbs::is(p,n)){s=dbs::open(p,n);type=1;}else if(location_is(p,n)){s=location_open(p,n);type=2;}else if(urls_is(p,n)){s=urls_open(p,n);type=4;}else if(txt::is(p,n)){s=txt::open(p,n);type=3;}
 if(!s)return nullptr;Wrap*w=(Wrap*)malloc(sizeof(Wrap));if(!w){destroy(type,s);return nullptr;}*w={type,s};return w;
 }catch(...){return nullptr;}
}
extern "C" int64_t lab_decode(void*v,uint8_t*o,size_t c){
 if(!v||(!o&&c))return -1;auto*w=(Wrap*)v;
 if(w->type==1)return dbs::decode((dbs::State*)w->s,o,c);
 if(w->type==2)return location_decode((LocationState*)w->s,o,c);
 if(w->type==4)return urls_decode((UrlsState*)w->s,o,c);
 return txt::decode((txt::State*)w->s,o,c);
}
extern "C" int64_t lab_rows(void*v,const uint64_t*id,size_t n,uint8_t*o,size_t c,uint64_t*x){
 if(!v||!x||(!id&&n)||(!o&&c))return -1;if(!n){x[0]=0;return 0;}auto*w=(Wrap*)v;
 if(w->type==1)return dbs::rows((dbs::State*)w->s,id,n,o,c,x);
 if(w->type==2)return location_rows((LocationState*)w->s,id,n,o,c,x);
 if(w->type==4)return urls_rows((UrlsState*)w->s,id,n,o,c,x);
 return txt::rows((txt::State*)w->s,id,n,o,c,x);
}
extern "C" void lab_close(void*v){if(!v)return;auto*w=(Wrap*)v;destroy(w->type,w->s);free(w);}
#endif
