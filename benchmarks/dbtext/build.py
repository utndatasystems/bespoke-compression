"""Build the original baseline wrappers and selected native candidates."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / 'compression-lab-isolated/vendor'))
from compression_lab.native_workloads import BASELINES as WORKLOAD_BASELINES, variants

UPSTREAM = HERE.parent / 'upstream'
ADAPTERS = HERE.parents[1] / 'compression-lab-isolated/vendor/compression_lab/data/strings'
BASELINES = (*WORKLOAD_BASELINES['bulk'], *WORKLOAD_BASELINES['query-access'])
LEGACY_BASELINES = ('lz4', 'zstd1', 'fsst', 'onpairplus')


def build(output, methods=None, row_framing='lf', workload=None):
    if row_framing not in ('lf', 'nul', 'none'):
        raise ValueError('row_framing must be lf, nul, or none')
    allowed = list(BASELINES)
    variants(workload, row_framing)
    defaults = list(LEGACY_BASELINES) if row_framing == 'lf' else (
        list(LEGACY_BASELINES) if row_framing == 'nul' else ['lz4', 'zstd1'])
    selected = list(methods) if methods is not None else list(WORKLOAD_BASELINES[workload]) if workload else defaults
    if not selected or len(set(selected)) != len(selected) or set(selected) - set(allowed):
        raise ValueError('Choose distinct known methods: ' + ', '.join(allowed))
    if workload and set(selected).intersection(BASELINES) - set(WORKLOAD_BASELINES[workload]):
        raise ValueError('Selected baselines do not belong to the ' + workload + ' workload')
    if row_framing == 'none' and set(selected) - set(WORKLOAD_BASELINES['bulk']):
        raise ValueError('Opaque-byte workloads support bulk methods only')
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    records = []
    source_pins = {}

    def pin(path, expected=None):
        if not path.is_file():
            raise RuntimeError('Missing source: ' + str(path) + '; run benchmarks/upstream/fetch.py for OnPair+')
        checksum = hashlib.sha256(path.read_bytes()).hexdigest()
        if expected is not None and checksum != expected:
            raise RuntimeError('Source checksum mismatch: ' + str(path))
        source_pins[str(path)] = checksum

    # Verify the public measured snapshots at build time, not only when fetched.
    source_map = json.loads((HERE.parent / 'SOURCE_MAP.json').read_text())
    for name, entry in source_map['kernels'].items():
        if name.startswith('dbtext/'):
            pin(HERE.parent / name, entry['sha256'])
    for name, expected in source_map['upstream']['fsst']['files'].items():
        pin(UPSTREAM / 'fsst' / name, expected)
    if 'onpairplus' in selected:
        lock = json.loads((UPSTREAM / 'onpair-lock.json').read_text())
        for entry in lock['files']:
            pin(UPSTREAM / 'token' / entry['path'], entry['sha256'])
        pin(HERE / 'onpair-base.cpp')
    wrappers = HERE if row_framing == 'lf' else ADAPTERS
    if wrappers != HERE:
        for name in ('codec.h', 'reference.cpp', 'onpair.cpp'):
            pin(wrappers / name)
    elif {'zstd3', 'zstd19'}.intersection(selected):
        for name in ('codec.h', 'reference.cpp'):
            pin(ADAPTERS / name)
    compiler = shutil.which('g++')
    if not compiler:
        raise RuntimeError('g++ is required')
    profile = dict(row_framing=row_framing, methods=selected, source_sha256=source_pins,
                   compiler=dict(path=compiler, sha256=hashlib.sha256(Path(compiler).read_bytes()).hexdigest()))
    if workload: profile['workload'] = workload
    (output / 'build-profile.json').write_text(json.dumps(profile, indent=2) + '\n')

    def run(argv):
        argv = list(map(str, argv))
        result = subprocess.run(argv, capture_output=True, text=True, timeout=None)
        records.append(dict(argv=argv, returncode=result.returncode,
                            stdout=result.stdout, stderr=result.stderr))
        (output / 'build.json').write_text(json.dumps(records, indent=2) + '\n')
        if result.returncode:
            raise RuntimeError(result.stderr)

    if 'fsst' in selected:
        run(['cmake', '-S', UPSTREAM / 'fsst', '-B', output / 'fsst-lib',
             '-DBUILD_SHARED_LIBS=ON', '-DCMAKE_BUILD_TYPE=Release'])
        run(['cmake', '--build', output / 'fsst-lib', '--target', 'fsst', '--parallel', '2'])
    run(['g++', '-std=c++17', '-O3', '-DNDEBUG', HERE / 'benchmark.cpp', '-ldl', '-o', output / 'driver'])
    common = ['g++', '-std=c++23', '-O3', '-DNDEBUG', '-fPIC', '-shared',
              '-I' + str(wrappers), '-I' + str(UPSTREAM / 'fsst'),
              '-L' + str(output / 'fsst-lib')]
    if row_framing == 'nul':
        common.append('-DLAB_ROW_DELIMITER=0')
    built_methods = []
    for name, number, library, variant in [('lz4', 0, 'lz4', 'bulk'),
                                          ('zstd1', 1, 'zstd', 'bulk'), ('zstd3', 1, 'zstd', 'bulk'),
                                          ('zstd19', 1, 'zstd', 'bulk'), ('fsst', 2, 'fsst', 'rows')]:
        if name not in selected:
            continue
        folder = output / name
        folder.mkdir(exist_ok=True)
        for role in ('encoder', 'decoder'):
            source = ADAPTERS / 'reference.cpp' if name in ('zstd3', 'zstd19') else wrappers / 'reference.cpp'
            run([*common, f'-DMETHOD={number}', *(['-DDECODE_ONLY'] if role == 'decoder' else []),
                 *(['-DLAB_ZSTD_LEVEL='+name[4:]] if name.startswith('zstd') else []),
                 source, '-l' + library, '-o', folder / (role + '.so')])
        built_methods.append((name, variant))
    token = UPSTREAM / 'token/src'
    if 'onpairplus' in selected:
        folder = output / 'onpairplus'
        folder.mkdir(exist_ok=True)
        # Match the measured wrapper flags; the algorithm remains upstream OnPair+.
        run([*common, '-include', 'span', '-include', 'string', '-include', 'cstring',
             '-include', 'stdexcept', '-I' + str(token), '-fno-tree-vectorize', wrappers / 'onpair.cpp',
             HERE / 'onpair-base.cpp', token / 'compressor/onpair_advanced/OnPairAdvancedCompressor.cpp',
             token / 'compressor/onpair_advanced/LongestPrefixMatcher.cpp', '-o', folder / 'encoder.so'])
        run([*common, '-fno-tree-vectorize', '-DDECODE_ONLY', wrappers / 'onpair.cpp', '-o', folder / 'decoder.so'])
        built_methods.append(('onpairplus', 'rows'))
    for name, variant in built_methods:
        (output / name / 'manifest.json').write_text(json.dumps(dict(
            name=name, variant=variant, encoder='encoder.so', decoder='decoder.so')) + '\n')
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--methods', nargs='+', help='Build only the selected methods')
    parser.add_argument('--row-framing', choices=['lf', 'nul', 'none'], default='lf')
    parser.add_argument('--workload', choices=list(WORKLOAD_BASELINES), help='Build the matching baseline family')
    args = parser.parse_args()
    print(build(args.out, args.methods, args.row_framing, args.workload))
