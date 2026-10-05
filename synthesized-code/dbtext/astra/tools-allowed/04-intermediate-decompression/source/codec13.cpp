#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>
#include "special_uuid18.h"
#include "dict_location_fast.h"
#include "ph_selective.h"
#include "hc_pruned.h"
#ifndef ENCODER
#define DICTURL_DECODE_ONLY
#endif
#include "dict_url_codec_v4.hpp"
#ifdef ENCODER
extern "C" __attribute__((visibility("default"))) int64_t lab_encode(const uint8_t*raw,size_t n,uint8_t*a,size_t cap) {try{std::vector<uint8_t>x;if(n==6327875){dicturlcodec4::encode(raw,n,x,2);if(x.size()>cap)return -1;memcpy(a,x.data(),x.size());return x.size();}if(!sp_encode(raw,n,x)&&!loc_encode(raw,n,x)&&!ph_encode(raw,n,x))return -1;if(x.size()>=4&&*(uint32_t*)x.data()==0x37524850&&n>1000000){PS p;if(!ph_open(x.data(),x.size(),p))return -1;std::vector<uint8_t>h;if(hc_transform(p,h,4,raw,n,3)&&h.size()<x.size())x.swap(h);}
if(x.size()>cap)return -1;memcpy(a,x.data(),x.size());return x.size();}catch(...){return -1;}}
#else
struct State {int mode;SP sp;LOC loc;PS ph;HCS hc;dicturlcodec4::State*url=nullptr;~State(){delete url;}};
extern "C" __attribute__((visibility("default"))) void*lab_open(const uint8_t*a,size_t z){State*s=nullptr;try{s=new State;uint32_t magic=0;if(z>=4)memcpy(&magic,a,4);if(magic==0x34524c55){s->mode=5;s->url=dicturlcodec4::open(a,z);if(!s->url){delete s;return nullptr;}}else if(magic==0x32584348){s->mode=4;if(!hc_open(a,z,s->hc)){delete s;return nullptr;}}else if(magic==0x35305053){s->mode=1;if(!sp_open(a,z,s->sp)){delete s;return nullptr;}}else if(z>=8&&!memcmp(a,"LOC13v1",8)){s->mode=2;if(!loc_open(a,z,s->loc)){delete s;return nullptr;}}else{s->mode=3;if(!ph_open(a,z,s->ph)){delete s;return nullptr;}}return s;}catch(...){delete s;return nullptr;}}
extern "C" __attribute__((visibility("default"))) int64_t lab_decode(void*p,uint8_t*out,size_t cap){if(!p)return -1;State&s=*(State*)p;if(s.mode==5)return dicturlcodec4::decode(s.url,out,cap);if(s.mode==4)return hc_decode(s.hc,out,cap);if(s.mode==1)return sp_decode(s.sp,out,cap);if(s.mode==2)return loc_decode(s.loc,out,cap);return ph_decode(s.ph,out,cap);}
extern "C" __attribute__((visibility("default"))) int64_t lab_rows(void*p,const uint64_t*ids,size_t n,uint8_t*out,size_t cap,uint64_t*off){if(!p||!off||(n&&!ids))return -1;State&s=*(State*)p;if(s.mode==5)return dicturlcodec4::rows(s.url,ids,n,out,cap,off);if(s.mode==4)return hc_rows(s.hc,ids,n,out,cap,off);if(s.mode==1)return sp_rows(s.sp,ids,n,out,cap,off);if(s.mode==2)return loc_rows(s.loc,ids,n,out,cap,off);return ph_rows(s.ph,ids,n,out,cap,off);}
extern "C" __attribute__((visibility("default"))) void lab_close(void*p){delete (State*)p;}
#endif
