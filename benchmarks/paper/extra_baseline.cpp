// Conventional one-shot codecs through the same strings-v1 ABI as the other
// baselines. Eight bytes store the original length; the rest is a standard
// codec stream. No corpus fitting, transforms, dictionaries or output cache.
#include "codec.h"
#include <cstring>
#include <memory>
#include <stdexcept>
#if CODEC == 1
#include <brotli/encode.h>
#include <brotli/decode.h>
#elif CODEC == 2
#include <zlib.h>
#elif CODEC == 3
#include <bzlib.h>
#elif CODEC == 4
#include <lzma.h>
#endif

static void need(bool ok) { if (!ok) throw std::runtime_error("invalid codec input"); }
static constexpr size_t limit = 512 * 1024 * 1024;

#ifndef DECODE_ONLY
extern "C" int64_t lab_encode(const uint8_t* raw, size_t size,
                              uint8_t* out, size_t capacity) try {
    need(size < limit && capacity >= 8 && capacity <= limit);
    static const uint8_t empty = 0;
    if (size == 0) raw = &empty;  // Some codec APIs require a non-null empty input.
    uint64_t length = size;
    std::memcpy(out, &length, 8);
#if CODEC == 1
    size_t written = capacity - 8;
    need(BrotliEncoderCompress(LEVEL, BROTLI_DEFAULT_WINDOW, BROTLI_MODE_GENERIC,
                              size, raw, &written, out + 8) == BROTLI_TRUE);
#elif CODEC == 2
    uLongf written = capacity - 8;
    need(compress2(out + 8, &written, raw, size, LEVEL) == Z_OK);
#elif CODEC == 3
    unsigned int written = capacity - 8;
    need(BZ2_bzBuffToBuffCompress(reinterpret_cast<char*>(out + 8), &written,
         const_cast<char*>(reinterpret_cast<const char*>(raw)), size, LEVEL, 0, 30) == BZ_OK);
#elif CODEC == 4
    size_t written = 0;
    // XZ's conventional checksum is CRC64; all presets use this same setting.
    need(lzma_easy_buffer_encode(LEVEL, LZMA_CHECK_CRC64, nullptr, raw, size,
                                out + 8, &written, capacity - 8) == LZMA_OK);
#else
    need(capacity - 8 >= size);
    std::memcpy(out + 8, raw, size);
    size_t written = size;
#endif
    return written + 8;
} catch (...) { return -1; }
#else
struct State { const uint8_t* data; size_t size, raw; };

extern "C" void* lab_open(const uint8_t* data, size_t size) try {
    need(size >= 8);
    uint64_t raw;
    std::memcpy(&raw, data, 8);
    need(raw < limit);
    return new State{data + 8, size - 8, size_t(raw)};
} catch (...) { return nullptr; }

extern "C" int64_t lab_decode(void* state, uint8_t* out, size_t capacity) try {
    auto& s = *static_cast<State*>(state);
    need(capacity >= s.raw && s.size <= limit);
#if CODEC == 1
    size_t written = s.raw;
    need(BrotliDecoderDecompress(s.size, s.data, &written, out) == BROTLI_DECODER_RESULT_SUCCESS);
#elif CODEC == 2
    uLongf written = capacity;
    uLong consumed = s.size;
    need(uncompress2(out, &written, s.data, &consumed) == Z_OK && consumed == s.size);
#elif CODEC == 3
    unsigned int written = capacity;
    need(BZ2_bzBuffToBuffDecompress(reinterpret_cast<char*>(out), &written,
         const_cast<char*>(reinterpret_cast<const char*>(s.data)), s.size, 0, 0) == BZ_OK);
#elif CODEC == 4
    uint64_t memory_limit = 1024ULL * 1024 * 1024;
    size_t consumed = 0, written = 0;
    need(lzma_stream_buffer_decode(&memory_limit, 0, nullptr, s.data, &consumed,
                                  s.size, out, &written, s.raw) == LZMA_OK && consumed == s.size);
#else
    need(s.size == s.raw);
    std::memcpy(out, s.data, s.raw);
    size_t written = s.raw;
#endif
    need(written == s.raw);
    return written;
} catch (...) { return -1; }

extern "C" void lab_close(void* state) { delete static_cast<State*>(state); }
#endif
