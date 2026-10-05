"""Replay the current OnPair16 baseline on all pinned DBText columns."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'compression-lab-isolated/vendor'))
from compression_lab import runner
from compression_lab.benchmark_run import measurement_lock

runner.HOST_LOCK = Path(os.environ.get('COMPRESSION_BENCHMARK_LOCK', '/tmp/compression-lab-host-benchmark-v1.lock'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--cpu', type=int, default=min(os.sched_getaffinity(0)))
    parser.add_argument('--replays', type=int, default=7)
    args = parser.parse_args()
    if args.cpu not in os.sched_getaffinity(0) or args.replays < 1:
        parser.error('Choose an allowed CPU and at least one replay')
    pins = sorted(json.loads((ROOT / 'datasets/dbtext.json').read_bytes()), key=lambda p: p['name'])
    inputs = [args.data_dir.resolve() / p['name'] for p in pins]
    for pin, path in zip(pins, inputs):
        if path.stat().st_size != pin['bytes'] or hashlib.sha256(path.read_bytes()).hexdigest() != pin['sha256']:
            raise RuntimeError('Input identity mismatch: ' + str(path))
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=False)
    build = args.build_dir.resolve()
    os.sched_setaffinity(0, {args.cpu})
    environment = dict(os.environ, DEBUG='1', LC_ALL='C')
    environment.pop('LOOP', None)
    records = []
    with measurement_lock():
        for repeat in range(args.replays):
            folder = output / str(repeat + 1)
            folder.mkdir()
            archives = folder / 'archives'
            archives.mkdir()
            environment['AUDIT_EXPORT_DIR'] = str(archives)
            result = subprocess.run([str(build / 'rows'), *map(str, inputs)], env=environment,
                                    capture_output=True, text=True, timeout=None)
            (folder / 'stdout.txt').write_text(result.stdout)
            (folder / 'stderr.txt').write_text(result.stderr)
            result.check_returncode()
            scores = {}
            for line in result.stdout.splitlines():
                fields = line.split()
                if len(fields) == 3 and fields[0] in {'1', '3', '10', '30', '100'}:
                    scores[fields[0]] = float(fields[2])
            if len(scores) != 5:
                raise RuntimeError('Incomplete selective benchmark output')
            for index, (pin, path) in enumerate(zip(pins, inputs)):
                restored = folder / (pin['name'] + '.decoded')
                subprocess.run([str(build / 'decode-archive'), str(archives / f'{index}.bin'), str(restored)], check=True)
                if restored.stat().st_size != pin['bytes'] or hashlib.sha256(restored.read_bytes()).hexdigest() != pin['sha256']:
                    raise RuntimeError('Independent reconstruction mismatch: ' + pin['name'])
            records.append(dict(replay=repeat + 1, krows_per_second=scores, exact_roundtrips=len(inputs)))
            (output / 'trials.json').write_text(json.dumps(records, indent=2) + '\n')
            print('OnPair16 replay', repeat + 1, 'passed all', len(inputs), 'columns', flush=True)
    receipt = dict(cpu=args.cpu, inputs=pins, replays=args.replays,
                   krows_per_second={p: statistics.median(r['krows_per_second'][p] for r in records)
                                     for p in ('1', '3', '10', '30', '100')})
    (output / 'summary.json').write_text(json.dumps(receipt, indent=2) + '\n')


if __name__ == '__main__':
    main()
