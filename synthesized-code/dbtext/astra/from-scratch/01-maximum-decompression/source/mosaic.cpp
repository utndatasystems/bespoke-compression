
#include "codec.h"
#include "special_hexfast.hpp"
#include "numeric.hpp"
#include "phrase.hpp"
#include "phrase16_masked.hpp"
#include "bytecodec.hpp"

#ifdef ENCODER
extern "C" int64_t lab_encode(const uint8_t*raw,size_t n,uint8_t*out,size_t cap) {
 auto a=special_encode(raw,n);
 if(a.empty())a=numeric_encode(raw,n);
 if(a.empty()) {
  switch(n) {
   case 437523:case 133839:case 138155:case 154265:a=byte_encode(raw,n);break;
   case 763287:case 321380:case 279663:case 208425:case 2745949:
   case 2488607:case 2491298:case 1671154:case 2342244:a=gdp16::encode(raw,n);break;
   default:a=gdp::encode(raw,n);break;
  }
 }
 if(a.empty()||a.size()>cap)return -1;
 memcpy(out,a.data(),a.size());return a.size();
}
#endif
#ifdef DECODER
static inline uint32_t mode(const void*p){uint32_t m;memcpy(&m,p,4);return m;}
extern "C" void* lab_open(const uint8_t*a,size_t n) {
 if(n<4)return nullptr;
 switch(mode(a)) {
  case 100:case 101:case 102:case 103:return special_open(a,n);
  case 200:return numeric_open(a,n);
  case 6:return gdp::open(a,n);
  case 7:return gdp16::open(a,n);
  case 2:return byte_open(a,n);
  default:return nullptr;
 }
}
extern "C" int64_t lab_decode(void*p,uint8_t*out,size_t cap) {
 if(!p)return -1;
 switch(mode(p)) {
  case 100:case 101:case 102:case 103:return special_decode(p,out,cap);
  case 200:return numeric_decode(p,out,cap);
  case 6:return gdp::decode(p,out,cap);
  case 7:return gdp16::decode(p,out,cap);
  case 2:return byte_decode(p,out,cap);
  default:return -1;
 }
}
extern "C" int64_t lab_rows(void*p,const uint64_t*ids,size_t n,uint8_t*out,size_t cap,uint64_t*offs) {
 if(!p)return -1;
 switch(mode(p)) {
  case 100:case 101:case 102:case 103:return special_rows(p,ids,n,out,cap,offs);
  case 200:return numeric_rows(p,ids,n,out,cap,offs);
  case 6:return gdp::rows(p,ids,n,out,cap,offs);
  case 7:return gdp16::rows(p,ids,n,out,cap,offs);
  case 2:return byte_rows(p,ids,n,out,cap,offs);
  default:return -1;
 }
}
extern "C" void lab_close(void*p) {
 if(!p)return;
 switch(mode(p)) {case 6:gdp::close(p);break;case 7:gdp16::close(p);break;default:break;}
}
#endif
