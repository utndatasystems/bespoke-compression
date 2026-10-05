#pragma once
#include <stddef.h>
#include <stdint.h>

/* strings-v1: native C ABI. Each input is one complete column or byte file.
 * All persistent dataset-specific information must be in the archive.
 * The pinned workload declares row_framing: lf (legacy default), nul, or none.
 * Rows include their declared delimiter; a final unterminated row is preserved
 * exactly. Embedded other bytes are not boundaries. none supports bulk only.
 * Return -1/null on failure. No output may exceed the supplied capacity.
 * Implement encoder and decoder in separate shared libraries. No file I/O,
 * clocks, threads, external services or persistent uncompressed caches.
 * Decode functions return bytes written; rows also fills count+1 offsets. */
#ifdef __cplusplus
extern "C" {
#endif
int64_t lab_encode(const uint8_t* raw, size_t size, uint8_t* archive, size_t capacity);
void* lab_open(const uint8_t* archive, size_t size);
int64_t lab_decode(void* state, uint8_t* output, size_t capacity);
int64_t lab_rows(void* state, const uint64_t* ids, size_t count,
                 uint8_t* output, size_t capacity, uint64_t* offsets);
void lab_close(void* state);
#ifdef __cplusplus
}
#endif
