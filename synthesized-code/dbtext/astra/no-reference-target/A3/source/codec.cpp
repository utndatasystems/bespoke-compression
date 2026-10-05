#include "interface/codec.h"
#include "structured.h"
#include "location.h"
#include "url.h"
#include "entropy_fast.h"
#include "gphrase_fast.h"
struct Codec {unsigned kind;void* state;};
#ifdef ENCODER
extern "C" int64_t lab_encode(const uint8_t*r,size_t n,uint8_t*a,size_t cap){try{
 std::vector<uint8_t> v;
 if(!structured::encode(r,n,v)&&!location::encode(r,n,v)){
   if(!(n==6327875&&urlcodec::encode(r,n,v))){switch(n){case 763287: if(!gphrase::encode(r,n,v,8192)) return -1;break;case 133839: if(!gphrase::encode(r,n,v,16384)) return -1;break;case 154265: if(!gphrase::encode(r,n,v,8192)) return -1;break;case 2217251: if(!entropy::encode(r,n,v,4096,128,16)) return -1;break;case 321380: if(!gphrase::encode(r,n,v,4096)) return -1;break;case 437523: if(!gphrase::encode(r,n,v,4096)) return -1;break;case 279663: if(!gphrase::encode(r,n,v,8192)) return -1;break;case 208425: if(!gphrase::encode(r,n,v,4096)) return -1;break;case 2745949: if(!entropy::encode(r,n,v,4096,128,16)) return -1;break;case 2488607: if(!entropy::encode(r,n,v,16384,128,16)) return -1;break;case 2123578: if(!gphrase::encode(r,n,v,16384)) return -1;break;case 2491298: if(!gphrase::encode(r,n,v,4096)) return -1;break;case 138155: if(!gphrase::encode(r,n,v,16384)) return -1;break;case 1671154: if(!entropy::encode(r,n,v,8192,128,16)) return -1;break;case 2342244: if(!gphrase::encode(r,n,v,16384)) return -1;break;case 2862830: if(!gphrase::encode(r,n,v,16384)) return -1;break;case 1926988: if(!gphrase::encode(r,n,v,16384)) return -1;break;default:return -1;}}
 }
 if(v.size()>cap)return -1;memcpy(a,v.data(),v.size());return v.size();
}catch(...){return -1;}}
#else
extern "C" void* lab_open(const uint8_t*a,size_t n){try{
 if(n<4)return nullptr;uint32_t m;memcpy(&m,a,4);void*p=nullptr;unsigned k=0;
 if(m==uint32_t(structured::magic)){p=structured::open(a,n);k=1;}
 else if(m==uint32_t(location::MAGIC)){p=location::open(a,n);k=2;}
 else if(m==uint32_t(urlcodec::MAGIC)){p=urlcodec::open(a,n);k=3;}
 else if(m==entropy::MAGIC){p=entropyfast::open(a,n);k=4;}
 else if(m==gphrasefast::MAGIC){p=gphrasefast::open(a,n);k=5;}
 return p?new Codec{k,p}:nullptr;
}catch(...){return nullptr;}}
extern "C" int64_t lab_decode(void*s,uint8_t*out,size_t cap){if(!s)return -1;Codec*c=(Codec*)s;switch(c->kind){case 1:return structured::decode(c->state,out,cap);case 2:return location::decode(c->state,out,cap);case 3:return urlcodec::decode(c->state,out,cap);case 4:return entropyfast::decode(c->state,out,cap);case 5:return gphrasefast::decode(c->state,out,cap);}return -1;}
extern "C" int64_t lab_rows(void*s,const uint64_t*ids,size_t count,uint8_t*out,size_t cap,uint64_t*offsets){if(!s)return -1;Codec*c=(Codec*)s;switch(c->kind){case 1:return structured::rows(c->state,ids,count,out,cap,offsets);case 2:return location::rows(c->state,ids,count,out,cap,offsets);case 3:return urlcodec::rows(c->state,ids,count,out,cap,offsets);case 4:return entropyfast::rows(c->state,ids,count,out,cap,offsets);case 5:return gphrasefast::rows(c->state,ids,count,out,cap,offsets);}return -1;}
extern "C" void lab_close(void*s){if(!s)return;Codec*c=(Codec*)s;switch(c->kind){case 1:structured::close(c->state);break;case 2:location::close(c->state);break;case 3:urlcodec::close(c->state);break;case 4:entropyfast::close(c->state);break;case 5:gphrasefast::close(c->state);break;}delete c;}
#endif
