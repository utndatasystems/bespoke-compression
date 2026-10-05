"""Replay the unmodified FSST paper selection/timing loop with our ABI adapter."""
import argparse
import json
import os
from pathlib import Path
import statistics
import subprocess
import hashlib
import sys

HERE = Path(__file__).resolve().parent
FSST = HERE.parent / 'upstream/fsst'
sys.path.insert(0, str(HERE.parents[1] / 'compression-lab-isolated/vendor'))
from compression_lab.benchmark_run import measurement_lock, write_metadata
from compression_lab import runner

runner.HOST_LOCK = Path(os.environ.get('COMPRESSION_BENCHMARK_LOCK', '/tmp/compression-lab-host-benchmark-v1.lock'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True, help='Output from dbtext/build.py')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--column', action='append', help='Repeat to select pinned DBText columns; default all 23')
    parser.add_argument('--cpu', type=int, default=min(os.sched_getaffinity(0)))
    parser.add_argument('--methods', nargs='*', choices=['fsst','lz4','uncompressed','onpairplus','astra'],
                        default=['fsst','lz4','onpairplus'])
    parser.add_argument('--candidate', action='append', default=[], metavar='NAME=MANIFEST',
                        help='Add a reviewed row-capable codec; repeat for additional models')
    parser.add_argument('--smoke', action='store_true', help='Use one supplied column and one replay')
    parser.add_argument('--replays', type=int, default=7)
    args = parser.parse_args()
    if args.replays < 1:
        parser.error('--replays must be positive')
    build, out = args.build_dir.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    if args.smoke and not args.column:
        inputs = sorted(p.resolve() for p in args.data_dir.iterdir() if p.is_file())[:1]
    else:
        pins = json.loads((HERE.parent / 'dbtext/columns.json').read_text())
        if args.column:
            unknown = set(args.column) - {col['name'] for col in pins}
            if unknown:
                parser.error('Unknown column: ' + ', '.join(sorted(unknown)))
            pins = [col for col in pins if col['name'] in args.column]
        inputs = []
        for col in pins:
            path = (args.data_dir / col['name']).resolve()
            if hashlib.sha256(path.read_bytes()).hexdigest() != col['sha256']:
                raise RuntimeError('Wrong input column: ' + col['name'])
            inputs.append(path)
        inputs.sort()
        if args.smoke:
            inputs = inputs[:1]
    if not inputs or any(p.stat().st_size >= 7000000 or not p.read_bytes().endswith(b'\n') for p in inputs):
        raise RuntimeError('Paper selective path requires LF-terminated columns below its 7 MB cutoff')
    argv = ['g++', '-std=c++17', '-O3', '-DNDEBUG', '-I' + str(FSST / 'paper'), '-I' + str(FSST),
            str(HERE / 'wrapper.cpp'), '-L' + str(build / 'fsst-lib'), '-lfsst', '-llz4', '-ldl',
            '-Wl,-rpath,' + str(build / 'fsst-lib'), '-o', str(out / 'filtertest')]
    result = subprocess.run(argv, capture_output=True, text=True, timeout=None)
    (out / 'build.json').write_text(json.dumps(dict(argv=argv, stdout=result.stdout, stderr=result.stderr), indent=2))
    if result.returncode:
        raise RuntimeError(result.stderr)
    os.sched_setaffinity(0, {args.cpu})
    available = {'fsst': ['fsst', '1000'], 'lz4': ['lz4', '1000'],
                 'uncompressed': ['nocompression', '1000']}
    for name, folder in [('onpairplus', 'onpairplus'), ('astra', 'astra-rows')]:
        available[name] = ['native', str(build / folder / 'encoder.so'), str(build / folder / 'decoder.so')]
    methods = {name: available[name] for name in args.methods}
    for spec in args.candidate:
        name, separator, value = spec.partition('=')
        if not separator or not name.replace('-', '').replace('_', '').isalnum() or name in methods:
            parser.error('Each candidate needs a distinct NAME=MANIFEST')
        path = Path(value).resolve(); manifest = json.loads(path.read_text())
        if manifest.get('variant') != 'rows':
            parser.error('Selected-row benchmarks require a row-capable codec: ' + name)
        methods[name] = ['native', *[str((path.parent / manifest[role]).resolve()) for role in ('encoder','decoder')]]
    if not methods:
        parser.error('Select at least one method')
    env = dict(os.environ, DEBUG='1', LC_ALL='C', OMP_NUM_THREADS='1', OPENBLAS_NUM_THREADS='1')
    env.pop('LOOP', None)
    write_metadata(out, protocol='fsst-paper-selective-v1', dataset='dbtext',
        inputs={p.name: p for p in inputs}, cpu=args.cpu, smoke=args.smoke,
        parameters=dict(replays=1 if args.smoke else args.replays, warmups=100, calls=100,
                        decode='warm selected rows', aggregation='geometric mean of per-column row throughput',
                        selectivities=[1, 3, 10, 30, 100], seed=123, lz4_block_rows=1000),
        artifacts=[out / 'filtertest', build / 'fsst-lib/libfsst.so',
                   *[Path(p) for command in methods.values() if command[0]=='native' for p in command[1:]]])
    records = []
    with measurement_lock():
        for repeat in range(1 if args.smoke else args.replays):
            names = list(methods)
            offset = repeat % len(names)
            for name in names[offset:] + names[:offset]:
                print('Replay', repeat + 1, name, flush=True)
                result = subprocess.run([str(out / 'filtertest'), *methods[name], *map(str, inputs)],
                                        env=env, capture_output=True, text=True, timeout=None)
                for stream in ('stdout', 'stderr'):
                    (out / f'{repeat + 1}-{name}.{stream}').write_text(getattr(result, stream))
                if result.returncode:
                    raise RuntimeError(result.stderr)
                scores = {}
                for line in result.stdout.splitlines():
                    fields = line.split()
                    if len(fields) == 3 and fields[0] in {'1', '3', '10', '30', '100'}:
                        scores[fields[0]] = float(fields[2])
                if len(scores) != 5:
                    raise RuntimeError('Incomplete paper benchmark output')
                size = int(next(line.split(':')[1] for line in result.stdout.splitlines() if line.startswith('# total compress size:')))
                records.append(dict(replay=repeat + 1, method=name, archive_bytes=size, krows_per_second=scores))
                (out / 'trials.json').write_text(json.dumps(records, indent=2) + '\n')
    summary = {name: {p: statistics.median(r['krows_per_second'][p] for r in records if r['method'] == name)
                     for p in ('1', '3', '10', '30', '100')} for name in methods}
    (out / 'summary.json').write_text(json.dumps(dict(smoke=args.smoke, cpu=args.cpu, krows_per_second=summary), indent=2) + '\n')


if __name__ == '__main__':
    main()
