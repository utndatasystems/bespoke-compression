#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    const uint8_t* sig;
    uint64_t raw_size;
    uint32_t n_rows;
    uint8_t fmt;
    uint8_t emit;
    uint32_t row_bits;
    uint64_t base;
    const uint8_t* meta;       /* LOC: raw meta (21 bytes) */
    uint32_t meta_len;
    const uint8_t* code_lens;  /* TOKENS: huffman lens, new ids */
    const uint8_t* dict_lens;  /* FULL orig strings lens (atoms first) */
    const uint8_t* dict_concat;
    const uint8_t* specials;   /* 12B records, archive order */
    uint32_t n_dict;           /* NEW dict count */
    uint32_t n_special;
    uint32_t n_syms;
    uint32_t dict_total;       /* FULL concat bytes */
    uint32_t tmask;
    uint32_t natoms;
    uint32_t atom_mode;        /* 0 word, 1 utf8 char */
    uint32_t n_orig;           /* orig symbol count (dict part) */
    const uint32_t* perm;      /* orig dict id -> new id */
    const uint32_t* merges;    /* (a,b,c) triples, flattened */
    const uint32_t* merge_off; /* npasses+1 offsets */
    uint32_t npasses;
    const uint8_t* delta_lens; /* 256 huffman lens for row deltas */
} ColDef;
extern const int NCOLS;
extern const ColDef COLS[];
#ifdef __cplusplus
}
#endif
