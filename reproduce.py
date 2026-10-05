#!/usr/bin/env python3
"""Validate retained sources, build codecs, verify exact inputs, or regenerate figures."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent


def load_builder():
    spec = importlib.util.spec_from_file_location('codec_build', ROOT / 'synthesized-code/build.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def catalog():
    return json.loads((ROOT / 'synthesized-code/INDEX.json').read_bytes())['runs']


def check_sources(require_original=False):
    builder = load_builder()
    for entry in catalog():
        builder.verify(ROOT / entry['path'])
    print('Verified retained source hashes for', len(catalog()), 'codecs.', flush=True)
    rebuilt = [r['id'] for r in catalog() if r.get('recipe_status', 'recorded_commands') != 'recorded_commands']
    if rebuilt:
        print('Original build recipe pending:', ', '.join(rebuilt), flush=True)
        if require_original:
            raise RuntimeError('Recover original recipes before certifying the complete paper build.')
    for directory, manifest, key in [
        (ROOT/'paper/figures', ROOT/'paper/figures/FILES.json', None),
        (ROOT/'benchmarks/final/sources', ROOT/'benchmarks/final/sources/SOURCES.json', None),
        (ROOT/'compression-lab-isolated/vendor', ROOT/'compression-lab-isolated/runtime-lock.json', 'files')]:
        pins=json.loads(manifest.read_bytes())
        if key: pins=pins[key]
        for name,record in pins.items():
            expected=record if isinstance(record,str) else record['sha256']
            if hashlib.sha256((directory/name).read_bytes()).hexdigest()!=expected:
                raise RuntimeError('Pinned reproduction input changed: '+str(directory/name))
    print('Verified final figure, driver and tool-only runtime hashes.',flush=True)


def codec_jobs(action, work, data, only):
    if sys.platform != 'linux' or platform.machine() != 'x86_64':
        raise RuntimeError('Native codecs require Linux x86-64; use Linux or WSL2.')
    entries = [r for r in catalog() if not only or r['id'] in only]
    if only and set(only) != {r['id'] for r in entries}:
        raise ValueError('Unknown codec ID; see synthesized-code/INDEX.json')
    outcomes = []
    for entry in entries:
        run = ROOT / entry['path']
        output = work / 'build' / entry['id']
        log = work / 'logs' / (entry['id'] + '-' + action + '.log')
        log.parent.mkdir(parents=True, exist_ok=True)
        if action == 'build':
            command = [sys.executable, str(ROOT / 'synthesized-code/build.py'), str(run), '--output', str(output)]
        else:
            command = [sys.executable, str(ROOT / 'synthesized-code/run.py'), str(run), str(data / entry['dataset']),
                       '--build-dir', str(output), '--dataset', entry['dataset'],
                       '--receipt', str(work / 'roundtrips' / (entry['id'] + '.json'))]
        with log.open('wb') as stream:
            try:
                result = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT,
                                        timeout=None)
                status = 'passed' if result.returncode == 0 else 'failed'
                code = result.returncode
            except subprocess.TimeoutExpired:
                status, code = 'timed_out', None
        outcomes.append({'id': entry['id'], 'status': status, 'exit_code': code})
        print(entry['id'], action, status, flush=True)
    receipt = {'action': action, 'platform': platform.platform(), 'runs': outcomes}
    (work / (action + '-audit.json')).write_text(json.dumps(receipt, indent=2) + '\n')
    if any(r['status'] != 'passed' for r in outcomes):
        raise RuntimeError('Failures recorded in ' + str(work / (action + '-audit.json')))


def plots(output, inputs=None):
    figures = json.loads((ROOT / 'synthesized-code/FIGURES.json').read_bytes())['figures']
    output.mkdir(parents=True, exist_ok=False)
    receipts = []
    for figure in figures:
        original = inputs / Path(figure['directory']).name if inputs else ROOT / figure['directory']
        target = output / figure['id']
        target.mkdir()
        shutil.copytree(original, target, dirs_exist_ok=True)
        subprocess.run([sys.executable, str(target / figure['script'])], cwd=ROOT, check=True)
        for name in figure['pdfs']:
            path = target / name
            if not path.is_file():
                raise RuntimeError('Missing reproduced figure: ' + str(path))
            receipts.append({'figure': figure['id'], 'file': name,
                             'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    assets = ROOT / 'paper/figures/schematics'
    for item in json.loads((ROOT / 'synthesized-code/FIGURES.json').read_bytes())['schematics']:
        path = output / ('figure-' + str(item['figure']) + '.pdf')
        shutil.copyfile(assets / item['file'], path)
        receipts.append({'figure': 'figure-' + str(item['figure']), 'file': path.name,
                         'sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'kind': 'schematic asset'})
    (output / 'figure-receipt.json').write_text(json.dumps(receipts, indent=2) + '\n')
    print('Reproduced', len(receipts), 'paper PDFs in', output, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['check', 'build', 'verify', 'plots', 'all', 'benchmark'])
    parser.add_argument('--work', type=Path, default=ROOT / 'work/reproduction')
    parser.add_argument('--data', type=Path, default=ROOT / 'work/data')
    parser.add_argument('--only', nargs='+', help='Codec IDs from synthesized-code/INDEX.json')
    parser.add_argument('--cpu', type=int, help='Required for fresh benchmarks; choose an allowed CPU')
    parser.add_argument('--deps-prefix', type=Path, help='Optional local native library installation')
    parser.add_argument('--project-lock', type=Path, help='Optional second shared server benchmark lock')
    parser.add_argument('--datasets', nargs='+', choices=['dbtext','openstack','python','yelp','sqlstorm-tpcds'])
    parser.add_argument('--require-original-recipes', action='store_true',
                        help='Reject verification recipes reconstructed from the submitted source')
    args = parser.parse_args()
    work = args.work.resolve()
    if args.action != 'plots':
        check_sources(args.require_original_recipes)
    if args.action in ('build', 'all'):
        codec_jobs('build', work, args.data.resolve(), args.only)
    if args.action in ('verify', 'all'):
        codec_jobs('verify', work, args.data.resolve(), args.only)
    if args.action in ('plots', 'all'):
        plots(work / 'plots')
    if args.action == 'benchmark':
        if args.cpu is None: parser.error('--cpu is required for benchmark')
        argv = [sys.executable, str(ROOT / 'benchmarks/final/run.py'), '--work',str(work),
                '--data',str(args.data.resolve()), '--cpu',str(args.cpu)]
        if args.deps_prefix: argv += ['--deps-prefix',str(args.deps_prefix.resolve())]
        if args.project_lock: argv += ['--project-lock',str(args.project_lock.resolve())]
        if args.datasets: argv += ['--datasets',*args.datasets]
        subprocess.run(argv,check=True)
        complete = all((work / 'final-benchmarks' / ds / 'COMPLETE.json').is_file()
                       for ds in ['dbtext','openstack','python','yelp','sqlstorm-tpcds'])
        if complete:
            plots(work / 'fresh-plots', work / 'fresh-figure-inputs')
        else:
            print('Selected datasets completed; finish all five before rendering the complete fresh comparison.')


if __name__ == '__main__':
    main()
