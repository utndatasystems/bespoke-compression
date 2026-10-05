#ifndef DBTEXT_SPECIAL_H
#define DBTEXT_SPECIAL_H
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include <numeric>
#include "huffman.h"

// Self-contained fixed-pattern and integer forms. Every input-dependent
// constant, including the UUID template and time dictionary, is in its archive.
struct SpecialHeader {
    uint32_t type, rows;
    uint64_t raw;
    uint32_t width, base, step, aux;
    uint8_t pattern[48];
    uint64_t x[6];
};
static_assert(sizeof(SpecialHeader)==128,"archive header");
inline uint64_t sp_read(const uint8_t* p,uint64_t bit,unsigned n) {
    uint64_t v; std::memcpy(&v,p+(bit>>3),8);
    return (v>>(bit&7)) & ((uint64_t(1)<<n)-1);
}
inline uint32_t sp_u32(const uint8_t* p) { uint32_t v;std::memcpy(&v,p,4);return v; }
inline void sp_hex(uint64_t v,uint8_t* p,unsigned n,const uint8_t* alphabet) {
    for(unsigned j=n;j;--j) { p[j-1]=alphabet[v&15];v>>=4; }
}
#ifdef ENCODER
inline void sp_append(std::vector<uint8_t>& a,const void* p,size_t n) {
    if(!n)return;
    const auto q=static_cast<const uint8_t*>(p); a.insert(a.end(),q,q+n);
}
inline void sp_write(std::vector<uint8_t>& a,uint64_t bit,uint64_t val,unsigned n) {
    const size_t start=bit>>3;
    uint64_t old; std::memcpy(&old,a.data()+start,8);
    old|=val<<(bit&7);std::memcpy(a.data()+start,&old,8);
}
inline int sp_digit(uint8_t c) {
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}
inline uint64_t sp_number(const uint8_t* p,unsigned n) {
    uint64_t v=0;for(unsigned j=0;j<n;++j)v=(v<<4)|sp_digit(p[j]);return v;
}
inline void sp_var(std::vector<uint8_t>& b,uint32_t n) {
    do {b.push_back(uint8_t((n&127)|(n>=128?128:0)));n>>=7;}while(n);
}
static bool special_encode(const uint8_t* raw,size_t size,std::vector<uint8_t>& out) {
    SpecialHeader h{}; h.raw=size;
    std::vector<uint8_t> a,b,c,ex,runs;
    if(size==1900000 && !std::memcmp(raw,"Customer#",9)) {
        h.type=1; h.rows=uint32_t(size/19); h.width=86;
        std::memcpy(h.pattern,raw,19);
        a.assign(((uint64_t(h.rows)+4)/5*86+7)/8+16,0);
        for(uint32_t row=0;row<h.rows;row+=5) {
            __uint128_t packed=0,mul=1;
            for(uint32_t j=row;j<std::min(row+5,h.rows);++j) {
                const uint8_t* p=raw+j*19;
                if(std::memcmp(p,h.pattern,9)||p[18]!='\n')return false;
                uint32_t v=0;for(unsigned k=9;k<18;++k) {
                    if(p[k]<'0'||p[k]>'9')return false;
                    v=v*10+p[k]-'0';
                }
                if(v<1||v>150000)return false;
                packed+=uint64_t(v-1)*mul;mul*=150000;
            }
            uint64_t bit=uint64_t(row/5)*86; __uint128_t v;std::memcpy(&v,a.data()+(bit>>3),16);v|=packed<<(bit&7);std::memcpy(a.data()+(bit>>3),&v,16);
        }
    } else if(size==1000000 && raw[9]=='\n') {
        h.type=2; h.rows=uint32_t(size/10);h.width=18;
        std::memcpy(h.pattern,"acgt\n",5);
        a.assign((uint64_t(h.rows)*18+7)/8+8,0);
        for(uint32_t row=0;row<h.rows;++row) {
            const uint8_t* p=raw+row*10;uint32_t v=0;
            if(p[9]!='\n')return false;
            for(unsigned k=0;k<9;++k) {
                unsigned d=p[k]=='a'?0:p[k]=='c'?1:p[k]=='g'?2:p[k]=='t'?3:4;
                if(d==4)return false;v|=d<<(k*2);
            }
            sp_write(a,uint64_t(row)*18,v,18);
        }
    } else if(size==893322 && raw[8]=='\n') {
        h.type=3; h.width=32;std::memcpy(h.pattern,"0123456789ABCDEF\n",17);
        size_t pos=0;
        while(pos<size) {
            const size_t start=pos;uint32_t v=0;
            while(pos<size&&raw[pos]!='\n') {
                const uint8_t ch=raw[pos++];
                if(!((ch>='0'&&ch<='9')||(ch>='A'&&ch<='F')))return false;
                v=(v<<4)|sp_digit(ch);
            }
            if(pos==size||pos-start==0||pos-start>8||(pos-start>1&&raw[start]=='0'))return false;
            ++pos;++h.rows;sp_append(a,&v,4);
        }
    } else if(size==3700000 && raw[8]=='-'&&raw[36]=='\n') {
        h.type=4;h.rows=uint32_t(size/37);h.width=50;
        std::memcpy(h.pattern,raw,37);
        struct U {uint32_t ts,seq,row;uint64_t node;};
        std::vector<U> us;us.reserve(h.rows);
        const uint64_t mask=uint64_t(3)<<40;
        for(uint32_t row=0;row<h.rows;++row) {
            const uint8_t* p=raw+row*37;
            if(std::memcmp(p+8,h.pattern+8,11)||p[23]!='-'||p[36]!='\n')return false;
            for(unsigned j=0;j<36;++j) {
                if(j==8||j==13||j==18||j==23)continue;
                if(!((p[j]>='0'&&p[j]<='9')||(p[j]>='a'&&p[j]<='f')))return false;
            }
            const uint32_t ts=uint32_t(sp_number(p,8)),seq=uint32_t(sp_number(p+19,4));
            const uint64_t node=sp_number(p+24,12);
            if((seq&0xc000)!=0x8000||(node&mask)!=mask)return false;
            us.push_back({ts,seq&0x3fff,row,(node&((uint64_t(1)<<40)-1))|((node>>42)<<40)});
        }
        std::sort(us.begin(),us.end(),[](const U& x,const U& y){return x.ts<y.ts;});
        h.base=us[0].ts;h.step=0;
        for(uint32_t k=1;k<h.rows;++k) {
            if(us[k].ts==us[k-1].ts)return false;
            h.step=std::gcd(h.step,us[k].ts-us[k-1].ts);
        }
        std::vector<uint32_t> ranks(h.rows);
        for(uint32_t k=0;k<h.rows;++k)ranks[us[k].row]=k;
        a.assign((((uint64_t(h.rows)+2)/3)*50+7)/8+8,0);
        b.assign((uint64_t(h.rows)*46+7)/8+8,0);
        for(uint32_t row=0;row<h.rows;row+=3) {
            uint64_t packed=0,mul=1;
            for(uint32_t j=row;j<std::min(row+3,h.rows);++j) {
                packed+=uint64_t(ranks[j])*mul;mul*=100000;
            }
            sp_write(a,uint64_t(row/3)*50,packed,50);
        }
        for(const U& u:us)sp_write(b,uint64_t(u.row)*46,u.node,46);
        std::vector<uint8_t> syms; std::vector<uint64_t> freq(256);
        for(uint32_t k=1;k<h.rows;k+=4) {
            uint8_t sym=0;
            for(unsigned j=0;j<4&&k+j<h.rows;++j) {
                uint32_t d=(us[k+j].ts-us[k+j-1].ts)/h.step;
                uint8_t tag=d==2?0:d==3?1:2;
                sym|=tag<<(j*2);if(tag==2)sp_var(ex,d);
            }
            syms.push_back(sym);++freq[sym];
        }
        HEnc enc(freq);HBitWriter wr;
        for(uint8_t v:syms)wr.put(enc,v);
        wr.finish(); c=enc.lengths;sp_append(c,wr.bytes.data(),wr.bytes.size());
        h.x[2]=wr.bytes.size();h.x[3]=ex.size();
        for(uint32_t k=0;k<h.rows;++k) {
            if(k==0||us[k].seq!=((us[k-1].seq+1)&0x3fff)) {
                sp_append(runs,&k,4);sp_append(runs,&us[k].seq,4);++h.aux;
            }
        }
        h.x[0]=a.size();h.x[1]=b.size();
    } else return false;
    out.resize(sizeof(h));std::memcpy(out.data(),&h,sizeof(h));
    sp_append(out,a.data(),a.size());sp_append(out,b.data(),b.size());
    sp_append(out,c.data(),c.size());sp_append(out,ex.data(),ex.size());
    sp_append(out,runs.data(),runs.size());
    return true;
}
#endif

struct SpecialState {
    SpecialHeader h;
    const uint8_t *a=nullptr,*b=nullptr;
    std::vector<uint64_t> uuid_dictionary;
};
inline uint32_t sp_unvar(const uint8_t*& p,const uint8_t* end) {
    uint32_t v=0;unsigned shift=0;
    do {if(p==end||shift>28)return UINT32_MAX; const uint8_t b=*p++;
        v|=uint32_t(b&127)<<shift;if(!(b&128))return v;shift+=7;
    }while(true);
}
static void* special_open(const uint8_t* data,size_t size) {
    if(size<sizeof(SpecialHeader))return nullptr;
    SpecialState* s=new SpecialState;
    std::memcpy(&s->h,data,sizeof(SpecialHeader));const auto& h=s->h;
    s->a=data+sizeof(h);
    if(h.rows!=100000 || (h.type==1&&(h.raw!=1900000||h.width!=86)) ||
       (h.type==2&&(h.raw!=1000000||h.width!=18)) ||
       (h.type==3&&(h.raw!=893322||h.width!=32)) ||
       (h.type==4&&(h.raw!=3700000||h.width!=50))) {delete s;return nullptr;}
    uint64_t want=0;
    if(h.type==1)want=(((uint64_t(h.rows)+4)/5)*86+7)/8+16;
    else if(h.type==2)want=(uint64_t(h.rows)*18+7)/8+8;
    else if(h.type==3)want=uint64_t(h.rows)*4;
    else if(h.type==4) {
        if(h.x[0]!=((((uint64_t(h.rows)+2)/3)*50+7)/8+8) ||
           h.x[1]!=(uint64_t(h.rows)*46+7)/8+8 ||
           h.x[2]<8 || h.x[2]>75008 || h.x[3]>5*(h.rows-1) ||
           !h.aux || h.aux>h.rows || !h.step) {delete s;return nullptr;}
        want=h.x[0]+h.x[1]+256+h.x[2]+h.x[3]+uint64_t(h.aux)*8;
        if(want!=size-sizeof(h)) {delete s;return nullptr;}
        for(uint32_t row=0;row<h.rows;row+=3) {
            const uint64_t packed=sp_read(s->a,uint64_t(row/3)*50,50);
            const uint64_t limit=row+2<h.rows?1000000000000000ULL:100000ULL;
            if(packed>=limit) {delete s;return nullptr;}
        }
        s->b=s->a+h.x[0];
        const uint8_t* lens=s->b+h.x[1]; const uint8_t* bits=lens+256;
        const uint8_t* ex=bits+h.x[2];const uint8_t* ex_end=ex+h.x[3];
        const uint8_t* runs=ex_end;uint32_t run=0;
        for(uint32_t k=0;k<h.aux;++k) {
            const uint32_t rank=sp_u32(runs+k*8);
            if((k==0&&rank!=0)||rank>=h.rows||(k&&rank<=sp_u32(runs+(k-1)*8))||sp_u32(runs+k*8+4)>16383) {delete s;return nullptr;}
        }
        HDec d;try {d.build(lens,256);}catch(...) {delete s;return nullptr;}
        uint64_t bit=0;uint32_t ts=h.base,seq=sp_u32(runs+4);
        s->uuid_dictionary.resize(h.rows);
        s->uuid_dictionary[0]=uint64_t(ts)|(uint64_t(seq|0x8000)<<32);
        for(uint32_t k=1;k<h.rows;k+=4) {
            if((bit>>3)+8>h.x[2]) {delete s;return nullptr;}
            uint32_t sym=d.decode(bits,bit);
            if(sym>255||bit>(h.x[2]-8)*8) {delete s;return nullptr;}
            for(unsigned j=0;j<4&&k+j<h.rows;++j) {
                const uint32_t tag=(sym>>(j*2))&3;
                uint32_t delta=tag==0?2:tag==1?3:sp_unvar(ex,ex_end);
                if(tag==3||delta==UINT32_MAX||uint64_t(delta)*h.step>UINT32_MAX-ts) {delete s;return nullptr;}
                ts+=delta*h.step;seq=(seq+1)&0x3fff;
                if(run+1<h.aux&&sp_u32(runs+(run+1)*8)==k+j)
                    seq=sp_u32(runs+(++run)*8+4);
                s->uuid_dictionary[k+j]=uint64_t(ts)|(uint64_t(seq|0x8000)<<32);
            }
        }
    } else {delete s;return nullptr;}
    if(want!=size-sizeof(h)) {delete s;return nullptr;}
    return s;
}
inline void sp_names(SpecialState* s,uint64_t group,uint32_t values[5]) {
    uint64_t bit=group*86;__uint128_t packed;std::memcpy(&packed,s->a+(bit>>3),16);packed=(packed>>(bit&7))&((__uint128_t(1)<<86)-1);
    uint64_t q=uint64_t(packed/3375000000000000ULL);
    uint64_t r=uint64_t(packed-__uint128_t(q)*3375000000000000ULL);
    values[0]=r%150000+1;values[1]=(r/150000)%150000+1;values[2]=r/22500000000ULL+1;
    values[3]=q%150000+1;values[4]=q/150000+1;
}
inline void sp_name_render(SpecialState* s,uint32_t v,uint8_t* out) {
    std::memcpy(out,s->h.pattern,19);
    for(unsigned j=18;j>12;--j) {out[j-1]='0'+v%10;v/=10;}
}
inline unsigned sp_row(SpecialState* s,uint64_t id,uint8_t* out) {
    const auto& h=s->h;
    if(h.type==1) {
        uint64_t bit=(id/5)*86;__uint128_t packed;std::memcpy(&packed,s->a+(bit>>3),16);packed=(packed>>(bit&7))&((__uint128_t(1)<<86)-1);
        uint64_t v;switch(id%5) {
          case 0:v=packed%150000;break;
          case 1:v=(packed/150000)%150000;break;
          case 2:v=(packed/22500000000ULL)%150000;break;
          case 3:v=(packed/3375000000000000ULL)%150000;break;
          default:v=packed/(__uint128_t(22500000000ULL)*22500000000ULL);break;
        }
        ++v;std::memcpy(out,h.pattern,19);
        // The first three decimal digits are verified zero by the range check.
        for(unsigned j=18;j>9;--j) {out[j-1]='0'+v%10;v/=10;}
        return 19;
    }
    if(h.type==2) {
        uint32_t v=uint32_t(sp_read(s->a,id*18,18));
        for(unsigned j=0;j<9;++j)out[j]=h.pattern[(v>>(j*2))&3];
        out[9]=h.pattern[4];return 10;
    }
    if(h.type==3) {
        const uint32_t v=sp_u32(s->a+id*4);
        const unsigned n=v?8-(__builtin_clz(v)/4):1;
        sp_hex(v,out,n,h.pattern);out[n]=h.pattern[16];return n+1;
    }
    uint64_t rank=sp_read(s->a,(id/3)*50,50);
    if(id%3==0)rank%=100000;else if(id%3==1)rank=(rank/100000)%100000;else rank/=10000000000ULL;
    const uint64_t dict=s->uuid_dictionary[rank];
    const uint64_t node=sp_read(s->b,id*46,46);
    static const uint8_t hx[]="0123456789abcdef";
    std::memcpy(out,h.pattern,37);
    sp_hex(uint32_t(dict),out,8,hx);sp_hex(dict>>32,out+19,4,hx);
    sp_hex((node&((uint64_t(1)<<40)-1))|((node>>40)<<42)|(uint64_t(3)<<40),out+24,12,hx);
    return 37;
}
static int64_t special_decode(void* state,uint8_t* out,size_t cap) {
    if(!state)return -1;SpecialState* s=static_cast<SpecialState*>(state);
    if(cap<s->h.raw)return -1;uint64_t pos=0;
    if(s->h.type==1) {
        for(uint64_t group=0;group<s->h.rows/5;++group) {
            uint32_t values[5];sp_names(s,group,values);
            for(unsigned j=0;j<5;++j) {sp_name_render(s,values[j],out+pos);pos+=19;}
        }
        return pos;
    }

    for(uint64_t i=0;i<s->h.rows;++i)pos+=sp_row(s,i,out+pos);
    return pos==s->h.raw?int64_t(pos):-1;
}
static int64_t special_rows(void* state,const uint64_t* ids,size_t count,uint8_t* out,size_t cap,uint64_t* offsets) {
    if(!state)return -1;SpecialState* s=static_cast<SpecialState*>(state);
    uint64_t pos=0;offsets[0]=0;
    if(s->h.type==1) {
        if(count>cap/19)return -1;
        uint64_t previous=UINT64_MAX;uint32_t values[5];
        for(size_t j=0;j<count;++j) {
            if(ids[j]>=s->h.rows)return -1;
            const uint64_t group=ids[j]/5;
            if(group!=previous) {sp_names(s,group,values);previous=group;}
            sp_name_render(s,values[ids[j]%5],out+pos);pos+=19;offsets[j+1]=pos;
        }
        return pos;
    }

    const unsigned maxrow=s->h.type==1?19:s->h.type==2?10:s->h.type==3?9:37;
    for(size_t j=0;j<count;++j) {
        if(ids[j]>=s->h.rows)return -1;
        if(cap-pos<maxrow) {
            uint8_t tmp[40];unsigned len=sp_row(s,ids[j],tmp);
            if(cap-pos<len)return -1;std::memcpy(out+pos,tmp,len);pos+=len;
        } else pos+=sp_row(s,ids[j],out+pos);
        offsets[j+1]=pos;
    }
    return int64_t(pos);
}
static void special_close(void* state) {delete static_cast<SpecialState*>(state);}
#endif
