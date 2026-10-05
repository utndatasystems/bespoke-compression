#include "codec.h"
#include <chrono>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using Bytes = std::vector<uint8_t>;
using Clock = std::chrono::steady_clock;
static void check(bool ok) { if (!ok) throw std::runtime_error("strings driver failure"); }
static Bytes read(const char* name) {
    std::ifstream f(name, std::ios::binary); check(bool(f));
    return Bytes(std::istreambuf_iterator<char>(f), {});
}
static void write(const std::string& name, const void* data, size_t n) {
    std::ofstream f(name, std::ios::binary); f.write(static_cast<const char*>(data), n); check(bool(f));
}
template<class T> static T symbol(void* library, const char* name) {
    auto ptr = dlsym(library, name); check(ptr != nullptr); return reinterpret_cast<T>(ptr);
}
static double seconds(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double>(b-a).count();
}

int main(int argc, char** argv) try {
    check(argc == 7); // library operation input output capacity row-ids-file
    std::string operation(argv[2]);
    check(operation == "encode" || operation == "decode" || operation == "rows");
    auto* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { std::cerr << dlerror() << '\n'; return 2; }
    auto input = read(argv[3]);
    size_t capacity = std::stoull(argv[5]);
    check(capacity <= 512ULL*1024*1024);
    Bytes output(capacity + 64, 0xA5);
    int64_t length = -1;
    double setup = 0, elapsed = 0, warm = 0, cleanup = 0;
    std::vector<uint64_t> offsets;
    if (operation == "encode") {
        auto encode = symbol<decltype(&lab_encode)>(library, "lab_encode");
        auto a = Clock::now(); length = encode(input.data(), input.size(), output.data(), capacity);
        auto b = Clock::now(); elapsed = seconds(a,b);
    } else {
        auto open = symbol<decltype(&lab_open)>(library, "lab_open");
        auto close = symbol<decltype(&lab_close)>(library, "lab_close");
        // Resolve all entrypoints before the operation, as for encoder setup.
        auto decode = symbol<decltype(&lab_decode)>(library, "lab_decode");
        decltype(&lab_rows) rows = nullptr;
        std::vector<uint64_t> ids;
        if (operation == "rows") {
            rows = symbol<decltype(&lab_rows)>(library, "lab_rows");
            auto bytes = read(argv[6]); check(bytes.size()%8 == 0);
            ids.resize(bytes.size()/8); std::memcpy(ids.data(), bytes.data(), bytes.size());
            offsets.resize(ids.size()+1);
        }
        auto call = [&](void* state) {
            return operation == "decode" ? decode(state,output.data(),capacity)
                : rows(state,ids.data(),ids.size(),output.data(),capacity,offsets.data());
        };
        auto a = Clock::now(); void* state = open(input.data(),input.size()); auto b = Clock::now();
        check(state != nullptr); setup = seconds(a,b);
        a = Clock::now(); length = call(state); b = Clock::now(); elapsed = seconds(a,b);
        check(length >= 0 && uint64_t(length) <= capacity);
        for (size_t i=capacity;i<output.size();++i) check(output[i] == 0xA5);
        // Retain and verify the first result before doing the warm query.
        write(argv[4], output.data(), size_t(length));
        if (operation == "rows") write(std::string(argv[4])+".offsets", offsets.data(), offsets.size()*8);
        std::fill(output.begin(),output.end(),0xA5);
        a = Clock::now(); auto warm_length = call(state); b = Clock::now(); warm = seconds(a,b);
        check(warm_length == length);
        auto first = read(argv[4]); check(first.size() == size_t(length));
        check(std::memcmp(first.data(),output.data(),first.size()) == 0);
        if (operation == "rows") {
            auto first_offsets = read((std::string(argv[4])+".offsets").c_str());
            check(first_offsets.size() == offsets.size()*8);
            check(std::memcmp(first_offsets.data(),offsets.data(),first_offsets.size()) == 0);
        }
        a = Clock::now(); close(state); b = Clock::now(); cleanup = seconds(a,b);
    }
    check(length >= 0 && uint64_t(length) <= capacity);
    for (size_t i=capacity;i<output.size();++i) check(output[i] == 0xA5);
    if (operation == "encode") write(argv[4],output.data(),size_t(length));
    std::cout << std::setprecision(17) << "{\"setup_seconds\":" << setup
              << ",\"operation_seconds\":" << elapsed << ",\"warm_seconds\":" << warm
              << ",\"cleanup_seconds\":" << cleanup << ",\"output_bytes\":" << length << "}\n";
    dlclose(library); return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
