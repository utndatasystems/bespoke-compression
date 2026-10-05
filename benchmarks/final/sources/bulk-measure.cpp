// Original binary inputs. One discarded warmup and seven single-call trials.
// Codec opening and decoding are recorded separately; harness I/O and checking are untimed.
// Correctness and archive retention are outside all timed regions.
#define main upstream_filtertest_main
#include "filtertest-observed.cpp"
#undef main
#include "compressor/onpair_advanced/OnPairAdvancedCompressor.hpp"
#include <dlfcn.h>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <numeric>
#include <span>
#include <bzlib.h>
#include <brotli/encode.h>
#include <brotli/decode.h>
#include <lz4hc.h>
#include <zstd.h>
#include <zlib.h>
#include <lzma.h>
using Bytes=std::vector<uint8_t>;
using Clock=std::chrono::steady_clock;
static void require(bool ok,const char*message){if(!ok)throw std::runtime_error(message);}
static void store(const std::string&name,const void*data,size_t n){std::ofstream f(name,std::ios::binary);f.write((const char*)data,n);require(bool(f),"write failed");}
template<class T>T sym(void*h,const char*n){auto p=dlsym(h,n);require(p,"missing ABI symbol");return reinterpret_cast<T>(p);}
static double seconds(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double>(b-a).count();}

int main(int argc,char**argv)try{
 require(argc==8,"method input output-directory corpus-mode encoder decoder decode-operation");
 std::string method=argv[1],mode=argv[4];std::filesystem::create_directories(argv[3]);
 const std::string outdir=argv[3];
 require(method=="FSST-column"||method=="OnPair+"||method=="LZ4-HC12"||method=="Zstd-1","unsupported bulk method");
 std::ifstream input(argv[2],std::ios::binary|std::ios::ate);require(bool(input),"missing input");
 auto length=input.tellg();require(length>0,"empty input");size_t rawSize=static_cast<size_t>(length);
 Bytes raw(rawSize+64,0);input.seekg(0);input.read(reinterpret_cast<char*>(raw.data()),rawSize);
 require(bool(input)&&size_t(input.gcount())==rawSize,"incomplete binary input");
 require(mode=="original","only original, unrepeated inputs are accepted");
 // No line parsing, trailing-byte repair, concatenation or duplicated input.
 // Unused row-interface state is retained only to minimize the harness diff.
 std::vector<std::string> corpus;
 std::vector<unsigned> hits(corpus.size());std::iota(hits.begin(),hits.end(),0);
 std::vector<char> output(rawSize+4096,0);std::function<size_t()> decode;
 std::function<void()> initialize=[]{};
 std::function<void()> release=[]{};
 Bytes archive;uint64_t bare=0,total=0,payload=0,index=0,dictionary=0,framing=0,padding=0;
 double encodeSeconds=0,bulkEncodeSeconds=0;
 std::unique_ptr<FSSTCompressionRunner> fsst;std::unique_ptr<LZ4CompressionRunner> lz4;
 fsst_decoder_t columnDecoder{};
 Bytes columnTable;
 using OnPair=sgtt::compressor::OnPairAdvancedCompressor<sgtt::compressor::onpair::MaxSymbolLength::SIXTEEN>;
 std::unique_ptr<OnPair> onpair;std::vector<std::byte> onArchive,onOutput;std::vector<std::vector<std::byte>> temporaries;
 void *eh=nullptr,*dh=nullptr,*state=nullptr;void(*closeState)(void*)=nullptr;
 std::vector<uint64_t> ids(hits.begin(),hits.end()),offsets(ids.size()+1);
 if(method=="FSST"){
  fsst=std::make_unique<FSSTCompressionRunner>();
  total=fsst->compressCorpus(corpus,bare,bulkEncodeSeconds,encodeSeconds,false);
  payload=bare-8192;padding=8192;index=4*corpus.size();dictionary=total-bare-index;
  store(outdir+"/archive.bin",fsst->compressedData.data(),fsst->compressedData.size());
  store(outdir+"/dictionary.bin",fsst->auditTable.data(),fsst->auditTable.size());
  store(outdir+"/offsets.bin",fsst->offsets.data(),fsst->offsets.size()*4);
  initialize=[&]{require(fsst_import(&fsst->decoder,fsst->auditTable.data())==fsst->auditTable.size(),"FSST table import");};
  decode=[&]{return fsst->decompressRows(output,hits);};
 }else if(method=="FSST-column"){
  // Original FSST library; one input string containing every original byte,
  // including newlines. This is a separate configuration from the paper rows.
  unsigned long inputLength=rawSize,compressedLength=0;
  unsigned char* inputPointer=raw.data();unsigned char* compressedPointer=nullptr;
  Bytes compressed(rawSize*2+16);columnTable.resize(FSST_MAXHEADER);
  auto start=Clock::now();
  auto encoder=fsst_create(1,&inputLength,&inputPointer,false);
  require(encoder,"FSST encoder creation");
  auto count=fsst_compress(encoder,1,&inputLength,&inputPointer,compressed.size(),compressed.data(),&compressedLength,&compressedPointer);
  require(count==1,"FSST column compression");
  auto tableLength=fsst_export(encoder,columnTable.data());
  require(tableLength>0&&tableLength<=columnTable.size(),"FSST table export");
  columnTable.resize(tableLength);
  // An explicit, charged frame contains original length and table length.
  framing=12;dictionary=tableLength;payload=bare=compressedLength;
  total=framing+dictionary+payload;archive.resize(total);
  uint64_t original=rawSize;uint32_t tableBytes=tableLength;
  memcpy(archive.data(),&original,8);memcpy(archive.data()+8,&tableBytes,4);
  memcpy(archive.data()+12,columnTable.data(),tableLength);
  memcpy(archive.data()+12+tableLength,compressedPointer,compressedLength);
  encodeSeconds=bulkEncodeSeconds=seconds(start,Clock::now());
  fsst_destroy(encoder);
  initialize=[&]{
   uint64_t original;uint32_t tableBytes;memcpy(&original,archive.data(),8);memcpy(&tableBytes,archive.data()+8,4);
   require(original==rawSize&&12+size_t(tableBytes)<=archive.size(),"FSST column header");
   require(fsst_import(&columnDecoder,archive.data()+12)==tableBytes,"FSST column import");
  };
  decode=[&]{return fsst_decompress(&columnDecoder,payload,archive.data()+12+dictionary,output.size(),reinterpret_cast<unsigned char*>(output.data()));};
  store(outdir+"/archive.bin",archive.data(),archive.size());
 }else if(method=="LZ4-paper-1000"){
  lz4=std::make_unique<LZ4CompressionRunner>(1000);
  total=lz4->compressCorpus(corpus,bare,bulkEncodeSeconds,encodeSeconds,false);payload=bare;
  // The paper's bare value is a separate compression without row-directory bytes.
  // Retain both that verified stream and the indexed archive actually decoded.
  std::ofstream f(outdir+"/archive.bin",std::ios::binary);
  for(auto b:lz4->blocks)f.write((char*)b,sizeof(*b)+b->compressedSize);
  require(bool(f),"archive write");
  store(outdir+"/bare-streams.bin",lz4->auditBare.data(),lz4->auditBare.size());
  store(outdir+"/bare-lengths.bin",lz4->auditBareLengths.data(),lz4->auditBareLengths.size()*4);
  decode=[&]{return lz4->decompressRows(output,hits);};
 }else if(method=="OnPair+"){
  onpair=std::make_unique<OnPair>(sgtt::compressor::OnPairConfig{true,true});
  auto start=Clock::now();onpair->prepare(rawSize);
  onpair->compress(std::span(reinterpret_cast<std::byte*>(raw.data()),rawSize),onArchive);
  encodeSeconds=bulkEncodeSeconds=seconds(start,Clock::now());total=onArchive.size();
  uint64_t dataOffset;memcpy(&dataOffset,onArchive.data(),8);require(dataOffset+8<=total,"OnPair offsets");
  payload=bare=total-dataOffset-8;dictionary=dataOffset-16;framing=24;
  onOutput.resize(rawSize+64);
  store(outdir+"/archive.bin",onArchive.data(),onArchive.size());
  onpair.reset();
  initialize=[&]{onpair=std::make_unique<OnPair>(sgtt::compressor::OnPairConfig{true,true});};
  release=[&]{onpair.reset();};
  decode=[&]{onpair->decompress(onArchive,onOutput,temporaries);return onpair->getUncompressedSize(onArchive);};
 }else if(method=="native"){
  eh=dlopen(argv[5],RTLD_NOW|RTLD_LOCAL);if(!eh)throw std::runtime_error(dlerror());
  dh=dlopen(argv[6],RTLD_NOW|RTLD_LOCAL);if(!dh)throw std::runtime_error(dlerror());
  auto enc=sym<int64_t(*)(const uint8_t*,size_t,uint8_t*,size_t)>(eh,"lab_encode");
  auto open=sym<void*(*)(const uint8_t*,size_t)>(dh,"lab_open");
  closeState=sym<void(*)(void*)>(dh,"lab_close");
  archive.resize(rawSize*4+16*1024*1024);
  auto a=Clock::now();auto written=enc(raw.data(),rawSize,archive.data(),archive.size());auto b=Clock::now();
  require(written>=0&&size_t(written)<=archive.size(),"encoding failed");archive.resize(written);total=bare=archive.size();
  encodeSeconds=bulkEncodeSeconds=seconds(a,b);
  initialize=[&,open]{state=open(archive.data(),archive.size());require(state,"archive open failed");};
  release=[&]{closeState(state);state=nullptr;};
  if(std::string(argv[7])=="rows"){
   auto rows=sym<int64_t(*)(void*,const uint64_t*,size_t,uint8_t*,size_t,uint64_t*)>(dh,"lab_rows");
   decode=[&,rows]{auto n=rows(state,ids.data(),ids.size(),(uint8_t*)output.data(),output.size(),offsets.data());require(n>=0,"rows failed");return size_t(n);};
  }else{
   auto full=sym<int64_t(*)(void*,uint8_t*,size_t)>(dh,"lab_decode");
   decode=[&,full]{auto n=full(state,(uint8_t*)output.data(),output.size());require(n>=0,"decode failed");return size_t(n);};
  }
  store(outdir+"/archive.bin",archive.data(),archive.size());
 }else{
  archive.resize(rawSize*2+1024*1024);auto a=Clock::now();size_t n=0;
  if(method=="Uncompressed"){memcpy(archive.data(),raw.data(),rawSize);n=rawSize;}
  else if(method.starts_with("LZ4-")){
   int level=method=="LZ4-HC9"?9:12;
   int z=method=="LZ4-default"?LZ4_compress_default((char*)raw.data(),(char*)archive.data(),rawSize,archive.size()):LZ4_compress_HC((char*)raw.data(),(char*)archive.data(),rawSize,archive.size(),level);require(z>0,"lz4 encode");n=z;
  }else if(method.starts_with("Zstd-")){n=ZSTD_compress(archive.data(),archive.size(),raw.data(),rawSize,std::stoi(method.substr(5)));require(!ZSTD_isError(n),"zstd encode");}
  else if(method.starts_with("Brotli-")){n=archive.size();require(BrotliEncoderCompress(std::stoi(method.substr(7)),22,BROTLI_MODE_GENERIC,rawSize,raw.data(),&n,archive.data()),"brotli encode");}
  else if(method.starts_with("bzip2-")){unsigned z=archive.size();require(BZ2_bzBuffToBuffCompress((char*)archive.data(),&z,(char*)raw.data(),rawSize,std::stoi(method.substr(6)),0,30)==BZ_OK,"bzip encode");n=z;}
  else if(method.starts_with("zlib-")){uLongf z=archive.size();require(compress2(archive.data(),&z,raw.data(),rawSize,std::stoi(method.substr(5)))==Z_OK,"zlib encode");n=z;}
  else if(method.starts_with("XZ-")){size_t z=0;require(lzma_easy_buffer_encode(std::stoi(method.substr(3)),LZMA_CHECK_CRC64,nullptr,raw.data(),rawSize,archive.data(),&z,archive.size())==LZMA_OK,"xz encode");n=z;}
  else throw std::runtime_error("unknown method");
  archive.resize(n);payload=bare=n;
  if(method!="Uncompressed"){
   // Same eight-byte length framing as the original baseline archives.
   archive.insert(archive.begin(),8,0);uint64_t length=rawSize;memcpy(archive.data(),&length,8);framing=8;
   initialize=[&]{uint64_t length;memcpy(&length,archive.data(),8);require(length==rawSize,"baseline original length");};
  }
  total=archive.size();encodeSeconds=bulkEncodeSeconds=seconds(a,Clock::now());
  store(outdir+"/archive.bin",archive.data(),archive.size());
  decode=[&]()->size_t{
   if(method=="Uncompressed"){memcpy(output.data(),archive.data(),archive.size());return archive.size();}
   auto data=archive.data()+8;size_t size=archive.size()-8;
   if(method.starts_with("LZ4-")){int z=LZ4_decompress_safe((char*)data,output.data(),size,output.size());require(z>=0,"lz4 decode");return z;}
   if(method.starts_with("Zstd-")){auto z=ZSTD_decompress(output.data(),output.size(),data,size);require(!ZSTD_isError(z),"zstd decode");return z;}
   if(method.starts_with("Brotli-")){size_t z=output.size();require(BrotliDecoderDecompress(size,data,&z,(uint8_t*)output.data())==BROTLI_DECODER_RESULT_SUCCESS,"brotli decode");return z;}
   if(method.starts_with("bzip2-")){unsigned z=output.size();require(BZ2_bzBuffToBuffDecompress(output.data(),&z,(char*)data,size,0,0)==BZ_OK,"bzip decode");return z;}
   if(method.starts_with("zlib-")){uLongf z=output.size();require(uncompress((uint8_t*)output.data(),&z,data,size)==Z_OK,"zlib decode");return z;}
   uint64_t limit=UINT64_MAX;size_t in=0,out=0;
   require(lzma_stream_buffer_decode(&limit,0,nullptr,data,&in,size,(uint8_t*)output.data(),&out,output.size())==LZMA_OK&&in==size,"xz decode");return out;
  };
 }
 auto verify=[&](size_t n){require(n==rawSize,"length mismatch");const void*p=method=="OnPair+"?(void*)onOutput.data():(void*)output.data();require(!memcmp(raw.data(),p,rawSize),"byte mismatch");};
 // Prefault/poison the output outside the timer and verify every reconstruction.
 // Each recorded trial invokes the decoder exactly once, never a batch of 100.
 std::vector<double> opening,decoding;
 for(int trial=-1;trial<7;++trial){
  if(method=="OnPair+")std::fill(onOutput.begin(),onOutput.end(),std::byte{0xa5});
  else std::fill(output.begin(),output.end(),char(0xa5));
  auto start=Clock::now();initialize();auto opened=Clock::now();
  auto n=decode();auto done=Clock::now();
  verify(n);release();
  if(trial>=0){opening.push_back(seconds(start,opened));decoding.push_back(seconds(opened,done));}
 }
 std::cout<<std::setprecision(17)<<"{\"raw_bytes\":"<<rawSize<<",\"input_files\":"<<1<<",\"archive_bytes\":"<<total<<",\"bare_bytes\":"<<bare<<",\"payload_bytes\":"<<payload<<",\"dictionary_bytes\":"<<dictionary<<",\"index_bytes\":"<<index<<",\"framing_bytes\":"<<framing<<",\"padding_bytes\":"<<padding<<",\"encode_seconds\":"<<encodeSeconds<<",\"paper_bulk_encode_seconds\":"<<bulkEncodeSeconds<<",\"warmups\":1,\"trials\":7,\"calls_per_trial\":1,\"duplicated_rows\":0,\"open_seconds\":[";
 for(size_t i=0;i<opening.size();++i){if(i)std::cout<<',';std::cout<<opening[i];}
 std::cout<<"],\"decode_seconds\":[";
 for(size_t i=0;i<decoding.size();++i){if(i)std::cout<<',';std::cout<<decoding[i];}
 std::cout<<"],\"byte_exact\":true,\"verified_decodes\":8}"<<std::endl;
 if(dh)dlclose(dh);if(eh)dlclose(eh);
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
