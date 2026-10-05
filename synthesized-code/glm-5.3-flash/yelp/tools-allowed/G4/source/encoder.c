#define _GNU_SOURCE
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if __has_include("/source/interface/codec.h")
#include "/source/interface/codec.h"
#else
#include "codec.h"
#endif
#include "common.h"

#define LEVEL 1

typedef struct { uint8_t* buf; size_t n, cap; } vec8;
static void vpush(vec8* v, const void* p, size_t len){
  if (v->n + len > v->cap){ size_t c = v->cap ? v->cap*2 : 4096; while (c < v->n+len) c*=2; v->buf = realloc(v->buf, c); v->cap = c; }
  memcpy(v->buf + v->n, p, len); v->n += len;
}

typedef struct { const char* p; uint32_t len; uint32_t id; } sentry;
typedef struct { uint32_t mask; int32_t* slot; sentry* items; uint32_t n, cap; } smap;
static uint64_t fnv(const char* p, uint32_t len){
  uint64_t h = 1469598103934665603ull;
  for (uint32_t i=0;i<len;i++){ h ^= (uint8_t)p[i]; h *= 1099511628211ull; }
  return h;
}
static void sm_init(smap* m){ m->mask=1023; m->slot=calloc(1024,4); m->items=malloc(64*sizeof(sentry)); m->n=0; m->cap=64; }
static void sm_grow(smap* m){
  free(m->slot); m->mask = m->mask*2+1; m->slot = calloc((size_t)m->mask+1,4);
  for (uint32_t i=0;i<m->n;i++){ uint64_t h=fnv(m->items[i].p,m->items[i].len); uint32_t j=(uint32_t)h&m->mask;
    while (m->slot[j]) j=(j+1)&m->mask; m->slot[j]=(int32_t)i+1; }
}
static uint32_t sm_get(smap* m, const char* p, uint32_t len, int* isnew){
  uint64_t h=fnv(p,len); uint32_t j=(uint32_t)h&m->mask;
  while (m->slot[j]){ sentry* e=&m->items[m->slot[j]-1];
    if (e->len==len && memcmp(e->p,p,len)==0){ *isnew=0; return e->id; } j=(j+1)&m->mask; }
  if (m->n+1 > (m->mask+1)*7/10) { sm_grow(m); j=(uint32_t)h&m->mask; while (m->slot[j]) j=(j+1)&m->mask; }
  if (m->n==m->cap){ m->cap*=2; m->items=realloc(m->items,m->cap*sizeof(sentry)); }
  m->items[m->n].p=p; m->items[m->n].len=len; m->items[m->n].id=m->n;
  m->slot[j]=(int32_t)m->n+1; *isnew=1; return m->n++;
}

static const char* KEYSTR[13] = { "business_id","name","address","city","state",
  "postal_code","latitude","longitude","stars","review_count","is_open","attributes","hours" };
static char MBUF[14][24]; static uint32_t MLEN[14];
static void init_markers(void){
  for (int i=0;i<14;i++){
    const char* k = (i<13) ? KEYSTR[i] : "categories";
    char* o = MBUF[i];
    if (i==0) *o++ = 0x7b;
    else if (i<=6) { *o++ = 0x22; *o++ = 0x2c; }
    else *o++ = 0x2c;
    *o++ = 0x22;
    while (*k) *o++ = *k++;
    *o++ = 0x22; *o++ = 0x3a;
    if (i<=5) *o++ = 0x22;
    MLEN[i] = (uint32_t)(o - MBUF[i]);
  }
}
static inline const uint8_t* mfind(const uint8_t* h, size_t hl, size_t from, int mi){
  if (from > hl) return NULL;
  return (const uint8_t*)memmem(h+from, hl-from, MBUF[mi], MLEN[mi]);
}

static int cmp_sent(const void* a, const void* b){
  const sentry* x=a; const sentry* y=b;
  uint32_t m = x->len<y->len?x->len:y->len;
  int c = memcmp(x->p,y->p,m);
  if (c) return c;
  return (x->len<y->len)?-1:(x->len>y->len)?1:0;
}

static int obj_end(const uint8_t* l, size_t s, size_t llen, size_t* out){
  size_t i=s+1; int instr=0;
  while (i<llen){
    uint8_t c=l[i];
    if (instr){ if (c==0x5c){ i+=2; continue; } if (c==0x22) instr=0; }
    else { if (c==0x22) instr=1; else if (c==0x7d){ *out=i+1; return 1; } }
    i++;
  }
  return 0;
}

static int g_fuse = 0;
static void blob_from_order(smap* m, const uint32_t* order, uint8_t** out, size_t* outlen, uint32_t* remap){
  size_t tot=0;
  for (uint32_t i=0;i<m->n;i++) tot += m->items[order[i]].len + (g_fuse ? g_fuse + 1 : 1);
  if (tot) tot--;
  uint8_t* b = malloc(tot?tot:1); size_t off=0;
  for (uint32_t i=0;i<m->n;i++){
    sentry* e=&m->items[order[i]];
    if (e->len) memcpy(b+off,e->p,e->len); off+=e->len;
    if (g_fuse==1) b[off++]=0x2c;
    else if (g_fuse==2) { b[off++]=0x2c; b[off++]=0x20; }
    if (i+1<m->n) b[off++]=0x0a;
    remap[order[i]] = i;
  }
  *out=b; *outlen=off;
}

typedef struct { size_t start, len; } range;

__attribute__((visibility("default")))
int64_t lab_encode(const uint8_t* raw, size_t size, uint8_t* archive, size_t capacity){
  init_markers();
  if (size == 0) return -1;
  int final_nl = (raw[size-1] == 0x0a);
  size_t N = 0;
  for (size_t i=0;i<size;i++) if (raw[i]==0x0a) N++;
  if (!final_nl) N++;
  if (N == 0 || N > 0xffffffull) return -1;

  size_t* loff = malloc((N+1)*sizeof(size_t));
  { size_t k=0; loff[k++]=0;
    for (size_t i=0;i<size;i++) if (raw[i]==0x0a) loff[k++]=i+1;
    if (!final_nl) loff[N]=size; }
  size_t maxline=0;
  for (size_t i=0;i<N;i++){ size_t ln = loff[i+1]-loff[i]-((i+1<N||final_nl)?1:0); if (ln>maxline) maxline=ln; }

  smap m_name,m_addr,m_city,m_state,m_postal,m_tok,m_ap,m_hp,m_star;
  sm_init(&m_name); sm_init(&m_addr); sm_init(&m_city); sm_init(&m_state); sm_init(&m_postal);
  sm_init(&m_tok); sm_init(&m_ap); sm_init(&m_hp); sm_init(&m_star);
  uint32_t* name_idx = malloc(N*4); uint32_t* addr_idx = malloc(N*4);
  uint16_t* city_idx = malloc(N*2); uint16_t* postal_idx = malloc(N*2);
  uint8_t* state_idx = malloc(N); uint8_t* star_idx = malloc(N); uint8_t* iso_idx = malloc(N);
  vec8 v_rc={0},v_cat={0},v_attr={0},v_hour={0},v_lat={0},v_lon={0},v_ids={0};
  uint32_t* tok_freq = NULL;
  uint8_t IDEC[256]; for (int i=0;i<256;i++) IDEC[i]=255;
  for (int i=0;i<64;i++) IDEC[(uint8_t)B64C[i]]=(uint8_t)i;
  uint64_t bacc=0; uint32_t bnb=0;

  #define FAIL() do{ goto fail; }while(0)
  for (size_t r=0; r<N; r++){
    const uint8_t* l = raw + loff[r];
    size_t llen = loff[r+1]-loff[r] - ((r+1<N||final_nl)?1:0);
    size_t pos=0; const uint8_t* p; size_t vs, ve;
    int isnew;
    for (int mi=0; mi<6; mi++){
      p = mfind(l,llen,pos,mi); if (!p) FAIL();
      vs = (p-l)+MLEN[mi];
      p = mfind(l,llen,vs,mi+1); if (!p) FAIL();
      ve = p-l;
      if (ve < vs) FAIL();
      if (mi==0){
        if (ve-vs != 22) FAIL();
        for (uint32_t k=0;k<22;k++){
          uint8_t dch = IDEC[l[vs+k]]; if (dch==255) FAIL();
          bacc = (bacc<<6)|dch; bnb+=6;
          while (bnb>=8){ bnb-=8; uint8_t ob=(bacc>>bnb)&0xff; vpush(&v_ids,&ob,1); }
          bacc &= (bnb>=64)?0xffffffffffffffffull:((1ull<<bnb)-1);
        }
        name_idx[r] = 0;
      } else if (mi==1){ uint32_t id=sm_get(&m_name,(const char*)l+vs,(uint32_t)(ve-vs),&isnew); name_idx[r]=id; }
      else if (mi==2){ uint32_t id=sm_get(&m_addr,(const char*)l+vs,(uint32_t)(ve-vs),&isnew); addr_idx[r]=id; }
      else if (mi==3){ uint32_t id=sm_get(&m_city,(const char*)l+vs,(uint32_t)(ve-vs),&isnew); if (id>65535) FAIL(); city_idx[r]=(uint16_t)id; }
      else if (mi==4){ uint32_t id=sm_get(&m_state,(const char*)l+vs,(uint32_t)(ve-vs),&isnew); if (id>255) FAIL(); state_idx[r]=(uint8_t)id; }
      else { uint32_t id=sm_get(&m_postal,(const char*)l+vs,(uint32_t)(ve-vs),&isnew); if (id>65535) FAIL(); postal_idx[r]=(uint16_t)id; }
      pos = ve;
    }
    p = mfind(l,llen,pos,6); if (!p) FAIL(); vs=(p-l)+MLEN[6];
    p = mfind(l,llen,vs,7); if (!p) FAIL(); ve=p-l;
    { if (ve-vs < 4 || ve-vs > 13) FAIL();
      const uint8_t* q=l+vs;
      if (!(q[0]>='0'&&q[0]<='9'&&q[1]>='0'&&q[1]<='9'&&q[2]==0x2e)) FAIL();
      int nf = (int)(ve-vs)-3;
      for (int k=3;k<(int)(ve-vs);k++) if (q[k]<'0'||q[k]>'9') FAIL();
      uint8_t buf[8]; int nb=0;
      buf[nb++]=(uint8_t)nf;
      buf[nb++]=(uint8_t)(((q[0]-0x30)<<4)|(q[1]-0x30));
      for (int k=0;k<nf;k+=2){ uint8_t hi=q[3+k]-0x30; uint8_t lo=(k+1<nf)?q[3+k+1]-0x30:0; buf[nb++]=(hi<<4)|lo; }
      vpush(&v_lat,buf,nb); }
    pos = ve;
    p = mfind(l,llen,pos,7); if (!p) FAIL(); vs=(p-l)+MLEN[7];
    p = mfind(l,llen,vs,8); if (!p) FAIL(); ve=p-l;
    { if (ve-vs < 5 || ve-vs > 15) FAIL();
      const uint8_t* q=l+vs;
      if (q[0]!=0x2d) FAIL();
      int dot=-1; for (int k=1;k<(int)(ve-vs);k++) if (q[k]==0x2e){ dot=k; break; } else if (q[k]<'0'||q[k]>'9') FAIL();
      if (dot<0) FAIL();
      int L=dot-1; if (L<2||L>3) FAIL();
      int nf=(int)(ve-vs)-dot-1; if (nf<1||nf>10) FAIL();
      for (int k=dot+1;k<(int)(ve-vs);k++) if (q[k]<'0'||q[k]>'9') FAIL();
      uint8_t buf[8]; int nb=0;
      buf[nb++]=(uint8_t)(((L-2)<<4)|nf);
      for (int k=0;k<L;k+=2){ uint8_t hi=q[1+k]-0x30; uint8_t lo=(k+1<L)?q[1+k+1]-0x30:0; buf[nb++]=(hi<<4)|lo; }
      for (int k=0;k<nf;k+=2){ uint8_t hi=q[dot+1+k]-0x30; uint8_t lo=(k+1<nf)?q[dot+1+k+1]-0x30:0; buf[nb++]=(hi<<4)|lo; }
      vpush(&v_lon,buf,nb); }
    pos = ve;
    for (int mi=8; mi<11; mi++){
      p = mfind(l,llen,pos,mi); if (!p) FAIL(); vs=(p-l)+MLEN[mi];
      p = mfind(l,llen,vs,mi+1); if (!p) FAIL(); ve=p-l;
      if (mi==8){ if (ve-vs>255) FAIL(); uint32_t id=sm_get(&m_star,(const char*)l+vs,(uint32_t)(ve-vs),&isnew); if (id>255) FAIL(); star_idx[r]=(uint8_t)id; }
      else if (mi==9){
        if (ve-vs<1||ve-vs>7) FAIL();
        uint64_t v=0;
        for (size_t k=vs;k<ve;k++){ if (l[k]<'0'||l[k]>'9') FAIL(); v=v*10+(l[k]-0x30); }
        if ((ve-vs)>1 && l[vs]=='0') FAIL(); if (v>0xffffffffull) FAIL();
        uint64_t x=v; uint8_t ob;
        do { ob=x&0x7f; x>>=7; if (x) ob|=0x80; vpush(&v_rc,&ob,1); } while (x);
      } else { if (ve-vs!=1||(l[vs]!='0'&&l[vs]!='1')) FAIL(); iso_idx[r]=(uint8_t)(l[vs]-'0'); }
      pos=ve;
    }
    p = mfind(l,llen,pos,11); if (!p) FAIL(); vs=(p-l)+MLEN[11];
    if (vs+4<=llen && memcmp(l+vs,"null",4)==0){ uint8_t z=0; vpush(&v_attr,&z,1); pos=vs+4; }
    else {
      size_t e; if (vs>=llen || l[vs]!=0x7b || !obj_end(l,vs,llen,&e)) FAIL();
      size_t bs=vs+1, be=e-1; if (be<bs) FAIL();
      range pr[256]; uint32_t pc=0; size_t start=bs; int instr=0,esc=0;
      for (size_t k=bs;k<be;k++){
        uint8_t c=l[k];
        if (instr){ if(esc)esc=0; else if(c==0x5c)esc=1; else if(c==0x22)instr=0; }
        else { if(c==0x22)instr=1; else if(c==0x2c){ if(pc>=256) FAIL(); pr[pc].start=start; pr[pc].len=k-start; pc++; start=k+1; } }
      }
      if (pc>=256) FAIL(); pr[pc].start=start; pr[pc].len=be-start; pc++;
      if (pc>255) FAIL();
      uint8_t cb=(uint8_t)pc; vpush(&v_attr,&cb,1);
      for (uint32_t k=0;k<pc;k++){ uint32_t id=sm_get(&m_ap,(const char*)l+pr[k].start,(uint32_t)pr[k].len,&isnew); if (id>65535) FAIL(); uint16_t w=(uint16_t)id; vpush(&v_attr,&w,2); }
      pos=e;
    }
    p = mfind(l,llen,pos,13); if (!p) FAIL(); vs=(p-l)+MLEN[13];
    if (vs+4<=llen && memcmp(l+vs,"null",4)==0){ uint8_t z=0; vpush(&v_cat,&z,1); pos=vs+4; }
    else {
      if (vs>=llen || l[vs]!=0x22) FAIL();
      size_t k=vs+1; while (1){ if (k>=llen) FAIL(); uint8_t c=l[k]; if (c==0x5c){k+=2;continue;} if (c==0x22) break; k++; }
      size_t cs=vs+1, ce=k; pos=k+1;
      range tr[256]; uint32_t tc=0; size_t start=cs;
      for (size_t q=cs;q+1<ce;q++){ if (l[q]==0x2c && l[q+1]==0x20){ if(tc>=256) FAIL(); tr[tc].start=start; tr[tc].len=q-start; tc++; start=q+2; q++; } }
      if (tc>=256) FAIL(); tr[tc].start=start; tr[tc].len=ce-start; tc++;
      if (tc>255) FAIL();
      uint8_t cb=(uint8_t)tc; vpush(&v_cat,&cb,1);
      for (uint32_t q=0;q<tc;q++){ uint32_t id=sm_get(&m_tok,(const char*)l+tr[q].start,(uint32_t)tr[q].len,&isnew); if (id>65535) FAIL(); if (isnew){ if (!tok_freq) tok_freq=calloc(65536,4); } tok_freq[id]++; uint16_t w=(uint16_t)id; vpush(&v_cat,&w,2); }
    }
    p = mfind(l,llen,pos,12); if (!p) FAIL(); vs=(p-l)+MLEN[12];
    if (vs+4<=llen && memcmp(l+vs,"null",4)==0){ uint8_t z=0; vpush(&v_hour,&z,1); pos=vs+4; }
    else {
      size_t e; if (vs>=llen || l[vs]!=0x7b || !obj_end(l,vs,llen,&e)) FAIL();
      size_t bs=vs+1, be=e-1; if (be<bs) FAIL();
      range pr[16]; uint32_t pc=0; size_t start=bs; int instr=0,esc=0;
      for (size_t k=bs;k<be;k++){
        uint8_t c=l[k];
        if (instr){ if(esc)esc=0; else if(c==0x5c)esc=1; else if(c==0x22)instr=0; }
        else { if(c==0x22)instr=1; else if(c==0x2c){ if(pc>=16) FAIL(); pr[pc].start=start; pr[pc].len=k-start; pc++; start=k+1; } }
      }
      if (pc>=16) FAIL(); pr[pc].start=start; pr[pc].len=be-start; pc++;
      if (pc>255) FAIL();
      uint8_t cb=(uint8_t)pc; vpush(&v_hour,&cb,1);
      for (uint32_t k=0;k<pc;k++){ uint32_t id=sm_get(&m_hp,(const char*)l+pr[k].start,(uint32_t)pr[k].len,&isnew); if (id>65535) FAIL(); uint16_t w=(uint16_t)id; vpush(&v_hour,&w,2); }
      pos=e;
    }
    if (pos != llen-1 || l[llen-1]!=0x7d) FAIL();
  }
  if (bnb){ bacc = (bacc << (8-bnb)) & 0xff; vpush(&v_ids,&bacc,1); }
  { uint8_t z[16]; memset(z,0,16); vpush(&v_ids,z,16); }
  /* ---- finalize dictionaries ---- */
  uint32_t n_name=m_name.n,n_addr=m_addr.n,n_city=m_city.n,n_state=m_state.n,n_postal=m_postal.n;
  uint32_t n_tok=m_tok.n,n_ap=m_ap.n,n_hp=m_hp.n,n_star=m_star.n;
  uint32_t* ord = malloc((n_name>n_addr?n_name:n_addr)*4);
  #define BUILD_SORTED(MM, BL, LL, RM) do { \
    for (uint32_t i=0;i<MM.n;i++) ord[i]=i; \
    sentry* tmp = malloc(MM.n*sizeof(sentry)); \
    memcpy(tmp,MM.items,MM.n*sizeof(sentry)); qsort(tmp,MM.n,sizeof(sentry),cmp_sent); \
    for (uint32_t i=0;i<MM.n;i++) ord[i]=tmp[i].id; free(tmp); \
    blob_from_order(&MM,ord,&BL,&LL,RM); } while(0)
  uint8_t *b_name,*b_addr,*b_city,*b_state,*b_postal,*b_tok,*b_ap,*b_hp,*b_star;
  size_t l_name,l_addr,l_city,l_state,l_postal,l_tok,l_ap,l_hp,l_star;
  uint32_t *rem_name=malloc(n_name*4),*rem_addr=malloc(n_addr*4),*rem_city=malloc(n_city*4),*rem_state=malloc(n_state*4),*rem_postal=malloc(n_postal*4),*rem_tok=malloc(n_tok*4),*rem_ap=malloc(n_ap*4),*rem_hp=malloc(n_hp*4),*rem_star=malloc(n_star*4);
  g_fuse=0;
  BUILD_SORTED(m_name,b_name,l_name,rem_name);
  g_fuse=0;
  BUILD_SORTED(m_addr,b_addr,l_addr,rem_addr);
  g_fuse=0;
  BUILD_SORTED(m_city,b_city,l_city,rem_city);
  g_fuse=0;
  BUILD_SORTED(m_state,b_state,l_state,rem_state);
  g_fuse=0;
  BUILD_SORTED(m_postal,b_postal,l_postal,rem_postal);
  { for (uint32_t i=0;i<n_tok;i++) ord[i]=i;
    uint32_t* tmp=malloc(n_tok*4); memcpy(tmp,ord,n_tok*4);
    for (uint32_t i=0;i<n_tok;i++){ uint32_t best=i;
      for (uint32_t j=i+1;j<n_tok;j++){ uint32_t f1=tok_freq[tmp[j]],f2=tok_freq[tmp[best]]; if (f1>f2||(f1==f2&&tmp[j]<tmp[best])) best=j; }
      uint32_t t=tmp[i]; tmp[i]=tmp[best]; tmp[best]=t; }
    for (uint32_t i=0;i<n_tok;i++) ord[i]=tmp[i]; free(tmp);
    g_fuse=2;
    blob_from_order(&m_tok,ord,&b_tok,&l_tok,rem_tok); g_fuse=0; }
  { for (uint32_t i=0;i<n_ap;i++) ord[i]=i; g_fuse=1; blob_from_order(&m_ap,ord,&b_ap,&l_ap,rem_ap); g_fuse=0; }
  { for (uint32_t i=0;i<n_hp;i++) ord[i]=i; g_fuse=1; blob_from_order(&m_hp,ord,&b_hp,&l_hp,rem_hp); g_fuse=0; }
  { for (uint32_t i=0;i<n_star;i++) ord[i]=i; blob_from_order(&m_star,ord,&b_star,&l_star,rem_star); }
  for (size_t r=0;r<N;r++){
    name_idx[r]=rem_name[name_idx[r]]; addr_idx[r]=rem_addr[addr_idx[r]];
    city_idx[r]=rem_city[city_idx[r]]; postal_idx[r]=rem_postal[postal_idx[r]]; state_idx[r]=rem_state[state_idx[r]];
  }
  { size_t k=0; while (k<v_cat.n){ uint8_t c=v_cat.buf[k++]; if (c){ for (uint8_t t=0;t<c;t++){ uint16_t w=rd_u16(v_cat.buf+k); w=rem_tok[w]; memcpy(v_cat.buf+k,&w,2); k+=2; } } } }

  /* ---- assemble rowblob ---- */
  size_t rowlen = 15*(size_t)N + v_rc.n + v_cat.n + v_attr.n + v_hour.n + v_lat.n + v_lon.n;
  uint8_t* rowblob = malloc(rowlen);
  { size_t off=0;
    memcpy(rowblob+off,name_idx,4*N); off+=4*N;
    memcpy(rowblob+off,addr_idx,4*N); off+=4*N;
    memcpy(rowblob+off,city_idx,2*N); off+=2*N;
    memcpy(rowblob+off,postal_idx,2*N); off+=2*N;
    memcpy(rowblob+off,state_idx,N); off+=N;
    memcpy(rowblob+off,star_idx,N); off+=N;
    memcpy(rowblob+off,iso_idx,N); off+=N;
    memcpy(rowblob+off,v_rc.buf,v_rc.n); off+=v_rc.n;
    memcpy(rowblob+off,v_cat.buf,v_cat.n); off+=v_cat.n;
    memcpy(rowblob+off,v_attr.buf,v_attr.n); off+=v_attr.n;
    memcpy(rowblob+off,v_hour.buf,v_hour.n); off+=v_hour.n;
    memcpy(rowblob+off,v_lat.buf,v_lat.n); off+=v_lat.n;
    memcpy(rowblob+off,v_lon.buf,v_lon.n); off+=v_lon.n; }

  /* ---- small blob ---- */
  if (!n_city||!n_state||!n_postal||!n_tok||!n_ap||!n_hp||!n_star) return -1;
  size_t small_len = l_city+l_state+l_postal+l_tok+l_ap+l_hp+l_star + 6;
  uint8_t* small = malloc(small_len);
  { size_t off=0; const uint8_t nl=0x0a;
    memcpy(small+off,b_city,l_city); off+=l_city; memcpy(small+off,&nl,1); off+=1;
    memcpy(small+off,b_state,l_state); off+=l_state; memcpy(small+off,&nl,1); off+=1;
    memcpy(small+off,b_postal,l_postal); off+=l_postal; memcpy(small+off,&nl,1); off+=1;
    memcpy(small+off,b_tok,l_tok); off+=l_tok; memcpy(small+off,&nl,1); off+=1;
    memcpy(small+off,b_ap,l_ap); off+=l_ap; memcpy(small+off,&nl,1); off+=1;
    memcpy(small+off,b_hp,l_hp); off+=l_hp; memcpy(small+off,&nl,1); off+=1;
    memcpy(small+off,b_star,l_star); off+=l_star; }

  /* ---- tables + verify (full byte-exact roundtrip) ---- */
  sref *t_name=malloc(n_name*sizeof(sref)),*t_addr=malloc(n_addr*sizeof(sref)),*t_city=malloc(n_city*sizeof(sref)),
         *t_state=malloc(n_state*sizeof(sref)),*t_postal=malloc(n_postal*sizeof(sref)),*t_tok=malloc(n_tok*sizeof(sref)),
         *t_ap=malloc(n_ap*sizeof(sref)),*t_hp=malloc(n_hp*sizeof(sref)),*t_star=malloc(n_star*sizeof(sref));
  if (!build_sref(b_name,l_name,n_name,t_name,0)) return -1; if (!build_sref(b_addr,l_addr,n_addr,t_addr,0)) return -1;
  { size_t B[7]; B[0]=0; B[1]=B[0]+l_city+1; B[2]=B[1]+l_state+1; B[3]=B[2]+l_postal+1; B[4]=B[3]+l_tok+1; B[5]=B[4]+l_ap+1; B[6]=B[5]+l_hp+1;
    if (!build_sref(small+B[0], l_city, n_city, t_city, 0)) return -1;
    if (!build_sref(small+B[1], l_state, n_state, t_state, B[1])) return -1;
    if (!build_sref(small+B[2], l_postal, n_postal, t_postal, B[2])) return -1;
    if (!build_sref(small+B[3], l_tok, n_tok, t_tok, B[3])) return -1;
    if (!build_sref(small+B[4], l_ap, n_ap, t_ap, B[4])) return -1;
    if (!build_sref(small+B[5], l_hp, n_hp, t_hp, B[5])) return -1;
    if (!build_sref(small+B[6], l_star, n_star, t_star, B[6])) return -1; }
  {
    Dec2 d; memset(&d,0,sizeof(d));
    d.name_blob=b_name; d.name=t_name; d.addr_blob=b_addr; d.addr=t_addr; d.small=small;
    d.city=t_city; d.state=t_state; d.postal=t_postal; d.tok=t_tok; d.ap=t_ap; d.hp=t_hp; d.star=t_star;
    d.ids=v_ids.buf; d.name_idx=rowblob; d.addr_idx=rowblob+4*N; d.city_idx=rowblob+8*N;
    d.postal_idx=rowblob+10*N; d.state_idx=rowblob+12*N; d.star_idx=rowblob+13*N; d.iso_idx=rowblob+14*N;
    size_t off=15*N;
    d.rc=rowblob+off; off+=v_rc.n; d.cat=rowblob+off; off+=v_cat.n;
    d.attr=rowblob+off; off+=v_attr.n; d.hour=rowblob+off; off+=v_hour.n;
    d.lat=rowblob+off; off+=v_lat.n; d.lon=rowblob+off; off+=v_lon.n;
    d.nrows=(uint32_t)N; d.final_nl=(uint8_t)final_nl;
    d.n_name=n_name; d.n_addr=n_addr; d.n_city=n_city; d.n_state=n_state; d.n_postal=n_postal;
    d.n_tok=n_tok; d.n_ap=n_ap; d.n_hp=n_hp; d.n_star=n_star;
    uint8_t* scratch = malloc(maxline+65536);
    size_t total=0;
    EmSt S; S.rc=d.rc; S.cat=d.cat; S.attr=d.attr; S.hour=d.hour; S.lat=d.lat; S.lon=d.lon; S.o=(char*)scratch; S.obase=(char*)scratch; S.ocap=maxline+65536;
    for (size_t r=0;r<N;r++){
      const uint8_t* l=raw+loff[r]; size_t llen=loff[r+1]-loff[r]-((r+1<N||final_nl)?1:0);
      S.o = (char*)scratch;
      S = emit3(S,&d,(uint32_t)r, 1);
      size_t el=(size_t)(S.o-(char*)scratch);
      size_t want=llen+((r+1<N||final_nl)?1:0);
      if (el!=want || memcmp(scratch,l,llen)!=0 || (want>llen&&scratch[llen]!=0x0a)) return -1;
      total+=el;
    }
    if (total!=size) return -1;
  }

  /* ---- compress: dicts zstd-1, rowblob LZ4_HC ---- */
  size_t zc_name=ZSTD_compressBound(l_name), zc_addr=ZSTD_compressBound(l_addr), zc_small=ZSTD_compressBound(small_len);
  size_t zc_row=(size_t)LZ4_compressBound((int)rowlen);
  uint8_t* z_name=malloc(zc_name); uint8_t* z_addr=malloc(zc_addr); uint8_t* z_small=malloc(zc_small); uint8_t* z_row=malloc(zc_row);
  size_t zn=ZSTD_compress(z_name,zc_name,b_name,l_name,LEVEL);
  size_t za=ZSTD_compress(z_addr,zc_addr,b_addr,l_addr,LEVEL);
  size_t zs=ZSTD_compress(z_small,zc_small,small,small_len,LEVEL);
  int zr=LZ4_compress_HC((const char*)rowblob,(char*)z_row,(int)rowlen,(int)zc_row,9);
  if (zr<=0) return -1;
  if (ZSTD_isError(zn)||ZSTD_isError(za)||ZSTD_isError(zs)) return -1;
  size_t hdr = 4+4+8+1+9*4+4+8*4+6*4+4;
  size_t total = hdr + v_ids.n-16 + zn + za + zs + (size_t)zr;
  if (total > capacity) return -1;
  uint8_t* o=archive;
  put_u32(&o,YLP_MAGIC); put_u32(&o,(uint32_t)N); put_u64(&o,(uint64_t)size);
  *o++=(uint8_t)final_nl;
  put_u32(&o,n_name); put_u32(&o,n_addr); put_u32(&o,n_city); put_u32(&o,n_state); put_u32(&o,n_postal);
  put_u32(&o,n_tok); put_u32(&o,n_ap); put_u32(&o,n_hp); put_u32(&o,n_star);
  put_u32(&o,(uint32_t)(v_ids.n-16));
  put_u32(&o,(uint32_t)zn); put_u32(&o,(uint32_t)l_name);
  put_u32(&o,(uint32_t)za); put_u32(&o,(uint32_t)l_addr);
  put_u32(&o,(uint32_t)zs); put_u32(&o,(uint32_t)small_len);
  put_u32(&o,(uint32_t)zr); put_u32(&o,(uint32_t)rowlen);
  put_u32(&o,(uint32_t)v_rc.n); put_u32(&o,(uint32_t)v_cat.n); put_u32(&o,(uint32_t)v_attr.n);
  put_u32(&o,(uint32_t)v_hour.n); put_u32(&o,(uint32_t)v_lat.n); put_u32(&o,(uint32_t)v_lon.n);
  put_u32(&o,fnv1a(rowblob,rowlen));
  memcpy(o,v_ids.buf,v_ids.n-16); o+=v_ids.n-16;
  memcpy(o,z_name,zn); o+=zn; memcpy(o,z_addr,za); o+=za;
  memcpy(o,z_small,zs); o+=zs; memcpy(o,z_row,(size_t)zr); o+=(size_t)zr;
  return (int64_t)total;
fail:
  return -1;
}
