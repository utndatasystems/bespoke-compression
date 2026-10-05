static bool alt_fn0(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,106);
memcpy(d+142,t.lit+142,190);
ff_time(d+46,ms);
ff_uuid(d+106,p+0);
uint32_t x1=*(p+16);
if(x1>=t.f[1].n)return false;memcpy(d+311,t.f[1].dict+x1*4,4);
uint32_t x2=*(p+17);
ff_decimal(d+322,x2,1);
uint32_t x3=(r16(p+18)|(uint32_t)(p+18)[2]<<16);
ff_decimal(d+324,x3,7);
return true;}
static bool alt_fn1(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,106);
memcpy(d+142,t.lit+142,190);
ff_time(d+46,ms);
ff_uuid(d+106,p+0);
uint32_t x1=*(p+16);
if(x1>=t.f[1].n)return false;memcpy(d+311,t.f[1].dict+x1*4,4);
uint32_t x2=(r16(p+17)|(uint32_t)(p+17)[2]<<16);
ff_decimal(d+324,x2,7);
return true;}
static bool alt_fn2(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,284);
ff_time(d+50,ms);
return true;}
static bool alt_fn3(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,352);
ff_time(d+50,ms);
return true;}
static bool alt_fn4(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,245);
ff_time(d+50,ms);
return true;}
static bool alt_fn5(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,104);
memcpy(d+140,t.lit+140,190);
ff_time(d+44,ms);
ff_uuid(d+104,p+0);
uint32_t x1=*(p+16);
if(x1>=t.f[1].n)return false;memcpy(d+309,t.f[1].dict+x1*4,4);
uint32_t x2=(r16(p+17)|(uint32_t)(p+17)[2]<<16);
ff_decimal(d+322,x2,7);
return true;}
static bool alt_fn6(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,158);
memcpy(d+194,t.lit+194,31);
ff_time(d+50,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+158,s.uu+x0*36,36);
return true;}
static bool alt_fn7(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,248);
ff_time(d+50,ms);
return true;}
static bool alt_fn8(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,282);
ff_time(d+48,ms);
return true;}
static bool alt_fn9(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,350);
ff_time(d+48,ms);
return true;}
static bool alt_fn10(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,243);
ff_time(d+48,ms);
return true;}
static bool alt_fn11(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,98);
memcpy(d+134,t.lit+134,85);
memcpy(d+255,t.lit+255,52);
ff_time(d+50,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+98,s.uu+x0*36,36);
uint32_t x1=r16(p+2);
if(x1>=s.nu)return false;memcpy(d+219,s.uu+x1*36,36);
return true;}
static bool alt_fn12(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,106);
memcpy(d+142,t.lit+142,201);
ff_time(d+46,ms);
uint32_t x0=*(p+0);
if(x0>=t.f[0].n)return false;memcpy(d+59,t.f[0].dict+x0*5,5);
uint32_t x1=r16(p+1);
if(x1>=s.nu)return false;memcpy(d+106,s.uu+x1*36,36);
uint32_t x2=*(p+3);
if(x2>=t.f[2].n)return false;memcpy(d+314,t.f[2].dict+x2*3,3);
uint32_t x3=*(p+4);
if(x3>=t.f[3].n)return false;memcpy(d+323,t.f[3].dict+x3*3,3);
uint32_t x4=(r16(p+5)|(uint32_t)(p+5)[2]<<16);
ff_decimal(d+335,x4,7);
return true;}
static bool alt_fn13(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,158);
memcpy(d+194,t.lit+194,76);
ff_time(d+50,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+158,s.uu+x0*36,36);
return true;}
static bool alt_fn14(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,101);
memcpy(d+137,t.lit+137,125);
ff_time(d+46,ms);
uint32_t x0=*(p+0);
if(x0>=t.f[0].n)return false;memcpy(d+59,t.f[0].dict+x0*5,5);
ff_uuid(d+101,p+1);
uint32_t x2=*(p+17);
ff_decimal(d+155,x2,2);
uint32_t x3=*(p+18);
ff_decimal(d+158,x3,3);
uint32_t x4=*(p+19);
if(x4>=t.f[4].n)return false;memcpy(d+189,t.f[4].dict+x4*4,4);
uint32_t x5=*(p+20);
ff_decimal(d+194,x5,2);
uint32_t x6=*(p+21);
ff_decimal(d+197,x6,2);
uint32_t x7=*(p+22);
if(x7>=t.f[7].n)return false;memcpy(d+242,t.f[7].dict+x7*3,3);
uint32_t x8=(r16(p+23)|(uint32_t)(p+23)[2]<<16);
ff_decimal(d+254,x8,7);
return true;}
static bool alt_fn15(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,109);
memcpy(d+145,t.lit+145,31);
ff_time(d+50,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+109,s.uu+x0*36,36);
return true;}
static bool alt_fn16(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,98);
memcpy(d+134,t.lit+134,85);
memcpy(d+255,t.lit+255,60);
ff_time(d+50,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+98,s.uu+x0*36,36);
uint32_t x1=r16(p+2);
if(x1>=s.nu)return false;memcpy(d+219,s.uu+x1*36,36);
return true;}
static bool alt_fn17(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,98);
memcpy(d+134,t.lit+134,85);
memcpy(d+255,t.lit+255,42);
ff_time(d+50,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+98,s.uu+x0*36,36);
uint32_t x1=r16(p+2);
if(x1>=s.nu)return false;memcpy(d+219,s.uu+x1*36,36);
return true;}
static bool alt_fn18(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,98);
memcpy(d+134,t.lit+134,85);
memcpy(d+255,t.lit+255,47);
ff_time(d+50,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+98,s.uu+x0*36,36);
uint32_t x1=r16(p+2);
if(x1>=s.nu)return false;memcpy(d+219,s.uu+x1*36,36);
return true;}
static bool alt_fn19(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,98);
memcpy(d+134,t.lit+134,85);
memcpy(d+255,t.lit+255,35);
ff_time(d+50,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+98,s.uu+x0*36,36);
uint32_t x1=r16(p+2);
if(x1>=s.nu)return false;memcpy(d+219,s.uu+x1*36,36);
return true;}
static bool alt_fn20(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,98);
memcpy(d+134,t.lit+134,85);
memcpy(d+255,t.lit+255,39);
ff_time(d+50,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+98,s.uu+x0*36,36);
uint32_t x1=r16(p+2);
if(x1>=s.nu)return false;memcpy(d+219,s.uu+x1*36,36);
return true;}
static bool alt_fn21(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,98);
memcpy(d+134,t.lit+134,85);
memcpy(d+255,t.lit+255,19);
ff_time(d+50,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+98,s.uu+x0*36,36);
uint32_t x1=r16(p+2);
if(x1>=s.nu)return false;memcpy(d+219,s.uu+x1*36,36);
return true;}
static bool alt_fn22(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,106);
memcpy(d+142,t.lit+142,135);
memcpy(d+313,t.lit+313,49);
ff_time(d+46,ms);
ff_uuid(d+106,p+0);
uint32_t x1=r16(p+16);
if(x1>=s.nu)return false;memcpy(d+277,s.uu+x1*36,36);
uint32_t x2=*(p+18);
if(x2>=t.f[2].n)return false;memcpy(d+341,t.f[2].dict+x2*4,4);
uint32_t x3=(r16(p+19)|(uint32_t)(p+19)[2]<<16);
ff_decimal(d+354,x3,7);
return true;}
static bool alt_fn23(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,103);
memcpy(d+139,t.lit+139,85);
memcpy(d+260,t.lit+260,17);
ff_time(d+50,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+103,s.uu+x0*36,36);
uint32_t x1=r16(p+2);
if(x1>=s.nu)return false;memcpy(d+224,s.uu+x1*36,36);
return true;}
static bool alt_fn24(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,158);
memcpy(d+194,t.lit+194,30);
ff_time(d+50,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+158,s.uu+x0*36,36);
return true;}
static bool alt_fn25(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,254);
ff_time(d+50,ms);
return true;}
static bool alt_fn26(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,125);
memcpy(d+161,t.lit+161,109);
memcpy(d+306,t.lit+306,14);
memcpy(d+356,t.lit+356,1);
ff_time(d+46,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+125,s.uu+x0*36,36);
ff_uuid(d+270,p+2);
uint32_t x2=r16(p+18);
if(x2>=s.nu)return false;memcpy(d+320,s.uu+x2*36,36);
return true;}
static bool alt_fn27(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,113);
memcpy(d+149,t.lit+149,33);
ff_time(d+50,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+113,s.uu+x0*36,36);
return true;}
static bool alt_fn28(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,99);
memcpy(d+135,t.lit+135,85);
memcpy(d+256,t.lit+256,62);
ff_time(d+50,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+99,s.uu+x0*36,36);
uint32_t x1=r16(p+2);
if(x1>=s.nu)return false;memcpy(d+220,s.uu+x1*36,36);
uint32_t x2=*(p+4);
ff_decimal(d+263,x2,2);
uint32_t x3=*(p+5);
ff_decimal(d+266,x3,2);
return true;}
static bool alt_fn29(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,99);
memcpy(d+135,t.lit+135,85);
memcpy(d+256,t.lit+256,40);
ff_time(d+50,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+99,s.uu+x0*36,36);
uint32_t x1=r16(p+2);
if(x1>=s.nu)return false;memcpy(d+220,s.uu+x1*36,36);
uint32_t x2=*(p+4);
ff_decimal(d+263,x2,2);
uint32_t x3=*(p+5);
ff_decimal(d+266,x3,2);
return true;}
static bool alt_fn30(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,99);
memcpy(d+135,t.lit+135,85);
memcpy(d+256,t.lit+256,23);
ff_time(d+50,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+99,s.uu+x0*36,36);
uint32_t x1=r16(p+2);
if(x1>=s.nu)return false;memcpy(d+220,s.uu+x1*36,36);
return true;}
static bool alt_fn31(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,113);
memcpy(d+149,t.lit+149,35);
ff_time(d+50,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+113,s.uu+x0*36,36);
return true;}
static bool alt_fn32(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,103);
memcpy(d+139,t.lit+139,85);
memcpy(d+260,t.lit+260,50);
memcpy(d+346,t.lit+346,5);
ff_time(d+50,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+103,s.uu+x0*36,36);
uint32_t x1=r16(p+2);
if(x1>=s.nu)return false;memcpy(d+224,s.uu+x1*36,36);
uint32_t x2=r16(p+4);
if(x2>=s.nu)return false;memcpy(d+310,s.uu+x2*36,36);
return true;}
static bool alt_fn33(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,103);
memcpy(d+139,t.lit+139,85);
memcpy(d+260,t.lit+260,38);
memcpy(d+334,t.lit+334,14);
ff_time(d+50,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+103,s.uu+x0*36,36);
uint32_t x1=r16(p+2);
if(x1>=s.nu)return false;memcpy(d+224,s.uu+x1*36,36);
uint32_t x2=r16(p+4);
if(x2>=s.nu)return false;memcpy(d+298,s.uu+x2*36,36);
return true;}
static bool alt_fn34(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,99);
memcpy(d+135,t.lit+135,85);
memcpy(d+256,t.lit+256,63);
ff_time(d+50,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+99,s.uu+x0*36,36);
uint32_t x1=r16(p+2);
if(x1>=s.nu)return false;memcpy(d+220,s.uu+x1*36,36);
uint32_t x2=*(p+4);
ff_decimal(d+263,x2,1);
uint32_t x3=*(p+5);
ff_decimal(d+265,x3,2);
return true;}
static bool alt_fn35(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,99);
memcpy(d+135,t.lit+135,130);
ff_time(d+46,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+99,s.uu+x0*36,36);
return true;}
static bool alt_fn36(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,101);
memcpy(d+137,t.lit+137,127);
ff_time(d+46,ms);
uint32_t x0=*(p+0);
if(x0>=t.f[0].n)return false;memcpy(d+59,t.f[0].dict+x0*5,5);
ff_uuid(d+101,p+1);
uint32_t x2=*(p+17);
ff_decimal(d+155,x2,2);
uint32_t x3=*(p+18);
ff_decimal(d+158,x3,3);
uint32_t x4=(r16(p+19)|(uint32_t)(p+19)[2]<<16);
ff_decimal(d+256,x4,7);
return true;}
static bool alt_fn37(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,106);
memcpy(d+142,t.lit+142,183);
ff_time(d+46,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+106,s.uu+x0*36,36);
uint32_t x1=(r16(p+2)|(uint32_t)(p+2)[2]<<16);
ff_decimal(d+317,x1,7);
return true;}
static bool alt_fn38(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,106);
memcpy(d+142,t.lit+142,138);
memcpy(d+316,t.lit+316,48);
ff_time(d+46,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+106,s.uu+x0*36,36);
uint32_t x1=r16(p+2);
if(x1>=s.nu)return false;memcpy(d+280,s.uu+x1*36,36);
uint32_t x2=(r16(p+4)|(uint32_t)(p+4)[2]<<16);
ff_decimal(d+356,x2,7);
return true;}
static bool alt_fn39(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,99);
memcpy(d+135,t.lit+135,85);
memcpy(d+256,t.lit+256,56);
ff_time(d+50,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+99,s.uu+x0*36,36);
uint32_t x1=r16(p+2);
if(x1>=s.nu)return false;memcpy(d+220,s.uu+x1*36,36);
uint32_t x2=*(p+4);
ff_decimal(d+265,x2,2);
return true;}
static bool alt_fn40(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,251);
ff_time(d+50,ms);
return true;}
static bool alt_fn41(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,205);
ff_time(d+50,ms);
uint32_t x0=*(p+0);
ff_decimal(d+203,x0,1);
return true;}
static bool alt_fn42(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,273);
ff_time(d+50,ms);
return true;}
static bool alt_fn43(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,101);
memcpy(d+137,t.lit+137,124);
ff_time(d+46,ms);
uint32_t x0=*(p+0);
if(x0>=t.f[0].n)return false;memcpy(d+59,t.f[0].dict+x0*5,5);
ff_uuid(d+101,p+1);
uint32_t x2=*(p+17);
ff_decimal(d+155,x2,2);
uint32_t x3=*(p+18);
ff_decimal(d+158,x3,2);
uint32_t x4=*(p+19);
if(x4>=t.f[4].n)return false;memcpy(d+188,t.f[4].dict+x4*4,4);
uint32_t x5=*(p+20);
ff_decimal(d+193,x5,2);
uint32_t x6=*(p+21);
ff_decimal(d+196,x6,2);
uint32_t x7=*(p+22);
if(x7>=t.f[7].n)return false;memcpy(d+241,t.f[7].dict+x7*3,3);
uint32_t x8=(r16(p+23)|(uint32_t)(p+23)[2]<<16);
ff_decimal(d+253,x8,7);
return true;}
static bool alt_fn44(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,106);
memcpy(d+142,t.lit+142,189);
ff_time(d+46,ms);
ff_uuid(d+106,p+0);
uint32_t x1=*(p+16);
if(x1>=t.f[1].n)return false;memcpy(d+302,t.f[1].dict+x1*3,3);
uint32_t x2=*(p+17);
if(x2>=t.f[2].n)return false;memcpy(d+311,t.f[2].dict+x2*3,3);
uint32_t x3=(r16(p+18)|(uint32_t)(p+18)[2]<<16);
ff_decimal(d+323,x3,7);
return true;}
static bool alt_fn45(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,213);
ff_time(d+46,ms);
uint32_t x0=*(p+0);
if(x0>=t.f[0].n)return false;memcpy(d+59,t.f[0].dict+x0*5,5);
uint32_t x1=*(p+1);
ff_decimal(d+106,x1,2);
uint32_t x2=*(p+2);
ff_decimal(d+109,x2,3);
uint32_t x3=r16(p+3);
ff_decimal(d+205,x3,7);
return true;}
static bool alt_fn46(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,215);
ff_time(d+46,ms);
uint32_t x0=*(p+0);
if(x0>=t.f[0].n)return false;memcpy(d+59,t.f[0].dict+x0*5,5);
uint32_t x1=*(p+1);
ff_decimal(d+106,x1,2);
uint32_t x2=*(p+2);
ff_decimal(d+109,x2,3);
uint32_t x3=r16(p+3);
ff_decimal(d+207,x3,7);
return true;}
static bool alt_fn47(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,265);
ff_time(d+50,ms);
return true;}
static bool alt_fn48(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,327);
ff_time(d+50,ms);
return true;}
static bool alt_fn49(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,106);
memcpy(d+142,t.lit+142,189);
ff_time(d+46,ms);
ff_uuid(d+106,p+0);
uint32_t x1=(r16(p+16)|(uint32_t)(p+16)[2]<<16);
ff_decimal(d+323,x1,7);
return true;}
static bool alt_fn50(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,101);
memcpy(d+137,t.lit+137,126);
ff_time(d+46,ms);
uint32_t x0=*(p+0);
if(x0>=t.f[0].n)return false;memcpy(d+59,t.f[0].dict+x0*5,5);
ff_uuid(d+101,p+1);
uint32_t x2=*(p+17);
ff_decimal(d+155,x2,2);
uint32_t x3=*(p+18);
ff_decimal(d+158,x3,2);
uint32_t x4=(r16(p+19)|(uint32_t)(p+19)[2]<<16);
ff_decimal(d+255,x4,7);
return true;}
static bool alt_fn51(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,101);
memcpy(d+137,t.lit+137,110);
ff_time(d+46,ms);
uint32_t x0=*(p+0);
if(x0>=t.f[0].n)return false;memcpy(d+59,t.f[0].dict+x0*5,5);
ff_uuid(d+101,p+1);
uint32_t x2=*(p+17);
ff_decimal(d+155,x2,2);
uint32_t x3=*(p+18);
ff_decimal(d+158,x3,3);
uint32_t x4=(r16(p+19)|(uint32_t)(p+19)[2]<<16);
ff_decimal(d+239,x4,7);
return true;}
static bool alt_fn52(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,156);
memcpy(d+192,t.lit+192,31);
ff_time(d+48,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+156,s.uu+x0*36,36);
return true;}
static bool alt_fn53(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,106);
memcpy(d+142,t.lit+142,183);
ff_time(d+46,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+106,s.uu+x0*36,36);
uint32_t x1=*(p+2);
ff_decimal(d+315,x1,1);
uint32_t x2=(r16(p+3)|(uint32_t)(p+3)[2]<<16);
ff_decimal(d+317,x2,7);
return true;}
static bool alt_fn54(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,106);
memcpy(d+142,t.lit+142,138);
memcpy(d+316,t.lit+316,48);
ff_time(d+46,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+106,s.uu+x0*36,36);
uint32_t x1=r16(p+2);
if(x1>=s.nu)return false;memcpy(d+280,s.uu+x1*36,36);
uint32_t x2=*(p+4);
ff_decimal(d+354,x2,1);
uint32_t x3=(r16(p+5)|(uint32_t)(p+5)[2]<<16);
ff_decimal(d+356,x3,7);
return true;}
static bool alt_fn55(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,99);
memcpy(d+135,t.lit+135,85);
memcpy(d+256,t.lit+256,56);
ff_time(d+50,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+99,s.uu+x0*36,36);
uint32_t x1=r16(p+2);
if(x1>=s.nu)return false;memcpy(d+220,s.uu+x1*36,36);
uint32_t x2=*(p+4);
ff_decimal(d+263,x2,1);
uint32_t x3=*(p+5);
ff_decimal(d+265,x3,2);
return true;}
static bool alt_fn56(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,101);
memcpy(d+137,t.lit+137,120);
ff_time(d+46,ms);
uint32_t x0=*(p+0);
if(x0>=t.f[0].n)return false;memcpy(d+59,t.f[0].dict+x0*5,5);
ff_uuid(d+101,p+1);
uint32_t x2=*(p+17);
ff_decimal(d+155,x2,2);
uint32_t x3=*(p+18);
ff_decimal(d+158,x3,3);
uint32_t x4=(r16(p+19)|(uint32_t)(p+19)[2]<<16);
ff_decimal(d+249,x4,7);
return true;}
static bool alt_fn57(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,214);
ff_time(d+46,ms);
uint32_t x0=*(p+0);
if(x0>=t.f[0].n)return false;memcpy(d+59,t.f[0].dict+x0*5,5);
uint32_t x1=*(p+1);
ff_decimal(d+106,x1,2);
uint32_t x2=*(p+2);
ff_decimal(d+109,x2,2);
uint32_t x3=r16(p+3);
ff_decimal(d+206,x3,7);
return true;}
static bool alt_fn58(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,198);
ff_time(d+46,ms);
uint32_t x0=*(p+0);
if(x0>=t.f[0].n)return false;memcpy(d+59,t.f[0].dict+x0*5,5);
uint32_t x1=*(p+1);
ff_decimal(d+106,x1,2);
uint32_t x2=*(p+2);
ff_decimal(d+109,x2,3);
uint32_t x3=r16(p+3);
ff_decimal(d+190,x3,7);
return true;}
static bool alt_fn59(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,246);
ff_time(d+48,ms);
return true;}
static bool alt_fn60(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,208);
ff_time(d+46,ms);
uint32_t x0=*(p+0);
if(x0>=t.f[0].n)return false;memcpy(d+59,t.f[0].dict+x0*5,5);
uint32_t x1=*(p+1);
ff_decimal(d+106,x1,2);
uint32_t x2=*(p+2);
ff_decimal(d+109,x2,3);
uint32_t x3=r16(p+3);
ff_decimal(d+200,x3,7);
return true;}
static bool alt_fn61(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,212);
ff_time(d+46,ms);
uint32_t x0=*(p+0);
if(x0>=t.f[0].n)return false;memcpy(d+59,t.f[0].dict+x0*5,5);
uint32_t x1=*(p+1);
ff_decimal(d+106,x1,2);
uint32_t x2=*(p+2);
ff_decimal(d+109,x2,2);
uint32_t x3=r16(p+3);
ff_decimal(d+204,x3,7);
return true;}
static bool alt_fn62(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,249);
ff_time(d+48,ms);
return true;}
static bool alt_fn63(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,271);
ff_time(d+48,ms);
return true;}
static bool alt_fn64(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,325);
ff_time(d+48,ms);
return true;}
static bool alt_fn65(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,195);
ff_time(d+46,ms);
uint32_t x0=*(p+0);
if(x0>=t.f[0].n)return false;memcpy(d+59,t.f[0].dict+x0*5,5);
uint32_t x1=*(p+1);
ff_decimal(d+106,x1,2);
uint32_t x2=*(p+2);
ff_decimal(d+109,x2,3);
uint32_t x3=r16(p+3);
ff_decimal(d+187,x3,7);
return true;}
static bool alt_fn66(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,104);
memcpy(d+140,t.lit+140,201);
ff_time(d+44,ms);
uint32_t x0=*(p+0);
if(x0>=t.f[0].n)return false;memcpy(d+57,t.f[0].dict+x0*5,5);
uint32_t x1=r16(p+1);
if(x1>=s.nu)return false;memcpy(d+104,s.uu+x1*36,36);
uint32_t x2=*(p+3);
if(x2>=t.f[2].n)return false;memcpy(d+312,t.f[2].dict+x2*3,3);
uint32_t x3=*(p+4);
if(x3>=t.f[3].n)return false;memcpy(d+321,t.f[3].dict+x3*3,3);
uint32_t x4=(r16(p+5)|(uint32_t)(p+5)[2]<<16);
ff_decimal(d+333,x4,7);
return true;}
static bool alt_fn67(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,96);
memcpy(d+132,t.lit+132,85);
memcpy(d+253,t.lit+253,52);
ff_time(d+48,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+96,s.uu+x0*36,36);
uint32_t x1=r16(p+2);
if(x1>=s.nu)return false;memcpy(d+217,s.uu+x1*36,36);
return true;}
static bool alt_fn68(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,156);
memcpy(d+192,t.lit+192,76);
ff_time(d+48,ms);
uint32_t x0=r16(p+0);
if(x0>=s.nu)return false;memcpy(d+156,s.uu+x0*36,36);
return true;}
static bool alt_fn69(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,101);
memcpy(d+137,t.lit+137,107);
ff_time(d+46,ms);
uint32_t x0=*(p+0);
if(x0>=t.f[0].n)return false;memcpy(d+59,t.f[0].dict+x0*5,5);
ff_uuid(d+101,p+1);
uint32_t x2=*(p+17);
ff_decimal(d+155,x2,2);
uint32_t x3=*(p+18);
ff_decimal(d+158,x3,3);
uint32_t x4=(r16(p+19)|(uint32_t)(p+19)[2]<<16);
ff_decimal(d+236,x4,7);
return true;}
static bool alt_fn70(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,325);
ff_time(d+50,ms);
return true;}
static bool alt_fn71(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,109);
memcpy(d+145,t.lit+145,126);
ff_time(d+52,ms);
ff_uuid(d+109,p+0);
return true;}
static bool alt_fn72(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,203);
ff_time(d+48,ms);
return true;}
static bool alt_fn73(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,101);
memcpy(d+137,t.lit+137,109);
ff_time(d+46,ms);
uint32_t x0=*(p+0);
if(x0>=t.f[0].n)return false;memcpy(d+59,t.f[0].dict+x0*5,5);
ff_uuid(d+101,p+1);
uint32_t x2=*(p+17);
ff_decimal(d+155,x2,2);
uint32_t x3=*(p+18);
ff_decimal(d+158,x3,2);
uint32_t x4=(r16(p+19)|(uint32_t)(p+19)[2]<<16);
ff_decimal(d+238,x4,7);
return true;}
static bool alt_fn74(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,216);
ff_time(d+46,ms);
uint32_t x0=*(p+0);
if(x0>=t.f[0].n)return false;memcpy(d+59,t.f[0].dict+x0*5,5);
uint32_t x1=*(p+1);
ff_decimal(d+106,x1,2);
uint32_t x2=*(p+2);
ff_decimal(d+109,x2,3);
uint32_t x3=r16(p+3);
ff_decimal(d+208,x3,7);
return true;}
static bool alt_fn75(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,219);
ff_time(d+46,ms);
uint32_t x0=*(p+0);
if(x0>=t.f[0].n)return false;memcpy(d+59,t.f[0].dict+x0*5,5);
uint32_t x1=*(p+1);
ff_decimal(d+106,x1,2);
uint32_t x2=*(p+2);
ff_decimal(d+109,x2,3);
uint32_t x3=r16(p+3);
ff_decimal(d+211,x3,7);
return true;}
static bool alt_fn76(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,101);
memcpy(d+137,t.lit+137,119);
ff_time(d+46,ms);
uint32_t x0=*(p+0);
if(x0>=t.f[0].n)return false;memcpy(d+59,t.f[0].dict+x0*5,5);
ff_uuid(d+101,p+1);
uint32_t x2=*(p+17);
ff_decimal(d+155,x2,2);
uint32_t x3=*(p+18);
ff_decimal(d+158,x3,2);
uint32_t x4=(r16(p+19)|(uint32_t)(p+19)[2]<<16);
ff_decimal(d+248,x4,7);
return true;}
static bool alt_fn77(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,109);
memcpy(d+145,t.lit+145,96);
ff_time(d+52,ms);
ff_uuid(d+109,p+0);
return true;}
static bool alt_fn78(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,205);
ff_time(d+46,ms);
uint32_t x0=r16(p+0);
ff_decimal(d+59,x0,5);
uint32_t x1=*(p+2);
ff_decimal(d+106,x1,2);
uint32_t x2=*(p+3);
ff_decimal(d+109,x2,3);
uint32_t x3=r16(p+4);
ff_decimal(d+197,x3,7);
return true;}
static bool alt_fn79(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){
memcpy(d+0,t.lit+0,101);
memcpy(d+137,t.lit+137,128);
ff_time(d+46,ms);
uint32_t x0=*(p+0);
if(x0>=t.f[0].n)return false;memcpy(d+59,t.f[0].dict+x0*5,5);
ff_uuid(d+101,p+1);
uint32_t x2=*(p+17);
ff_decimal(d+155,x2,2);
uint32_t x3=*(p+18);
ff_decimal(d+158,x3,3);
uint32_t x4=(r16(p+19)|(uint32_t)(p+19)[2]<<16);
ff_decimal(d+257,x4,7);
return true;}

static const uint16_t compact_signatures[]={332,46,4,106,36,1,16,311,4,3,1,322,1,0,1,324,7,0,3,332,46,3,106,36,1,16,311,4,3,1,324,7,0,3,284,50,0,352,50,0,245,50,0,330,44,3,104,36,1,16,309,4,3,1,322,7,0,3,225,50,1,158,36,2,2,248,50,0,282,48,0,350,48,0,243,48,0,307,50,2,98,36,2,2,219,36,2,2,343,46,5,59,5,3,1,106,36,2,2,314,3,3,1,323,3,3,1,335,7,0,3,270,50,1,158,36,2,2,262,46,9,59,5,3,1,101,36,1,16,155,2,0,1,158,3,0,1,189,4,3,1,194,2,0,1,197,2,0,1,242,3,3,1,254,7,0,3,176,50,1,109,36,2,2,315,50,2,98,36,2,2,219,36,2,2,297,50,2,98,36,2,2,219,36,2,2,302,50,2,98,36,2,2,219,36,2,2,290,50,2,98,36,2,2,219,36,2,2,294,50,2,98,36,2,2,219,36,2,2,274,50,2,98,36,2,2,219,36,2,2,362,46,4,106,36,1,16,277,36,2,2,341,4,3,1,354,7,0,3,277,50,2,103,36,2,2,224,36,2,2,224,50,1,158,36,2,2,254,50,0,357,46,3,125,36,2,2,270,36,1,16,320,36,2,2,182,50,1,113,36,2,2,318,50,4,99,36,2,2,220,36,2,2,263,2,0,1,266,2,0,1,296,50,4,99,36,2,2,220,36,2,2,263,2,0,1,266,2,0,1,279,50,2,99,36,2,2,220,36,2,2,184,50,1,113,36,2,2,351,50,3,103,36,2,2,224,36,2,2,310,36,2,2,348,50,3,103,36,2,2,224,36,2,2,298,36,2,2,319,50,4,99,36,2,2,220,36,2,2,263,1,0,1,265,2,0,1,265,46,1,99,36,2,2,264,46,5,59,5,3,1,101,36,1,16,155,2,0,1,158,3,0,1,256,7,0,3,325,46,2,106,36,2,2,317,7,0,3,364,46,3,106,36,2,2,280,36,2,2,356,7,0,3,312,50,3,99,36,2,2,220,36,2,2,265,2,0,1,251,50,0,205,50,1,203,1,0,1,273,50,0,261,46,9,59,5,3,1,101,36,1,16,155,2,0,1,158,2,0,1,188,4,3,1,193,2,0,1,196,2,0,1,241,3,3,1,253,7,0,3,331,46,4,106,36,1,16,302,3,3,1,311,3,3,1,323,7,0,3,213,46,4,59,5,3,1,106,2,0,1,109,3,0,1,205,7,0,2,215,46,4,59,5,3,1,106,2,0,1,109,3,0,1,207,7,0,2,265,50,0,327,50,0,331,46,2,106,36,1,16,323,7,0,3,263,46,5,59,5,3,1,101,36,1,16,155,2,0,1,158,2,0,1,255,7,0,3,247,46,5,59,5,3,1,101,36,1,16,155,2,0,1,158,3,0,1,239,7,0,3,223,48,1,156,36,2,2,325,46,3,106,36,2,2,315,1,0,1,317,7,0,3,364,46,4,106,36,2,2,280,36,2,2,354,1,0,1,356,7,0,3,312,50,4,99,36,2,2,220,36,2,2,263,1,0,1,265,2,0,1,257,46,5,59,5,3,1,101,36,1,16,155,2,0,1,158,3,0,1,249,7,0,3,214,46,4,59,5,3,1,106,2,0,1,109,2,0,1,206,7,0,2,198,46,4,59,5,3,1,106,2,0,1,109,3,0,1,190,7,0,2,246,48,0,208,46,4,59,5,3,1,106,2,0,1,109,3,0,1,200,7,0,2,212,46,4,59,5,3,1,106,2,0,1,109,2,0,1,204,7,0,2,249,48,0,271,48,0,325,48,0,195,46,4,59,5,3,1,106,2,0,1,109,3,0,1,187,7,0,2,341,44,5,57,5,3,1,104,36,2,2,312,3,3,1,321,3,3,1,333,7,0,3,305,48,2,96,36,2,2,217,36,2,2,268,48,1,156,36,2,2,244,46,5,59,5,3,1,101,36,1,16,155,2,0,1,158,3,0,1,236,7,0,3,325,50,0,271,52,1,109,36,1,16,203,48,0,246,46,5,59,5,3,1,101,36,1,16,155,2,0,1,158,2,0,1,238,7,0,3,216,46,4,59,5,3,1,106,2,0,1,109,3,0,1,208,7,0,2,219,46,4,59,5,3,1,106,2,0,1,109,3,0,1,211,7,0,2,256,46,5,59,5,3,1,101,36,1,16,155,2,0,1,158,2,0,1,248,7,0,3,241,52,1,109,36,1,16,205,46,4,59,5,0,2,106,2,0,1,109,3,0,1,197,7,0,2,265,46,5,59,5,3,1,101,36,1,16,155,2,0,1,158,3,0,1,257,7,0,3};
static const uint64_t compact_hashes[]={115668743112478987ull,400197898705978073ull,768531259983163012ull,1060157113683039014ull,1258164535966145125ull,1273304919678056901ull,1304041794044589940ull,1421393776853847037ull,1555464100788007600ull,1829732575062174169ull,1953727829063883592ull,2246988327613048645ull,2527635773611096014ull,2647097751059401712ull,2755454361949868875ull,3202448943291663950ull,3362793730063598480ull,3364706880296307170ull,3823700898526796534ull,4040773215187737324ull,4645328274969043530ull,4670431914037675220ull,4718482602565643801ull,4719549487729152857ull,4764538810963409393ull,4807046931077552431ull,4924552138555653141ull,4924617233425821459ull,5194051017195573226ull,5931701289309348783ull,5980753872821375481ull,6310053451901831903ull,6581968058683142302ull,6782218329303698950ull,6867601076004966125ull,6930435139134824004ull,7061800990953355135ull,7294653979973612417ull,7862822139399297382ull,8013260356709755939ull,8112031828482826044ull,8282988733901077575ull,8517713908171824700ull,8569109841970160655ull,8647549860138114869ull,8852658098903185861ull,9846057538996892571ull,10231987722890384036ull,10405367991302215912ull,10423017923896536400ull,11039629149084278576ull,11128574161453356986ull,11138104960063763207ull,11630308786142337605ull,11660020016421660662ull,11665984515918101138ull,11838693735754437972ull,11972895597100790122ull,12193120673874110556ull,12489072272543501250ull,12671094348715441344ull,12825533842716387742ull,12923868682536346396ull,13628398366995570593ull,13698288060596844963ull,14134470853289896153ull,14768074605263038443ull,14979499815951864537ull,15430618338199537598ull,15439673349689053538ull,15456879831251012830ull,15479770600464221113ull,15571671599309401781ull,15594809368356474708ull,15756433578325098442ull,15879996756480975787ull,15942533859151238737ull,17256322651978863931ull,17280187482881928250ull,17465154268690574619ull};
static const uint16_t compact_offsets[]={267,520,802,998,723,68,811,65,19,71,468,285,742,197,157,397,808,894,111,481,764,580,292,662,594,311,37,968,1017,647,640,907,805,230,783,949,341,363,577,427,186,853,539,453,118,897,991,558,404,378,871,583,88,175,617,40,348,904,864,43,219,478,260,761,164,270,0,830,438,74,471,681,330,208,930,58,34,249,700,77};
static const Fn compact_functions[]={alt_fn25,alt_fn44,alt_fn62,alt_fn78,alt_fn57,alt_fn8,alt_fn65,alt_fn7,alt_fn1,alt_fn9,alt_fn40,alt_fn27,alt_fn58,alt_fn19,alt_fn15,alt_fn35,alt_fn64,alt_fn70,alt_fn13,alt_fn43,alt_fn60,alt_fn48,alt_fn28,alt_fn54,alt_fn50,alt_fn29,alt_fn3,alt_fn76,alt_fn79,alt_fn53,alt_fn52,alt_fn73,alt_fn63,alt_fn22,alt_fn61,alt_fn75,alt_fn31,alt_fn33,alt_fn47,alt_fn37,alt_fn18,alt_fn67,alt_fn45,alt_fn39,alt_fn14,alt_fn71,alt_fn77,alt_fn46,alt_fn36,alt_fn34,alt_fn69,alt_fn49,alt_fn12,alt_fn17,alt_fn51,alt_fn4,alt_fn32,alt_fn72,alt_fn68,alt_fn5,alt_fn21,alt_fn42,alt_fn24,alt_fn59,alt_fn16,alt_fn26,alt_fn0,alt_fn66,alt_fn38,alt_fn10,alt_fn41,alt_fn55,alt_fn30,alt_fn20,alt_fn74,alt_fn6,alt_fn2,alt_fn23,alt_fn56,alt_fn11};
static Fn alt_lookup(uint64_t hash,T&t){
 unsigned lo=0,hi=80;
 while(lo<hi){unsigned mid=(lo+hi)/2;if(compact_hashes[mid]<hash)lo=mid+1;else hi=mid;}
 if(lo==80||compact_hashes[lo]!=hash)return nullptr;
 const uint16_t*p=compact_signatures+compact_offsets[lo];
 if(t.len!=p[0]||t.toff!=p[1]||t.nf!=p[2])return nullptr;p+=3;
 for(unsigned i=0;i<t.nf;i++,p+=4){const F&f=t.f[i];if(f.off!=p[0]||f.width!=p[1]||f.type!=p[2]||f.nb!=p[3])return nullptr;}
 return compact_functions[lo];
}
