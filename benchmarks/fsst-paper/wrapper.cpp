// Keep the authors' benchmark, selection, timing and aggregation unchanged.
#define main fsst_paper_main
#include "filtertest.cpp"
#undef main
#include <dlfcn.h>
#include <stdexcept>

class NativeRunner : public CompressionRunner {
   void *encoderHandle, *decoderHandle, *state = nullptr;
   int64_t (*encode)(const uint8_t*, size_t, uint8_t*, size_t);
   void* (*open)(const uint8_t*, size_t);
   int64_t (*rows)(void*, const uint64_t*, size_t, uint8_t*, size_t, uint64_t*);
   void (*close)(void*);
   vector<uint8_t> archive;
   vector<uint64_t> ids, boundaries;

   template<class T> static T symbol(void* handle, const char* name) {
      auto value = dlsym(handle, name);
      if (!value) throw runtime_error(string("missing symbol: ") + name);
      return reinterpret_cast<T>(value);
   }

public:
   NativeRunner(const char* encoder, const char* decoder) {
      encoderHandle = dlopen(encoder, RTLD_NOW | RTLD_LOCAL);
      if (!encoderHandle) throw runtime_error(dlerror());
      decoderHandle = dlopen(decoder, RTLD_NOW | RTLD_LOCAL);
      if (!decoderHandle) throw runtime_error(dlerror());
      encode = symbol<decltype(encode)>(encoderHandle, "lab_encode");
      open = symbol<decltype(open)>(decoderHandle, "lab_open");
      rows = symbol<decltype(rows)>(decoderHandle, "lab_rows");
      close = symbol<decltype(close)>(decoderHandle, "lab_close");
   }
   ~NativeRunner() {
      if (state) close(state);
      dlclose(decoderHandle);
      dlclose(encoderHandle);
   }
   uint64_t compressCorpus(const vector<string>& data, unsigned long& bareSize,
                          double& bulkTime, double& compressionTime, bool) override {
      if (state) { close(state); state = nullptr; }
      size_t rawSize = 0;
      for (const auto& line : data) rawSize += line.size() + 1;
      vector<uint8_t> raw; raw.reserve(rawSize);
      for (const auto& line : data) {
         raw.insert(raw.end(), line.begin(), line.end()); raw.push_back('\n');
      }
      archive.resize(4 * raw.size() + 16 * 1024 * 1024);
      const auto start = chrono::steady_clock::now();
      const auto written = encode(raw.data(), raw.size(), archive.data(), archive.size());
      const auto end = chrono::steady_clock::now();
      if (written < 0 || static_cast<size_t>(written) > archive.size())
         throw runtime_error("native encoding failed");
      archive.resize(written);
      state = open(archive.data(), archive.size());
      if (!state) throw runtime_error("native archive open failed");
      bulkTime = compressionTime = chrono::duration<double>(end - start).count();
      bareSize = archive.size();
      return archive.size();
   }
   uint64_t decompressRows(vector<char>& target, const vector<unsigned>& lines) override {
      // ABI conversion and returned row offsets stay inside the measured call.
      ids.assign(lines.begin(), lines.end());
      boundaries.resize(lines.size() + 1);
      auto written = rows(state, ids.data(), ids.size(),
                          reinterpret_cast<uint8_t*>(target.data()), target.size(), boundaries.data());
      if (written < 0 || static_cast<size_t>(written) > target.size())
         throw runtime_error("native selected-row decoding failed");
      return written;
   }
};

int main(int argc, const char* argv[]) try {
   if (argc >= 5 && string(argv[1]) == "native") {
      NativeRunner runner(argv[2], argv[3]);
      vector<string> files(argv + 4, argv + argc);
      return !doTest(runner, files, true).first;
   }
   return fsst_paper_main(argc, argv);
} catch (const exception& e) {
   cerr << e.what() << endl;
   return 1;
}
