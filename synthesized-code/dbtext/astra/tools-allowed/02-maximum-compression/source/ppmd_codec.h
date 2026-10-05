#pragma once
#include <vector>
#include <cstdlib>
#include <cstdint>
#include "ppmd/Ppmd8.h"
namespace PP {
using B=uint8_t;using V=std::vector<B>;
static void* alloc(ISzAllocPtr,size_t n){return malloc(n);}
static void release(ISzAllocPtr,void*p){free(p);}
static const ISzAlloc allocator={alloc,release};
struct Input{IByteIn iface;const B*at,*end;bool failed;};
static Byte read(IByteInPtr i){auto*p=(Input*)i;if(p->at>=p->end){p->failed=true;return 0;}return *p->at++;}
static V decode(const B*data,size_t len,size_t outn){
 if(len<7||data[0]<2||data[0]>16||data[1]<20||data[1]>26||data[2]>1||outn>(64u<<20))return {};
 V out(outn);CPpmd8 p;Ppmd8_Construct(&p);if(!Ppmd8_Alloc(&p,UInt32(1)<<data[1],&allocator))return {};
 Input in{{read},data+3,data+len,false};p.Stream.In=&in.iface;
 bool ok=Ppmd8_Init_RangeDec(&p)!=0;Ppmd8_Init(&p,data[0],data[2]);
 for(size_t i=0;ok&&i<outn;i++){int c=Ppmd8_DecodeSymbol(&p);if(c<0||in.failed)ok=false;else out[i]=c;}
 if(ok)ok=Ppmd8_DecodeSymbol(&p)==PPMD8_SYM_END&&!in.failed&&Ppmd8_RangeDec_IsFinishedOK(&p);
 Ppmd8_Free(&p,&allocator);if(!ok)return {};return out;
}
#ifdef ENCODER
static bool use=true;
static unsigned wt_order=3;
struct Output{IByteOut iface;B*at,*end;bool failed;};
static void write(IByteOutPtr i,Byte c){auto*p=(Output*)i;if(p->at==p->end)p->failed=true;else *p->at++=c;}
static V encode(const B*data,size_t len,unsigned order=6,unsigned memexp=26,unsigned restore=0){
 if(!use||order<2||order>16||memexp<20||memexp>26||restore>1)return {};
 V out(len*2+1024);out[0]=order;out[1]=memexp;out[2]=restore;CPpmd8 p;Ppmd8_Construct(&p);if(!Ppmd8_Alloc(&p,UInt32(1)<<memexp,&allocator))return {};
 Output stream{{write},out.data()+3,out.data()+out.size(),false};p.Stream.Out=&stream.iface;Ppmd8_Init_RangeEnc(&p);Ppmd8_Init(&p,order,restore);
 for(size_t i=0;i<len;i++)Ppmd8_EncodeSymbol(&p,data[i]);Ppmd8_EncodeSymbol(&p,PPMD8_SYM_END);Ppmd8_Flush_RangeEnc(&p);Ppmd8_Free(&p,&allocator);
 if(stream.failed)return {};out.resize(stream.at-out.data());return out;
}
#endif
}
