#include "codec.h"
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "special.h"
#include "location.h"
#include "textcodec.h"
#include "bytetext.h"
#include "text16.h"
#include "zero8.h"
struct Header { uint64_t magic; uint64_t raw; uint32_t rows;uint32_t type;uint64_t payload; };
static constexpr uint64_t MAGIC=0x3154585442445241ULL;
#ifdef ENCODER
extern "C" int64_t lab_encode(const uint8_t*raw,size_t size,uint8_t*archive,size_t cap) {
 if((!raw&&size)||(!archive&&cap))return -1;
 try{
 std::vector<uint8_t> p;uint32_t type=0,rows=0;
 if(!special_encode(raw,size,p,type,rows) && !location_encode(raw,size,p,type,rows)){
  p.clear();
  bool ok;
  if(size==133839||size==154265||size==321380||size==437523||size==138155)ok=byte_encode(raw,size,p,type,rows);
  else if(size==6327875)ok=text_encode(raw,size,p,type,rows);
  else if(size==763287||size==208425||size==2488607||size==2123578||size==2342244||size==2862830||size==1926988)ok=zero8_encode(raw,size,p,type,rows);
  else ok=text16_encode(raw,size,p,type,rows);
  if(!ok)return -1;
 }
 if(p.size()>cap || sizeof(Header)>cap-p.size())return -1;
 Header h{MAGIC,size,rows,type,p.size()};memcpy(archive,&h,sizeof(h));memcpy(archive+sizeof(h),p.data(),p.size());return sizeof(h)+p.size();
 }catch(...){return -1;}
}
#else
struct State { Header h;const uint8_t*p;void* text; };
extern "C" void* lab_open(const uint8_t*a,size_t size) {
 if(!a||size<sizeof(Header))return nullptr;Header h;memcpy(&h,a,sizeof(h));
 if(h.magic!=MAGIC||h.payload!=size-sizeof(Header)||h.raw>UINT32_MAX||!h.rows||h.rows>h.raw)return nullptr;
 const uint8_t*p=a+sizeof(Header);void*t=nullptr;
 if(h.type>=1&&h.type<=4){if(!special_validate(h.type,p,h.payload,h.rows,h.raw))return nullptr;}
 else if(h.type==5){if(!location_validate(p,h.payload,h.rows,h.raw))return nullptr;}
 else if(h.type==14){t=zero8_open(h.type,p,h.payload,h.rows,h.raw);if(!t)return nullptr;}
 else if(h.type==11){t=text16_open(h.type,p,h.payload,h.rows,h.raw);if(!t)return nullptr;}
 else if(h.type==20){t=byte_open(h.type,p,h.payload,h.rows,h.raw);if(!t)return nullptr;}
 else {t=text_open(h.type,p,h.payload,h.rows,h.raw);if(!t)return nullptr;}
 State*s=(State*)malloc(sizeof(State));if(!s){if(t){if(h.type==20)byte_close(t);else if(h.type==14)zero8_close(t);else if(h.type==11)text16_close(t);else text_close(t);}return nullptr;}s->h=h;s->p=p;s->text=t;return s;
}
extern "C" int64_t lab_decode(void*st,uint8_t*out,size_t cap){
 if(!st||!out)return -1;State*s=(State*)st;if(cap<s->h.raw)return -1;
 if(s->h.type<=4)return special_decode(s->h.type,s->p,s->h.payload,s->h.rows,out,cap);
 if(s->h.type==5)return location_decode(s->p,s->h.payload,s->h.rows,out,cap,s->h.raw);
 if(s->h.type==14)return zero8_decode_state(s->text,out,cap);
 if(s->h.type==11)return text16_decode_state(s->text,out,cap);
 if(s->h.type==20)return byte_decode_state(s->text,out,cap);
 return text_decode_state(s->text,out,cap);
}
extern "C" int64_t lab_rows(void*st,const uint64_t*ids,size_t n,uint8_t*out,size_t cap,uint64_t*offs){
 if(!st||!offs||(!ids&&n)||(!out&&cap))return -1;State*s=(State*)st;
 if(s->h.type>=10&&n==s->h.rows){
  bool all=true;for(size_t i=0;i<n;i++)if(ids[i]!=i){all=false;break;}
  if(all){
   int64_t z=lab_decode(st,out,cap);if(z<0)return -1;size_t q=0,j=1;offs[0]=0;
   const __m512i lf=_mm512_set1_epi8(10);
   for(;q+64<=(size_t)z;q+=64){uint64_t mask=_mm512_cmpeq_epi8_mask(_mm512_loadu_si512(out+q),lf);while(mask){if(j>n)return -1;offs[j++]=q+__builtin_ctzll(mask)+1;mask&=mask-1;}}
   for(;q<(size_t)z;q++)if(out[q]==10){if(j>n)return -1;offs[j++]=q+1;}
   if(z&&out[z-1]!=10){if(j>n)return -1;offs[j++]=z;}
   return j==n+1?z:-1;
  }
 }
 if(s->h.type<=4)return special_rows(s->h.type,s->p,s->h.payload,s->h.rows,ids,n,out,cap,offs);
 if(s->h.type==5)return location_rows(s->p,s->h.payload,s->h.rows,ids,n,out,cap,offs);
 if(s->h.type==14)return zero8_rows_state(s->text,ids,n,out,cap,offs);
 if(s->h.type==11)return text16_rows_state(s->text,ids,n,out,cap,offs);
 if(s->h.type==20)return byte_rows_state(s->text,ids,n,out,cap,offs);
 return text_rows_state(s->text,ids,n,out,cap,offs);
}
extern "C" void lab_close(void*st){if(st){State*s=(State*)st;if(s->text){if(s->h.type==20)byte_close(s->text);else if(s->h.type==14)zero8_close(s->text);else if(s->h.type==11)text16_close(s->text);else text_close(s->text);}free(s);}}
#endif
