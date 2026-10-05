import struct,pathlib,collections
counter=collections.Counter()
for p in pathlib.Path('/work').glob('*.arc'):
 a=p.read_bytes(); raw,nl,nt,nu,_,ro=struct.unpack_from('<QIIIIQ',a,8);pos=40+nu*16;ts=[]
 for _ in range(nt):
  ln,to,nf,rb=struct.unpack_from('<HHHH',a,pos);pos+=8+ln;fs=[]
  for _ in range(nf):
   off,width,ty,nb,_,num=struct.unpack_from('<HBBBBH',a,pos);pos+=8
   if ty==3:pos+=num*width
   fs.append((off,width,ty,nb))
  ts.append(((ln,to,tuple(fs)),rb))
 assert pos==ro
 for _ in range(nl):
  tid=a[pos];pos+=1
  if tid==255:tid=struct.unpack_from('<H',a,pos)[0];pos+=2
  delta=struct.unpack_from('<H',a,pos)[0];pos+=2
  if delta==65535:pos+=4
  sig,rb=ts[tid];pos+=rb;counter[sig]+=1
 assert pos==len(a)
def shash(sig):
 h=14695981039346656037
 for v in [sig[0],sig[1],len(sig[2])]+[x for f in sig[2] for x in f]:h=((h^v)*1099511628211)&((1<<64)-1)
 return h
N=int(__import__('sys').argv[1]) if len(__import__('sys').argv)>1 else 80
sigs=counter.most_common(N);out=[]
for i,(sig,count) in enumerate(sigs):
 ln,to,fs=sig
 out.append(f'static bool alt_fn{i}(T&t,S&s,uint8_t*d,const uint8_t*p,uint32_t ms){{')
 holes=[(off,off+w) for off,w,ty,nb in fs if ty in (1,2)]
 cp=0
 for hp,he in holes+[(ln,ln)]:
  if hp>cp:out.append(f'memcpy(d+{cp},t.lit+{cp},{hp-cp});')
  cp=he
 out.append(f'ff_time(d+{to},ms);')
 sp=0
 for j,(off,w,ty,nb) in enumerate(fs):
  dst=f'd+{off}'; src=f'p+{sp}'
  if ty==1:out.append(f'ff_uuid({dst},{src});')
  else:
   x=f'x{j}'; expr=f'*({src})' if nb==1 else f'r16({src})' if nb==2 else f'(r16({src})|(uint32_t)({src})[2]<<16)' if nb==3 else f'r32({src})'
   out.append(f'uint32_t {x}={expr};')
   if ty==0:out.append(f'ff_decimal({dst},{x},{w});')
   elif ty==2:out.append(f'if({x}>=s.nu)return false;memcpy({dst},s.uu+{x}*36,36);')
   elif ty==3:out.append(f'if({x}>=t.f[{j}].n)return false;memcpy({dst},t.f[{j}].dict+{x}*{w},{w});')
  sp+=nb
 out.append('return true;}')
out.append('static Fn alt_lookup(uint64_t hash,T&t){switch(hash){')
for i,(sig,count) in enumerate(sigs):
 ln,to,fs=sig;checks=[f't.len!={ln}',f't.toff!={to}',f't.nf!={len(fs)}']
 for j,(off,w,ty,nb) in enumerate(fs):checks += [f't.f[{j}].off!={off}',f't.f[{j}].width!={w}',f't.f[{j}].type!={ty}',f't.f[{j}].nb!={nb}']
 out.append(f'case {shash(sig)}ull:if('+('||'.join(checks))+f')return nullptr;return alt_fn{i};')
out.append('default:return nullptr;}}')
pathlib.Path('/work/alt_generated.h').write_text('\n'.join(out))
print('signatures',len(counter),'selected',N,'rows',sum(c for _,c in sigs),'total',sum(counter.values()))
