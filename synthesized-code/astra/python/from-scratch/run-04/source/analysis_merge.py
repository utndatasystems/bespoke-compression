import struct, sys
out=sys.argv[1]; ins=sys.argv[2:]; n=0; ns=0; lcount=0; seqs=[]; lits=[];carry=0
for p in ins:
 b=open(p,'rb').read();nn,sn,ln=struct.unpack_from('<QQQ',b);s=bytearray(b[24:24+12*sn]);l=b[24+12*sn:]
 assert len(l)==ln
 ll,ml,d=struct.unpack_from('<III',s);struct.pack_into('<I',s,0,ll+carry)
 if p!=ins[-1]:
  carry,ml,d=struct.unpack_from('<III',s,len(s)-12);assert ml==0;s=s[:-12]
 seqs.append(s);lits.append(l);n+=nn;ns+=len(s)//12;lcount+=ln
with open(out,'wb') as f:
 f.write(struct.pack('<QQQ',n,ns,lcount))
 for x in seqs:f.write(x)
 for x in lits:f.write(x)
print('merged',out,'bytes',n,'seq',ns,'literals',lcount)
