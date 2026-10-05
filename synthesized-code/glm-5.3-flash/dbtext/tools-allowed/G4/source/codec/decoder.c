/* DBText codec - decoder (strings-v1 ABI)
 * FMT_FIXED(1): per-row bit codes + per-column emitter (c_name, genome, hex, uuid, location)
 * FMT_TOKENS(2): phrase dictionary + canonical Huffman token stream with inline payloads
 * FMT_RAW(3): verbatim copy, LF row framing
 * Row access: huffman-coded per-row bit deltas; absolute offset table built in lab_open.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <lzma.h>
#include <stdio.h>
#include <stdlib.h>
#include "zstd.h"
#include "codec.h"

#define FMT_FIXED  1
#define FMT_TOKENS 2
#define FMT_RAW    3
enum { EM_CNAME=1, EM_GENOME, EM_HEX32, EM_UUID, EM_LOC };
enum { SP_LIT=1, SP_DEC=2, SP_TS_SEC=3, SP_TS_MIN=4, SP_DATE=5, SP_HEX8=6 };
#define RB 10
#define MAXBITS 20

typedef struct { uint8_t kind, p0, p1, pad; int64_t p2; } SP;

/* ------------- bit reader: MSB-first, 64-bit window -------------
 * buf: pending bits left-aligned in the top cnt bits.
 * refill ingests whole bytes; the overlapping high bits it re-ORs are
 * identical to what is already buffered, so the OR is idempotent.
 */
typedef struct { const uint8_t* p; uint64_t buf; uint32_t cnt; } BR;

static inline void br_init(BR* b, const uint8_t* p) { b->p = p; b->buf = 0; b->cnt = 0; }

static inline void br_refill(BR* b) {
    uint64_t v;
    memcpy(&v, b->p, 8);
    v = __builtin_bswap64(v);
    uint32_t cnt = b->cnt;
    uint32_t bytes = (64 - cnt) >> 3;
    uint32_t fill = bytes << 3;
    if (fill) b->buf |= (v >> (64 - fill)) << (64 - cnt - fill);
    b->p += bytes;
    b->cnt = cnt + fill;
}
/* seek to arbitrary bit offset (mis < 8) */
static inline void br_seek(BR* b, const uint8_t* base, uint64_t bit) {
    b->p = base + (bit >> 3);
    b->buf = 0;
    b->cnt = 0;
    br_refill(b);
    uint32_t mis = (uint32_t)(bit & 7);
    b->buf <<= mis;
    b->cnt -= mis;
}
static inline uint64_t br_read(BR* b, uint32_t n) { /* 1 <= n <= 57 */
    if (b->cnt < n) br_refill(b);
    uint64_t v = (b->buf >> (64 - n)) & ((1ULL << n) - 1);
    b->buf <<= n;
    b->cnt -= n;
    return v;
}
static inline void br_read_into(BR* b, uint8_t* o, uint32_t k) {
    while (k >= 4) {
        uint64_t v = br_read(b, 32);
        o[0] = (uint8_t)(v >> 24); o[1] = (uint8_t)(v >> 16);
        o[2] = (uint8_t)(v >> 8);  o[3] = (uint8_t)v;
        o += 4; k -= 4;
    }
    while (k) { *o++ = (uint8_t)br_read(b, 8); k--; }
}

/* ---------------- formatting ---------------- */
static uint16_t D2[100], H2[256], H2L[256];
static int tabs_ready = 0;
static void init_tabs(void) {
    if (tabs_ready) return;
    for (int i = 0; i < 100; i++)
        D2[i] = (uint16_t)(uint8_t)('0' + i / 10) | (uint16_t)((uint16_t)(uint8_t)('0' + i % 10) << 8);
    const char* hx = "0123456789ABCDEF";
    const char* hx2 = "0123456789abcdef";
    for (int i = 0; i < 256; i++) {
        H2[i] = (uint16_t)(uint8_t)hx[i >> 4] | (uint16_t)((uint16_t)(uint8_t)hx[i & 15] << 8);
        H2L[i] = (uint16_t)(uint8_t)hx2[i >> 4] | (uint16_t)((uint16_t)(uint8_t)hx2[i & 15] << 8);
    }
    tabs_ready = 1;
}

/* decimal, zero-padded to nd digits (nd==0 -> minimal) */
static inline uint8_t* fmt_u64(uint8_t* o, uint64_t v, uint32_t nd) {
    uint8_t tmp[24];
    uint32_t n = 0;
    while (v >= 100) {
        uint32_t r = (uint32_t)(v % 100);
        v /= 100;
        uint16_t p = D2[r];
        tmp[n++] = (uint8_t)(p >> 8); tmp[n++] = (uint8_t)p;
    }
    if (v >= 10) {
        uint16_t p = D2[v];
        tmp[n++] = (uint8_t)(p >> 8); tmp[n++] = (uint8_t)p;
    } else tmp[n++] = (uint8_t)('0' + (uint32_t)v);
    if (!nd) nd = n;
    for (uint32_t i = nd; i > n; i--) *o++ = '0';
    uint32_t k = nd < n ? nd : n;
    for (uint32_t i = 0; i < k; i++) *o++ = tmp[n - 1 - i];
    return o;
}

static void civil_from_days(int64_t z, int64_t* y, uint32_t* m, uint32_t* d) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    uint32_t doe = (uint32_t)(z - era * 146097);
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t yy = (int64_t)yoe + era * 400;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint32_t mp = (5 * doy + 2) / 153;
    uint32_t dd = doy - (153 * mp + 2) / 5 + 1;
    uint32_t mm = mp + (mp < 10 ? 3 : -9);
    *y = yy + (mm <= 2); *m = mm; *d = dd;
}

static inline uint8_t* emit_ts(uint8_t* o, int64_t days, uint32_t h, uint32_t mi, uint32_t s,
                               int with_time, int with_sec) {
    int64_t y; uint32_t m, d;
    civil_from_days(days, &y, &m, &d);
    o = fmt_u64(o, (uint64_t)y, 4); *o++ = '-';
    o = fmt_u64(o, m, 2); *o++ = '-';
    o = fmt_u64(o, d, 2);
    if (with_time) {
        *o++ = 'T';
        o = fmt_u64(o, h, 2);
        *o++ = ':';
        o = fmt_u64(o, mi, 2);
        if (with_sec) { *o++ = ':'; o = fmt_u64(o, s, 2); }
    }
    return o;
}

/* ---------------- huffman ---------------- */
typedef struct {
    uint32_t* root;
    uint32_t* pool;
    uint32_t* soff;
    uint8_t*  slen;
    uint32_t  nsub;
} HF;

typedef struct { uint64_t data; uint32_t off; uint16_t len; uint8_t kind; uint8_t pad; } SYM;

typedef struct {
    uint8_t fmt, emit, flags;
    uint32_t n_rows;
    uint64_t raw_size, base;
    uint32_t row_bits;
    uint8_t* data;
    const uint8_t* payload;
    uint32_t block_log;
    const uint8_t* deltas;
    HF hf;
    HF hf_d;
    SYM* syms;        /* rank-indexed (hot first) after lab_open */
    uint8_t* hotblob;
    uint8_t* lens_copy;
    uint8_t* dictstr;
    uint32_t n_syms, n_dict, n_special;
    SP* specials;
    HF h_ld, h_nd;
    uint32_t loc_flags;
    uint8_t dlens_buf[256];
    uint32_t* rowoff;
    uint64_t total_bits;
    uint32_t dbytes;
    uint64_t arc_size;
    uint8_t rowoff_ready;
} Col;

static void hf_free(HF* h) { free(h->root); free(h->pool); free(h->soff); free(h->slen); memset(h, 0, sizeof(*h)); }

static int hf_build(HF* h, const uint8_t* lens, uint32_t n) {
    uint32_t cnt[MAXBITS + 1] = {0};
    uint32_t maxlen = 0;
    for (uint32_t i = 0; i < n; i++)
        if (lens[i]) {
            if (lens[i] > MAXBITS) return -1; /* malformed lengths */
            cnt[lens[i]]++;
            if (lens[i] > maxlen) maxlen = lens[i];
        }
    if (!maxlen) return -1;
    {
        uint64_t space = 0; /* Kraft: canonical codes must exactly fill maxlen bits;
                             * incomplete sets would leave unfilled LUT slots */
        for (uint32_t l = 1; l <= maxlen; l++) {
            space += (uint64_t)cnt[l] << (maxlen - l);
            if (space > (1ULL << maxlen)) return -1;
        }
        if (space != (1ULL << maxlen)) return -1;
    }
    uint32_t next[MAXBITS + 2];
    uint32_t code = 0;
    for (uint32_t l = 1; l <= maxlen; l++) { next[l] = code; code = (code + cnt[l]) << 1; }
    uint32_t* codes = (uint32_t*)malloc((size_t)n * 4 + 4);
    if (!codes) return -1;
    for (uint32_t i = 0; i < n; i++) codes[i] = lens[i] ? next[lens[i]]++ : 0;
    uint8_t* pmax = (uint8_t*)calloc(1u << RB, 1);
    uint64_t poolneed = 0;
    uint32_t nsubmax = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t l = lens[i];
        if (l <= RB) continue;
        uint32_t pre = codes[i] >> (l - RB);
        if (l > pmax[pre]) { poolneed += (1ULL << (l - RB)) - (pmax[pre] ? (1ULL << (pmax[pre] - RB)) : 0); pmax[pre] = (uint8_t)l; }
    }
    for (uint32_t pre = 0; pre < (1u << RB); pre++) if (pmax[pre]) nsubmax++;
    h->root = (uint32_t*)calloc(1u << RB, 4);
    h->pool = (uint32_t*)malloc((size_t)poolneed * 4 + 64);
    h->soff = (uint32_t*)malloc((size_t)(nsubmax ? nsubmax : 1) * 4 + 4);
    h->slen = (uint8_t*)malloc((size_t)(nsubmax ? nsubmax : 1) + 1);
    if (!h->root || !h->pool || !h->soff || !h->slen) { free(codes); free(pmax); return -1; }
    h->nsub = 0;
    uint32_t poolused = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t l = lens[i];
        if (!l) continue;
        uint32_t cc = codes[i];
        if (l <= RB) {
            uint32_t lo = cc << (RB - l);
            uint32_t e = (l << 24) | i;
            for (uint32_t j = 0; j < (1u << (RB - l)); j++) h->root[lo + j] = e;
        } else {
            uint32_t pre = cc >> (l - RB);
            uint32_t e = h->root[pre];
            if (!(e & 0x80000000u)) {
                uint32_t id = h->nsub++;
                uint32_t sz = 1u << (pmax[pre] - RB);
                h->soff[id] = poolused;
                h->slen[id] = pmax[pre];
                for (uint32_t j = 0; j < sz; j++) h->pool[poolused + j] = 0xFFFFFFFFu;
                poolused += sz;
                h->root[pre] = 0x80000000u | id;
                e = h->root[pre];
            }
            uint32_t id = e & 0x7FFFFF;
            uint32_t sh = pmax[pre] - l;
            uint32_t idx = (cc & ((1u << (l - RB)) - 1)) << sh;
            uint32_t ev = (l << 24) | i;
            for (uint32_t j = 0; j < (1u << sh); j++)
                h->pool[h->soff[id] + idx + j] = ev;
        }
    }
    free(codes); free(pmax);
    return 0;
}

/* decode one symbol; returns symbol, sets *L to code length */
static inline uint32_t hf_dec_len(const HF* h, BR* b, uint32_t* L) {
    if (b->cnt < RB) br_refill(b);
    uint32_t e = h->root[b->buf >> (64 - RB)];
    uint32_t len = e >> 24;
    if (len <= RB) {
        b->buf <<= len;
        b->cnt -= len;
        *L = len;
        return e & 0xFFFFFF;
    }
    uint32_t id = e & 0x7FFFFF;
    uint32_t l2 = h->slen[id];
    if (b->cnt < l2) br_refill(b);
    uint32_t v = h->pool[h->soff[id] + ((b->buf >> (64 - l2)) & ((1u << (l2 - RB)) - 1))];
    uint32_t vl = v >> 24;
    b->buf <<= vl;
    b->cnt -= vl;
    *L = vl;
    return v & 0xFFFFFF;
}

static inline uint32_t hf_dec(const HF* h, BR* b) {
    uint32_t L;
    return hf_dec_len(h, b, &L);
}

/* --------------- FMT_FIXED emitters --------------- */
static const uint32_t RESTBITS[16] = {0,0,4,7,10,14,17,20,24,27,30,34,37,40,44,47};

/* exact maximum output bytes per fixed-row emitter (guards in *_rows) */
static uint32_t fixed_row_max(uint8_t emit) {
    switch (emit) {
    case EM_CNAME: return 19;
    case EM_GENOME: return 10;
    case EM_HEX32: return 9;
    case EM_UUID: return 37;
    default: return 41; /* EM_LOC: max row on this dataset is 41 bytes */
    }
}

typedef struct { uint32_t null, L1, d1, nib, L2, d2; uint64_t r1, r2; } LocRec;

static inline void loc_parse(BR* b, const Col* c, LocRec* r) {
    r->null = 0; r->L1 = 1; r->d1 = 0; r->nib = 0; r->L2 = 1; r->d2 = 0; r->r1 = 0; r->r2 = 0;
    if ((c->loc_flags & 1) && br_read(b, 1)) { r->null = 1; return; }
    r->L1 = (uint32_t)br_read(b, 4);
    r->d1 = hf_dec(&c->h_ld, b);
    if (r->L1 > 1) r->r1 = br_read(b, RESTBITS[r->L1]);
    r->nib = (uint32_t)br_read(b, 1);
    r->L2 = (uint32_t)br_read(b, 4);
    r->d2 = hf_dec(&c->h_nd, b);
    if (r->L2 > 1) r->r2 = br_read(b, RESTBITS[r->L2]);
}

static inline uint32_t loc_need(const LocRec* r) {
    return r->null ? 5u : 12u + r->L1 + r->L2;
}

static inline uint8_t* loc_write(const Col* c, const LocRec* r, uint8_t* o) {
    if (r->null) { memcpy(o, "NULL\n", 5); return o + 5; }
    *o++ = '('; *o++ = '4'; *o++ = '0'; *o++ = '.';
    *o++ = (uint8_t)('0' + r->d1);
    if (r->L1 > 1) o = fmt_u64(o, r->r1, r->L1 - 1);
    *o++ = ','; *o++ = ' ';
    *o++ = '-'; *o++ = '7'; *o++ = (uint8_t)('3' + r->nib); *o++ = '.';
    *o++ = (uint8_t)('0' + r->d2);
    if (r->L2 > 1) o = fmt_u64(o, r->r2, r->L2 - 1);
    *o++ = ')'; *o++ = '\n';
    return o;
}

static inline uint32_t hex_nib(uint32_t v) {
    return v ? (32 - __builtin_clz(v) + 3) / 4 : 1;
}

static inline uint8_t* hex_write(uint32_t v, uint8_t* o) {
    uint32_t nib = hex_nib(v);
    const char* hx = "0123456789ABCDEF";
    for (uint32_t i = 0; i < nib; i++) o[i] = (uint8_t)hx[(v >> ((nib - 1 - i) * 4)) & 15];
    o[nib] = '\n';
    return o + nib + 1;
}

static inline uint8_t* emit_fixed_row(BR* b, Col* c, uint8_t* o) {
    switch (c->emit) {
    case EM_CNAME: {
        uint32_t v = (uint32_t)br_read(b, c->row_bits) + (uint32_t)c->base;
        memcpy(o, "Customer#", 9); o += 9;
        o = fmt_u64(o, v, 9);
        *o++ = '\n';
        return o;
    }
    case EM_GENOME: {
        uint64_t v = br_read(b, c->row_bits);
        for (int i = 0; i < 9; i++) o[i] = (uint8_t)("acgt"[(v >> (16 - 2 * i)) & 3]);
        o[9] = '\n';
        return o + 10;
    }
    case EM_HEX32: {
        uint32_t v = (uint32_t)br_read(b, c->row_bits);
        return hex_write(v, o);
    }
    case EM_UUID: {
        uint32_t tl = (uint32_t)br_read(b, 22) + (uint32_t)c->base;
        uint32_t cs = (uint32_t)br_read(b, 14);
        uint32_t nh = (uint32_t)br_read(b, 6);
        uint64_t nl = br_read(b, 40);
        uint64_t node = (((uint64_t)nh << 2) | 3u) << 40 | nl;
        uint8_t* q = o;
        for (int i = 3; i >= 0; i--) { uint16_t hp = H2L[(tl >> (i * 8)) & 0xFF]; *q++ = (uint8_t)hp; *q++ = (uint8_t)(hp >> 8); }
        memcpy(q, "-2da5-11e8-", 11); q += 11;
        uint32_t cv = cs + 0x8000u;
        for (int i = 1; i >= 0; i--) { uint16_t hp = H2L[(cv >> (i * 8)) & 0xFF]; *q++ = (uint8_t)hp; *q++ = (uint8_t)(hp >> 8); }
        *q++ = '-';
        for (int i = 5; i >= 0; i--) { uint16_t hp = H2L[(node >> (i * 8)) & 0xFF]; *q++ = (uint8_t)hp; *q++ = (uint8_t)(hp >> 8); }
        *q++ = '\n';
        return q;
    }
    default: { /* EM_LOC */
        LocRec r;
        loc_parse(b, c, &r);
        return loc_write(c, &r, o);
    }
    }
}

/* --------------- FMT_TOKENS --------------- */
static inline uint32_t special_need(const SP* P) {
    switch (P->kind) {
    case SP_LIT: return P->p0;
    case SP_DEC: return P->p1;
    case SP_TS_SEC: return 19;
    case SP_TS_MIN: return 16;
    case SP_DATE: return 10;
    default: return 8; /* SP_HEX8 */
    }
}
static inline uint8_t* emit_special(Col* c, uint32_t spi, BR* b, uint8_t* o) {
    const SP* P = &c->specials[spi];
    switch (P->kind) {
    case SP_LIT:
        br_read_into(b, o, P->p0);
        return o + P->p0;
    case SP_DEC: {
        uint64_t v = P->p0 ? br_read(b, P->p0) : 0;
        return fmt_u64(o, (uint64_t)((int64_t)v + P->p2), P->p1);
    }
    case SP_TS_SEC: {
        int64_t v = (int64_t)br_read(b, P->p0) + P->p2;
        int64_t days = v / 86400, rem = v % 86400;
        if (rem < 0) { rem += 86400; days--; }
        return emit_ts(o, days, (uint32_t)(rem / 3600), (uint32_t)((rem / 60) % 60), (uint32_t)(rem % 60), 1, 1);
    }
    case SP_TS_MIN: {
        int64_t v = (int64_t)br_read(b, P->p0) + P->p2;
        int64_t days = v / 1440, rem = v % 1440;
        if (rem < 0) { rem += 1440; days--; }
        return emit_ts(o, days, (uint32_t)(rem / 60), (uint32_t)(rem % 60), 0, 1, 0);
    }
    case SP_DATE: {
        int64_t v = (int64_t)br_read(b, P->p0) + P->p2;
        return emit_ts(o, v, 0, 0, 0, 0, 0);
    }
    default: {
        uint64_t v = br_read(b, 32);
        uint8_t* q = o;
        if (P->p1) {
            for (int i = 3; i >= 0; i--) { uint16_t hp = H2L[(v >> (i * 8)) & 0xFF]; *q++ = (uint8_t)hp; *q++ = (uint8_t)(hp >> 8); }
        } else {
            for (int i = 3; i >= 0; i--) { uint16_t hp = H2[(v >> (i * 8)) & 0xFF]; *q++ = (uint8_t)hp; *q++ = (uint8_t)(hp >> 8); }
        }
        return q;
    }
    }
}

static inline uint8_t* tokens_decode_row(Col* c, BR* b, uint64_t rowbits, uint8_t* o, const uint8_t* oend) {
    int64_t rem = (int64_t)rowbits;
    const uint8_t* ds = c->hotblob ? c->hotblob : c->dictstr;
    while (rem > 0) {
        uint32_t L;
        uint32_t s = hf_dec_len(&c->hf, b, &L);
        SYM* y = &c->syms[s];
        rem -= L;
        if (!y->kind) {
            uint32_t l = y->len;
            if (l <= 8) {
                /* inline bytes in the SYM entry; the 8-byte store over-writes
                 * up to 7 bytes the next symbol (or the oend guard) covers */
                if (o + 8 <= oend) memcpy(o, &y->data, 8);
                else if (o + l <= oend) memcpy(o, &y->data, l);
                else { fprintf(stderr, "NULL-SHORT\n"); return NULL; }
            } else {
                const uint8_t* src = ds + y->off;
                if (o + l + 8 <= oend) {
                    uint64_t w0;
                    uint32_t done = 0;
                    while (l - done >= 8) {
                        memcpy(&w0, src + done, 8);
                        memcpy(o + done, &w0, 8);
                        done += 8;
                    }
                    memcpy(&w0, src + done, 8);
                    memcpy(o + done, &w0, 8);
                } else if (o + l <= oend) {
                    memcpy(o, src, l);
                } else return NULL;
            }
            o += l;
        } else {
            rem -= y->len; /* payload bits stored in len for specials */
            if ((size_t)(oend - o) < special_need(&c->specials[y->off])) return NULL;
            o = emit_special(c, y->off, b, o);
        }
    }
    return o;
}

/* ---------------- archive parsing ---------------- */
static int xz_inflate(const uint8_t* src, size_t slen, uint8_t* dst, size_t dlen) {
    size_t in_pos = 0, out_pos = 0;
    uint64_t memlim = UINT64_MAX;
    lzma_ret r = lzma_stream_buffer_decode(&memlim, 0, NULL, src, &in_pos, slen, dst, &out_pos, dlen);
    return (r == LZMA_OK && out_pos == dlen) ? 0 : -1;
}

static int meta_inflate(const uint8_t* src, size_t slen, uint8_t* dst, size_t dlen, int use_zstd) {
    if (use_zstd) {
        static ZSTD_DCtx* dc = NULL;
        if (!dc) dc = ZSTD_createDCtx();
        size_t r = ZSTD_decompressDCtx(dc, dst, dlen, src, slen);
        return r == dlen ? 0 : -1;
    }
    return xz_inflate(src, slen, dst, dlen);
}

static uint32_t rd32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t rd64(const uint8_t* p) { uint64_t v; memcpy(&v, p, 8); return v; }

static int parse_meta(Col* c, const uint8_t* m, uint32_t mlen) {
    const uint8_t* p = m;
    if (c->fmt == FMT_TOKENS) {
        uint32_t ndict = rd32(p); p += 4;
        uint32_t nspec = rd32(p); p += 4;
        if (8u + (uint64_t)nspec * 12 + ndict + nspec > mlen) return -1;
        c->n_dict = ndict; c->n_special = nspec;
        c->n_syms = ndict + nspec;
        c->specials = (SP*)calloc(nspec ? nspec : 1, sizeof(SP));
        c->syms = (SYM*)calloc(c->n_syms ? c->n_syms : 1, sizeof(SYM));
        if (!c->specials || !c->syms) return -1;
        for (uint32_t i = 0; i < nspec; i++) {
            c->specials[i].kind = p[0]; c->specials[i].p0 = p[1]; c->specials[i].p1 = p[2];
            int64_t a; memcpy(&a, p + 4, 8);
            c->specials[i].p2 = a;
            p += 12;
        }
        uint8_t* lens = (uint8_t*)malloc(c->n_syms + 1);
        if (!lens) return -1;
        memcpy(lens, p, c->n_syms); p += c->n_syms;
        int rc = hf_build(&c->hf, lens, c->n_syms);
        c->lens_copy = lens; /* kept for the rank pass */
        if (rc) return -1;
        /* front-coded sorted dictionary: ndict x (skip u8, len u8) + suffix blob */
        const uint8_t* rec = p;
        uint64_t total = 0;
        uint64_t scur = 0;
        const uint8_t* suf = rec + (size_t)ndict * 2;
        if ((uint64_t)(size_t)(rec - m) + (uint64_t)ndict * 2 > mlen) return -1;
        uint64_t sufbudget = mlen - (uint64_t)(size_t)(suf - m);
        {
            uint32_t prev_len = 0;
            for (uint32_t i = 0; i < ndict; i++) {
                uint32_t skip = rec[2 * (size_t)i], len = rec[2 * (size_t)i + 1];
                if (len < skip || (i && skip > prev_len) ||
                    (uint64_t)total + len > (1ULL << 30) ||
                    scur + (len - skip) > sufbudget) return -1;
                total += len;
                scur += len - skip;
                prev_len = len;
            }
        }
        c->dictstr = (uint8_t*)malloc((size_t)total + 16);
        if (!c->dictstr) return -1;
        {
            uint32_t cur = 0, prev_off = 0;
            scur = 0;
            for (uint32_t i = 0; i < ndict; i++) {
                uint32_t skip = rec[2 * (size_t)i], len = rec[2 * (size_t)i + 1];
                if (skip) memcpy(c->dictstr + cur, c->dictstr + prev_off, skip);
                memcpy(c->dictstr + cur + skip, suf + scur, len - skip);
                c->syms[i].off = cur;
                c->syms[i].len = (uint16_t)len;
                c->syms[i].kind = 0;
                scur += len - skip;
                prev_off = cur;
                cur += len;
            }
        }
        for (uint32_t i = 0; i < ndict; i++) {
            SYM* y = &c->syms[i];
            if (y->len <= 8) memcpy(&y->data, c->dictstr + y->off, y->len);
        }
        p = suf + scur; /* blob holds sum(len - skip) bytes */
        if ((uint64_t)(p - m) + 256 > mlen) return -1;
        memcpy(c->dlens_buf, p, 256);
        for (uint32_t i = 0; i < nspec; i++) {
            c->syms[ndict + i].off = i;
            c->syms[ndict + i].kind = c->specials[i].kind;
            c->syms[ndict + i].len = c->specials[i].p0; /* payload bits for all kinds */
        }
        return 0;
    }
    if (c->fmt == FMT_FIXED && c->emit == EM_LOC) {
        if (mlen < 277) return -1;
        c->loc_flags = p[0]; p += 1;
        uint8_t ld[10], nd[10];
        memcpy(ld, p, 10); p += 10;
        memcpy(nd, p, 10); p += 10;
        memcpy(c->dlens_buf, p, 256);
        if (hf_build(&c->h_ld, ld, 10)) return -1;
        if (hf_build(&c->h_nd, nd, 10)) return -1;
        return 0;
    }
    return 0;
}

/* frequency-rank remap: hot symbols first so the SYM array stays cache-resident
 * under random access; deferred until row access actually happens */
static void build_rank(Col* c) {
    if (c->fmt == FMT_TOKENS && c->lens_copy) {
    /* frequency-rank remap: short codes (hot symbols) get low ranks so the
     * SYM array stays cache-resident under random access */
    uint32_t ns = c->n_syms;
    const uint8_t* L2 = c->lens_copy;
    uint32_t cnt2[MAXBITS + 2] = {0};
    for (uint32_t i = 0; i < ns; i++) cnt2[L2[i]]++;
    uint32_t pos[MAXBITS + 2], run = 0;
    for (uint32_t l = 0; l <= MAXBITS + 1; l++) { pos[l] = run; run += cnt2[l]; }
    uint32_t* order = (uint32_t*)malloc((size_t)ns * 4 + 4);
    for (uint32_t i = 0; i < ns; i++) order[pos[L2[i]]++] = i;
    uint32_t* rank = (uint32_t*)malloc((size_t)ns * 4 + 4);
    for (uint32_t k = 0; k < ns; k++) rank[order[k]] = k;
    uint64_t total = 0;
    for (uint32_t i = 0; i < c->n_dict; i++) total += c->syms[i].len;
    uint8_t* blob = (uint8_t*)malloc((size_t)total + 16);
    SYM* sr = (SYM*)malloc((size_t)ns * sizeof(SYM) + 16);
    uint64_t off = 0;
    for (uint32_t k = 0; k < ns; k++) {
        uint32_t id = order[k];
        SYM* y = &c->syms[id];
        if (!y->kind) {
            memcpy(blob + off, c->dictstr + y->off, y->len);
            sr[k] = *y;
            sr[k].off = (uint32_t)off;
            off += y->len;
        } else {
            sr[k] = *y;
        }
    }
    for (uint32_t j = 0; j < (1u << RB); j++) {
        uint32_t e = c->hf.root[j];
        /* only short-code entries carry a symbol id; long entries (bit31)
         * reference subtables and empty slots (0) stay untouched */
        if (e && !(e & 0x80000000u)) c->hf.root[j] = (e & 0xFF000000u) | rank[e & 0xFFFFFFu];
    }
    for (uint32_t j = 0; j < c->hf.nsub; j++) {
        uint32_t sz = 1u << (c->hf.slen[j] - RB);
        uint32_t* pp = c->hf.pool + c->hf.soff[j];
        for (uint32_t t = 0; t < sz; t++) {
            uint32_t e = pp[t];
            if (e != 0xFFFFFFFFu) pp[t] = (e & 0xFF000000u) | rank[e & 0xFFFFFFu];
        }
    }
    free(rank); free(order); free(c->lens_copy); c->lens_copy = NULL;
    free(c->dictstr); c->dictstr = NULL;
    c->syms = sr;
    c->hotblob = blob;
    }
}

/* per-row bit offsets are only needed for random access; built on demand */
static int ensure_rowoff(Col* c) {
    if (c->rowoff_ready) return 0;
    if (c->fmt == FMT_TOKENS) build_rank(c);
    if (!c->rowoff) c->rowoff = (uint32_t*)malloc(((size_t)c->n_rows + 2) * 4);
    if (!c->rowoff) return -1;
    if (c->fmt == FMT_RAW) {
        if (43 + c->raw_size > c->arc_size) return -1; /* truncated raw archive */
        uint32_t r = 0;
        c->rowoff[r++] = 0;
        const uint8_t* q = c->payload;
        for (uint64_t i = 0; i < c->raw_size; i++)
            if (q[i] == '\n') c->rowoff[r++] = (uint32_t)(i + 1);
        c->rowoff[r] = (uint32_t)c->raw_size;
    } else if (c->row_bits) {
        for (uint32_t i = 0; i <= c->n_rows; i++) c->rowoff[i] = i * c->row_bits;
    } else {
        if (!c->hf_d.root && hf_build(&c->hf_d, c->dlens_buf, 256) != 0) return -1;
        BR b;
        br_init(&b, c->deltas);
        uint64_t acc = 0;
        const uint8_t* dend = c->payload + 40;
        for (uint32_t i = 0; i < c->n_rows; i++) {
            c->rowoff[i] = (uint32_t)acc;
            uint32_t d = hf_dec(&c->hf_d, &b);
            if (d == 255) d = (uint32_t)br_read(&b, 16);
            acc += d;
            if (b.p > dend) return -1; /* malformed delta stream */
        }
        c->rowoff[c->n_rows] = (uint32_t)acc;
    }
    c->rowoff_ready = 1;
    return 0;
}

void* lab_open(const uint8_t* archive, size_t size) {
    init_tabs();
    if (size < 64 || memcmp(archive, "DBT1", 4) != 0) return NULL;
    Col* c = (Col*)calloc(1, sizeof(Col));
    if (!c) return NULL;
    c->data = (uint8_t*)malloc(size + 64);
    if (!c->data) { free(c); return NULL; }
    c->arc_size = (uint64_t)size;
    memcpy(c->data, archive, size);
    memset(c->data + size, 0, 64);
    const uint8_t* p = c->data;
    c->fmt = p[4];
    c->flags = p[5];
    c->n_rows = rd32(p + 6);
    c->raw_size = rd64(p + 10);
    c->emit = p[18];
    c->row_bits = rd32(p + 19);
    c->base = rd64(p + 23);
    uint32_t mcomp = rd32(p + 31);
    uint64_t mraw = rd64(p + 35);
    p += 43;
    /* malformed-archive bounds: every derived pointer must stay in-buffer */
    if ((uint64_t)mcomp > (uint64_t)(size - 43) || mraw > (uint64_t)size * 1024) { lab_close(c); return NULL; }
    if (c->fmt == FMT_RAW) {
        c->payload = c->data + 43;
        return c;
    }
    if (mcomp) {
        uint8_t* m = (uint8_t*)malloc((size_t)mraw + 64);
        if (!m) { lab_close(c); return NULL; }
        if (meta_inflate(p, mcomp, m, mraw, c->flags & 1) != 0) { free(m); lab_close(c); return NULL; }
        int rc = parse_meta(c, m, (uint32_t)mraw);
        free(m);
        if (rc) { lab_close(c); return NULL; }
        p += mcomp;
    }
    uint64_t avail = (uint64_t)(size - (size_t)(p - c->data));
    if (avail < 10) { lab_close(c); return NULL; }
    c->block_log = p[0];
    {
        uint64_t tbw = rd64(p + 2);
        c->total_bits = tbw & 0xFFFFFFFFull;
        c->dbytes = (uint32_t)(tbw >> 32);
    }
    p += 10;
    avail -= 10;
    c->deltas = p;
    if (c->n_rows > c->raw_size || c->n_rows > avail * 8) { lab_close(c); return NULL; }
    if (c->row_bits) {
        if (c->row_bits > 4096 || (uint64_t)c->n_rows * c->row_bits > avail * 8) { lab_close(c); return NULL; }
        c->payload = p;
    } else {
        /* payload starts right after the delta stream (dbytes from header) */
        if ((uint64_t)c->dbytes > avail ||
            c->total_bits > (avail - c->dbytes) * 8) { lab_close(c); return NULL; }
        c->payload = p + c->dbytes;
    }
    return c;
}

int64_t lab_decode(void* st, uint8_t* out, size_t cap) {
    Col* c = (Col*)st;
    if (!c || cap < c->raw_size) return -1;
    uint8_t* o = out;
    const uint8_t* oend = out + cap;
    if (c->fmt == FMT_FIXED) {
        BR b;
        br_init(&b, c->payload);
        for (uint32_t i = 0; i < c->n_rows; i++) {
            o = emit_fixed_row(&b, c, o);
            if (o > oend) return -1; /* caller-supplied capacity too small */
            if ((((uint64_t)(uintptr_t)(b.p - c->payload)) << 3) - b.cnt > c->total_bits)
                return -1; /* malformed: consumed more bits than archived */
        }
        return (int64_t)(o - out);
    }
    if (c->fmt == FMT_TOKENS) {
        BR b;
        br_init(&b, c->payload);
        /* row bit-deltas are not needed: decode the token stream contiguously */
        o = tokens_decode_row(c, &b, c->total_bits, o, oend);
        if (!o) return -1;
        return (int64_t)(o - out);
    }
    if (c->fmt == FMT_RAW) {
        memcpy(out, c->payload, c->raw_size);
        return (int64_t)c->raw_size;
    }
    return -1;
}

int64_t lab_rows(void* st, const uint64_t* ids, size_t count, uint8_t* out, size_t cap, uint64_t* offsets) {
    Col* c = (Col*)st;
    if (!c) return -1;
    if (!count) { offsets[0] = 0; return 0; }
    if (ensure_rowoff(c) != 0) return -1;
    uint8_t* o = out;
    const uint8_t* oend = out + cap;
    offsets[0] = 0;
    if (c->fmt == FMT_RAW) {
        for (size_t j = 0; j < count; j++) {
            uint32_t id = (uint32_t)ids[j];
            uint32_t s0 = c->rowoff[id], s1 = c->rowoff[id + 1];
            if ((size_t)(oend - o) < s1 - s0) return -1;
            memcpy(o, c->payload + s0, s1 - s0);
            o += s1 - s0;
            offsets[j + 1] = (uint64_t)(o - out);
        }
        return (int64_t)(o - out);
    }
    if (c->fmt == FMT_FIXED) {
        if (c->emit == EM_HEX32) {
            for (size_t j = 0; j < count; j++) {
                uint32_t id = (uint32_t)ids[j];
                BR b;
                br_seek(&b, c->payload, c->rowoff[id]);
                uint32_t v = (uint32_t)br_read(&b, c->row_bits);
                if ((size_t)(oend - o) < hex_nib(v) + 1) return -1;
                o = hex_write(v, o);
                offsets[j + 1] = (uint64_t)(o - out);
            }
            return (int64_t)(o - out);
        }
        if (c->emit == EM_LOC) {
            for (size_t j = 0; j < count; j++) {
                uint32_t id = (uint32_t)ids[j];
                BR b;
                br_seek(&b, c->payload, c->rowoff[id]);
                LocRec r;
                loc_parse(&b, c, &r);
                if ((size_t)(oend - o) < loc_need(&r)) return -1;
                o = loc_write(c, &r, o);
                if ((((uint64_t)(uintptr_t)(b.p - c->payload)) << 3) - b.cnt > c->total_bits + 64)
                    return -1;
                offsets[j + 1] = (uint64_t)(o - out);
            }
            return (int64_t)(o - out);
        }
        uint32_t need = fixed_row_max(c->emit);
        for (size_t j = 0; j < count; j++) {
            uint32_t id = (uint32_t)ids[j];
            if ((size_t)(oend - o) < need) return -1;
            BR b;
            br_seek(&b, c->payload, c->rowoff[id]);
            o = emit_fixed_row(&b, c, o);
            if ((((uint64_t)(uintptr_t)(b.p - c->payload)) << 3) - b.cnt > c->total_bits + 64)
                return -1;
            offsets[j + 1] = (uint64_t)(o - out);
        }
        return (int64_t)(o - out);
    }
    if (c->fmt == FMT_TOKENS) {
        for (size_t j = 0; j < count; j++) {
            uint32_t id = (uint32_t)ids[j];
            uint64_t rowbits = (uint64_t)(c->rowoff[id + 1] - c->rowoff[id]);
            BR b;
            br_seek(&b, c->payload, c->rowoff[id]);
            o = tokens_decode_row(c, &b, rowbits, o, oend);
            if (!o) return -1;
            offsets[j + 1] = (uint64_t)(o - out);
        }
        return (int64_t)(o - out);
    }
    return -1;
}

void lab_close(void* st) {
    Col* c = (Col*)st;
    if (!c) return;
    hf_free(&c->hf); hf_free(&c->hf_d); hf_free(&c->h_ld); hf_free(&c->h_nd);
    free(c->specials); free(c->syms); free(c->dictstr); free(c->hotblob); free(c->lens_copy);
    free(c->rowoff); free(c->data);
    free(c);
}
