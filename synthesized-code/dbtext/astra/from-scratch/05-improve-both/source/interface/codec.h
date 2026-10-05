#pragma once
#include <stddef.h>
#include <stdint.h>

/* Native strings-v1 ABI, compatible with the pinned Lab driver.
 * Each encode call receives one complete input file. The decoder receives the
 * archive, independently of the encoder. Both are separate shared libraries.
 * Return -1/null on failure; respect the supplied output capacity.
 *
 * Row boundaries come from protocol.json. Preserve delimiters, empty rows and
 * a final unterminated row. lab_rows receives sorted zero-based IDs and writes
 * concatenated row bytes plus count+1 uint64_t output offsets, starting at zero.
 * Its output offsets describe this call, not necessarily a stored row index.
 * Bulk-only candidates need not implement lab_rows.
 *
 * The current native adapter executes on one pinned CPU with no subprocesses,
 * threads, network or external input files. Software construction in /work is
 * separate and can use installed/downloaded tools and libraries.
 * Archive, decoder and dependency files are retained individually. Their final
 * size-accounting policy is declared in protocol.json; this header imposes no
 * algorithm choice. Data-dependent initialization belongs in lab_open/decode,
 * never in untimed library constructors. Full output reconstruction is required.
 */
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
