#ifndef OPENSTACK_UUIDPACK_H
#define OPENSTACK_UUIDPACK_H
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
namespace uuidpack {
// UUID-v4 payload packs its 122 unconstrained bits. The primary stream uses
// 15 bytes per value; the top two bits use a separate plane, four values/byte.
// Count is supplied by the containing format. Source/destination must not alias.
inline size_t packed_size(size_t count) {return count*15+(count+3)/4;}
inline bool eligible(const uint8_t*src,size_t count) {
 for(size_t i=0;i<count;++i)if((src[i*16+6]&0xf0)!=0x40||(src[i*16+8]&0xc0)!=0x80)return false;
 return true;
}
inline bool pack(const uint8_t*src,size_t count,std::vector<uint8_t>&out) {
 if(count>SIZE_MAX/16||!eligible(src,count))return false;
 size_t start=out.size(),size=packed_size(count);if(start>SIZE_MAX-size)return false;
 out.resize(start+size,0);uint8_t*dst=out.data()+start,*plane=dst+count*15;
 for(size_t i=0;i<count;++i) {
  uint64_t lo,hi;std::memcpy(&lo,src+i*16,8);std::memcpy(&hi,src+i*16+8,8);
  uint64_t a=(lo&0x000fffffffffffffull)|((lo>>56)<<52)|((hi&15)<<60);
  uint64_t b=((hi>>4)&3)|((hi>>8)<<2);
  std::memcpy(dst+i*15,&a,8);std::memcpy(dst+i*15+8,&b,7);plane[i/4]|=uint8_t(b>>56)<<((i%4)*2);
 }
 return true;
}
inline void unpack_one(const uint8_t*src,const uint8_t*plane,size_t i,uint8_t*dst) {
 uint64_t a,b;std::memcpy(&a,src+i*15,8);
 // The eighth byte read is safe because a nonempty two-bit plane follows
 // the primary stream. Mask it away and restore the explicit plane bits.
 std::memcpy(&b,src+i*15+8,8);b=(b&0x00ffffffffffffffull)|(uint64_t((plane[i/4]>>((i%4)*2))&3)<<56);
 uint64_t lo=(a&0x000fffffffffffffull)|0x0040000000000000ull|(((a>>52)&255)<<56);
 uint64_t hi=(a>>60)|((b&3)<<4)|0x80ull|((b>>2)<<8);
 std::memcpy(dst,&lo,8);std::memcpy(dst+8,&hi,8);
}
inline bool unpack(const uint8_t*src,size_t bytes,size_t count,uint8_t*dst) {
 if(count>SIZE_MAX/16||bytes!=packed_size(count))return false;
 const uint8_t*plane=src+count*15;
 for(size_t i=0;i<count;++i)unpack_one(src,plane,i,dst+i*16);
 return true;
}
}
#endif
