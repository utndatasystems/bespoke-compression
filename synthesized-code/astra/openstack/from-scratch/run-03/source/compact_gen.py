import pathlib,re,sys
src=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else '/work/opt_generated.h').read_text()
base=src[:src.index('static Fn alt_lookup')]
entries=[];signatures=[]
for h,checks,i in re.findall(r'case (\d+)ull:if\((.*?)\)return nullptr;return alt_fn(\d+);',src):
 vals=[int(x) for x in re.findall(r'!=(\d+)',checks)]
 assert len(vals)==3+vals[2]*4
 entries.append((int(h),int(i),len(signatures)));signatures+=vals
entries.sort()
base+='\nstatic const uint16_t compact_signatures[]={'+','.join(map(str,signatures))+'};\n'
base+='static const uint64_t compact_hashes[]={'+','.join(str(h)+'ull' for h,i,o in entries)+'};\n'
base+='static const uint16_t compact_offsets[]={'+','.join(str(o) for h,i,o in entries)+'};\n'
base+='static const Fn compact_functions[]={'+','.join('alt_fn'+str(i) for h,i,o in entries)+'};\n'
base+=f'''static Fn alt_lookup(uint64_t hash,T&t){{
 unsigned lo=0,hi={len(entries)};
 while(lo<hi){{unsigned mid=(lo+hi)/2;if(compact_hashes[mid]<hash)lo=mid+1;else hi=mid;}}
 if(lo=={len(entries)}||compact_hashes[lo]!=hash)return nullptr;
 const uint16_t*p=compact_signatures+compact_offsets[lo];
 if(t.len!=p[0]||t.toff!=p[1]||t.nf!=p[2])return nullptr;p+=3;
 for(unsigned i=0;i<t.nf;i++,p+=4){{const F&f=t.f[i];if(f.off!=p[0]||f.width!=p[1]||f.type!=p[2]||f.nb!=p[3])return nullptr;}}
 return compact_functions[lo];
}}\n'''
pathlib.Path(sys.argv[2] if len(sys.argv)>2 else '/work/compact_generated.h').write_text(base)
print('compact signatures',len(entries),'metadata bytes',len(signatures)*2+len(entries)*18)
