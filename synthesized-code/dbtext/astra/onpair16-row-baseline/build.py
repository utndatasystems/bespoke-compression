"""Build the published OnPair16 bridge against pinned author sources."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from urllib.request import urlopen

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
TOKEN = ROOT / 'benchmarks/upstream/token'
FSST = ROOT / 'benchmarks/upstream/fsst'
COMMIT = 'e202e36e2b33a4768d1122f96880b64416234175'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=False)
    subprocess.run(['python3', str(ROOT / 'benchmarks/upstream/fetch.py')], check=True)
    pins = json.loads((HERE / 'UPSTREAM_HASHES.json').read_bytes())
    for name, digest in pins.items():
        path = TOKEN / name
        if not path.exists():
            data = urlopen(f'https://raw.githubusercontent.com/umbra-db/token-vldb2026/{COMMIT}/{name}', timeout=None).read()
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise RuntimeError('Upstream checksum mismatch: ' + name)
    includes = [HERE, TOKEN / 'src', TOKEN / 'thirdparty', TOKEN / 'thirdparty/onpair/include',
                TOKEN / 'thirdparty/onpair/external/robin_hood', FSST, FSST / 'paper']
    flags = ['g++', '-std=c++23', '-O3', '-DNDEBUG', '-include', 'span', '-include', 'string',
             '-include', 'cstring', '-include', 'stdexcept', *['-I' + str(p) for p in includes]]
    commands = []

    def run(command):
        commands.append(list(map(str, command)))
        subprocess.run(commands[-1], check=True)

    run(['cmake', '-S', FSST, '-B', output / 'fsst', '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_SHARED_LIBS=ON'])
    run(['cmake', '--build', output / 'fsst', '--target', 'fsst', '--parallel', '2'])
    run([*flags, '-shared', '-fPIC', HERE / 'row_adapter.cpp', TOKEN / 'thirdparty/onpair/src/onpair16.cpp',
         '-o', output / 'librow-adapter.so'])
    link = ['-L' + str(output), '-lrow-adapter', '-Wl,-rpath,$ORIGIN']
    run([*flags, HERE / 'rows.cpp', ROOT / 'benchmarks/dbtext/onpair-base.cpp',
         TOKEN / 'src/compressor/onpair_advanced/OnPairAdvancedCompressor.cpp',
         TOKEN / 'src/compressor/onpair_advanced/LongestPrefixMatcher.cpp', *link,
         '-L' + str(output / 'fsst'), '-lfsst', '-llz4', '-Wl,-rpath,$ORIGIN/fsst', '-o', output / 'rows'])
    run([*flags, HERE / 'decode_archive.cpp', *link, '-o', output / 'decode-archive'])
    receipt = dict(commands=commands, upstream_commit=COMMIT, upstream_sha256=pins,
                   source_sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                  for p in HERE.iterdir() if p.suffix in ('.cpp', '.hpp')})
    (output / 'build-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')


if __name__ == '__main__':
    main()
