#ifndef OPENSTACK_SPLIT_TEMPLATES_H
#define OPENSTACK_SPLIT_TEMPLATES_H
#include <map>
#include <functional>
#include <utility>
// Encoder-only fitting pass. Partition digit-run templates when eliminating
// numeric patches pays for every byte of the additional template metadata.
// Value-ordered maps and original template/row order make fitting deterministic.
static void split_templates(std::vector<Template>& ts,
                            std::vector<uint32_t>& ids,
                            const std::vector<std::string>& lines) {
  using Rows=std::vector<uint32_t>;
  std::vector<Rows> groups(ts.size());
  for(uint32_t i=0;i<ids.size();++i) groups[ids[i]].push_back(i);
  std::vector<Template> original=std::move(ts);
  ts.clear();
  auto refit=[&](const Template& parent,const Rows& rows) {
    Template child;child.line=lines[rows.front()];child.fields=parent.fields;
    child.freq=static_cast<uint32_t>(rows.size());
    for(Field& f:child.fields) {
      f.vary=false;
      f.base=f.mx=f.kind==1?0:val(child.line,f);
      for(uint32_t r:rows) {
        const std::string& s=lines[r];
        if(!f.vary&&s.compare(f.off,f.len,child.line,f.off,f.len)!=0)f.vary=true;
        if(f.kind!=1) {uint64_t v=val(s,f);f.base=std::min(f.base,v);f.mx=std::max(f.mx,v);}
      }
    }
    return child;
  };
  auto cost=[](const Template& t) {
    uint64_t n=t.line.size()+6;
    for(const Field& f:t.fields)if(f.vary) {
      n+=13;
      if(f.kind==0) {
        uint64_t range=f.mx-f.base;unsigned width=1;
        while(width<8&&(range>>(width*8)))++width;
        n+=uint64_t(t.freq)*width;
      }
    }
    return n;
  };
  std::function<void(const Template&,const Rows&)> partition;
  partition=[&](const Template& t,const Rows& rows) {
    const uint64_t oldcost=cost(t);uint64_t bestcost=oldcost;
    std::vector<Template> bestchildren;std::map<uint64_t,Rows> bestgroups;
    for(const Field& f:t.fields)if(f.vary&&f.kind==0) {
      std::map<uint64_t,Rows> parts;
      bool many=false;
      for(uint32_t r:rows) {
        uint64_t v=val(lines[r],f);parts[v].push_back(r);
        if(parts.size()>64) {many=true;break;}
      }
      if(many||parts.size()<2)continue;
      uint64_t trialcost=0;std::vector<Template> children;
      for(const auto& part:parts) {
        children.push_back(refit(t,part.second));trialcost+=cost(children.back());
      }
      // Strict improvement keeps field-order tie breaking stable.
      if(trialcost<bestcost) {
        bestcost=trialcost;bestchildren=std::move(children);bestgroups=std::move(parts);
      }
    }
    if(bestchildren.empty()) {
      uint32_t id=static_cast<uint32_t>(ts.size());
      ts.push_back(t);for(uint32_t r:rows)ids[r]=id;
    } else {
      size_t c=0;for(const auto& part:bestgroups)partition(bestchildren[c++],part.second);
    }
  };
  for(size_t i=0;i<original.size();++i)if(!groups[i].empty())partition(original[i],groups[i]);
}
#endif
