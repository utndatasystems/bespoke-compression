#include "entropy.h"
#include <stdint.h>
#include <string.h>
#include <vector>
#include <fstream>
#include <iostream>
#include <chrono>
static void write32(uint8_t*p,uint32_t v){memcpy(p,&v,4);}
int main(int argc,char**argv){
 const char*input=argc>1?argv[1]:"/work/final1024.seq";const char*output=argc>2?argv[2]:"/work/fitted.bin";
 auto start=std::chrono::steady_clock::now();std::ifstream in(input,std::ios::binary|std::ios::ate);if(!in){std::cerr<<"Cannot open input\n";return 1;}auto length=in.tellg();if(length<=0||uint64_t(length)%12||uint64_t(length)/12>UINT32_MAX){std::cerr<<"Invalid sequence file length\n";return 2;}uint32_t ns=uint64_t(length)/12;in.seekg(0);std::vector<uint8_t>raw(size_t(length),uint8_t(0));if(!in.read((char*)raw.data(),raw.size()))return 3;
 std::vector<uint8_t>result(60),plane(ns),decoded(ns),reconstructed(raw.size());memcpy(result.data(),"FITLZ001",8);write32(result.data()+8,ns);
 for(unsigned j=0;j<12;j++){
  for(size_t i=0;i<ns;i++)memcpy(plane.data()+i,raw.data()+12*i+j,1);
  auto coded=ent::encode(plane);if(coded.size()>UINT32_MAX||!ent::decode(coded.data(),coded.size(),decoded.data(),ns)||decoded!=plane){std::cerr<<"Plane round trip failed\n";return 4;}
  for(size_t i=0;i<ns;i++)memcpy(reconstructed.data()+12*i+j,decoded.data()+i,1);
  write32(result.data()+12+4*j,coded.size());result.insert(result.end(),coded.begin(),coded.end());std::cerr<<"plane="<<j<<" bytes="<<coded.size()<<"\n";
 }
 if(reconstructed!=raw){std::cerr<<"Sequence bytes changed\n";return 5;}
 std::ofstream out(output,std::ios::binary);if(!out||!out.write((const char*)result.data(),result.size()))return 6;out.close();
 double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();std::cerr<<"sequences="<<ns<<" raw="<<raw.size()<<" fitted="<<result.size()<<" generation_and_verification_seconds="<<elapsed<<"\n";return 0;
}
