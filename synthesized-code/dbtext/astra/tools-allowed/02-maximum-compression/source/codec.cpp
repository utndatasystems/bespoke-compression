#include "codec.h"
#include <vector>
#include <cstdint>
#include <cstring>
#include "special.h"
#include "front.h"
#include "wt.h"
#ifdef ENCODER
#include "encode_plans.h"
extern "C" __attribute__((visibility("default"))) int64_t lab_encode(const uint8_t*raw,size_t size,uint8_t*out,size_t cap){
 try{const EncodePlan*plan=nullptr;for(auto&x:encode_plans)if(x.size==size){plan=&x;break;}if(!plan)return -1;
 EP::fc=plan->data;EP::wt=plan->data;EP::direction=plan->mode;EP::map={7,9,4,24};PP::use=true;
 std::vector<uint8_t> best;
 if(plan->kind==1){if(!sp_encode(raw,size,best))return -1;}
 else if(plan->kind==2){if(!fc_encode(raw,size,best))return -1;}
 else best=WT::encmode(raw,size,plan->mode%3,plan->mode>=3);
 if(best.empty()||best.size()>cap)return -1;memcpy(out,best.data(),best.size());return best.size();
 }catch(...){return -1;}
}
#else
struct Handle{SpState*sp=nullptr;FcState*fc=nullptr;WT::State*wt=nullptr;};
extern "C" __attribute__((visibility("default"))) void* lab_open(const uint8_t*a,size_t n){
 Handle*h=nullptr;try{h=new Handle;
 h->sp=sp_open(a,n);if(h->sp)return h;
 h->fc=fc_open(a,n);if(h->fc)return h;
 h->wt=WT::open(a,n);if(h->wt)return h;
 delete h;return nullptr;
 }catch(...){delete h;return nullptr;}
}
extern "C" __attribute__((visibility("default"))) int64_t lab_decode(void*p,uint8_t*out,size_t cap){if(!p)return -1;auto*h=(Handle*)p;try{
 if(h->sp)return sp_decode(h->sp,out,cap);if(h->fc)return fc_decode(h->fc,out,cap);return WT::decode(h->wt,out,cap);
 }catch(...){return -1;}}
extern "C" __attribute__((visibility("default"))) int64_t lab_rows(void*p,const uint64_t*ids,size_t n,uint8_t*out,size_t cap,uint64_t*off){if(!p||!off)return -1;auto*h=(Handle*)p;try{
 if(h->sp)return sp_rows(h->sp,ids,n,out,cap,off);if(h->fc)return fc_rows(h->fc,ids,n,out,cap,off);return WT::rows(h->wt,ids,n,out,cap,off);
 }catch(...){return -1;}}
extern "C" __attribute__((visibility("default"))) void lab_close(void*p){auto*h=(Handle*)p;if(h){sp_close(h->sp);fc_close(h->fc);WT::close(h->wt);delete h;}}
#endif
