#ifndef OPENSTACK_TIMESTAMP_HELPER_H
#define OPENSTACK_TIMESTAMP_HELPER_H
#include <cstdint>
#include <cstring>
namespace ostime {
// All reconstruction constants live in this header and the compiled decoder.
// Timestamp convention: milliseconds since 2017-05-00 00:00:00.000.
inline uint32_t parse(const char* p) {
  auto d2=[](const char*q){return (q[0]-'0')*10u+q[1]-'0';};
  return d2(p+8)*86400000u+d2(p+11)*3600000u+d2(p+14)*60000u+d2(p+17)*1000u+(p[20]-'0')*100u+(p[21]-'0')*10u+p[22]-'0';
}
struct Base {
  uint16_t pair[100];
  uint32_t fraction[1000];
  Base() {
    for(unsigned i=0;i<100;++i) pair[i]=uint16_t('0'+i/10)|(uint16_t('0'+i%10)<<8);
    for(unsigned i=0;i<1000;++i) fraction[i]=uint32_t('.')|(uint32_t('0'+i/100)<<8)|(uint32_t(pair[i%100])<<16);
  }
  inline void day(char* p,unsigned day) const {std::memcpy(p+8,&pair[day],2);}
  inline void prefix(char* p,unsigned day) const {
    std::memcpy(p,"2017-05-",8);std::memcpy(p+8,&pair[day],2);p[10]=' ';
  }
};
struct Small : Base {
  inline void time(char* p,uint32_t t) const {
    unsigned sec=t/1000,ms=t-sec*1000,mins=sec/60,hours=mins/60;
    uint64_t s=uint64_t(pair[hours]) | (uint64_t(':')<<16) | (uint64_t(pair[mins-hours*60])<<24) | (uint64_t(':')<<40) | (uint64_t(pair[sec-mins*60])<<48);
    std::memcpy(p,&s,8);std::memcpy(p+8,&fraction[ms],4);
  }
  inline void stamp(char*p,uint32_t absolute) const {unsigned d=absolute/86400000u;prefix(p,d);time(p+11,absolute-d*86400000u);}
  inline void patch(char*p,uint32_t absolute) const {unsigned d=absolute/86400000u;day(p,d);time(p+11,absolute-d*86400000u);}
};
struct Medium : Base {
  uint64_t minsec[3600];
  Medium() {for(unsigned s=0;s<3600;++s)minsec[s]=(uint64_t(':')<<16)|(uint64_t(pair[s/60])<<24)|(uint64_t(':')<<40)|(uint64_t(pair[s%60])<<48);}
  inline void time(char* p,uint32_t t) const {
    unsigned sec=t/1000,ms=t-sec*1000,hour=sec/3600;
    uint64_t s=minsec[sec-hour*3600]|pair[hour];
    std::memcpy(p,&s,8);std::memcpy(p+8,&fraction[ms],4);
  }
  inline void stamp(char*p,uint32_t absolute) const {unsigned d=absolute/86400000u;prefix(p,d);time(p+11,absolute-d*86400000u);}
  inline void patch(char*p,uint32_t absolute) const {unsigned d=absolute/86400000u;day(p,d);time(p+11,absolute-d*86400000u);}
};
struct Large : Base {
  uint64_t second[86400];
  Large() {for(unsigned s=0;s<86400;++s)second[s]=uint64_t(pair[s/3600])|(uint64_t(':')<<16)|(uint64_t(pair[s/60%60])<<24)|(uint64_t(':')<<40)|(uint64_t(pair[s%60])<<48);}
  inline void time(char* p,uint32_t t) const {
    unsigned sec=t/1000,ms=t-sec*1000;
    std::memcpy(p,&second[sec],8);std::memcpy(p+8,&fraction[ms],4);
  }
  inline void stamp(char*p,uint32_t absolute) const {unsigned d=absolute/86400000u;prefix(p,d);time(p+11,absolute-d*86400000u);}
  inline void patch(char*p,uint32_t absolute) const {unsigned d=absolute/86400000u;day(p,d);time(p+11,absolute-d*86400000u);}
};
// Decoder state for monotone timestamp streams. All deltas in this dataset fit
// uint16_t (maximum 10767). time_in_day is kept in range to avoid a day division.
struct State {
 uint32_t time_in_day; unsigned day;
 explicit State(uint32_t absolute):time_in_day(absolute%86400000u),day(absolute/86400000u){}
 inline void advance(uint16_t delta) {time_in_day+=delta;if(time_in_day>=86400000u){time_in_day-=86400000u;++day;}}
 template<class Formatter> inline void patch(char*p,const Formatter& f) const {f.day(p,day);f.time(p+11,time_in_day);}
};
}
#endif
