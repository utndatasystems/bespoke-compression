"""Build the conventional configurations in the paper using recorded wrappers."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
BULK=['lz4','lz4hc9','lz4hc12','zstd1','zstd3','zstd19','zstd20','zstd21','zstd22']
EXTRA={**{'brotli'+str(n):(1,n,['brotlienc','brotlidec','brotlicommon']) for n in [5,9,11]},
       **{'zlib'+str(n):(2,n,['z']) for n in [1,6,9]},
       **{'bzip2-'+str(n):(3,n,['bz2']) for n in [1,9]},
       **{'xz'+str(n):(4,n,['lzma']) for n in [0,6,9]},'raw-copy':(0,0,[])}


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--methods',nargs='+',choices=BULK+list(EXTRA)+['fsst','onpairplus'],default=BULK+list(EXTRA))
    ap.add_argument('--row-framing',choices=['lf','nul','none'],default='none')
    ap.add_argument('--deps-prefix',type=Path)
    args=ap.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
    selected=args.methods
    if args.deps_prefix:
        prefix=args.deps_prefix.resolve()
        for variable, folders in {'CPATH':['include'], 'LIBRARY_PATH':['lib/x86_64-linux-gnu','lib'],
                                  'LD_LIBRARY_PATH':['lib/x86_64-linux-gnu','lib']}.items():
            os.environ[variable] = ':'.join([*[str(prefix / folder) for folder in folders],
                                           *([os.environ[variable]] if variable in os.environ else [])])
    if len(selected)!=len(set(selected)):ap.error('Methods must be distinct')
    pins=json.loads((HERE/'SOURCE.json').read_text())
    for name,digest in pins['sha256'].items():
        if hashlib.sha256((ROOT/name).read_bytes()).hexdigest()!=digest:raise RuntimeError('Source changed: '+name)
    if {'fsst','onpairplus'} & set(selected):
        if args.row_framing=='none':ap.error('FSST/OnPair+ require LF or NUL row framing')
        subprocess.run(['python3',str(ROOT/'benchmarks/upstream/fetch.py')],check=True)
        subprocess.run(['python3',str(ROOT/'benchmarks/dbtext/build.py'),'--out',str(out),'--row-framing',args.row_framing,'--methods',*[m for m in selected if m in ('fsst','onpairplus')]],check=True)
    commands=[]
    def run(argv):
        commands.append(list(map(str,argv)))
        subprocess.run(commands[-1],check=True)
    includes=['-I'+str(ROOT/'benchmarks/dbtext'),'-I'+str(ROOT/'benchmarks/upstream/fsst')]
    if args.deps_prefix:
        prefix=args.deps_prefix.resolve()
        includes += ['-I'+str(prefix/'include'),'-L'+str(prefix/'lib/x86_64-linux-gnu'),'-L'+str(prefix/'lib')]
    run(['g++','-std=c++17','-O3','-DNDEBUG',ROOT/'benchmarks/dbtext/benchmark.cpp','-ldl','-o',out/'driver'])
    for name in selected:
        if name in ('fsst','onpairplus'):continue
        folder=out/name;folder.mkdir()
        for role in ('encoder','decoder'):
            if name in BULK:
                flags=['-DMETHOD='+('1' if name.startswith('zstd') else '0')]
                if name.startswith('zstd'):flags+=['-DLAB_ZSTD_LEVEL='+name[4:]]
                if name.startswith('lz4hc'):flags+=['-DLAB_LZ4HC_LEVEL='+name[5:]]
                source=HERE/'bulk-reference.cpp';std='c++20';libs=['zstd' if name.startswith('zstd') else 'lz4']
            else:
                codec,level,libs=EXTRA[name];flags=[f'-DCODEC={codec}',f'-DLEVEL={level}']
                if codec==1 and role=='decoder':libs=['brotlidec','brotlicommon']
                source=HERE/'extra_baseline.cpp';std='c++17'
            run(['g++','-std='+std,'-O3','-DNDEBUG','-fPIC','-shared',*includes,*flags,
                 *(['-DDECODE_ONLY'] if role=='decoder' else []),source,*['-l'+n for n in libs],'-o',folder/(role+'.so')])
        (folder/'manifest.json').write_text(json.dumps(dict(name=name,variant='bulk',encoder='encoder.so',decoder='decoder.so'))+'\n')
    (out/'build-profile.json').write_text(json.dumps(dict(row_framing=args.row_framing,methods=selected),indent=2)+'\n')
    (out/'paper-build.json').write_text(json.dumps(dict(commands=commands,source_sha256=pins['sha256'],compiler=subprocess.check_output(['g++','--version'],text=True)),indent=2)+'\n')
    print(out)


if __name__=='__main__':main()
