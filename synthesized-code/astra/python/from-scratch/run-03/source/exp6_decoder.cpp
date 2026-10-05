#include "codec.h"
#include "analyze_ransn.h"
#include "exp6_reconstruct.h"
#include <cstdlib>
struct Header {uint64_t magic,n,count,tail,nlit,nll,nbits,sz[6];};
static constexpr uint64_t MAGIC=0x365245545459505aull;
struct State {Header h;const uint8_t*streams[6];};
extern "C" void* lab_open(const uint8_t*p,size_t z){if(z<sizeof(Header))return nullptr;Header h;memcpy(&h,p,sizeof(h));if(h.magic!=MAGIC||h.n>100000000||h.count>h.n/5||h.nlit>h.n||h.tail>h.nlit||h.nll>h.count||h.nbits>h.count*30)return nullptr;p+=sizeof(h);z-=sizeof(h);State*s=(State*)malloc(sizeof(State));if(!s)return nullptr;s->h=h;for(int j=0;j<6;j++){if(h.sz[j]>z){free(s);return nullptr;}s->streams[j]=p;p+=h.sz[j];z-=h.sz[j];}if(z||h.sz[2]!=h.nlit||h.sz[0]!=(h.count*6+7)/8+32){free(s);return nullptr;}return s;}
extern "C" int64_t lab_decode(void*v,uint8_t*out,size_t cap){if(!v)return -1;State*s=(State*)v;auto&h=s->h;if(cap<h.n)return -1;uint8_t*ll=(uint8_t*)malloc(h.nll+32);if(!ll)return -1;aransn::Decoder od;bool ok=aransn::decode(s->streams[5],h.sz[5],ll,h.nll,11)&&od.init(s->streams[1],h.sz[1],9);exp6rec::State<5,1,true> r;ok=ok&&r.init(s->streams[4],h.sz[4],h.nbits,s->streams[2],h.nlit,s->streams[3],h.sz[3],ll,h.nll,out,h.n,cap);alignas(64) uint8_t ctrl[1032],logs[1024];const uint8_t*cp=s->streams[0];const __m128i shifts=_mm_set1_epi64x(0x2a241e18120c0600ULL),mask=_mm_set1_epi8(63);for(size_t i=0;ok&&i<h.count;i+=1024){size_t n=std::min<uint64_t>(1024,h.count-i);for(size_t k=0;k<n;k+=8){uint64_t w;memcpy(&w,cp,8);cp+=6;__m128i x=_mm_multishift_epi64_epi8(shifts,_mm_set1_epi64x(w));_mm_storel_epi64((__m128i*)(ctrl+k),_mm_and_si128(x,mask));}ok=od.decode(logs,n)&&r.block(ctrl,logs,n);}ok=ok&&od.finish()&&r.finish(h.tail);free(ll);return ok?int64_t(h.n):-1;}
extern "C" void lab_close(void*s){free(s);}
