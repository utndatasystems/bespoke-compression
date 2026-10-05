"""Build the actual final server drivers, retaining their measurement kernels."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]

def build(out):
    out.mkdir(parents=True, exist_ok=False)
    pins = json.loads((HERE / 'sources/SOURCES.json').read_text())
    for name, record in pins.items():
        if hashlib.sha256((HERE / 'sources' / name).read_bytes()).hexdigest() != record['sha256']:
            raise RuntimeError('Final driver source changed: ' + name)
    shutil.copytree(HERE / 'sources', out / 'sources')
    subprocess.run([sys.executable, str(ROOT / 'benchmarks/upstream/fetch.py')], check=True)
    subprocess.run(['make', 'libfsst.a'], cwd=out / 'sources/fsst', check=True)
    token = ROOT / 'benchmarks/upstream/token/src'
    fsst = out / 'sources/fsst'
    includes = [out / 'sources', fsst, ROOT / 'benchmarks/upstream/fsst/paper', token,
                ROOT / 'benchmarks/upstream/token/thirdparty']
    commands = []
    for dataset in ['dbtext', 'bulk']:
        for profile, flags in [('generic', []), ('fsst', ['-march=native']), ('onpair', ['-fno-tree-vectorize'])]:
            argv = ['g++', '-std=c++23', '-O3', '-DNDEBUG', '-include', 'span', '-include', 'string',
                    '-include', 'cstring', '-include', 'stdexcept', str(out / f'sources/{dataset}-measure.cpp'),
                    str(ROOT / 'benchmarks/dbtext/onpair-base.cpp'),
                    str(token / 'compressor/onpair_advanced/OnPairAdvancedCompressor.cpp'),
                    str(token / 'compressor/onpair_advanced/LongestPrefixMatcher.cpp'),
                    *['-I' + str(p) for p in includes], str(fsst / 'libfsst.a'),
                    '-llz4', '-lzstd', '-lbrotlienc', '-lbrotlidec', '-lbrotlicommon', '-lbz2', '-ldl',
                    '-lz', '-llzma', *flags, '-o', str(out / f'{dataset}-{profile}')]
            subprocess.run(argv, check=True)
            commands.append(argv)
    driver = ROOT / 'compression-lab-isolated/include/dbtext_rows.cpp'
    argv = ['g++', '-std=c++17', '-O3', '-DNDEBUG', str(driver), '-ldl', '-o', str(out / 'rows')]
    subprocess.run(argv, check=True)
    commands.append(argv)
    (out / 'build-receipt.json').write_text(json.dumps({'commands': commands,
        'source_pins': pins, 'compiler': subprocess.check_output(['g++', '--version'], text=True),
        'binaries': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir() if p.is_file()}}, indent=2) + '\n')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    build(parser.parse_args().out.resolve())
