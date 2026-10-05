#include "codec.h"
#include <cstdint>
#include <vector>
#include "special_compact.h"
#include "structure_hybrid2.h"
#include "phrase.h"
#ifdef ENCODER
extern "C" __attribute__((visibility("default"))) int64_t lab_encode(const uint8_t*r,size_t n,uint8_t*a,size_t cap){
 try {std::vector<uint8_t> v;
 if(!special::encode(r,n,v) && !structure::encode(r,n,v)) { if(!phrase::encode(r,n,v)) return -1; }
 if(v.size()>cap)return -1; memcpy(a,v.data(),v.size()); return v.size(); }catch(...){return -1;}
}
#else
struct Any {int type;void*p;};
extern "C" __attribute__((visibility("default"))) void* lab_open(const uint8_t*a,size_t n){
 if(!a)return nullptr;try {void*p;if((p=special::open(a,n)))return new Any{1,p};
 if((p=structure::open(a,n)))return new Any{2,p};
 if((p=phrase::open(a,n)))return new Any{3,p};return nullptr;}catch(...){return nullptr;}
}
extern "C" __attribute__((visibility("default"))) int64_t lab_decode(void*v,uint8_t*o,size_t c){if(!v||!o)return -1;Any*s=(Any*)v;
 if(s->type==1)return special::decode((special::State*)s->p,o,c);
 if(s->type==2)return structure::decode((structure::State*)s->p,o,c);
 return phrase::decode((phrase::State*)s->p,o,c);}
extern "C" __attribute__((visibility("default"))) int64_t lab_rows(void*v,const uint64_t*i,size_t n,uint8_t*o,size_t c,uint64_t*x){if(!v||!x||(n&&(!i||!o)))return -1;if(!n){x[0]=0;return 0;}Any*s=(Any*)v;
 if(s->type==1)return special::rows((special::State*)s->p,i,n,o,c,x);
 if(s->type==2)return structure::rows((structure::State*)s->p,i,n,o,c,x);
 return phrase::rows((phrase::State*)s->p,i,n,o,c,x);}
extern "C" __attribute__((visibility("default"))) void lab_close(void*v){if(!v)return;Any*s=(Any*)v;
 if(s->type==1)special::close((special::State*)s->p);
 else if(s->type==2)structure::close((structure::State*)s->p);
 else phrase::close((phrase::State*)s->p);delete s;}
#endif
