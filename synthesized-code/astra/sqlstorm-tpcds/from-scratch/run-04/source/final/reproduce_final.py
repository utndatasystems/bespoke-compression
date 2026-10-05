#!/usr/bin/env python3
"""Regenerate the selected fitted token codec without changing selected artifacts.
Run: python3 /work/final/reproduce_final.py /work/final_refit
Only Python's stdlib and the installed GCC C++ compiler are needed.
"""
import hashlib,json,pathlib,subprocess,sys,time
source=pathlib.Path(__file__).resolve().parent
stage=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else '/work/final_refit').resolve()
stage.mkdir(parents=True,exist_ok=True)
records=[]
def run(name,args):
    start=time.monotonic()
    subprocess.run(args,check=True,cwd=stage)
    records.append({'stage':name,'seconds':time.monotonic()-start,'argv':args})
for name in ['analysis_repair.cpp','lzexp_tok.cpp','lzexp_dp.cpp','lzfinal_pack.cpp','lzfinal_entropy.h','lzfinal_encoder.cpp','lzfinal_decoder.cpp']:
    body=(source/name).read_text().replace('"/work/', '"'+str(stage)+'/')
    (stage/name).write_text(body)
for name in ['analysis_repair','lzexp_tok','lzexp_dp','lzfinal_pack']:
    run('compile '+name,['g++','-w','-O3','-march=icelake-server',str(stage/(name+'.cpp')),'-o',str(stage/name)])
# Stopping RePair after the first 5888 pair rules is exactly equivalent to
# expanding all rules >=6144 in the original 65536-symbol fitted grammar.
run('fit 6144-symbol RePair',[str(stage/'analysis_repair'),'6144','0',str(stage/'analysis_repair.bin')])
run('derive token sequence and suffix array',[str(stage/'lzexp_tok'),'6144','3','1'])
# The selected parse uses twenty successive histogram fits, numbered 0..19.
run('twenty token dynamic-programming fits with 256 candidates',[str(stage/'lzexp_dp'),'6144','3','1','256','20'])
run('pack rANS archive and encoder payload',[str(stage/'lzfinal_pack'),'6144',str(stage/'lzsearch_dp6144_d256_r20_matches.bin'),'15'])
flags=['g++','-w','-O3','-march=icelake-server','-shared','-fPIC','-fno-exceptions','-fno-rtti','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-nostartfiles','-Wl,--build-id=none,-z,noseparate-code,-z,norelro','-s','-I/interface']
for name in ['encoder','decoder']:
    run('compile '+name,flags+[str(stage/('lzfinal_'+name+'.cpp')),'-o',str(stage/(name+'.so'))])
archive=(stage/'lzfinal_archive.bin').read_bytes()
report={'archive_bytes':len(archive),'archive_sha256':hashlib.sha256(archive).hexdigest(),'stages':records,'total_seconds':sum(x['seconds'] for x in records)}
selected=source/'lzfinal_archive.bin'
if selected.exists():
    report['matches_selected_archive']=archive==selected.read_bytes()
    assert report['matches_selected_archive']
report['matches_selected_payload']=(stage/'lzfinal_payload.h').read_bytes()==(source/'lzfinal_payload.h').read_bytes()
assert report['matches_selected_payload']
(stage/'fitting_costs.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
