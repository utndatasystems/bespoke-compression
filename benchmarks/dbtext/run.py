"""Run the existing DBText evaluator with a portable, bounded process launcher."""
import argparse
import json
import math
import os
from pathlib import Path
import resource
import shutil
import signal
import subprocess
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / 'compression-lab-isolated/vendor'))
from compression_lab import candidate, strings, runner
from compression_lab.benchmark_run import write_metadata
from compression_lab.native_workloads import BASELINES, variants
from compression_lab.util import safe, save, sha

runner.HOST_LOCK = Path(os.environ.get('COMPRESSION_BENCHMARK_LOCK', '/tmp/compression-lab-host-benchmark-v1.lock'))

STANDARD_LIBRARIES = {'libfsst.so', 'liblz4.so.1', 'libzstd.so.1', 'liblzma.so.5',
                      'libbrotlienc.so.1', 'libbrotlidec.so.1', 'libbrotlicommon.so.1',
                      'libz.so.1', 'libbz2.so.1.0'}


def library_pins(binaries, fsst, prefix=None):
    """Resolve the complete standard-codec closure, including Brotli common."""
    known = candidate.libraries()
    known['libfsst.so'] = fsst
    if prefix:
        for name in STANDARD_LIBRARIES - {'libfsst.so'}:
            for folder in ('lib/x86_64-linux-gnu', 'lib', 'lib64'):
                path = prefix / folder / name
                if path.is_file():
                    known[name] = path.resolve()
                    break
    pending = list(binaries); pins = {}
    while pending:
        for name in candidate.elf(pending.pop())['needed']:
            if name in candidate.PLATFORM or name in pins:
                continue
            if name not in STANDARD_LIBRARIES:
                raise RuntimeError('Undeclared custom dependency: ' + name)
            path = known.get(name)
            if path is None or not path.is_file():
                raise RuntimeError('Missing codec library: ' + name)
            pins[name] = dict(path=str(path), sha256=sha(path), standard_codec=True)
            pending.append(path)
    return pins


def prepare_methods(manifests, workload, row_framing):
    """Validate capabilities before creating a run; never rewrite source manifests."""
    variants(workload, row_framing)
    entries = []; names = set()
    for path in manifests:
        path = path.resolve(); manifest = json.loads(path.read_text())
        if set(manifest) != {'name', 'variant', 'encoder', 'decoder'} or manifest['variant'] not in ('bulk', 'rows'):
            raise ValueError('Expected a measured-library manifest with name, variant, encoder and decoder')
        name = manifest['name']
        if not isinstance(name, str) or not name.replace('-', '').replace('_', '').isalnum() or name in names:
            raise ValueError('Method names must be distinct simple identifiers')
        names.add(name)
        if workload == 'query-access' and manifest['variant'] != 'rows':
            raise ValueError('Query access requires a row-access capability: ' + name)
        variant = 'bulk' if workload == 'bulk' else manifest['variant']
        if row_framing == 'none' and variant != 'bulk':
            raise ValueError('Opaque-byte workloads support bulk methods only')
        entries.append(dict(manifest=path, name=name, capability=manifest['variant'], variant=variant,
                            **{role: safe(path.parent, manifest[role]) for role in ('encoder', 'decoder')}))
    if not entries: raise ValueError('Select at least one baseline or candidate')
    return entries


def native_run(config, library, operation, source, output, capacity, ids=None):
    """Only launches the driver; C++ owns every reported operation timer.

    This mirrors the server's process launcher, where user namespaces were
    unavailable. It is intended for the reviewed codecs supplied here.
    """
    output = Path(output)
    output.mkdir()
    argv = [config['driver']['path'], str(library), operation, str(source),
            str(output / 'data'), str(capacity), str(ids) if ids else '/dev/null']

    def limits():
        os.sched_setaffinity(0, {config['cpu']})
        resource.setrlimit(resource.RLIMIT_AS, (config['memory_bytes'],) * 2)

        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))

    env = dict(os.environ, LC_ALL='C', OMP_NUM_THREADS='1', OPENBLAS_NUM_THREADS='1', MKL_NUM_THREADS='1')
    env['LD_LIBRARY_PATH'] = ':'.join(sorted({str(Path(p['path']).parent) for p in config['libraries'].values()}))
    result = subprocess.run(argv, capture_output=True, text=True, env=env, cwd=output,
                            preexec_fn=limits, timeout=None)
    (output / 'stdout').write_text(result.stdout)
    (output / 'stderr').write_text(result.stderr)
    if result.returncode:
        reason = (signal.Signals(-result.returncode).name if result.returncode < 0
                  else 'exit ' + str(result.returncode))
        raise RuntimeError(f'Native driver failed ({reason}): {library}\n{result.stderr}'.rstrip())
    timing = json.loads(result.stdout)
    for key in ('setup_seconds', 'operation_seconds', 'warm_seconds', 'cleanup_seconds'):
        if not math.isfinite(timing[key]) or timing[key] < 0:
            raise RuntimeError('Invalid driver timing')
    if timing['operation_seconds'] <= 0 or timing['output_bytes'] != (output / 'data').stat().st_size:
        raise RuntimeError('Invalid driver output')
    return timing, {'resources': {'cpu': config['cpu'], 'filesystem_sandbox': False},
                    'note': 'Operation timing comes from the unchanged native driver.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--columns', type=Path, default=HERE / 'columns.json',
                        help='Pinned [{name, bytes, sha256}] input manifest; default: complete DBText')
    parser.add_argument('--row-framing', choices=['lf', 'nul', 'none'], default='lf')
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--cpu', type=int, default=min(os.sched_getaffinity(0)))
    parser.add_argument('--methods', nargs='*', help='Baseline IDs; an empty list evaluates only supplied candidates')
    parser.add_argument('--workload', choices=list(BASELINES), help='Bulk or individual-query access')
    parser.add_argument('--candidate', type=Path, action='append', default=[],
                        help='Reviewed measured-library manifest; repeat for additional candidates')
    parser.add_argument('--deps-prefix', type=Path, help='Use codec libraries from this native prefix')
    parser.add_argument('--smoke', action='store_true', help='One supplied column, two passes; not a reported benchmark')
    args = parser.parse_args()
    build = args.build_dir.resolve()
    profile = json.loads((build / 'build-profile.json').read_text()) if (build / 'build-profile.json').exists() else {}
    if profile.get('row_framing', 'lf') != args.row_framing:
        raise RuntimeError('Build and evaluation row framing must match')
    workload = args.workload or profile.get('workload')
    defaults = profile.get('methods') or (list(BASELINES[workload]) if workload else
        ['lz4', 'zstd1', 'fsst', 'onpairplus'])
    if workload and profile.get('workload') != workload:
        defaults = [name for name in defaults if name in BASELINES[workload]]
    methods = args.methods if args.methods is not None else defaults
    if len(set(methods)) != len(methods):
        raise RuntimeError('Methods must be distinct')
    if workload and set(methods).intersection((*BASELINES['bulk'], *BASELINES['query-access'])) - set(BASELINES[workload]):
        raise ValueError('Selected baselines do not belong to the ' + workload + ' workload')
    entries = prepare_methods([*(safe(build, method+'/manifest.json') for method in methods), *args.candidate],
                              workload, args.row_framing)
    if args.smoke:
        columns = [(p.name, p) for p in sorted(args.data_dir.iterdir()) if p.is_file()][:1]
    else:
        columns = []
        for item in json.loads(args.columns.read_text()):
            path = args.data_dir / item['name']
            if path.stat().st_size != item['bytes'] or sha(path) != item['sha256']:
                raise RuntimeError('Wrong dataset column: ' + item['name'])
            columns.append((item['name'], path))
    binaries = []
    for entry in entries:
        for role in ('encoder', 'decoder'):
            binaries.append(entry[role])
    pins = library_pins(binaries, build / 'fsst-lib/libfsst.so',
                        args.deps_prefix.resolve() if args.deps_prefix else None)
    strings.init(args.out, columns, build / 'driver', pins, args.cpu, row_framing=args.row_framing,
                 driver_sources={'driver.cpp': HERE/'benchmark.cpp', 'codec.h': HERE/'codec.h'})
    config = strings.config(args.out)
    write_metadata(args.out, protocol='strings-v1', inputs=dict(columns), cpu=args.cpu, smoke=args.smoke,
        dataset='DBText' if args.columns.resolve() == (HERE/'columns.json').resolve() else args.columns.stem,
        parameters={**{key:config[key] for key in ('row_framing','warmups','seed','memory_bytes',
                                                 'timeout_seconds','selectivities','full_decode','row_decode')},
                    'trials':1 if args.smoke else config['trials'],
                    **({'evaluation_workload':workload} if workload else {})},
        artifacts=[build/'driver',*binaries,*(entry['manifest'] for entry in entries),
                   *(pin['path'] for pin in pins.values())])
    results = []
    for entry in entries:
        print('Measuring', entry['name'], flush=True)
        folder = args.out/'candidates'/entry['name']; folder.mkdir(parents=True)
        for role in ('encoder', 'decoder'):
            shutil.copyfile(entry[role], folder/(role+'.so'))
        save(folder/'manifest.json', dict(name=entry['name'], variant=entry['variant'],
                                         encoder='encoder.so', decoder='decoder.so'))
        save(folder/'provenance.json', dict(source_manifest=str(entry['manifest']),
             source_sha256=sha(entry['manifest']), capability=entry['capability'],
             evaluation_variant=entry['variant'], binaries={role:sha(folder/(role+'.so')) for role in ('encoder','decoder')}))
        result = strings.evaluate(args.out, folder/'manifest.json', quick=args.smoke, run=native_run)
        if not result['quality_passed']:
            raise RuntimeError(json.dumps(result.get('error')))
        results.append(result)
    save(args.out / 'summary.json', {'smoke': args.smoke, 'results': results})


if __name__ == '__main__':
    main()
