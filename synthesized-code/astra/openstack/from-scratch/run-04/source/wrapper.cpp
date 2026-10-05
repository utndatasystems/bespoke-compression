#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#define lab_encode inner_encode
#define lab_open inner_open
#define lab_decode inner_decode
#define lab_close inner_close
#include "codec.cpp"
#undef lab_encode
#undef lab_open
#undef lab_decode
#undef lab_close
#ifndef ENCODER
#define ENTROPY_DECODER_ONLY
#define SMALLLZ_DECODER_ONLY
#endif
#include "entropy.h"
#include "smalllz.h"
#include "uuidpack.h"
static uint32_t read32(const uint8_t*p){uint32_t v;memcpy(&v,p,4);return v;}
static uint64_t read64(const uint8_t*p){uint64_t v;memcpy(&v,p,8);return v;}
static constexpr uint64_t MAGIC=0x315a4c34544f5355ull;
#ifdef ENCODER
extern "C" int64_t lab_encode(const uint8_t*r,size_t n,uint8_t*a,size_t cap){try{
 std::vector<uint8_t> tmp(n*2+1000000);int64_t k=inner_encode(r,n,tmp.data(),tmp.size());if(k<32)return -1;tmp.resize(k);
 uint32_t nu=read32(tmp.data()+24),nt=read32(tmp.data()+20);size_t p=32;
 std::vector<uint8_t> dict,ex;
 for(uint32_t i=0;i<nu;++i){unsigned len=tmp[p++];if(len!=36){put(ex,i,4);put(ex,len,1);}unsigned z=len==36?16:len/2;dict.insert(dict.end(),tmp.begin()+p,tmp.begin()+p+z);p+=z;}
 size_t mb=p;for(uint32_t i=0;i<nt;++i){uint32_t len=read32(tmp.data()+p);uint16_t nf;memcpy(&nf,tmp.data()+p+4,2);p+=6+len+nf*13;}
 auto meta=smalllz::encode(tmp.data()+mb,p-mb);
 #ifdef NO_ENTROPY
 std::vector<uint8_t> payload(tmp.size()-p+5);payload[0]=0;uint32_t plen=tmp.size()-p;memcpy(payload.data()+1,&plen,4);memcpy(payload.data()+5,tmp.data()+p,plen);
 #else
 auto payload=entropy::encode(tmp.data()+p,tmp.size()-p);
 #endif
 uint32_t dflag=0;if(dict.size()==nu*16ull&&uuidpack::eligible(dict.data(),nu)){std::vector<uint8_t> packed;uuidpack::pack(dict.data(),nu,packed);dict=std::move(packed);dflag=0x80000000u;}
 std::vector<uint8_t> out;put(out,MAGIC,8);put(out,tmp.size(),8);put(out,ex.size()/5,4);put(out,uint32_t(dict.size())|dflag,4);put(out,meta.size(),4);put(out,payload.size(),4);
 out.insert(out.end(),tmp.begin(),tmp.begin()+32);out.insert(out.end(),ex.begin(),ex.end());out.insert(out.end(),dict.begin(),dict.end());out.insert(out.end(),meta.begin(),meta.end());out.insert(out.end(),payload.begin(),payload.end());
 fprintf(stderr,"wrapper exceptions=%zu uuid=%zu metadata=%zu->%zu payload=%zu->%zu total=%zu\n",ex.size(),dict.size(),p-mb,meta.size(),tmp.size()-p,payload.size(),out.size());
 if(out.size()>cap)return -1;memcpy(a,out.data(),out.size());return out.size();
}catch(...){return -1;}}
#else
struct Outer{void*inner;uint8_t*raw;};
extern "C" void*lab_open(const uint8_t*a,size_t n){
 if(!a||n<64||read64(a)!=MAGIC)return nullptr;
 uint64_t rawlen=read64(a+8);uint32_t ne=read32(a+16),ds=read32(a+20),ms=read32(a+24),ps=read32(a+28),nu=read32(a+56);
 bool packed=ds>>31;ds&=0x7fffffffu;
 if(packed&&ds!=uuidpack::packed_size(nu))return nullptr;
 if(rawlen<32||rawlen>128*1024*1024||uint64_t(ne)*5+ds+ms+ps+64!=n||ne>nu||nu>4000000||ms<5||ps<5)return nullptr;
 const uint8_t*ex=a+64,*dict=ex+ne*5,*meta=dict+ds,*payload=meta+ms;
 uint64_t mraw=smalllz::decoded_size(meta,ms),praw=entropy::decoded_size(payload,ps);
 if(32+uint64_t(nu)+(packed?uint64_t(nu)*16:ds)+mraw+praw!=rawlen)return nullptr;
 uint8_t*raw=(uint8_t*)malloc(rawlen+16);if(!raw)return nullptr;memcpy(raw,a+32,32);memset(raw+rawlen,0,16);
 uint8_t*q=raw+32;const uint8_t*d=dict;uint32_t ei=0,last=0;
 for(uint32_t i=0;i<nu;++i){unsigned len=36;if(ei<ne){uint32_t ix=read32(ex+ei*5);if(ix<i||ix>=nu||(ei&&ix<=last)){free(raw);return nullptr;}if(ix==i){len=ex[ei*5+4];if(len!=32&&len!=40){free(raw);return nullptr;}last=ix;++ei;}}unsigned z=len==36?16:len/2;if(packed){if(z!=16){free(raw);return nullptr;}*q++=len;uuidpack::unpack_one(dict,dict+nu*15ull,i,q);q+=16;}else{if(size_t(meta-d)<z){free(raw);return nullptr;}*q++=len;memcpy(q,d,z);q+=z;d+=z;}}
 if(ei!=ne||(!packed&&d!=meta)||!smalllz::decode(meta,ms,q,mraw)){free(raw);return nullptr;}q+=mraw;
 if(!entropy::decode(payload,ps,q,praw)){free(raw);return nullptr;}
 void*in=inner_open(raw,rawlen);if(!in){free(raw);return nullptr;}
 Outer*s=(Outer*)malloc(sizeof(Outer));if(!s){inner_close(in);free(raw);return nullptr;}s->inner=in;s->raw=raw;return s;
}
extern "C" int64_t lab_decode(void*v,uint8_t*out,size_t cap){if(!v)return -1;return inner_decode(((Outer*)v)->inner,out,cap);}
extern "C" void lab_close(void*v){if(v){Outer*s=(Outer*)v;inner_close(s->inner);free(s->raw);free(s);}}
#endif
