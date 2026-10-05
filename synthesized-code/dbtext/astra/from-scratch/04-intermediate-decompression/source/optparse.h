#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
// Reparse original rows for minimum token count using every learned string.
// Tie-breaks prefer tokens that had greater frequency in the training parse.
static void optimize_parse(std::vector<std::vector<uint16_t>>&rows,const std::vector<std::string>&dict,const uint8_t*raw,size_t n){
 std::vector<uint32_t> oldfreq(dict.size());for(const auto&r:rows)for(auto id:r)oldfreq[id]++;
 std::unordered_map<uint64_t,uint32_t> edges;size_t chars=0;for(const auto&s:dict)chars+=s.size();edges.reserve(chars);
 std::vector<int32_t> terminal(1,-1);
 for(size_t id=0;id<dict.size();id++){uint32_t node=0;for(unsigned char c:dict[id]){uint64_t key=(uint64_t(node)<<8)|c;auto it=edges.find(key);if(it==edges.end()){uint32_t next=terminal.size();terminal.push_back(-1);edges.emplace(key,next);node=next;}else node=it->second;}int32_t prev=terminal[node];if(prev<0||oldfreq[id]>oldfreq[prev])terminal[node]=id;}
 std::vector<uint32_t> dp;std::vector<uint16_t> choice;
 size_t begin=0,row=0;
 for(size_t end=0;end<n;end++)if(raw[end]=='\n'||end+1==n){size_t size=end+1-begin;dp.resize(size+1);choice.resize(size);dp[size]=0;
  for(size_t at=size;at-->0;){uint32_t node=0,best=0xffffffffU;uint16_t chosen=0;for(size_t j=at;j<size;j++){auto it=edges.find((uint64_t(node)<<8)|raw[begin+j]);if(it==edges.end())break;node=it->second;int32_t id=terminal[node];if(id>=0){uint32_t cost=dp[j+1]+1;if(cost<best||(cost==best&&oldfreq[id]>oldfreq[chosen])){best=cost;chosen=id;}}}dp[at]=best;choice[at]=chosen;}
  auto&r=rows[row++];r.clear();r.reserve(dp[0]);for(size_t at=0;at<size;){uint16_t id=choice[at];r.push_back(id);at+=dict[id].size();}begin=end+1;
 }
}
