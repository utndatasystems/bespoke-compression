import struct, collections, math, sys, time
start=time.monotonic()
source=sys.argv[1] if len(sys.argv)>1 else '/work/parse.bin'
output=sys.argv[2] if len(sys.argv)>2 else '/work/analysis_costs.h'
b=open(source,'rb').read();n,ns,nl=struct.unpack_from('<QQQ',b)
mc=collections.Counter();dc=collections.Counter()
for ll,ml,d in struct.iter_unpack('<III',b[24:24+12*ns]):
 mc[min(ml,255)]+=1;dc[max(0,d.bit_length()-1)]+=1
with open(output,'w') as f:
 f.write('static const float mlcost[256]={'+','.join(str(round(-math.log2((mc[i]+0.2)/(ns+51.2)),3))+'f' for i in range(256))+'};\n')
 f.write('static const float distcost[32]={'+','.join(str(round(-math.log2((dc[i]+0.2)/(ns+6.4)),3)+i)+'f' for i in range(32))+'};\n')
print('fit_seconds',time.monotonic()-start)
