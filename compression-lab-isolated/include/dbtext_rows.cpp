// DBText row-query harness. Selection and timed loop follow FSST filtertest.cpp
// at 4e188a2d3279983dd575434f533d57783c5afe0c. No encoder or raw input is loaded.
#include "codec.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>
using Bytes = std::vector<uint8_t>;
using Clock = std::chrono::steady_clock;
extern "C" int __lsan_is_turned_off(){return 1;}
extern "C" const char* __asan_default_options(){return "detect_leaks=0:abort_on_error=1:symbolize=0";}
extern "C" const char* __ubsan_default_options(){return "halt_on_error=1:print_stacktrace=0";}
struct Guarded {
    void* mapping; size_t mapped; uint8_t* data;
    explicit Guarded(size_t n) {
        size_t page = size_t(sysconf(_SC_PAGESIZE)), payload = ((n+page-1)/page)*page;
        mapped = payload+2*page;
        mapping = mmap(nullptr,mapped,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(mapping==MAP_FAILED) throw std::runtime_error("mmap failed");
        if(payload && mprotect((char*)mapping+page,payload,PROT_READ|PROT_WRITE))
            throw std::runtime_error("mprotect failed");
        data = (uint8_t*)mapping+page+payload-n;
    }
    ~Guarded(){munmap(mapping,mapped);}
};
void write(const std::string& path,const void* data,size_t size) {
    std::ofstream f(path,std::ios::binary);
    if(size) f.write((const char*)data,size);
    if(!f) throw std::runtime_error("output write failed");
}
template<class T> T symbol(void* lib,const char* name) {
    void* s=dlsym(lib,name);
    if(!s) throw std::runtime_error(std::string("missing ")+name);
    return reinterpret_cast<T>(s);
}
int main(int argc,char**argv) try {
    if(argc!=7) return 2;
    size_t count=std::stoull(argv[3]),rawSize=std::stoull(argv[4]);
    if(count>UINT32_MAX || rawSize>SIZE_MAX-4096) return 2;
    const std::string folder=argv[5],mode=argv[6];
    if(mode!="measure" && mode!="validate") return 2;
    std::ifstream f(argv[2],std::ios::binary);
    if(!f) return 2;
    Bytes archive(std::istreambuf_iterator<char>(f),{});
    const size_t archiveSize=archive.size();
    Guarded input(archiveSize);
    if(archiveSize) memcpy(input.data,archive.data(),archiveSize);
    archive.clear(); archive.shrink_to_fit();
    void* lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!lib) throw std::runtime_error(dlerror());
    auto open=symbol<decltype(&lab_open)>(lib,"lab_open");
    auto rows=symbol<decltype(&lab_rows)>(lib,"lab_rows");
    auto close=symbol<decltype(&lab_close)>(lib,"lab_close");
    const auto start=Clock::now();
    void* state=open(input.data,archiveSize);
    const double setup=std::chrono::duration<double>(Clock::now()-start).count();
    if(!state) throw std::runtime_error("archive open failed");
    std::vector<unsigned> shuffled(count);
    std::iota(shuffled.begin(),shuffled.end(),0);
    std::mt19937 g(123); std::shuffle(shuffled.begin(),shuffled.end(),g);
    std::vector<uint64_t> ids;
    std::cout<<std::setprecision(17)<<"{\"setup_seconds\":"<<setup<<",\"queries\":[";
    unsigned index=0;
    auto query = [&](const std::vector<unsigned>& hits,unsigned percent,size_t capacity,bool timed) {
        Guarded output(capacity),guardOffsets((hits.size()+1)*sizeof(uint64_t));
        auto offsets=(uint64_t*)guardOffsets.data;
        auto call = [&]() {
            // ABI conversion stays inside the query, as in the retained native adapter.
            ids.assign(hits.begin(),hits.end());
            return rows(state,ids.data(),ids.size(),output.data,capacity,offsets);
        };
        int64_t got;
        double seconds=0;
        if(timed) {
            for(unsigned i=0;i!=100;++i) got=call();
            const auto begin=Clock::now();
            for(unsigned i=0;i!=100;++i) got=call();
            seconds=std::chrono::duration<double>(Clock::now()-begin).count();
        } else got=call();
        if(got>=0 && uint64_t(got)>capacity) throw std::runtime_error("output capacity exceeded");
        std::string prefix=folder+"/"+std::to_string(index);
        write(prefix+".ids",ids.data(),ids.size()*sizeof(uint64_t));
        if(got>=0) {
            write(prefix+".data",output.data,size_t(got));
            write(prefix+".offsets",offsets,(hits.size()+1)*sizeof(uint64_t));
        }
        if(index++)std::cout<<",";
        std::cout<<"{\"index\":"<<(index-1)<<",\"percent\":"<<percent
                 <<",\"rows\":"<<hits.size()<<",\"capacity\":"<<capacity
                 <<",\"written\":"<<got<<",\"seconds\":"<<seconds
                 <<",\"calls\":"<<(timed?100:1)<<"}";
    };
    for(unsigned percent : {1,3,10,30,100}) {
        auto hits=shuffled; hits.resize(hits.size()*percent/100);
        if(hits.empty()) continue;
        std::sort(hits.begin(),hits.end());
        query(hits,percent,rawSize+4096,mode=="measure");
    }
    // Untimed arbitrary valid queries, including repeated selections after other queries.
    std::vector<std::vector<unsigned>> checks{{}};
    if(count) {
        checks.push_back({unsigned(count-1)}); checks.push_back({0});
        std::vector<unsigned> alternating;
        for(unsigned i=0;i<count;i+=2) alternating.push_back(i);
        checks.push_back(alternating);
        if(count>2) checks.push_back({0,unsigned(count/2),unsigned(count-1)});
        checks.push_back({unsigned(count-1)});
    }
    for(const auto& hits:checks) {
        query(hits,0,rawSize,false);
        if(mode=="validate") {query(hits,0,0,false);if(rawSize)query(hits,0,rawSize-1,false);}
    }
    std::cout<<"]}\n";
    close(state); dlclose(lib);
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<"\n";return 1;}
