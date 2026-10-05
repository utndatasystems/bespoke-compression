#pragma once
// Include after common.h. Groups timestamps by the ordered template pair.
// Uses the already implemented integer codec; no extra entropy implementation.
#include <vector>
#include <cstdint>
namespace time_extra {
struct Groups {
 std::vector<uint32_t> id;
 std::vector<uint32_t> counts;
};
inline Groups group_ids(const std::vector<uint64_t>& order, unsigned nt,
                        unsigned threshold) {
 if(!nt||nt>4096||!threshold||threshold>1000000||order.size()>300000)throw 1;
 size_t keys=size_t(nt)*(nt+1);
 std::vector<uint32_t> freq(keys+1),map(keys+1,UINT32_MAX);
 unsigned prev=nt;
 for(auto current:order){
  if(current>=nt)throw 1;
  ++freq[size_t(prev)*nt+current];prev=current;
 }
 Groups g;g.id.reserve(order.size());prev=nt;
 // Assign compact IDs in first appearance order; all infrequent pairs share
 // the final key, whose compact ID still follows first appearance order.
 for(auto current:order) {
  size_t key=size_t(prev)*nt+current;
  if(freq[key]<threshold)key=keys;
  uint32_t id=map[key];
  if(id==UINT32_MAX){id=g.counts.size();map[key]=id;g.counts.push_back(0);}
  ++g.counts[id];g.id.push_back(id);prev=current;
 }
 return g;
}
// candidates[m][row] is an integer residual already transformed as required.
// Mode meaning is entirely the caller's responsibility. At most 256 modes.
template<class CandidateArrays>
inline Bytes encode(const std::vector<uint64_t>& order,unsigned nt,
                    const CandidateArrays& candidates){
 if(candidates.empty()||candidates.size()>256)throw 1;
 for(auto&v:candidates)if(v.size()!=order.size())throw 1;
 Bytes best;
 for(unsigned threshold:{20u,32u,50u,64u,100u,200u}){
  auto groups=group_ids(order,nt,threshold);size_t ng=groups.counts.size();
  std::vector<Bytes> chosen(ng);std::vector<uint8_t> modes(ng);
  for(size_t mode=0;mode<candidates.size();mode++){
   std::vector<std::vector<uint64_t>> cols(ng);
   for(size_t g=0;g<ng;g++)cols[g].reserve(groups.counts[g]);
   for(size_t i=0;i<order.size();i++)cols[groups.id[i]].push_back(candidates[mode][i]);
   for(size_t g=0;g<ng;g++){auto b=ints_encode(cols[g]);if(chosen[g].empty()||b.size()<chosen[g].size()){chosen[g]=std::move(b);modes[g]=mode;}}
  }
  Bytes b;put(b,threshold);
  for(size_t g=0;g<ng;g++){b.push_back(modes[g]);block(b,chosen[g]);}
  if(best.empty()||b.size()<best.size())best=std::move(b);
 }
 return best;
}
// Output stays in original row order. Values are encoded residuals; caller
// applies its predictor according to mode[row] during full reconstruction.
inline void decode_reader(Reader& r,const std::vector<uint64_t>& order,unsigned nt,
                   std::vector<uint64_t>& values,std::vector<uint8_t>& mode,
                   unsigned max_modes=4){
 if(!max_modes||max_modes>256)throw 1;
 uint64_t threshold=r.get();
 if(!threshold||threshold>1000000)throw 1;
 auto groups=group_ids(order,nt,unsigned(threshold));size_t ng=groups.counts.size();
 std::vector<std::vector<uint64_t>> cols(ng);std::vector<uint8_t> modes(ng);
 std::vector<uint32_t> cursor(ng);
 for(size_t g=0;g<ng;g++){
  modes[g]=r.byte();if(modes[g]>=max_modes)throw 1;
  cols[g]=readints(r,groups.counts[g]);
 }
 if(!r.done())throw 1;
 values.resize(order.size());mode.resize(order.size());
 for(size_t i=0;i<order.size();i++){unsigned g=groups.id[i];values[i]=cols[g][cursor[g]++];mode[i]=modes[g];}
}
inline void decode(const Bytes& b,const std::vector<uint64_t>& order,unsigned nt,
                   std::vector<uint64_t>& values,std::vector<uint8_t>& modes,
                   unsigned max_modes=4){
 Reader r(b);decode_reader(r,order,nt,values,modes,max_modes);
}
inline bool decode(const uint8_t*data,size_t size,const std::vector<uint64_t>& order,
                   unsigned nt,std::vector<uint64_t>&values,Bytes&modes,
                   unsigned max_modes=4){
 if(!data)return false;
 try {Reader r(data,size);decode_reader(r,order,nt,values,modes,max_modes);return true;}
 catch(...){return false;}
}
}
