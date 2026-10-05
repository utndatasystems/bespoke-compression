#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <new>
#include "interface/codec.h"
#include "special.h"
#include "location.h"
#ifdef ENCODER
#define BPE_ENCODER
#endif
#include "dict_codec.hpp"
struct Header{uint64_t magic,raw,rows,type,payload;};
constexpr uint64_t MAGIC=0x315854424442414cull;
#ifdef ENCODER
extern "C" int64_t lab_encode(const uint8_t*raw,size_t size,uint8_t*archive,size_t cap){try{std::vector<uint8_t> p;uint64_t nr=0;unsigned type=encode_special(raw,size,p,nr);if(!type)type=encode_location(raw,size,p,nr);if(!type){type=10;nr=0;for(size_t i=0;i<size;i++)nr+=raw[i]=='\n';nr+=size&&raw[size-1]!='\n';p=bpe_encode(raw,size);}Header h{MAGIC,size,nr,type,p.size()};if(cap<sizeof(h)||p.size()>cap-sizeof(h))return -1;memcpy(archive,&h,sizeof(h));memcpy(archive+sizeof(h),p.data(),p.size());return sizeof(h)+p.size();}catch(...){return -1;}}
#else
struct State{Header h;const uint8_t*p;BpeState*b=nullptr;};
extern "C" void* lab_open(const uint8_t*a,size_t size){try{if(size<sizeof(Header))return nullptr;Header h;memcpy(&h,a,sizeof(h));if(h.magic!=MAGIC||h.payload!=size-sizeof(h)||h.raw>6400000||h.rows>h.raw||h.rows>230000)return nullptr;const uint8_t*p=a+sizeof(h);State*s=new State{h,p};bool ok=false;if(h.type>=1&&h.type<=4)ok=special_validate(h.type,p,h.payload,h.rows,h.raw);else if(h.type==5)ok=location_validate(p,h.payload,h.rows,h.raw);else if(h.type==10){s->b=bpe_open(p,h.payload,h.raw,h.rows);ok=s->b;}if(!ok){delete s;return nullptr;}return s;}catch(...){return nullptr;}}
extern "C" int64_t lab_decode(void*ptr,uint8_t*out,size_t cap){if(!ptr)return -1;auto*s=(State*)ptr;if(cap<s->h.raw)return -1;if(s->h.type<=4)return special_decode(s->h.type,s->p,s->h.payload,s->h.rows,out,cap);if(s->h.type==5)return location_decode(s->p,s->h.payload,s->h.rows,out,cap);return bpe_decode(s->b,out,cap);}
extern "C" int64_t lab_rows(void*ptr,const uint64_t*ids,size_t n,uint8_t*out,size_t cap,uint64_t*offs){if(!ptr||!offs||(!ids&&n))return -1;auto*s=(State*)ptr;if(s->h.type<=4)return special_rows(s->h.type,s->p,s->h.payload,s->h.rows,ids,n,out,cap,offs);if(s->h.type==5)return location_rows(s->p,s->h.payload,s->h.rows,ids,n,out,cap,offs);return bpe_rows(s->b,ids,n,out,cap,offs);}
extern "C" void lab_close(void*ptr){auto*s=(State*)ptr;if(s){if(s->b)bpe_close(s->b);delete s;}}
#endif
