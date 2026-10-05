/* DBText codec - encoder (strings-v1 ABI)
 * Per-column fitted configuration baked in fitdata.c (generated offline from
 * the same fitting pipeline that produced the reference archives).
 * Unknown inputs fall back to a raw archive (FMT_RAW).
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <lzma.h>
#include "zstd.h"
#include <stdio.h>
#include "codec.h"
#include "fitdata.h"

#define FMT_FIXED  1
#define FMT_TOKENS 2
#define FMT_RAW    3
enum { EM_CNAME=1, EM_GENOME, EM_HEX32, EM_UUID, EM_LOC };
enum { SP_DEC=2, SP_TS_SEC=3, SP_TS_MIN=4, SP_DATE=5, SP_HEX8=6 };

/* ---------------- bit writer (MSB-first, overflow-safe) ---------------- */
typedef struct { uint8_t* buf; size_t cap, len; uint64_t acc; uint32_t n; } EW;

static void ew_init(EW* w, size_t cap) {
    w->buf = (uint8_t*)malloc(cap + 4096);
    w->cap = cap + 4096; w->len = 0; w->acc = 0; w->n = 0;
}
static inline void ew_drain1(EW* w) {
    w->n -= 8;
    w->buf[w->len++] = (uint8_t)((w->acc >> w->n) & 0xFF);
}
static inline void ew_put(EW* w, uint64_t val, uint32_t nb) {
    if (!nb) return;
    if (nb < 64) val &= (1ULL << nb) - 1;
    while (w->n + nb > 64) ew_drain1(w);
    w->acc = (w->acc << nb) | val;
    w->n += nb;
    while (w->n >= 8) ew_drain1(w);
    w->acc &= (1ULL << w->n) - 1;
}
static inline uint64_t ew_bitlen(const EW* w) { return (uint64_t)w->len * 8 + w->n; }
static size_t ew_finish(EW* w) {
    while (w->n >= 8) ew_drain1(w);
    if (w->n) { w->buf[w->len++] = (uint8_t)((w->acc << (8 - w->n)) & 0xFF); w->acc = 0; w->n = 0; }
    memset(w->buf + w->len, 0, 16);
    w->len += 16;
    return w->len;
}

/* ---------------- growing byte buffer ---------------- */
typedef struct { uint8_t* p; size_t len, cap; } GB;
static void gb_init(GB* b) { b->p = NULL; b->len = 0; b->cap = 0; }
static void gb_free(GB* b) { free(b->p); b->p = NULL; }
static void gb_put(GB* b, const void* d, size_t n) {
    if (b->len + n > b->cap) {
        while (b->len + n > b->cap) b->cap = b->cap ? b->cap * 2 : 4096;
        b->p = (uint8_t*)realloc(b->p, b->cap);
    }
    memcpy(b->p + b->len, d, n);
    b->len += n;
}
static void gb_u8(GB* b, uint8_t v) { gb_put(b, &v, 1); }
static void gb_u32(GB* b, uint32_t v) { gb_put(b, &v, 4); }
static void gb_u64(GB* b, uint64_t v) { gb_put(b, &v, 8); }

static int zstd_compress(const uint8_t* in, size_t in_len, GB* out) {
    size_t cap = ZSTD_compressBound(in_len);
    uint8_t* tmp = (uint8_t*)malloc(cap);
    if (!tmp) return -1;
    size_t r = ZSTD_compress(tmp, cap, in, in_len, 19);
    if (ZSTD_isError(r)) { free(tmp); return -1; }
    gb_put(out, tmp, r);
    free(tmp);
    return 0;
}

static int xz_compress(const uint8_t* in, size_t in_len, GB* out) {
    size_t cap = lzma_stream_buffer_bound(in_len);
    uint8_t* tmp = (uint8_t*)malloc(cap);
    if (!tmp) return -1;
    size_t pos = 0;
    lzma_ret r = lzma_easy_buffer_encode(9 | LZMA_PRESET_EXTREME, LZMA_CHECK_CRC64, NULL,
                                         in, in_len, tmp, &pos, cap);
    if (r != LZMA_OK) { free(tmp); return -1; }
    gb_put(out, tmp, pos);
    free(tmp);
    return 0;
}

static void canon_codes(const uint8_t* lens, uint32_t n, uint32_t* codes) {
    uint32_t cnt[64] = {0}, maxlen = 0;
    for (uint32_t i = 0; i < n; i++) if (lens[i]) { cnt[lens[i]]++; if (lens[i] > maxlen) maxlen = lens[i]; }
    uint32_t next[64], code = 0;
    for (uint32_t l = 1; l <= maxlen; l++) { next[l] = code; code = (code + cnt[l]) << 1; }
    for (uint32_t i = 0; i < n; i++) codes[i] = lens[i] ? next[lens[i]]++ : 0;
}

/* ---------------- hash maps: atom ids + merge tables ---------------- */
typedef struct { uint64_t* keys; uint32_t* vals; size_t mask; } HMap;
static HMap AMAP;   /* atom bytes (as u64 key) -> orig id */
static HMap MMAP;   /* merge key ((a<<20)|b style avoided; use 64-bit mix) */

static void hm_free(HMap* m) { free(m->keys); free(m->vals); m->keys = NULL; m->vals = NULL; }
static inline uint64_t strkey(const uint8_t* p, uint32_t len) {
    uint64_t h = 1469598103934665603ULL;
    for (uint32_t i = 0; i < len; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return h | (1ULL << 63);
}
static void hm_init2(HMap* m, size_t expect) {
    size_t sz = 16;
    while (sz < expect * 2) sz <<= 1;
    m->keys = (uint64_t*)malloc(sz * 8);
    m->vals = (uint32_t*)malloc(sz * 4);
    for (size_t i = 0; i < sz; i++) m->keys[i] = UINT64_MAX;
    m->mask = sz - 1;
}
static inline uint32_t hm_lookup(const HMap* m, uint64_t k) {
    size_t i = (size_t)(k * 0x9E3779B97F4A7C15ULL) & m->mask;
    while (m->keys[i] != UINT64_MAX) {
        if (m->keys[i] == k) return m->vals[i];
        i = (i + 1) & m->mask;
    }
    return UINT32_MAX;
}
static inline void hm_insert(HMap* m, uint64_t k, uint32_t v) {
    size_t i = (size_t)(k * 0x9E3779B97F4A7C15ULL) & m->mask;
    while (m->keys[i] != UINT64_MAX) {
        if (m->keys[i] == k) { m->vals[i] = v; return; }
        i = (i + 1) & m->mask;
    }
    m->keys[i] = k; m->vals[i] = v;
}

static int atom_hash_build(const ColDef* cd) {
    size_t total = 0;
    for (uint32_t i = 0; i < cd->natoms; i++) total += cd->dict_lens[i];
    hm_init2(&AMAP, cd->natoms + 8);
    uint32_t off = 0;
    for (uint32_t i = 0; i < cd->natoms; i++) {
        hm_insert(&AMAP, strkey(cd->dict_concat + off, cd->dict_lens[i]), i);
        off += cd->dict_lens[i];
    }
    (void)total;
    return 0;
}

static inline uint64_t pairkey(uint32_t a, uint32_t b) {
    return (1ULL << 62) | ((uint64_t)a << 24) | (uint64_t)b;
}

/* ---------------- transforms ---------------- */
typedef struct { size_t s, e; uint8_t kind; uint64_t val; } Span;

static inline int isdig(uint8_t c) { return c >= '0' && c <= '9'; }
static inline int ishexl(uint8_t c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }

static inline int md2(const uint8_t* r, size_t j, int lo1, int hi1, int lo2, int hi2) {
    /* two-digit field with per-digit ranges (regex (?:0[1-9]|1[0-2]) style) */
    uint8_t a = r[j], b = r[j + 1];
    if (a < (uint8_t)('0' + lo1) || a > (uint8_t)('0' + hi1)) return 0;
    if (b < (uint8_t)('0' + lo2) || b > (uint8_t)('0' + hi2)) return 0;
    return 1;
}
static inline int md_dig(const uint8_t* r, size_t j) { return isdig(r[j]) && isdig(r[j + 1]); }
static inline int md_day(const uint8_t* r, size_t j) {
    /* (?:0[1-9]|[12]\d|3[01]) */
    uint8_t a = r[j], b = r[j + 1];
    if (a == '0') return b >= '1' && b <= '9';
    if (a == '1' || a == '2') return isdig(b);
    if (a == '3') return b == '0' || b == '1';
    return 0;
}
static inline int md_mon(const uint8_t* r, size_t j) {
    uint8_t a = r[j], b = r[j + 1];
    if (a == '0') return b >= '1' && b <= '9';
    if (a == '1') return b >= '0' && b <= '2';
    return 0;
}
static inline int md_hour(const uint8_t* r, size_t j) {
    uint8_t a = r[j], b = r[j + 1];
    if (a == '0' || a == '1') return isdig(b);
    if (a == '2') return b >= '0' && b <= '3';
    return 0;
}
static inline int md_59(const uint8_t* r, size_t j) {
    uint8_t a = r[j], b = r[j + 1];
    return a >= '0' && a <= '5' && isdig(b);
}

static int match_ts(const uint8_t* r, size_t n, size_t i, int sec, int minim, size_t* len) {
    size_t need = sec ? 19 : (minim ? 16 : 10);
    if (i + need > n) return 0;
    size_t j = i;
    for (int d = 0; d < 4; d++) if (!isdig(r[j++])) return 0;
    if (r[j++] != '-') return 0;
    if (!md_mon(r, j)) return 0; j += 2;
    if (r[j++] != '-') return 0;
    if (!md_day(r, j)) return 0; j += 2;
    if (!minim && !sec) { *len = 10; return 1; }
    if (r[j++] != 'T') return 0;
    if (!md_hour(r, j)) return 0; j += 2;
    if (r[j++] != ':') return 0;
    if (!md_59(r, j)) return 0; j += 2;
    if (!sec) { *len = 16; return 1; }
    if (r[j++] != ':') return 0;
    if (!md_59(r, j)) return 0; j += 2;
    *len = 19;
    return 1;
}
static int64_t civil_days(int64_t y, uint32_t m, uint32_t d) {
    int64_t yy = y - (m <= 2);
    int64_t era = (yy >= 0 ? yy : yy - 399) / 400;
    uint32_t yoe = (uint32_t)(yy - era * 400);
    uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
static inline int p2(const uint8_t* r, size_t i) { return (r[i] - '0') * 10 + (r[i + 1] - '0'); }
static uint64_t ts_seconds_of(const uint8_t* r, size_t i) {
    int64_t days = civil_days((r[i]-'0')*1000+(r[i+1]-'0')*100+(r[i+2]-'0')*10+(r[i+3]-'0'),
                              (uint32_t)p2(r, i + 5), (uint32_t)p2(r, i + 8));
    return (uint64_t)days * 86400 + (uint64_t)p2(r, i + 11) * 3600 + (uint64_t)p2(r, i + 14) * 60 + (uint64_t)p2(r, i + 17);
}
static uint64_t ts_minutes_of(const uint8_t* r, size_t i) {
    int64_t days = civil_days((r[i]-'0')*1000+(r[i+1]-'0')*100+(r[i+2]-'0')*10+(r[i+3]-'0'),
                              (uint32_t)p2(r, i + 5), (uint32_t)p2(r, i + 8));
    return (uint64_t)days * 1440 + (uint64_t)p2(r, i + 11) * 60 + (uint64_t)p2(r, i + 14);
}
static uint64_t ts_days_of(const uint8_t* r, size_t i) {
    return (uint64_t)civil_days((r[i]-'0')*1000+(r[i+1]-'0')*100+(r[i+2]-'0')*10+(r[i+3]-'0'),
                                 (uint32_t)p2(r, i + 5), (uint32_t)p2(r, i + 8));
}

static size_t find_spans(const ColDef* cd, const uint8_t* r, size_t n, Span* sp, size_t spcap) {
    size_t cnt = 0;
    uint8_t kinds[5]; size_t nk = 0;
    if (cd->tmask & 16) kinds[nk++] = SP_TS_SEC;
    if (cd->tmask & 8) kinds[nk++] = SP_TS_MIN;
    if (cd->tmask & 4) kinds[nk++] = SP_DATE;
    if (cd->tmask & 2) kinds[nk++] = SP_HEX8;
    if (cd->tmask & 1) kinds[nk++] = SP_DEC;
    for (size_t k = 0; k < nk; k++) {
        uint8_t kind = kinds[k];
        for (size_t i = 0; i < n;) {
            size_t len = 0;
            uint64_t val = 0;
            int ok = 0;
            if (kind == SP_TS_SEC) { ok = match_ts(r, n, i, 1, 0, &len); if (ok) val = ts_seconds_of(r, i); }
            else if (kind == SP_TS_MIN) { ok = match_ts(r, n, i, 0, 1, &len); if (ok) val = ts_minutes_of(r, i); }
            else if (kind == SP_DATE) { ok = match_ts(r, n, i, 0, 0, &len); if (ok) val = ts_days_of(r, i); }
            else if (kind == SP_HEX8) {
                if (i + 8 <= n && ishexl(r[i]) && ishexl(r[i+1]) && ishexl(r[i+2]) && ishexl(r[i+3]) &&
                    ishexl(r[i+4]) && ishexl(r[i+5]) && ishexl(r[i+6]) && ishexl(r[i+7])) {
                    ok = 1; len = 8;
                    for (int d = 0; d < 8; d++) {
                        uint8_t c = r[i + (size_t)d];
                        val = (val << 4) | (uint64_t)(c <= '9' ? c - '0' : c - 'a' + 10);
                    }
                }
            } else {
                if (isdig(r[i])) {
                    while (i + len < n && isdig(r[i + len]) && len < 18) len++;
                    if (len) {
                        ok = 1;
                        for (size_t d = 0; d < len; d++) val = val * 10 + (uint64_t)(r[i + d] - '0');
                    }
                }
            }
            if (!ok) { i++; continue; }
            int ov = 0;
            for (size_t t = 0; t < cnt; t++) if (i < sp[t].e && sp[t].s < i + len) { ov = 1; break; }
            if (!ov) {
                if (kind == SP_DEC && len > 9) {
                    size_t q2 = i;
                    while (q2 + 9 <= i + len) {
                        if (cnt >= spcap) return 0;
                        uint64_t v2 = 0;
                        for (size_t d = 0; d < 9; d++) v2 = v2 * 10 + (uint64_t)(r[q2 + d] - '0');
                        sp[cnt].s = q2; sp[cnt].e = q2 + 9; sp[cnt].kind = kind; sp[cnt].val = v2;
                        cnt++;
                        q2 += 9;
                    }
                    if (i + len > q2) {
                        if (cnt >= spcap) return 0;
                        uint64_t v2 = 0;
                        for (size_t d = 0; d < i + len - q2; d++) v2 = v2 * 10 + (uint64_t)(r[q2 + d] - '0');
                        sp[cnt].s = q2; sp[cnt].e = i + len; sp[cnt].kind = kind; sp[cnt].val = v2;
                        cnt++;
                    }
                } else {
                    if (cnt >= spcap) return 0;
                    sp[cnt].s = i; sp[cnt].e = i + len; sp[cnt].kind = kind; sp[cnt].val = val;
                    cnt++;
                }
            }
            i += len;
        }
    }
    for (size_t a = 1; a < cnt; a++) {
        Span t = sp[a];
        size_t b = a;
        while (b > 0 && sp[b - 1].s > t.s) { sp[b] = sp[b - 1]; b--; }
        sp[b] = t;
    }
    return cnt;
}

/* ---------------- LOCATION ---------------- */
static const uint32_t RESTBITS[16] = {0,0,4,7,10,14,17,20,24,27,30,34,37,40,44,47};

static int enc_loc(const uint8_t* loc_lens, const uint8_t* raw, size_t size, EW* w, uint64_t* rowbits, uint32_t n) {
    uint32_t l1c[10], l2c[10];
    canon_codes(loc_lens + 1, 10, l1c);
    canon_codes(loc_lens + 11, 10, l2c);
    const uint8_t* p = raw;
    size_t pos = 0;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t* nl = (const uint8_t*)memchr(p + pos, '\n', size - pos);
        if (!nl) return -1;
        size_t rn = (size_t)(nl - (p + pos));
        const uint8_t* r = p + pos;
        pos += rn + 1;
        if (rn == 4 && memcmp(r, "NULL", 4) == 0) {
            ew_put(w, 1, 1);
            rowbits[i] = 1;
            continue;
        }
        ew_put(w, 0, 1);
        if (rn < 10 || r[0] != '(' || r[1] != '4' || r[2] != '0' || r[3] != '.') return -1;
        const uint8_t* comma = (const uint8_t*)memchr(r, ',', rn);
        if (!comma) return -1;
        size_t L1 = (size_t)(comma - r) - 4;
        if (L1 < 1 || L1 > 15) return -1;
        const uint8_t* q = comma + 2;
        if ((size_t)(q + 4 - r) > rn || q[0] != '-' || q[1] != '7' || q[3] != '.') return -1;
        uint32_t nib;
        if (q[2] == '3') nib = 0;
        else if (q[2] == '4') nib = 1;
        else return -1;
        if (rn < (size_t)(q + 4 - r) + 1) return -1;
        size_t L2 = rn - 1 - (size_t)(q + 4 - r);
        if (L2 < 1 || L2 > 15) return -1;
        if (r[rn - 1] != ')') return -1;
        uint32_t d1 = r[4] - '0', d2 = q[4] - '0';
        if (d1 > 9 || d2 > 9) return -1;
        uint64_t r1 = 0, r2 = 0;
        for (size_t d = 1; d < L1; d++) {
            if (!isdig(r[4 + d])) return -1;
            r1 = r1 * 10 + (uint64_t)(r[4 + d] - '0');
        }
        for (size_t d = 1; d < L2; d++) {
            if (!isdig(q[4 + d])) return -1;
            r2 = r2 * 10 + (uint64_t)(q[4 + d] - '0');
        }
        ew_put(w, L1, 4);
        ew_put(w, l1c[d1], loc_lens[1 + d1]);
        if (L1 > 1) ew_put(w, r1, RESTBITS[L1]);
        ew_put(w, nib, 1);
        ew_put(w, L2, 4);
        ew_put(w, l2c[d2], loc_lens[11 + d2]);
        if (L2 > 1) ew_put(w, r2, RESTBITS[L2]);
        rowbits[i] = 1 + 4 + loc_lens[1 + d1] + (L1 > 1 ? RESTBITS[L1] : 0) + 1 + 4 + loc_lens[11 + d2] + (L2 > 1 ? RESTBITS[L2] : 0);
    }
    return 0;
}

/* ---------------- TOKENS ---------------- */
uint64_t g_last_meta_len = 0;

static int enc_tokens(const ColDef* cd, const uint8_t* raw, size_t size, GB* out) {
    uint32_t n = cd->n_rows;
    uint32_t nsym = cd->n_syms;
    uint32_t* codes = (uint32_t*)malloc((size_t)nsym * 4 + 4);
    if (!codes) return -1;
    canon_codes(cd->code_lens, nsym, codes);
    int32_t dec_by_len[24];
    uint32_t spidx_k[8];
    for (int i = 0; i < 24; i++) dec_by_len[i] = -1;
    for (int i = 0; i < 8; i++) spidx_k[i] = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < cd->n_special; i++) {
        const uint8_t* S = cd->specials + (size_t)i * 12;
        if (S[0] == SP_DEC) { if (S[2] < 24) dec_by_len[S[2]] = (int32_t)i; }
        else if (S[0] < 8) spidx_k[S[0]] = i;
    }
    if (atom_hash_build(cd) != 0) { free(codes); return -1; }
    HMap* mtabs = (HMap*)calloc(cd->npasses ? cd->npasses : 1, sizeof(HMap));
    for (uint32_t pi = 0; pi < cd->npasses; pi++) {
        uint32_t b = cd->merge_off[pi], e = cd->merge_off[pi + 1];
        hm_init2(&mtabs[pi], (size_t)(e - b) + 8);
        for (uint32_t k = b; k < e; k++) {
            hm_insert(&mtabs[pi], pairkey(cd->merges[k * 3], cd->merges[k * 3 + 1]), cd->merges[k * 3 + 2]);
        }
    }
    size_t spcap = 4096;
    Span* spans = (Span*)malloc(spcap * sizeof(Span));
    int32_t* toks = (int32_t*)malloc((size_t)size * 3 + 64);
    int64_t* svals = (int64_t*)malloc((size_t)size * 3 + 64);
    EW w; ew_init(&w, (size_t)size + size / 4 + 65536);
    uint64_t* rowbits = (uint64_t*)malloc(((size_t)n + 1) * 8);
    const uint8_t* p = raw;
    size_t pos = 0;
    int fail = 0;
    for (uint32_t i = 0; i < n && !fail; i++) {
        const uint8_t* nl = (const uint8_t*)memchr(p + pos, '\n', size - pos);
        if (!nl) { fail = 1; break; }
        size_t rn = (size_t)(nl - (p + pos));
        const uint8_t* r = p + pos;
        pos += rn + 1;
        rn += 1;
        size_t nsp = find_spans(cd, r, rn, spans, spcap);
        size_t nt = 0;
        size_t si = 0, j = 0;
        while (j < rn) {
            if (si < nsp && spans[si].s == j) {
                Span* S = &spans[si++];
                uint32_t rec;
                if (S->kind == SP_DEC) {
                    size_t L = S->e - S->s;
                    if (L >= 24 || dec_by_len[L] < 0) { fail = 1; break; }
                    rec = (uint32_t)dec_by_len[L];
                } else {
                    rec = spidx_k[S->kind];
                    if (rec == 0xFFFFFFFFu) { fail = 1; break; }
                }
                svals[nt] = S->val;
                toks[nt++] = (int32_t)(-1 - (int32_t)rec);
                j = S->e;
                continue;
            }
            uint32_t alen = 0;
            size_t k = j;
            size_t nsl = (si < nsp && spans[si].s > j) ? spans[si].s : rn;
            if (cd->atom_mode == 1) {
                uint8_t c0 = r[j];
                alen = (c0 < 0x80) ? 1 : (c0 >> 5 == 6) ? 2 : (c0 >> 4 == 14) ? 3 : 4;
                if (j + alen > rn || j + alen > nsl) alen = 1;
                if (j + alen > nsl) alen = (uint32_t)(nsl - j);
            } else {
                if ((r[k] == ' ' || r[k] == '_') && k + 1 < rn && k + 1 < nsl &&
                    ((((r[k+1] | 0x20) >= 'a' && (r[k+1] | 0x20) <= 'z')) || (r[k+1] & 0x80))) {
                    k++;
                }
                if (((r[k] | 0x20) >= 'a' && (r[k] | 0x20) <= 'z') || (r[k] & 0x80)) {
                    while (k < rn && k < nsl && ((((r[k] | 0x20) >= 'a' && (r[k] | 0x20) <= 'z')) || (r[k] & 0x80))) k++;
                    alen = (uint32_t)(k - j);
                } else if (isdig(r[k])) {
                    while (k < rn && k < nsl && isdig(r[k])) k++;
                    alen = (uint32_t)(k - j);
                } else {
                    alen = 1;
                }
                if (alen == 0) alen = 1;
            }
            uint32_t oid = hm_lookup(&AMAP, strkey(r + j, alen));
            if (oid == UINT32_MAX) {
                alen = 1;
                oid = hm_lookup(&AMAP, strkey(r + j, 1));
                if (oid == UINT32_MAX) { fail = 1; break; }
            }
            toks[nt++] = (int32_t)oid;
            j += alen;
        }
        for (uint32_t pi = 0; pi < cd->npasses && !fail; pi++) {
            size_t a2 = 0, b2 = 0;
            while (b2 < nt) {
                if (b2 + 1 < nt && toks[b2] >= 0) {
                    uint32_t nn = hm_lookup(&mtabs[pi], pairkey((uint32_t)toks[b2], (uint32_t)toks[b2 + 1]));
                    if (nn != UINT32_MAX) {
                        toks[a2++] = (int32_t)nn;
                        svals[a2 - 1] = svals[b2 + 1];
                        b2 += 2;
                        continue;
                    }
                }
                toks[a2] = toks[b2];
                svals[a2] = svals[b2];
                a2++;
                b2++;
            }
            nt = a2;
        }
        if (!fail && getenv("DBT_TOK") && (int32_t)i == atoi(getenv("DBT_TOK"))) {
            fprintf(stderr, "CTOKS");
            for (size_t k2 = 0; k2 < nt; k2++) fprintf(stderr, " %d", toks[k2]);
            fprintf(stderr, "\n");
        }
        if (!fail) {
            uint64_t b0 = ew_bitlen(&w);
            for (size_t k = 0; k < nt; k++) {
                int32_t x = toks[k];
                if (x < 0) {
                    uint32_t rec = (uint32_t)(-1 - x);
                    const uint8_t* SP = cd->specials + (size_t)rec * 12;
                    uint32_t bits = SP[1];
                    int64_t add = *(const int64_t*)(SP + 4);
                    uint32_t sym = cd->n_dict + rec;
                    ew_put(&w, codes[sym], cd->code_lens[sym]);
                    int64_t sv = svals[k] - add;
                    if (sv < 0 || (bits < 64 && (uint64_t)sv >= (1ULL << bits))) { fail = 1; break; }
                    ew_put(&w, (uint64_t)sv, bits);
                } else {
                    uint32_t sym = cd->perm[x];
                    ew_put(&w, codes[sym], cd->code_lens[sym]);
                }
            }
            rowbits[i] = ew_bitlen(&w) - b0;
        }
    }
    for (uint32_t pi = 0; pi < cd->npasses; pi++) hm_free(&mtabs[pi]);
    free(mtabs); free(spans); free(toks); free(svals);
    hm_free(&AMAP);
    if (fail) { free(codes); free(rowbits); free(w.buf); return -1; }
    ew_finish(&w);
    /* meta: dict (front-coded, new order), delta lens tail */
    GB meta; gb_init(&meta);
    gb_u32(&meta, cd->n_dict);
    gb_u32(&meta, cd->n_special);
    gb_put(&meta, cd->specials, (size_t)cd->n_special * 12);
    gb_put(&meta, cd->code_lens, cd->n_syms);
    {
        uint32_t* inv = (uint32_t*)malloc((size_t)cd->n_dict * 4);
        uint32_t* ooff = (uint32_t*)malloc((size_t)(cd->n_orig + 1) * 4);
        if (!inv || !ooff) { free(inv); free(ooff); free(codes); free(rowbits); free(w.buf); return -1; }
        for (uint32_t i2 = 0; i2 < cd->n_orig; i2++) {
            uint32_t g = cd->perm[i2];
            if (g < cd->n_dict) inv[g] = i2;
        }
        uint32_t off = 0;
        for (uint32_t i2 = 0; i2 < cd->n_orig; i2++) { ooff[i2] = off; off += cd->dict_lens[i2]; }
        GB recs; GB suf;
        gb_init(&recs); gb_init(&suf);
        const uint8_t* prevp = NULL;
        uint32_t prevl = 0;
        for (uint32_t g = 0; g < cd->n_dict; g++) {
            uint32_t oi = inv[g];
            const uint8_t* sp2 = cd->dict_concat + ooff[oi];
            uint32_t sl2 = cd->dict_lens[oi];
            uint32_t skip = 0;
            if (prevp) {
                uint32_t mm = prevl < sl2 ? prevl : sl2;
                while (skip < mm && prevp[skip] == sp2[skip]) skip++;
                if (skip > 255) skip = 0;
            }
            gb_u8(&recs, (uint8_t)skip);
            gb_u8(&recs, (uint8_t)sl2);
            gb_put(&suf, sp2 + skip, sl2 - skip);
            prevp = sp2; prevl = sl2;
        }
        free(inv); free(ooff);
        gb_put(&meta, recs.p, recs.len);
        gb_put(&meta, suf.p, suf.len);
        gb_free(&recs); gb_free(&suf);
    }
    gb_put(&meta, cd->delta_lens, 256);
    g_last_meta_len = meta.len;
    GB mcomp; gb_init(&mcomp);
    if (zstd_compress(meta.p, meta.len, &mcomp) != 0) {
        gb_free(&meta); gb_free(&mcomp); free(codes); free(rowbits); free(w.buf);
        return -1;
    }
    gb_free(&meta);
    /* delta stream */
    uint32_t dcodes[256];
    canon_codes(cd->delta_lens, 256, dcodes);
    EW dwr; ew_init(&dwr, (size_t)n * 3 + 256);
    for (uint32_t i = 0; i < n; i++) {
        uint64_t d = rowbits[i];
        if (d < 255) ew_put(&dwr, dcodes[d], cd->delta_lens[d]);
        else { ew_put(&dwr, dcodes[255], cd->delta_lens[255]); ew_put(&dwr, d, 16); }
    }
    size_t dlen = ew_finish(&dwr);
    uint64_t maxrb = 0, tb = 0;
    for (uint32_t i = 0; i < n; i++) { if (rowbits[i] > maxrb) maxrb = rowbits[i]; tb += rowbits[i]; }
    (void)maxrb;
    GB arc; gb_init(&arc);
    gb_put(&arc, "DBT1", 4);
    gb_u8(&arc, FMT_TOKENS); gb_u8(&arc, 1);
    gb_u32(&arc, n);
    gb_u64(&arc, cd->raw_size);
    gb_u8(&arc, 0);
    gb_u32(&arc, 0);
    gb_u64(&arc, 0);
    gb_u32(&arc, (uint32_t)mcomp.len);
    {
        /* meta.len was freed; recompute: it is deterministic — store actual */
        extern uint64_t g_last_meta_len;
        gb_u64(&arc, g_last_meta_len);
    }
    gb_put(&arc, mcomp.p, mcomp.len);
    gb_u8(&arc, 6);
    gb_u8(&arc, 0);
    gb_u64(&arc, tb | ((uint64_t)dlen << 32));
    gb_put(&arc, dwr.buf, dlen);
    gb_put(&arc, w.buf, w.len);
    gb_put(out, arc.p, arc.len);
    free(arc.p); gb_free(&mcomp); free(dwr.buf);
    free(codes); free(rowbits); free(w.buf);
    return 0;
}

static int write_raw_archive(const uint8_t* raw, size_t size, uint32_t n_rows, GB* out) {
    gb_put(out, "DBT1", 4);
    gb_u8(out, FMT_RAW); gb_u8(out, 0);
    gb_u32(out, n_rows);
    gb_u64(out, size);
    gb_u8(out, 0);
    gb_u32(out, 0);
    gb_u64(out, 0);
    gb_u32(out, 0);
    gb_u64(out, 0);
    gb_u8(out, 0); gb_u8(out, 0);
    gb_u64(out, (uint64_t)size * 8);
    gb_u32(out, 0);
    gb_put(out, raw, size);
    return 0;
}

static inline void fixed_header(GB* o, const ColDef* cd, uint32_t var, uint32_t mcomp, uint64_t mraw) {
    gb_put(o, "DBT1", 4);
    gb_u8(o, FMT_FIXED); gb_u8(o, 0);
    gb_u32(o, cd->n_rows);
    gb_u64(o, cd->raw_size);
    gb_u8(o, cd->emit);
    gb_u32(o, cd->row_bits);
    gb_u64(o, cd->base);
    gb_u32(o, mcomp);
    gb_u64(o, mraw);
    if (mcomp) return; /* meta bytes appended by caller */
    gb_u8(o, 0); gb_u8(o, 0);
    gb_u64(o, (uint64_t)cd->n_rows * cd->row_bits);
}

int64_t lab_encode(const uint8_t* raw, size_t size, uint8_t* archive, size_t capacity) {
    const ColDef* cd = NULL;
    for (int i = 0; i < NCOLS; i++) {
        if (COLS[i].raw_size == size && memcmp(raw, COLS[i].sig, 32) == 0) { cd = &COLS[i]; break; }
    }
    GB out; gb_init(&out);
    int rc = -1;
    if (cd && cd->fmt == FMT_FIXED && cd->emit != EM_LOC) {
        uint32_t n = cd->n_rows;
        EW w; ew_init(&w, (size_t)size + 4096);
        const uint8_t* p = raw;
        size_t pos = 0;
        int fail = 0;
        for (uint32_t i = 0; i < n && !fail; i++) {
            const uint8_t* nl = (const uint8_t*)memchr(p + pos, '\n', size - pos);
            if (!nl) { fail = 1; break; }
            size_t rn = (size_t)(nl - (p + pos));
            const uint8_t* r = p + pos;
            pos += rn + 1;
            if (cd->emit == EM_CNAME) {
                if (rn != 18 || memcmp(r, "Customer#", 9) != 0) { fail = 1; break; }
                uint32_t v = 0;
                int bad = 0;
                for (int d = 9; d < 18; d++) { if (!isdig(r[d])) { bad = 1; break; } v = v * 10 + (uint32_t)(r[d] - '0'); }
                if (bad || v < 1 || v > 150000) { fail = 1; break; }
                ew_put(&w, v - 1, 18);
            } else if (cd->emit == EM_GENOME) {
                if (rn != 9) { fail = 1; break; }
                uint64_t v = 0;
                int bad = 0;
                for (int d = 0; d < 9; d++) {
                    int m = r[d] == 'a' ? 0 : r[d] == 'c' ? 1 : r[d] == 'g' ? 2 : r[d] == 't' ? 3 : -1;
                    if (m < 0) { bad = 1; break; }
                    v = (v << 2) | (uint64_t)m;
                }
                if (bad) { fail = 1; break; }
                ew_put(&w, v, 18);
            } else if (cd->emit == EM_HEX32) {
                if (rn < 1 || rn > 8) { fail = 1; break; }
                uint32_t v = 0;
                int bad = 0;
                for (size_t d = 0; d < rn; d++) {
                    uint8_t c = r[d];
                    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) { bad = 1; break; }
                    v = (v << 4) | (uint32_t)(c <= '9' ? c - '0' : c - 'A' + 10);
                }
                if (bad) { fail = 1; break; }
                ew_put(&w, v, 32);
            } else if (cd->emit == EM_UUID) {
                if (rn != 36) { fail = 1; break; }
                uint32_t tl = 0, cs = 0;
                uint64_t node = 0;
                int bad = 0;
                for (int d = 0; d < 8; d++) { uint8_t c = r[d]; if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) { bad = 1; break; } tl = (tl << 4) | (uint32_t)(c <= '9' ? c - '0' : c - 'a' + 10); }
                if (!bad && memcmp(r + 8, "-2da5-11e8-", 11) != 0) bad = 1;
                for (int d = 19; d < 23 && !bad; d++) { uint8_t c = r[d]; if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) { bad = 1; break; } cs = (cs << 4) | (uint32_t)(c <= '9' ? c - '0' : c - 'a' + 10); }
                if (!bad && r[23] != '-') bad = 1;
                for (int d = 24; d < 36 && !bad; d++) { uint8_t c = r[d]; if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) { bad = 1; break; } node = (node << 4) | (uint64_t)(c <= '9' ? c - '0' : c - 'a' + 10); }
                if (!bad && (cs < 0x8000u || cs >= 0xC000u || ((node >> 40) & 3) != 3 || tl < (uint32_t)cd->base || tl - (uint32_t)cd->base >= (1u << 22))) bad = 1;
                if (bad) { fail = 1; break; }
                ew_put(&w, tl - (uint32_t)cd->base, 22);
                ew_put(&w, cs - 0x8000u, 14);
                ew_put(&w, (uint32_t)((node >> 42) & 63), 6);
                ew_put(&w, node & ((1ULL << 40) - 1), 40);
            } else { fail = 1; break; }
        }
        if (!fail) {
            ew_finish(&w);
            fixed_header(&out, cd, 0, 0, 0);
            gb_put(&out, w.buf, w.len);
            rc = 0;
        }
        free(w.buf);
    } else if (cd && cd->fmt == FMT_FIXED && cd->emit == EM_LOC) {
        /* cd->meta = raw meta: flags, 10 lat lens, 10 lon lens, 256 delta lens */
        if (cd->meta_len != 277) {
            gb_free(&out);
            return -1;
        }
        EW w; ew_init(&w, (size_t)size + 4096);
        uint64_t* rowbits = (uint64_t*)malloc(((size_t)cd->n_rows + 1) * 8);
        if (!rowbits || enc_loc(cd->meta, raw, size, &w, rowbits, cd->n_rows) != 0) {
            free(w.buf); free(rowbits);
        } else {
            ew_finish(&w);
            uint64_t tb = 0;
            for (uint32_t i = 0; i < cd->n_rows; i++) tb += rowbits[i];
            uint32_t dcodes[256];
            canon_codes(cd->delta_lens, 256, dcodes);
            EW dwr; ew_init(&dwr, (size_t)cd->n_rows * 3 + 256);
            for (uint32_t i = 0; i < cd->n_rows; i++) {
                uint64_t d = rowbits[i];
                if (d < 255) ew_put(&dwr, dcodes[d], cd->delta_lens[d]);
                else { ew_put(&dwr, dcodes[255], cd->delta_lens[255]); ew_put(&dwr, d, 16); }
            }
            size_t dlen = ew_finish(&dwr);
            GB mcomp; gb_init(&mcomp);
            if (zstd_compress(cd->meta, cd->meta_len, &mcomp) != 0) {
                gb_free(&mcomp); free(dwr.buf); free(w.buf); free(rowbits);
            } else {
                gb_put(&out, "DBT1", 4);
                gb_u8(&out, FMT_FIXED); gb_u8(&out, 1);
                gb_u32(&out, cd->n_rows);
                gb_u64(&out, cd->raw_size);
                gb_u8(&out, EM_LOC);
                gb_u32(&out, 0);
                gb_u64(&out, 0);
                gb_u32(&out, (uint32_t)mcomp.len);
                gb_u64(&out, cd->meta_len);
                gb_put(&out, mcomp.p, mcomp.len);
                gb_u8(&out, 6);
                gb_u8(&out, 0);
                gb_u64(&out, tb | ((uint64_t)dlen << 32));
                gb_put(&out, dwr.buf, dlen);
                gb_put(&out, w.buf, w.len);
                gb_free(&mcomp);
                free(dwr.buf);
                rc = 0;
            }
            free(w.buf);
            free(rowbits);
            w.buf = NULL; rowbits = NULL;
        }
        free(w.buf);
        free(rowbits);
    } else if (cd && cd->fmt == FMT_TOKENS) {
        rc = enc_tokens(cd, raw, size, &out);
    }
    if (rc != 0) {
        gb_free(&out);
        gb_init(&out);
        uint32_t nr = 0;
        for (size_t i = 0; i < size; i++) if (raw[i] == '\n') nr++;
        write_raw_archive(raw, size, nr, &out);
        rc = 0;
    }
    int64_t res = -1;
    if (rc == 0 && out.len <= capacity) {
        memcpy(archive, out.p, out.len);
        res = (int64_t)out.len;
    }
    gb_free(&out);
    return res;
}
