#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if __has_include("/source/interface/codec.h")
#include "/source/interface/codec.h"
#else
#include "codec.h"
#endif
#include "common.h"

static uint8_t* g_pool = NULL; static size_t g_pool_cap = 0, g_pool_used = 0;
static size_t zdd(void* dst, size_t cap, const void* src, size_t n){
  static ZSTD_DCtx* dc = 0;
  if (!dc) dc = ZSTD_createDCtx();
  return ZSTD_decompressDCtx(dc, dst, cap, src, n);
}

__attribute__((visibility("default")))
void* lab_open(const uint8_t* archive, size_t size){
  const uint8_t* p = archive;
  if (size < 113) return NULL;
  if (get_u32(&p) != YLP_MAGIC) return NULL;

  g_pool_used = 0;
  Dec2* d = (Dec2*)calloc(1, sizeof(Dec2));
  if (!d) return NULL;
  uint32_t N = get_u32(&p);
  uint64_t orig = get_u64(&p);
  uint8_t final_nl = *p++;
  uint32_t n_name=get_u32(&p), n_addr=get_u32(&p), n_city=get_u32(&p), n_state=get_u32(&p), n_postal=get_u32(&p);
  uint32_t n_tok=get_u32(&p), n_ap=get_u32(&p), n_hp=get_u32(&p), n_star=get_u32(&p);
  uint32_t ids_len=get_u32(&p);
  uint32_t name_z=get_u32(&p), name_r=get_u32(&p);
  uint32_t addr_z=get_u32(&p), addr_r=get_u32(&p);
  uint32_t small_z=get_u32(&p), small_r=get_u32(&p);
  uint32_t row_z=get_u32(&p), row_r=get_u32(&p);
  uint32_t rc_len=get_u32(&p), cat_len=get_u32(&p), attr_len=get_u32(&p);
  uint32_t hour_len=get_u32(&p), lat_len=get_u32(&p), lon_len=get_u32(&p);
  uint32_t row_crc=get_u32(&p);
  /* orig is verified after decode; capacity below it must fail before any write */
  if (!N || (uint64_t)ids_len + (uint64_t)name_z + (uint64_t)addr_z + (uint64_t)small_z + (uint64_t)row_z + 117ull > size){ free(d); return NULL; }
  if ((uint64_t)row_r < 15ull*(uint64_t)N + (uint64_t)rc_len + (uint64_t)cat_len + (uint64_t)attr_len + (uint64_t)hour_len + (uint64_t)lat_len + (uint64_t)lon_len){ free(d); return NULL; }

  size_t tot_est = (size_t)n_name+n_addr+n_city+n_state+n_postal+n_tok+n_ap+n_hp+n_star;
  size_t need = (size_t)name_r+256 + (size_t)addr_r+256 + (size_t)small_r+256 + (size_t)row_r+256 + tot_est*sizeof(sref) + 64;
  if (need > (1ull<<30)){ free(d); return NULL; }
  if (g_pool_cap < need){
    size_t nc = g_pool_cap ? g_pool_cap : (1u<<20);
    while (nc < need) nc *= 2;
    uint8_t* np = (uint8_t*)realloc(g_pool, nc);
    if (!np){ free(d); return NULL; }   /* keep old pool intact */
    g_pool = np; g_pool_cap = nc;
  }
  uint8_t* base = g_pool; g_pool_used = need;
  uint8_t* b_name = base;
  uint8_t* b_addr = base + (size_t)name_r+256;
  uint8_t* b_small = b_addr + (size_t)addr_r+256;
  uint8_t* b_row = b_small + (size_t)small_r+256;
  sref* all = (sref*)(b_row + (size_t)row_r+256);

  const uint8_t* ids = p; p += ids_len;
  if (ZSTD_isError(zdd(b_name, name_r, p, name_z))) { lab_close(d); return NULL; } p += name_z;
  if (ZSTD_isError(zdd(b_addr, addr_r, p, addr_z))) { lab_close(d); return NULL; } p += addr_z;
  if (ZSTD_isError(zdd(b_small, small_r, p, small_z))) { lab_close(d); return NULL; } p += small_z;
  { int zrr = LZ4_decompress_safe((const char*)p, (char*)b_row, (int)row_z, (int)row_r);
    if (zrr < 0 || (uint32_t)zrr != row_r) { lab_close(d); return NULL; }
    if (fnv1a(b_row, row_r) != row_crc) { lab_close(d); return NULL; } }
  uint32_t cnt[9]; cnt[0]=n_name;cnt[1]=n_addr;cnt[2]=n_city;cnt[3]=n_state;cnt[4]=n_postal;cnt[5]=n_tok;cnt[6]=n_ap;cnt[7]=n_hp;cnt[8]=n_star;
  sref* tabs[9];
  { size_t off=0; for (int k=0;k<9;k++){ tabs[k]=all+off; off+=cnt[k]; } }

  if (!build_sref(b_name,name_r,n_name,tabs[0],0)) { lab_close(d); return NULL; }
  if (!build_sref(b_addr,addr_r,n_addr,tabs[1],0)) { lab_close(d); return NULL; }
  { const uint8_t* q = b_small; size_t rem = small_r; size_t L[7];
    for (int k=0;k<7;k++){
      size_t len = take_blob(&q, rem, cnt[2+k]);
      if (len==(size_t)-1){ lab_close(d); return NULL; }
      L[k]=len; rem -= len + ((k<6)?1:0);
    }
    const uint8_t* s = b_small;
    size_t B[7]; B[0]=0; for (int k=1;k<7;k++) B[k]=B[k-1]+L[k-1]+1;
    if (!build_sref(s+B[0], L[0], n_city, tabs[2], 0)) { lab_close(d); return NULL; }
    if (!build_sref(s+B[1], L[1], n_state, tabs[3], B[1])) { lab_close(d); return NULL; }
    if (!build_sref(s+B[2], L[2], n_postal, tabs[4], B[2])) { lab_close(d); return NULL; }
    if (!build_sref(s+B[3], L[3], n_tok, tabs[5], B[3])) { lab_close(d); return NULL; }
    if (!build_sref(s+B[4], L[4], n_ap, tabs[6], B[4])) { lab_close(d); return NULL; }
    if (!build_sref(s+B[5], L[5], n_hp, tabs[7], B[5])) { lab_close(d); return NULL; }
    if (!build_sref(s+B[6], L[6], n_star, tabs[8], B[6])) { lab_close(d); return NULL; }
  }

  for (size_t q=0;q<(size_t)n_name+n_addr+n_city+n_state+n_postal+n_tok+n_ap+n_hp+n_star;q++) if (all[q].len > 4096u){ lab_close(d); return NULL; }
  d->name_blob=b_name; d->name=tabs[0]; d->addr_blob=b_addr; d->addr=tabs[1]; d->small=b_small;
  d->city=tabs[2]; d->state=tabs[3]; d->postal=tabs[4]; d->tok=tabs[5]; d->ap=tabs[6]; d->hp=tabs[7]; d->star=tabs[8];
  d->ids=ids; d->name_idx=b_row; d->addr_idx=b_row+4*(size_t)N; d->city_idx=b_row+8*(size_t)N;
  d->postal_idx=b_row+10*(size_t)N; d->state_idx=b_row+12*(size_t)N; d->star_idx=b_row+13*(size_t)N; d->iso_idx=b_row+14*(size_t)N;
  { size_t off=15*(size_t)N;
    d->rc0=b_row+off; off+=rc_len; d->cat0=b_row+off; off+=cat_len;
    d->attr0=b_row+off; off+=attr_len; d->hour0=b_row+off; off+=hour_len;
    d->lat0=b_row+off; off+=lat_len; d->lon0=b_row+off; off+=lon_len; }

  d->orig=orig;
  d->nrows=N; d->final_nl=final_nl;
  d->n_name=n_name; d->n_addr=n_addr; d->n_city=n_city; d->n_state=n_state; d->n_postal=n_postal;
  d->n_tok=n_tok; d->n_ap=n_ap; d->n_hp=n_hp; d->n_star=n_star;
  return d;
}

__attribute__((visibility("default")))
int64_t lab_decode(void* state, uint8_t* output, size_t capacity){
  Dec2* d = (Dec2*)state;
  if (!d) return -1;
  if (capacity < d->orig) return -1;   /* insufficient capacity: fail before any write */
  EmSt S; S.rc=d->rc0; S.cat=d->cat0; S.attr=d->attr0; S.hour=d->hour0; S.lat=d->lat0; S.lon=d->lon0;
  S.o = (char*)output; S.obase = (char*)output; S.ocap = capacity;
  uint32_t N = d->nrows;
  for (uint32_t i=0;i<N;i++){
    S = emit3(S, d, i, i+1==N);
  }
  size_t len = (size_t)(S.o - (char*)output);
  if (len != (size_t)d->orig || len > capacity) return -1;   /* guard-bail truncation is failure */
  return (int64_t)len;
}

__attribute__((visibility("default")))
int64_t lab_rows(void* state, const uint64_t* ids, size_t count, uint8_t* output, size_t capacity, uint64_t* offsets){
  (void)state;(void)ids;(void)count;(void)output;(void)capacity;(void)offsets;
  return -1;
}

__attribute__((visibility("default")))
void lab_close(void* state){
  Dec2* d = (Dec2*)state;
  if (!d) return;
  free(d); /* pool retained for reuse across opens */
}
