import struct, collections, math, sys, time
start=time.monotonic()
source=sys.argv[1] if len(sys.argv)>1 else '/work/opt64_p0.bin'
output=sys.argv[2] if len(sys.argv)>2 else '/work/analysis_costs_refit.h'
b=open(source,'rb').read();n,ns,nl=struct.unpack_from('<QQQ',b)
mc=collections.Counter();dc=collections.Counter();lc=collections.Counter()
for ll,ml,d in struct.iter_unpack('<III',b[24:24+12*ns]):
 mc[min(max(ml-3,0),255)]+=1;dc[max(0,d.bit_length()-1)]+=1;lc[min(ll,255)]+=1
lit=collections.Counter(b[24+12*ns:])
def cost(c,i,N,K): return round(-math.log2((c[i]+0.2)/(N+0.2*K)),4)
def arr(f,name,v): f.write('static const float '+name+'['+str(len(v))+']={'+','.join(str(x)+'f' for x in v)+'};\n')
with open(output,'w') as f:
 arr(f,'mlcost',[cost(mc,min(max(i-3,0),255),ns,256) for i in range(259)])
 arr(f,'distcost',[cost(dc,i,ns,32)+i for i in range(32)])
 arr(f,'llcost',[cost(lc,i,ns,256) for i in range(256)])
 arr(f,'litcost',[cost(lit,i,nl,256) for i in range(256)])
print('fit_seconds',time.monotonic()-start)
