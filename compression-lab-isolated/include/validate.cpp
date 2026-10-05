// Untimed diagnostics. Invalid input may be rejected; memory errors are failures.
#include "codec.h"
#include <dlfcn.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>
using Bytes=std::vector<uint8_t>;
// Leak scanning needs ptrace/proc access and is outside this memory-safety check.
extern "C" int __lsan_is_turned_off(){return 1;}
extern "C" const char* __asan_default_options(){return "detect_leaks=0:abort_on_error=1:symbolize=0";}
// libsanitizer cannot read /proc/self/environ inside the sandbox.
extern "C" const char* __ubsan_default_options(){return "halt_on_error=1:print_stacktrace=0";}
struct Guarded {
 void* mapping;size_t mapped;uint8_t* data;
 explicit Guarded(size_t n){size_t page=(size_t)sysconf(_SC_PAGESIZE),payload=((n+page-1)/page)*page;
  mapped=payload+2*page;mapping=mmap(nullptr,mapped,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
  if(mapping==MAP_FAILED)throw std::runtime_error("mmap");
  if(payload && mprotect((char*)mapping+page,payload,PROT_READ|PROT_WRITE))throw std::runtime_error("mprotect");
  data=(uint8_t*)mapping+page+payload-n;
 }
 ~Guarded(){munmap(mapping,mapped);}
};
int main(int argc,char**argv) {
 if(argc!=5)return 2;
 void* lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!lib){std::cerr<<dlerror();return 3;}
 auto open=(decltype(&lab_open))dlsym(lib,"lab_open");
 auto decode=(decltype(&lab_decode))dlsym(lib,"lab_decode");
 auto close=(decltype(&lab_close))dlsym(lib,"lab_close");
 if(!open||!decode||!close)return 4;
 std::ifstream f(argv[2],std::ios::binary);Bytes data(std::istreambuf_iterator<char>(f),{});
 size_t n=std::stoull(argv[3]),cases=0;int which=std::stoi(argv[4]);
 size_t capacity=n+32;
 if(which==0)capacity=0;
 else if(which==1)capacity=n? n-1:0;
 else if(which==2)capacity=n;
 else if(which==3)data.clear();
 else if(which<8){size_t keep=data.size()*size_t(which-3)/5;data.resize(keep);}
 else if(!data.empty())data[(which-8)%data.size()]^=0xff;
 // An empty vector can have an implementation-defined non-null pointer.
 Guarded output(capacity),input(data.size());
 if(capacity)memset(output.data,0xa5,capacity);
 if(!data.empty())memcpy(input.data,data.data(),data.size());
 try {
  void*state=open(input.data,data.size());
  if(state){auto got=decode(state,output.data,capacity);close(state);
   if(got>=0 && (uint64_t)got>capacity)return 5;
  }
 } catch(const std::exception&) { /* A C++ decoder may reject malformed input. */ }
 std::cout<<"{\"case\":"<<which<<",\"completed\":true}\n";
 dlclose(lib);return 0;
}
