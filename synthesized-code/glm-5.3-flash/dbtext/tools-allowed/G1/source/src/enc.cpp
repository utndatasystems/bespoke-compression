#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <queue>
#include <algorithm>
#include "codec.h"
#include "fmt.h"

using std::vector;
using std::string;

/* ---------- archive writer ---------- */
struct W {
  uint8_t* p; size_t cap, n; int bad;
  void init(uint8_t* b, size_t c){ p=b; cap=c; n=0; bad=0; memset(b,0,c<64?c:64); }
  inline void need(size_t k){ if (n+k > cap) { bad=1; } }
  inline void u8(uint32_t v){ if(!bad){ if(n+1>cap){bad=1;return;} p[n++]=(uint8_t)v; } }
  inline void u16(uint32_t v){ if(!bad){ if(n+2>cap){bad=1;return;} p[n]=(uint8_t)v; p[n+1]=(uint8_t)(v>>8); n+=2; } }
  inline void u32(uint32_t v){ if(!bad){ if(n+4>cap){bad=1;return;} wr32(p+n,v); n+=4; } }
  inline void u64(uint64_t v){ if(!bad){ if(n+8>cap){bad=1;return;} wr64(p+n,v); n+=8; } }
  inline void bytes(const void* b, size_t k){ if(!bad){ if(n+k>cap){bad=1;return;} memcpy(p+n,b,k); n+=k; } }
  inline void zero(size_t k){ if(!bad){ if(n+k>cap){bad=1;return;} memset(p+n,0,k); n+=k; } }
};

/* ---------- rows ---------- */
struct Rows {
  vector<uint32_t> off; // rows+1 offsets into data
  const uint8_t* d; size_t n;
  bool trailing;
  void split(const uint8_t* dd, size_t nn){
    d=dd; n=nn; off.clear();
    size_t s=0;
    for (size_t i=0;i<nn;i++) if (dd[i]=='\n'){ off.push_back((uint32_t)s); s=i+1; }
    trailing = (nn>0 && dd[nn-1]=='\n');
    if (!trailing && nn>0){ off.push_back((uint32_t)s); }
    off.push_back((uint32_t)nn); // sentinel not a row
    // note: if trailing, last real row ends at nn-1; rows = off.size()-1 when !trailing
  }
  uint32_t count() const { return trailing ? (uint32_t)off.size()-1 : (uint32_t)off.size()-1; }
  // careful: with trailing newline, split points give rows = number of '\n'; off currently holds start of each row
};

static void split_rows(const uint8_t* d, size_t n, vector<uint32_t>& start, vector<uint32_t>& len, bool& trailing){
  start.clear(); len.clear();
  size_t s=0;
  for (size_t i=0;i<n;i++) if (d[i]=='\n'){ start.push_back((uint32_t)s); len.push_back((uint32_t)(i-s)); s=i+1; }
  trailing = (n>0 && d[n-1]=='\n');
  if (!trailing && n>0){ start.push_back((uint32_t)s); len.push_back((uint32_t)(n-s)); }
}

/* ---------- header ---------- */
static void put_hdr(W& w, uint32_t eng, uint32_t rows, uint64_t orig, uint32_t flags){
  w.u32(LB_MAGIC); w.u8(eng); w.u8(flags); w.u16(LB_HDR);
  w.u32(rows); w.u64(orig);
}

/* ================= BPE ================= */
struct Bpe {
  vector<string> vocab;
  vector<uint32_t> lenv;
  // tokenizer state
  vector<uint32_t> tok, prv, nxt;
  vector<uint8_t> dead;
  std::unordered_map<uint64_t,int64_t> cnt;
  std::unordered_map<uint64_t,vector<uint32_t>> pos;
  struct HE { uint64_t c; uint32_t a,b; };
  struct HECmp { bool operator()(const HE& x, const HE& y) const {
    if (x.c!=y.c) return x.c<y.c; if (x.a!=y.a) return x.a<y.a; return x.b<y.b; } };
  std::priority_queue<HE, vector<HE>, HECmp> heap;
  static uint64_t key(uint32_t a,uint32_t b){ return ((uint64_t)a<<32)|b; }

  void train(const uint8_t* d, size_t n, uint32_t max_vocab, uint32_t max_len, int64_t min_cnt){
    vocab.resize(256); lenv.resize(256);
    for (int i=0;i<256;i++){ vocab[i].assign(1,(char)i); lenv[i]=1; }
    tok.resize(n); prv.resize(n); nxt.resize(n); dead.assign(n,1);
    for (size_t i=0;i<n;i++){
      tok[i]=d[i]; dead[i]=0;
      prv[i] = i? (int32_t)(i-1):-1;
      nxt[i] = (i+1<n)? (int32_t)(i+1):-1;
    }
    // seed pair counts
    for (size_t i=0;i+1<n;i++){
      uint32_t a=tok[i], b=tok[i+1];
      if (a==10||b==10) continue;
      if (lenv[a]+lenv[b]>max_len) continue;
      uint64_t k=key(a,b); cnt[k]++;
      pos[k].push_back((uint32_t)i);
    }
    for (auto& kv : cnt) if (kv.second>=min_cnt){
      uint32_t a=(uint32_t)(kv.first>>32), b=(uint32_t)kv.first;
      heap.push({(uint64_t)kv.second,a,b});
    }
    while (vocab.size() < max_vocab && !heap.empty()){
      HE h = heap.top(); heap.pop();
      uint64_t k = key(h.a,h.b);
      auto it = cnt.find(k);
      if (it==cnt.end() || it->second < min_cnt) continue;
      if ((uint64_t)it->second != h.c) continue;
      uint32_t a=h.a, b=h.b;
      uint32_t v = (uint32_t)vocab.size();
      vocab.push_back(vocab[a]+vocab[b]); lenv.push_back(lenv[a]+lenv[b]);
      // merge occurrences
      vector<uint32_t> lst; lst.swap(pos[k]);
      cnt[k] = -1; // consumed marker
      for (uint32_t i : lst){
        if (dead[i] || tok[i]!=a) continue;
        int32_t j = nxt[i];
        if (j<0 || tok[j]!=b) continue;
        int32_t pi = prv[i], nk = nxt[j];
        // dec old pairs
        if (pi>=0 && tok[pi]!=10){ uint64_t kl=key(tok[pi],a); auto jt=cnt.find(kl); if (jt!=cnt.end() && jt->second>0) jt->second--; }
        if (nk>=0 && tok[nk]!=10){ uint64_t kr=key(b,tok[nk]); auto jt=cnt.find(kr); if (jt!=cnt.end() && jt->second>0) jt->second--; }
        // relink
        tok[i]=v; dead[j]=1; nxt[i]=nk; if (nk>=0) prv[nk]=i;
        // inc new pairs
        if (pi>=0 && tok[pi]!=10){ uint64_t kl=key(tok[pi],v); cnt[kl]++; pos[kl].push_back(pi);
          if (cnt[kl]>=min_cnt) heap.push({(uint64_t)std::min<int64_t>(cnt[kl],0x7fffffff),(uint32_t)(kl>>32),(uint32_t)kl}); }
        if (nk>=0 && tok[nk]!=10){ uint64_t kr=key(v,tok[nk]); cnt[kr]++; pos[kr].push_back(i);
          if (cnt[kr]>=min_cnt) heap.push({(uint64_t)std::min<int64_t>(cnt[kr],0x7fffffff),(uint32_t)(kr>>32),(uint32_t)kr}); }
      }
    }
  }
};

/* greedy longest-match tokenizer with 4-byte prefix index */
struct TokIndex {
  const vector<string>* vocab;
  std::unordered_map<uint32_t, vector<uint32_t>> m4, m2;
  vector<uint32_t> singles; // by byte, len1
  void build(const vector<string>& v){
    vocab=&v;
    for (uint32_t i=256;i<v.size();i++){
      const string& s=v[i];
      if (s.size()>=4){ uint32_t k; memcpy(&k,s.data(),4); m4[k].push_back(i); }
      else if (s.size()>=2){ uint32_t k=0; memcpy(&k,s.data(),2); m2[k].push_back(i); }
    }
    for (auto& kv : m4) std::sort(kv.second.begin(),kv.second.end(),[&](uint32_t x,uint32_t y){ return v[x].size()>v[y].size(); });
    for (auto& kv : m2) std::sort(kv.second.begin(),kv.second.end(),[&](uint32_t x,uint32_t y){ return v[x].size()>v[y].size(); });
  }
  // returns entry id or 0..255 for single byte (ids <256 are single bytes)
  inline uint32_t match(const uint8_t* d, size_t n, size_t p) const {
    size_t rem = n - p;
    if (rem >= 4){
      uint32_t k; memcpy(&k,d+p,4);
      auto it=m4.find(k);
      if (it!=m4.end()) for (uint32_t e : it->second){ uint32_t L=(*vocab)[e].size(); if (L<=rem && memcmp((*vocab)[e].data(),d+p,L)==0) return e; }
    }
    if (rem >= 2){
      uint32_t k=0; memcpy(&k,d+p,2);
      auto it=m2.find(k);
      if (it!=m2.end()) for (uint32_t e : it->second){ uint32_t L=(*vocab)[e].size(); if (L<=rem && memcmp((*vocab)[e].data(),d+p,L)==0) return e; }
    }
  return d[p];
  }
};

/* ================= TOK encode ================= */
#define TOK_ESC2 224
#define TOK_ESC2_PREFIXES 24   /* codes 224..247 -> 6144 2-byte ids */
#define TOK_ESC3 248           /* codes 248..255 -> 3-byte ids */

static bool enc_tok(W& w, const uint8_t* d, size_t n, uint32_t flags){
  vector<uint32_t> rs, rl; bool trailing;
  split_rows(d,n,rs,rl,trailing);
  uint32_t rows=(uint32_t)rs.size();
  if (!rows) return false;
  uint32_t max_row=0; for (uint32_t l : rl) if (l>max_row) max_row=l;
  Bpe bpe;
  uint32_t max_vocab = 24576;
  bpe.train(d,n,max_vocab,64,6);
  TokIndex ti; ti.build(bpe.vocab);
  /* greedy pass for initial usage */
  vector<vector<uint32_t>> rowtok(rows);
  vector<uint32_t> usage(bpe.vocab.size(),0);
  for (uint32_t r=0;r<rows;r++){
    size_t p=rs[r], e=p+rl[r];
    auto& t=rowtok[r];
    while (p<e){ uint32_t id=ti.match(d,e,p); t.push_back(id); usage[id]++; p+= (id<256?1:bpe.vocab[id].size()); }
  }
  uint32_t maxlen=0; for (size_t i=0;i<bpe.vocab.size();i++) if (bpe.vocab[i].size()>maxlen) maxlen=bpe.vocab[i].size();
  vector<uint32_t> order; vector<uint32_t> rank(bpe.vocab.size(),0);
  /* DP helpers */
  vector<uint32_t> dpv(max_row+2); vector<uint16_t> bestrk(max_row+2); vector<uint32_t> bestlen(max_row+2);
  vector<uint8_t> inord;
  auto rerank = [&]() {
    for (uint32_t i=0;i<256;i++) if (!usage[i]) usage[i]=1; /* keep all bytes representable */
    order.clear(); order.reserve(bpe.vocab.size());
    for (size_t i=0;i<bpe.vocab.size();i++) if (usage[i]) order.push_back((uint32_t)i);
    std::stable_sort(order.begin(),order.end(),[&](uint32_t x,uint32_t y){ return usage[x]>usage[y]; });
    std::fill(rank.begin(),rank.end(),0xffff); /* 0xffff = not in dictionary */
    for (size_t i=0;i<order.size();i++) rank[order[i]]=(uint32_t)i;
    inord.assign(bpe.vocab.size(),0);
    for (uint32_t id : order) inord[id]=1;
  };
  auto code_cost = [&](uint32_t rk) -> uint32_t {
    if (rk < TOK_ESC2) return 1;
    if (rk < TOK_ESC2 + TOK_ESC2_PREFIXES*256) return 2;
    return 3;
  };
  auto dp_tokenize = [&]() {
    for (uint32_t r=0;r<rows;r++){
      size_t p=rs[r], e=p+rl[r]; uint32_t L=(uint32_t)(e-p);
      auto& bc = rowtok[r]; bc.clear();
      if (!L) continue;
      for (uint32_t i=0;i<=L;i++) dpv[i]=0x3fffffff;
      dpv[L]=0;
      for (int i=(int)L-1;i>=0;i--){
        size_t rem=L-i;
        uint32_t best=0x3fffffff; uint16_t bid=0xffff; uint32_t blen=1;
        uint8_t ch=d[p+i];
        if (inord[ch] && dpv[i+1]+code_cost(rank[ch]) < best){ best=dpv[i+1]+code_cost(rank[ch]); bid=(uint16_t)ch; blen=1; }
        if (rem>=2){
          uint32_t k2=0; memcpy(&k2,d+p+i,2);
          auto it=ti.m2.find(k2);
          if (it!=ti.m2.end()) for (uint32_t eid : it->second){
            if (!inord[eid]) continue;
            uint32_t l2=(uint32_t)bpe.vocab[eid].size();
            if (l2<=rem && i+l2<=L && memcmp(bpe.vocab[eid].data(),d+p+i,l2)==0){
              uint32_t c2=code_cost(rank[eid])+dpv[i+l2]; if (c2<best){ best=c2; bid=(uint16_t)eid; blen=l2; } } }
        }
        if (rem>=4){
          uint32_t k4; memcpy(&k4,d+p+i,4);
          auto it=ti.m4.find(k4);
          if (it!=ti.m4.end()) for (uint32_t eid : it->second){
            if (!inord[eid]) continue;
            uint32_t l4=(uint32_t)bpe.vocab[eid].size();
            if (l4<=rem && i+l4<=L && memcmp(bpe.vocab[eid].data(),d+p+i,l4)==0){
              uint32_t c4=code_cost(rank[eid])+dpv[i+l4]; if (c4<best){ best=c4; bid=(uint16_t)eid; blen=l4; } } }
        }
        dpv[i]=best; bestrk[i]=bid; bestlen[i]=blen;
      }
      uint32_t i=0;
      while (i<L){ uint16_t id=bestrk[i]; uint32_t l=bestlen[i]; if (!l||id==0xffff){ id=(uint16_t)d[p+i]; l=1; } bc.push_back(id); usage[id]++; i+=l; }
    }
  };
  rerank();          /* rank from greedy usage */
  dp_tokenize();     /* round 1: usage now from DP */
  rerank();          /* re-rank */
  dp_tokenize();     /* round 2: final tokens for these ranks */
  uint32_t n_entries=(uint32_t)order.size();
  uint32_t n1 = TOK_N1;
  if (n_entries < n1) n1=n_entries;
  /* lens & blob in rank order */
  vector<uint8_t> lens(n_entries);
  size_t dict_bytes=0;
  for (uint32_t i=0;i<n_entries;i++){ const string& str=bpe.vocab[order[i]]; lens[i]=(uint8_t)str.size(); dict_bytes+=str.size(); }
  /* emit codes + per-row token byte deltas */
  vector<uint8_t> tokout; vector<uint8_t> deltas; deltas.reserve(rows+rows/16+16);
  vector<uint32_t> btok_start, bdelta_off;
  for (uint32_t r=0;r<rows;r++){
    if ((r % TOK_BLOCK)==0){ btok_start.push_back((uint32_t)tokout.size()); bdelta_off.push_back((uint32_t)deltas.size()); }
    size_t row_begin=tokout.size();
    for (uint32_t id : rowtok[r]){
      uint32_t rk=rank[id];
      if (rk<TOK_ESC2) tokout.push_back((uint8_t)rk);
      else if (rk < TOK_ESC2 + TOK_ESC2_PREFIXES*256){
        uint32_t id16=rk-TOK_ESC2; tokout.push_back((uint8_t)(TOK_ESC2+(id16>>8))); tokout.push_back((uint8_t)(id16&255));
      } else {
        uint32_t id24=rk-TOK_ESC2-TOK_ESC2_PREFIXES*256;
        tokout.push_back((uint8_t)(TOK_ESC3+(id24>>16))); tokout.push_back((uint8_t)((id24>>8)&255)); tokout.push_back((uint8_t)(id24&255));
      }
    }
    size_t row_bytes=tokout.size()-row_begin;
    if (row_bytes>=255){ deltas.push_back(255); deltas.push_back((uint8_t)(row_bytes&255)); deltas.push_back((uint8_t)(row_bytes>>8)); }
    else deltas.push_back((uint8_t)row_bytes);
  }
  uint32_t ntok_bytes=(uint32_t)tokout.size();
  uint32_t n_blocks=(uint32_t)btok_start.size();
  put_hdr(w,ENG_TOK,rows,n,flags);
  w.u8(TOK_LOG_BLOCK); w.u8(TOK_N1&255); w.u8(0); w.u8(0);
  w.u32(n_entries); w.u32(ntok_bytes); w.u32((uint32_t)dict_bytes); w.u32(n_blocks); w.u32(max_row); w.u32((uint32_t)deltas.size());
  w.bytes(lens.data(),n_entries);
  { vector<uint8_t> blob; blob.reserve(dict_bytes+64); for (uint32_t i=0;i<n_entries;i++){ const string& str=bpe.vocab[order[i]]; blob.insert(blob.end(),str.begin(),str.end()); } blob.resize(dict_bytes+64,0); w.bytes(blob.data(),blob.size()); }
  w.bytes(tokout.data(),ntok_bytes);
  w.bytes(deltas.data(),deltas.size());
  for (uint32_t b=0;b<n_blocks;b++){ w.u32(btok_start[b]); w.u32(bdelta_off[b]); }
  return !w.bad;
}

/* ================= CNAME ================= */
static bool all_digits(const uint8_t* p, uint32_t n){ for (uint32_t i=0;i<n;i++) if (p[i]<'0'||p[i]>'9') return false; return true; }
static bool enc_cname(W& w, const uint8_t* d, size_t n, uint32_t flags){
  vector<uint32_t> rs, rl; bool trailing;
  split_rows(d,n,rs,rl,trailing);
  uint32_t rows=(uint32_t)rs.size(); if (rows<2) return false;
  uint32_t L=rl[0];
  for (uint32_t l : rl) if (l!=L) return false;
  if (L<2 || L>24) return false;
  // find split: digits suffix length
  const uint8_t* r0=d+rs[0];
  uint32_t nd=0; while (nd<L && all_digits(r0+L-1-nd,1)) nd++;
  if (nd<1 || nd>9 || L-nd<1 || L-nd>15) return false;
  uint32_t pl=L-nd;
  uint64_t maxv=0;
  for (uint32_t r=0;r<rows;r++){
    const uint8_t* p=d+rs[r];
    if (memcmp(p,r0,pl)!=0) return false;
    if (!all_digits(p+pl,nd)) return false;
    uint64_t v=0; for (uint32_t i=0;i<nd;i++) v=v*10+(p[pl+i]-'0');
    if (v>=(1ull<<18)) return false;
    if (v>maxv) maxv=v;
    // canonical digits? allow leading zeros as given; decoder regenerates zero-padded => require canonical zero-pad width nd
    if (v >= 1000000000000000000ull) return false;
  }
  uint32_t vbits=18;
  // bit-pack values
  vector<uint8_t> vals(((uint64_t)rows*vbits+7)/8+16,0);
  BitW bw; bw_init(bw,vals.data(),vals.size());
  for (uint32_t r=0;r<rows;r++){ const uint8_t* p=d+rs[r]; uint64_t v=0; for (uint32_t i=0;i<nd;i++) v=v*10+(p[pl+i]-'0'); bw_put(bw,v,vbits); }
  put_hdr(w,ENG_CNAME,rows,n,flags);
  w.u8((uint8_t)pl); w.u8((uint8_t)nd); w.u8(0); w.u8(0); w.u32(vbits);
  w.bytes(r0,pl);
  w.bytes(vals.data(),bw_bytes(bw)+16);
  return !w.bad;
}

/* ================= GENOME ================= */
static bool enc_genome(W& w, const uint8_t* d, size_t n, uint32_t flags){
  vector<uint32_t> rs, rl; bool trailing;
  split_rows(d,n,rs,rl,trailing);
  uint32_t rows=(uint32_t)rs.size(); if (rows<2) return false;
  uint32_t L=rl[0];
  for (uint32_t l : rl) if (l!=L) return false;
  if (L<1 || L>32) return false;
  uint8_t syms[4]={0,0,0,0}; uint32_t ns=0;
  for (size_t i=0;i<n;i++){ if (d[i]=='\n') continue; uint8_t c=d[i]; uint32_t j; for (j=0;j<ns;j++) if (syms[j]==c) break; if (j==ns){ if (ns==4) return false; syms[ns++]=c; } }
  if (ns==0) return false;
  uint32_t sb = (ns<=2)?1:2;
  vector<uint8_t> vals(((uint64_t)rows*L*sb+7)/8+16,0);
  BitW bw; bw_init(bw,vals.data(),vals.size());
  for (uint32_t r=0;r<rows;r++){ const uint8_t* p=d+rs[r]; for (uint32_t i=0;i<L;i++){ uint8_t c=p[i]; uint32_t j; for (j=0;j<ns;j++) if (syms[j]==c) break; bw_put(bw,j,sb); } }
  put_hdr(w,ENG_GENOME,rows,n,flags);
  w.u8((uint8_t)L); w.u8((uint8_t)sb); w.u8(0); w.u8(0);
  w.u32(syms[0]|(syms[1]<<8)|(syms[2]<<16)|((uint32_t)syms[3]<<24));
  w.bytes(vals.data(),bw_bytes(bw)+16);
  return !w.bad;
}

/* ================= HEXU32 ================= */
static inline uint32_t hex_val(uint8_t c){ if (c>='0'&&c<='9') return c-'0'; if (c>='A'&&c<='F') return c-'A'+10; return 16; }
static bool enc_hexu32(W& w, const uint8_t* d, size_t n, uint32_t flags){
  vector<uint32_t> rs, rl; bool trailing;
  split_rows(d,n,rs,rl,trailing);
  uint32_t rows=(uint32_t)rs.size(); if (rows<2) return false;
  vector<uint8_t> vals((size_t)rows*4+16,0);
  vector<uint8_t> lens(((uint64_t)rows*3+7)/8+16,0);
  BitW bw; bw_init(bw,lens.data(),lens.size());
  uint32_t maxr=0;
  for (uint32_t r=0;r<rows;r++){
    const uint8_t* p=d+rs[r]; uint32_t L=rl[r];
    if (L<1||L>8) return false;
    uint64_t v=0;
    for (uint32_t i=0;i<L;i++){ uint32_t h=hex_val(p[i]); if (h==16) return false; v=(v<<4)|h; }
    if (v>0xffffffffull) return false;
    // canonical check: %X formatting
    char tmp[16]; int k=0; uint64_t vv=v; if (!vv) tmp[k++]='0'; while (vv){ uint32_t dg=(uint32_t)(vv&15); tmp[k++]= dg<10? ('0'+dg):('A'+dg-10); vv>>=4; }
    if ((uint32_t)k!=L) return false;
    for (uint32_t i=0;i<L;i++) if ((uint8_t)tmp[k-1-i]!=p[i]) return false;
    wr32(vals.data()+4ull*r,(uint32_t)v);
    bw_put(bw,L-1,3);
    if (L>maxr) maxr=L;
  }
  put_hdr(w,ENG_HEXU32,rows,n,flags);
  w.u32(0);
  w.bytes(vals.data(),(size_t)rows*4);
  w.bytes(lens.data(),bw_bytes(bw)+16);
  return !w.bad;
}

/* ================= UUID ================= */
static inline int hexnib(uint8_t c){ if (c>='0'&&c<='9') return c-'0'; if (c>='a'&&c<='f') return c-'a'+10; return -1; }
static bool enc_uuid(W& w, const uint8_t* d, size_t n, uint32_t flags){
  vector<uint32_t> rs, rl; bool trailing;
  split_rows(d,n,rs,rl,trailing);
  uint32_t rows=(uint32_t)rs.size(); if (rows<2) return false;
  // parse: 36 chars, dashes at 8,13,18,23
  uint8_t b6=0,b7=0; uint32_t tm=0; uint32_t tlmin=0xffffffff,tlmax=0;
  vector<uint32_t> tls(rows); vector<uint32_t> seqs(rows); vector<uint64_t> nodes(rows);
  for (uint32_t r=0;r<rows;r++){
    const uint8_t* p=d+rs[r]; uint32_t L=rl[r];
    if (L!=36) return false;
    if (p[8]!='-'||p[13]!='-'||p[18]!='-'||p[23]!='-') return false;
    uint8_t b[16];
    for (int i=0;i<16;i++){
      int h,l;
      if (i<4){ h=hexnib(p[2*i]); l=hexnib(p[2*i+1]); }
      else if (i<6){ h=hexnib(p[2*i+1]); l=hexnib(p[2*i+2]); }
      else if (i<8){ h=hexnib(p[2*i+2]); l=hexnib(p[2*i+3]); }
      else if (i<10){ h=hexnib(p[2*i+3]); l=hexnib(p[2*i+4]); }
      else { h=hexnib(p[2*i+4]); l=hexnib(p[2*i+5]); }
      if (h<0||l<0) return false;
      b[i]=(uint8_t)(h*16+l);
    }
    if (r==0){ b6=b[6]; b7=b[7]; tm=(uint32_t)(b[4]<<8|b[5]); }
    else { if (b[6]!=b6||b[7]!=b7) return false; if ((uint32_t)(b[4]<<8|b[5])!=tm) return false; }
    if ((b[8]&0xC0)!=0x80) return false; // variant top bits must be 10
    uint32_t tl=((uint32_t)b[0]<<24)|((uint32_t)b[1]<<16)|((uint32_t)b[2]<<8)|b[3];
    tls[r]=tl; seqs[r]=(uint32_t)(((b[8]&0x3f)<<8)|b[9]); nodes[r]=((uint64_t)b[10]<<40)|((uint64_t)b[11]<<32)|((uint64_t)b[12]<<24)|((uint64_t)b[13]<<16)|((uint64_t)b[14]<<8)|b[15];
    if (tl<tlmin) tlmin=tl; if (tl>tlmax) tlmax=tl;
  }
  uint64_t range=(uint64_t)tlmax-tlmin;
  if (range>=(1ull<<26)) return false;
  uint32_t tb=26;
  vector<uint8_t> vals(((uint64_t)rows*(tb+14+48)+7)/8+16,0);
  BitW bw; bw_init(bw,vals.data(),vals.size());
  for (uint32_t r=0;r<rows;r++){ bw_put(bw,tls[r]-tlmin,tb); bw_put(bw,seqs[r],14); bw_put(bw,nodes[r]&0xffffffffffffull,48); }
  put_hdr(w,ENG_UUID,rows,n,flags);
  w.u8(b6); w.u8(b7); w.u8(0); w.u8(0);
  w.u32(tm); w.u32(tlmin); w.u32(tb);
  w.bytes(vals.data(),bw_bytes(bw)+16);
  return !w.bad;
}

/* ================= LOCATION ================= */
static bool enc_loc(W& w, const uint8_t* d, size_t n, uint32_t flags){
  vector<uint32_t> rs, rl; bool trailing;
  split_rows(d,n,rs,rl,trailing);
  uint32_t rows=(uint32_t)rs.size(); if (rows<2) return false;
  // pattern: '(' INT '.' D1 ', -' INT2 '.' D2 ')' ; INT constant; INT2 in {A,B}
  // non-matching rows must all be identical null string
  vector<uint8_t> l1(rows), l2(rows); vector<uint8_t> f74(rows);
  vector<uint32_t> nulls;
  string lat_int, lng_a, lng_b, nullstr;
  for (uint32_t r=0;r<rows;r++){
    const uint8_t* p=d+rs[r]; uint32_t L=rl[r];
    // try parse
    if (L>=12 && p[0]=='('){
      uint32_t i=1;
      while (i<L && p[i]>='0'&&p[i]<='9') i++;
      if (i<2||i>3||i>=L||p[i]!='.') goto asnull;
      string li((const char*)p+1,i-1);
      if (lat_int.empty()) lat_int=li; else if (lat_int!=li) goto asnull;
      uint32_t ds=i+1; uint32_t j=ds;
      while (j<L && p[j]>='0'&&p[j]<='9') j++;
      uint32_t D1=j-ds;
      if (D1<8||D1>15) goto asnull;
      if (j+1>=L||p[j]!=','||p[j+1]!=' ') goto asnull;
      uint32_t k=j+2;
      if (k>=L||p[k]!='-') goto asnull;
      k++;
      uint32_t i2=k;
      while (k<L && p[k]>='0'&&p[k]<='9') k++;
      if (k-i2<1||k-i2>3||k>=L||p[k]!='.') goto asnull;
      string li2((const char*)p+i2,k-i2);
      if (lng_a.empty()) lng_a=li2; else if (lng_b.empty()&&li2!=lng_a) lng_b=li2;
      if (li2!=lng_a&&li2!=lng_b) goto asnull;
      uint32_t ds2=k+1; uint32_t m=ds2;
      while (m<L && p[m]>='0'&&p[m]<='9') m++;
      uint32_t D2=m-ds2;
      if (D2<10||D2>17) goto asnull;
      if (m!=L-1||p[m]!=')') goto asnull;
      l1[r]=(uint8_t)(D1-8); l2[r]=(uint8_t)(D2-10); f74[r]=(uint8_t)(li2==lng_b?1:0);
      continue;
    }
    asnull:
    nulls.push_back(r);
    string s((const char*)p,L);
    if (nullstr.empty()) nullstr=s; else if (nullstr!=s) return false;
  }
  if (lat_int.empty()||lng_a.empty()) return false;
  if (!lng_b.empty() && lng_a.size()!=lng_b.size()) return false;
  uint32_t null_row=0xffffffff;
  if (!nulls.empty()){ if (nulls.size()>1) return false; null_row=nulls[0]; }
  // pack
  vector<uint8_t> lens(((uint64_t)rows*7+7)/8+48,0);
  vector<uint8_t> digs(((uint64_t)n*4)+64,0); // upper bound
  BitW bl; bw_init(bl,lens.data(),lens.size());
  BitW bd; bw_init(bd,digs.data(),digs.size());
  for (uint32_t r=0;r<rows;r++){
    if (r==null_row){ bw_put(bl,0x7f,7); continue; }
    const uint8_t* p=d+rs[r];
    uint32_t i=1+lat_int.size()+1; // after '40.'
    uint32_t D1=8+l1[r], D2=10+l2[r];
    bw_put(bl,l1[r],3); bw_put(bl,l2[r],3); bw_put(bl,f74[r],1);
    for (uint32_t q=0;q<D1;q++) bw_put(bd,p[i+q]-'0',4);
    uint32_t j=i+D1+2+1+lng_a.size()+1;
    for (uint32_t q=0;q<D2;q++) bw_put(bd,p[j+q]-'0',4);
  }
  put_hdr(w,ENG_LOC,rows,n,flags);
  w.u32(null_row);
  w.u8((uint8_t)lat_int.size()); w.bytes(lat_int.data(),lat_int.size());
  w.u8((uint8_t)lng_a.size()); w.bytes(lng_a.data(),lng_a.size());
  w.u8((uint8_t)(lng_b.empty()?0:lng_b.size())); if(!lng_b.empty()) w.bytes(lng_b.data(),lng_b.size());
  w.u8((uint8_t)nullstr.size()); if(!nullstr.empty()) w.bytes(nullstr.data(),nullstr.size());
  { uint8_t pad=0; w.u8(pad); }
  {
    /* block directory: digits bit start + out start per block */
    uint32_t nb=(rows+TOK_BLOCK-1)/TOK_BLOCK;
    uint64_t dbit=0; uint64_t obytes=0;
    for (uint32_t b=0;b<nb;b++){
      w.u32((uint32_t)dbit); w.u32((uint32_t)obytes);
      uint32_t e=b*TOK_BLOCK+TOK_BLOCK; if (e>rows) e=rows;
      for (uint32_t r=b*TOK_BLOCK;r<e;r++){
        if (r==null_row){ obytes+=nullstr.size(); continue; }
        dbit += 4*(8+(uint64_t)l1[r] + 10+(uint64_t)l2[r]);
        obytes += 12 + 8+(uint64_t)l1[r] + lng_a.size() + 10+(uint64_t)l2[r];
      }
    }
  }
  w.bytes(lens.data(),bw_bytes(bl)+32);
  w.bytes(digs.data(),bw_bytes(bd)+32);
  return !w.bad;
}

extern "C" int64_t lab_encode(const uint8_t* raw, size_t size, uint8_t* archive, size_t capacity){
  W w; w.init(archive,capacity);
  uint32_t flags = (size>0 && raw[size-1]=='\n') ? 1 : 0;
  // engine detection order; each writer aborts via w.bad on mismatch/capacity
  do {
    if (enc_cname(w,raw,size,flags)) break;
    w.init(archive,capacity);
    if (enc_genome(w,raw,size,flags)) break;
    w.init(archive,capacity);
    if (enc_hexu32(w,raw,size,flags)) break;
    w.init(archive,capacity);
    if (enc_uuid(w,raw,size,flags)) break;
    w.init(archive,capacity);
    if (enc_loc(w,raw,size,flags)) break;
    w.init(archive,capacity);
    if (enc_tok(w,raw,size,flags)) break;
    w.init(archive,capacity);
    return -1;
  } while(0);
  if (w.bad) return -1;
  return (int64_t)w.n;
}
