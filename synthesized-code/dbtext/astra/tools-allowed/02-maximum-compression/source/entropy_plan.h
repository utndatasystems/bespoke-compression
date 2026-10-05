#pragma once
#ifdef ENCODER
#include <vector>
#include <zstd.h>
#include <brotli/encode.h>
#include <lzma.h>
#include <bzlib.h>
#include "ppmd_codec.h"
namespace EP {
struct Choice { int codec=-1,bz=9,order=4,exp=24; };
static Choice fc, map, wt;
static int direction=-1;
static std::vector<uint8_t> compress(const std::vector<uint8_t>&v,Choice c,bool no_crc=false){
 std::vector<uint8_t>b;size_t n=0;
 if(c.codec==0){b.resize(ZSTD_compressBound(v.size()));n=ZSTD_compress(b.data(),b.size(),v.data(),v.size(),19);if(ZSTD_isError(n))return{};b.resize(n);}
 else if(c.codec==1){b.resize(BrotliEncoderMaxCompressedSize(v.size()));n=b.size();if(!BrotliEncoderCompress(11,22,BROTLI_MODE_GENERIC,v.size(),v.data(),&n,b.data()))return{};b.resize(n);}
 else if(c.codec==3){b.resize(v.size()+v.size()/100+1024);unsigned k=b.size();if(BZ2_bzBuffToBuffCompress((char*)b.data(),&k,(char*)v.data(),v.size(),c.bz,0,30)!=BZ_OK)return{};b.resize(k);}
 else if(c.codec==4){b.resize(lzma_stream_buffer_bound(v.size()));if(lzma_easy_buffer_encode(9,no_crc?LZMA_CHECK_NONE:LZMA_CHECK_CRC32,nullptr,v.data(),v.size(),b.data(),&n,b.size())!=LZMA_OK)return{};b.resize(n);}
 else if(c.codec==5){bool save=PP::use;PP::use=true;b=PP::encode(v.data(),v.size(),c.order,c.exp);PP::use=save;}
 else if(c.codec==6)b=v;
 return b;
}
}
#endif
