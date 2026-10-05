/* minimal vendored zstd declarations (ABI-compatible subset) */
#ifndef ZSTD_H_MINI
#define ZSTD_H_MINI
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ZSTD_DCtx_s ZSTD_DCtx;
ZSTD_DCtx* ZSTD_createDCtx(void);
size_t ZSTD_freeDCtx(ZSTD_DCtx* dctx);
size_t ZSTD_decompressDCtx(ZSTD_DCtx* dctx, void* dst, size_t dstCap, const void* src, size_t srcSize);
size_t ZSTD_compress(void* dst, size_t dstCap, const void* src, size_t srcSize, int level);
size_t ZSTD_decompress(void* dst, size_t dstCap, const void* src, size_t srcSize);
unsigned ZSTD_isError(size_t code);
size_t ZSTD_compressBound(size_t srcSize);
#ifdef __cplusplus
}
#endif
#endif
