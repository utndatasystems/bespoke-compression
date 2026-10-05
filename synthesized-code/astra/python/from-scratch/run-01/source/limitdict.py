import struct,sys,collections
src=open('/work/grammar/dict4.bin','rb').read();d=[];p=0
while p<len(src):
 n=src[p];p+=1;d.append(src[p:p+n]);p+=n
for L in (16,24,32):
 words=d[:256]+[w for w in d[256:] if len(w)<=L]
 seen=set(words);added=[]
 for w in d:
  if len(w)>L:
   for p in range(0,len(w),L):
    a=w[p:p+L]
    if a not in seen:added.append(a);seen.add(a)
 print(L,'kept',len(words),'added',len(added))
 words=(words+added)[:65536]
 open('/work/root/dict'+str(L)+'.bin','wb').write(b''.join(bytes([len(w)])+w for w in words))
