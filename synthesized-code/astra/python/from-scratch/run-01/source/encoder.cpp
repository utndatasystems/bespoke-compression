#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>
#include <string>
#include "codec.h"
#include "payload.inc"
struct Header{uint64_t magic,rawsize,tokens,dictsize;};
struct Node{int child[256];int sym;Node(){memset(child,0,sizeof child);sym=-1;}};
extern "C" int64_t lab_encode(const uint8_t*raw,size_t size,uint8_t*archive,size_t capacity){
 if(size>1000000000||capacity<sizeof(Header)+sizeof meta+sizeof data)return -1;std::vector<std::string>dict;dict.reserve(65536);for(unsigned i=0;i<65536;i++)dict.emplace_back((const char*)data+(meta[i]&0xffffff),meta[i]>>24);
 std::vector<Node>trie(1);trie.reserve(400000);for(unsigned code=0;code<65536;code++){int p=0;for(uint8_t c:dict[code]){if(!trie[p].child[c]){int ix=trie.size();trie.emplace_back();trie[p].child[c]=ix;}p=trie[p].child[c];}trie[p].sym=code;}
 std::vector<uint32_t>cost(size+1);std::vector<uint16_t>token(size);std::vector<uint8_t>len(size);
 for(size_t p=size;p-->0;){uint32_t best=1+cost[p+1];int id=0,sz=1,node=0;for(size_t k=p;k<size&&k<p+64;k++){node=trie[node].child[raw[k]];if(!node)break;if(trie[node].sym>=0&&1+cost[k+1]<=best){best=1+cost[k+1];id=trie[node].sym;sz=k+1-p;}}cost[p]=best;token[p]=id;len[p]=sz;}
 size_t bytes=sizeof(Header)+sizeof meta+sizeof data+size_t(cost[0])*2;if(bytes>capacity)return -1;Header h{0x314650424d415247ULL,size,cost[0],sizeof data};memcpy(archive,&h,sizeof h);memcpy(archive+sizeof h,meta,sizeof meta);memcpy(archive+sizeof h+sizeof meta,data,sizeof data);uint8_t*out=archive+sizeof h+sizeof meta+sizeof data;for(size_t p=0;p<size;p+=len[p]){memcpy(out,&token[p],2);out+=2;}return bytes;
}
