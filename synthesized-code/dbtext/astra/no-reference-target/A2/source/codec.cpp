#include "codec.h"
#include "general.h"
#ifdef HAVE_SPECIAL
#include "special.h"
#endif
#ifdef HAVE_LOCATION
#include "location.h"
#endif
struct State {uint32_t type;void* data;};
#ifdef ENCODER
extern "C" __attribute__((visibility("default"))) int64_t lab_encode(const uint8_t* raw,size_t size,uint8_t* archive,size_t cap){
 try{
 std::vector<uint8_t> out; bool done=false;
#ifdef HAVE_SPECIAL
 done=special_encode(raw,size,out);
#endif
#ifdef HAVE_LOCATION
 if(!done)done=locations_encode(raw,size,out);
#endif
 if(!done)general_encode(raw,size,out);
 if(out.size()>cap)return -1;memcpy(archive,out.data(),out.size());return out.size();
 }catch(...){return -1;}
}
#else
extern "C" __attribute__((visibility("default"))) void* lab_open(const uint8_t* a,size_t size){
 try{
 if(size<4)return nullptr;
 uint32_t type=gr32(a);void* p=nullptr;
 if(type>=100)p=general_open(a,size);
#ifdef HAVE_LOCATION
 else if(type==50)p=locations_open(a,size);
#endif
#ifdef HAVE_SPECIAL
 else if(type<50)p=special_open(a,size);
#endif
 if(!p)return nullptr;return new State{type,p};
 }catch(...){return nullptr;}
}
extern "C" __attribute__((visibility("default"))) int64_t lab_decode(void* st,uint8_t* out,size_t cap){
 try{if(!st)return -1;auto s=(State*)st;
 if(s->type>=100)return general_decode(s->data,out,cap);
#ifdef HAVE_LOCATION
 if(s->type==50)return locations_decode(s->data,out,cap);
#endif
#ifdef HAVE_SPECIAL
 return special_decode(s->data,out,cap);
#endif
 return -1;
 }catch(...){return -1;}
}
extern "C" __attribute__((visibility("default"))) int64_t lab_rows(void* st,const uint64_t* ids,size_t n,uint8_t* out,size_t cap,uint64_t* offsets){
 try{if(!st||!offsets)return -1;auto s=(State*)st;
 if(s->type>=100)return general_rows(s->data,ids,n,out,cap,offsets);
#ifdef HAVE_LOCATION
 if(s->type==50)return locations_rows(s->data,ids,n,out,cap,offsets);
#endif
#ifdef HAVE_SPECIAL
 return special_rows(s->data,ids,n,out,cap,offsets);
#endif
 return -1;
 }catch(...){return -1;}
}
extern "C" __attribute__((visibility("default"))) void lab_close(void* st){
 if(!st)return;auto s=(State*)st;
 if(s->type>=100)general_close(s->data);
#ifdef HAVE_LOCATION
 else if(s->type==50)locations_close(s->data);
#endif
#ifdef HAVE_SPECIAL
 else special_close(s->data);
#endif
 delete s;
}
#endif
