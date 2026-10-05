import sys,struct
p=sys.argv[1];x=bytearray(open(p,'rb').read());assert x[:6]==b'\x7fELF\x02\x01'
phoff=struct.unpack_from('<Q',x,32)[0];phsize,phnum=struct.unpack_from('<HH',x,54)
end=0
for i in range(phnum):
 t,flags,off,addr,paddr,fs,ms,al=struct.unpack_from('<IIQQQQQQ',x,phoff+i*phsize)
 end=max(end,off+fs)
struct.pack_into('<Q',x,40,0);struct.pack_into('<HHH',x,58,0,0,0)
open(p,'wb').write(x[:end])
