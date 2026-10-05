#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#ifndef ENTROPY_DECODER_ONLY
#include <vector>
#include <algorithm>
#endif

// Original byte entropy codec: canonical Huffman with four independent streams.
// Format v0 stored: mode:u8=0, raw_size:u32 LE, bytes.
// Format v1: mode:u8=1 or 3 (single/pair lookup), raw_size:u32 LE, 128 bytes of nibbled code lengths,
// 4 x stream_size:u32 LE, four little-endian bitstreams each with 8 zero padding bytes.
namespace entropy {
static inline uint32_t get32(const uint8_t *p) { uint32_t v; memcpy(&v,p,4); return v; }
static inline void put32(uint8_t *p,uint32_t v) { memcpy(p,&v,4); }
static inline uint64_t get64(const uint8_t *p) { uint64_t v; memcpy(&v,p,8); return v; }
static inline unsigned reverse(unsigned x,unsigned n) {
 x=((x&0x5555)<<1)|((x>>1)&0x5555);
 x=((x&0x3333)<<2)|((x>>2)&0x3333);
 x=((x&0x0f0f)<<4)|((x>>4)&0x0f0f);
 x=(x<<8)|(x>>8); return (x&65535)>>(16-n);
}
static inline size_t decoded_size(const uint8_t *p,size_t n) { return n>=5?get32(p+1):0; }
static inline bool decode(const uint8_t *src,size_t bytes,uint8_t *dst,size_t capacity) {
 if(bytes<5) return false;
 uint32_t n=get32(src+1);
 if(n>capacity) return false;
 if(src[0]==0) { if(bytes!=size_t(n)+5) return false; memcpy(dst,src+5,n); return true; }
 if(src[0]==2) { if(bytes!=6) return false;memset(dst,src[5],n);return true; }
 if((src[0]!=1&&src[0]!=3)||bytes<149) return false;
 bool pairs=src[0]==3;
 uint16_t table[4096]; unsigned count[13]={0},next[13]={0};
 for(unsigned i=0;i<256;++i) { unsigned len=(src[5+i/2]>>((i&1)*4))&15; if(len>12) return false; ++count[len]; }
 unsigned code=0;
 for(unsigned i=1;i<=12;++i) { code=(code+(i==1?0:count[i-1]))<<1; next[i]=code; }
 if(code+count[12]!=4096) return false;
 for(unsigned i=0;i<256;++i) { unsigned len=(src[5+i/2]>>((i&1)*4))&15; if(!len) continue;
  unsigned c=reverse(next[len]++,len); uint16_t v=(uint16_t)((i<<4)|len);
  for(unsigned j=c;j<4096;j+=1u<<len) table[j]=v;
 }
 uint32_t pair[4096];
 if(pairs) for(unsigned i=0;i<4096;++i) {
  unsigned a=table[i],la=a&15,b=table[i>>la],lb=b&15;
  if(la+lb<=12) pair[i]=(a>>4)|((b>>4)<<8)|((la+lb)<<16)|(1u<<24);
  else pair[i]=(a>>4)|(la<<16);
 }
 uint32_t sizes[4]; size_t offset=149;
 const uint8_t *p[4],*end[4]; uint64_t bits[4]; uint32_t pos[4]={0,0,0,0};
 uint8_t *out[4]; unsigned q=n/4,rem=n%4,lens[4];
 for(unsigned k=0;k<4;++k) { sizes[k]=get32(src+133+4*k); if(sizes[k]<8||sizes[k]>bytes-offset) return false;
  p[k]=src+offset; end[k]=p[k]+sizes[k]-8; offset+=sizes[k];
  lens[k]=q+(k<rem); out[k]=dst; dst+=lens[k]; }
 if(offset!=bytes) return false;
 unsigned i=0;
 for(;i+4<=q;i+=4) {
  for(unsigned k=0;k<4;++k) { const uint8_t *r=p[k]+(pos[k]>>3); if(r>end[k]) return false; bits[k]=get64(r)>>(pos[k]&7); }
  if(pairs) for(unsigned r=0;r<2;++r) {
   for(unsigned k=0;k<4;++k) {
    unsigned v=pair[bits[k]&4095],len=(v>>16)&31;
    bits[k]>>=len; pos[k]+=len;
    if(!(v&(1u<<24))) { unsigned b=table[bits[k]&4095],l=b&15;
     bits[k]>>=l;pos[k]+=l;v|=(b>>4)<<8; }
    uint16_t syms=v;memcpy(out[k],&syms,2);out[k]+=2;
   }
  }
  else for(unsigned r=0;r<4;++r) {
   for(unsigned k=0;k<4;++k) {
    unsigned v=table[bits[k]&4095],len=v&15; *out[k]++=v>>4;
    bits[k]>>=len;pos[k]+=len;
   }
  }
 }
 for(;i<q;++i) {
  for(unsigned k=0;k<4;++k) {
   const uint8_t *r=p[k]+(pos[k]>>3); if(r>end[k]) return false;
   unsigned v=table[(get64(r)>>(pos[k]&7))&4095]; *out[k]++=v>>4; pos[k]+=v&15;
  }
 }
 for(unsigned k=0;k<rem;++k) { const uint8_t *r=p[k]+(pos[k]>>3);if(r>end[k]) return false;
  unsigned v=table[(get64(r)>>(pos[k]&7))&4095]; *out[k]=v>>4; }
 return true;
}
#ifndef ENTROPY_DECODER_ONLY
static inline std::vector<uint8_t> encode(const uint8_t *src,size_t n) {
 std::vector<uint8_t> stored(n+5); stored[0]=0; put32(stored.data()+1,uint32_t(n)); if(n) memcpy(stored.data()+5,src,n);
 if(n>0xffffffffu) return {};
 if(n&&std::all_of(src,src+n,[&](uint8_t x){return x==src[0];})) {stored.resize(6);stored[0]=2;stored[5]=src[0];return stored;}
 if(n<512) return stored;
 uint64_t freq[256]={0}; for(size_t i=0;i<n;++i) ++freq[src[i]];
 unsigned nonzero=0; for(unsigned i=0;i<256;++i) nonzero+=freq[i]!=0;
 if(nonzero==1) for(unsigned i=0;i<256;++i) if(!freq[i]) {freq[i]=1;break;}
 uint8_t lens[256]; uint64_t floor=1;
 for(;;) {
  struct Node { uint64_t w; int parent; } tree[511]; int active[511]; unsigned m=0;
  for(unsigned i=0;i<256;++i) { tree[i]={freq[i]?std::max(freq[i],floor):0,-1}; if(freq[i]) active[m++]=i; }
  unsigned used=256;
  while(m>1) { unsigned a=0,b=1; if(tree[active[a]].w>tree[active[b]].w) std::swap(a,b);
   for(unsigned j=2;j<m;++j) { if(tree[active[j]].w<tree[active[a]].w) {b=a;a=j;} else if(tree[active[j]].w<tree[active[b]].w) b=j; }
   unsigned x=active[a],y=active[b]; tree[used]={tree[x].w+tree[y].w,-1}; tree[x].parent=tree[y].parent=used;
   if(a>b) std::swap(a,b); active[b]=active[--m]; active[a]=used++;
  }
  unsigned maxlen=0; for(unsigned i=0;i<256;++i) {unsigned l=0; if(freq[i]) for(int j=i;tree[j].parent!=-1;j=tree[j].parent) ++l; lens[i]=l; maxlen=std::max(maxlen,l);}
  if(maxlen<=12) break; floor*=2;
 }
 unsigned count[13]={0},next[13]={0},codes[256];
 for(unsigned i=0;i<256;++i) ++count[lens[i]];
 unsigned c=0; for(unsigned i=1;i<=12;++i) {c=(c+(i==1?0:count[i-1]))<<1;next[i]=c;}
 for(unsigned i=0;i<256;++i) if(lens[i]) codes[i]=reverse(next[lens[i]]++,lens[i]);
 long double bad=0;
 for(unsigned i=0;i<256;++i) for(unsigned j=0;j<256;++j) if(lens[i]+lens[j]>12) bad+=(long double)freq[i]*freq[j];
 std::vector<uint8_t> out(149); out[0]=bad/(n*(long double)n)<0.025L?3:1; put32(out.data()+1,n);
 for(unsigned i=0;i<256;++i) out[5+i/2]|=lens[i]<<((i&1)*4);
 size_t begin=0;
 for(unsigned k=0;k<4;++k) {
  size_t len=n/4+(k<n%4),start=out.size(); uint64_t bits=0; unsigned have=0;
  for(size_t i=0;i<len;++i) {unsigned v=src[begin+i];bits|=uint64_t(codes[v])<<have;have+=lens[v];
   while(have>=8) {out.push_back(bits);bits>>=8;have-=8;} }
  if(have) out.push_back(bits);
  out.resize(out.size()+8,0); put32(out.data()+133+4*k,out.size()-start); begin+=len;
 }
 if(out.size()>=stored.size()) return stored;
 return out;
}
#endif
}
