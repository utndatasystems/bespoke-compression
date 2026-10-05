"""Replay all submitted configurations and populate the final figure inputs."""
import argparse
import contextlib
import csv
import fcntl
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import resource
import shutil
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
BASELINES = ['LZ4-default', 'LZ4-HC9', 'LZ4-HC12', *['Zstd-' + str(n) for n in [1,3,19,20,21,22]],
             *['Brotli-' + str(n) for n in [5,9,11]], *['zlib-' + str(n) for n in [1,6,9]],
             'bzip2-1', 'bzip2-9', 'XZ-0', 'XZ-6', 'XZ-9']
_CLOSURES = {}

def dependency_ledger(binary):
    """Inventory trusted frozen ELF dependencies outside the timed operation."""
    binary = binary.resolve()
    if binary in _CLOSURES: return _CLOSURES[binary]
    sys.path.insert(0, str(ROOT / 'compression-lab-isolated/vendor'))
    from compression_lab import candidate
    text = subprocess.check_output(['ldd', str(binary)], text=True)
    if 'not found' in text: raise RuntimeError('Missing runtime dependency: ' + text)
    records = []
    for line in text.splitlines():
        words = line.split()
        path = words[2] if len(words)>2 and words[1]==' =>' else None
        if '=>' in words: path = words[words.index('=>')+1]
        if not path and words and words[0].startswith('/'): path=words[0]
        if not path or not path.startswith('/'): continue
        file=Path(path).resolve()
        records.append({'soname':words[0] if '=>' in words else file.name,
                        'path':str(file), 'bytes':file.stat().st_size, 'sha256':sha(file),
                        'platform':words[0] in candidate.PLATFORM or 'ld-linux' in file.name})
    _CLOSURES[binary] = records
    return records

def load(path): return json.loads(path.read_bytes())
def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + '\n')
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def csvwrite(path, records):
    with path.open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(records[0]), lineterminator='\n')
        writer.writeheader(); writer.writerows(records)

def entries():
    return [r for r in load(ROOT / 'synthesized-code/INDEX.json')['runs']
            if 'luna' not in r['path'] and 'glm-5.3-flash/yelp' not in r['path'] and '/05-' not in r['path']]

def identity(entry):
    tail = entry['path'].split('/')[-1]
    stage = int(tail[-2:] if tail.startswith('run-') else tail[1:] if tail[0] in 'AG' else tail[:2])
    arm = ('glm' if 'glm-5.3-flash' in entry['path'] else 'boundary-only' if '/no-reference-target/' in entry['path']
           else 'tools-allowed' if '/tools-allowed/' in entry['path'] else 'from-scratch')
    return arm, stage

def command(argv, log):
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open('wb') as stream:
        result = subprocess.run(list(map(str, argv)), stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode: raise RuntimeError('Command failed; see ' + str(log))

def measure_original(method, data, pins, folder, driver, cpu, candidate=None):
    folder.mkdir(parents=True, exist_ok=False)
    records = []
    def limits():
        os.sched_setaffinity(0, {cpu})
        resource.setrlimit(resource.RLIMIT_AS, (2 * 1024**3,) * 2)
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    for pin in pins:
        target = folder / pin['name']; target.mkdir()
        manifest = load(candidate) if candidate else None
        encoder = candidate.parent / manifest['encoder'] if candidate else Path('-')
        decoder = candidate.parent / manifest['decoder'] if candidate else Path('-')
        argv = [driver, 'native' if candidate else method, data / pin['name'], target,
                'original', encoder, decoder, 'bulk']
        with (target / 'stdout.json').open('w') as out, (target / 'stderr.txt').open('w') as err:
            result = subprocess.run(list(map(str, argv)), stdout=out, stderr=err, timeout=None, preexec_fn=limits)
        receipt = {'argv': list(map(str, argv)), 'returncode': result.returncode,
                   'input': pin, 'driver_sha256': sha(driver)}
        save(target / 'receipt.json', receipt)
        result.check_returncode()
        m = load(target / 'stdout.json')
        if not (m['byte_exact'] and m['raw_bytes'] == pin['bytes'] and m['verified_decodes'] == 8
                and m['warmups'] == 1 and m['trials'] == 7 and m['calls_per_trial'] == 1):
            raise RuntimeError('Final protocol mismatch: ' + str(target))
        if any(not math.isfinite(t) or t < 0 for t in m['open_seconds'] + m['decode_seconds']):
            raise RuntimeError('Invalid timing')
        artifacts = {p.name: {'bytes': p.stat().st_size, 'sha256': sha(p)} for p in target.glob('*.bin')}
        receipt.update(measurement=m, artifacts=artifacts)
        save(target / 'receipt.json', receipt); records.append(m)
    n = sum(p['bytes'] for p in pins)
    elapsed = [sum(r['open_seconds'][i] + r['decode_seconds'][i] for r in records) for i in range(7)]
    if min(elapsed) <= 0: raise RuntimeError('Nonpositive corpus time')
    archive = sum(r['archive_bytes'] for r in records)
    decoder_bytes = (candidate.parent / load(candidate)['decoder']).stat().st_size if candidate else 0
    runtime = candidate.parent / load(candidate)['decoder'] if candidate else driver
    dependencies = dependency_ledger(runtime)
    nonplatform = sum(r['bytes'] for r in dependencies if not r['platform'])
    deployment = archive + runtime.stat().st_size + nonplatform
    ledger = {'runtime_sha256':sha(runtime), 'runtime_bytes':runtime.stat().st_size,
              'dependencies':dependencies, 'nonplatform_dependency_bytes':nonplatform,
              'deployment_bytes':deployment,
              'scope':'decoder and its full nonplatform closure' if candidate else
                      'upper bound: shared measurement driver includes multiple codecs and encoders'}
    save(folder / 'deployment-ledger.json',ledger)
    point = {'method': method, 'package_bytes': archive + decoder_bytes, 'archive_bytes': archive,
             'original_bytes':n, 'bits_per_byte':8*(archive+decoder_bytes)/n,
             'roundtrip_failures':0, 'deployment_bytes':deployment, 'deployment_scope':ledger['scope'],
             'decoder_bytes': decoder_bytes, 'ratio': n / (archive + decoder_bytes),
             'median': n / statistics.median(elapsed) / 1e6, 'low': n / max(elapsed) / 1e6,
             'high': n / min(elapsed) / 1e6, 'encode_MB_s': n / sum(r['encode_seconds'] for r in records) / 1e6,
             'exact_decodes': 8 * len(pins)}
    save(folder / 'summary.json', point)
    return point

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True, help='Existing complete build/verify output from reproduce.py all')
    parser.add_argument('--cpu', type=int, required=True)
    parser.add_argument('--deps-prefix', type=Path)
    parser.add_argument('--datasets', nargs='+', choices=['dbtext','openstack','python','yelp','sqlstorm-tpcds'],
                        default=['dbtext','openstack','python','yelp','sqlstorm-tpcds'])
    parser.add_argument('--project-lock', type=Path, help='Additional shared benchmark lock on a research server')
    args = parser.parse_args(); work = args.work.resolve(); data = args.data.resolve()
    if args.cpu not in os.sched_getaffinity(0): parser.error('CPU must be in the allowed affinity')
    if args.deps_prefix:
        prefix = args.deps_prefix.resolve()
        for variable, folders in {'CPATH':['include'], 'LIBRARY_PATH':['lib/x86_64-linux-gnu','lib'],
                                   'LD_LIBRARY_PATH':['lib/x86_64-linux-gnu','lib']}.items():
            os.environ[variable] = ':'.join([*[str(prefix / f) for f in folders],
                                            *([os.environ[variable]] if variable in os.environ else [])])
    verify = load(work / 'verify-audit.json')
    if any(r['status'] != 'passed' for r in verify['runs']): raise RuntimeError('Complete codec verification required')
    final = work / 'final-benchmarks'
    if not (final / 'drivers').exists():
        command([sys.executable, HERE / 'build.py', '--out', final / 'drivers'], work / 'logs/final-driver-build.log')
    drivers = final / 'drivers'
    figures = work / 'fresh-figure-inputs'
    if not figures.exists(): shutil.copytree(ROOT / 'paper/figures', figures)
    sys.path.insert(0, str(ROOT / 'compression-lab-isolated/vendor'))
    from compression_lab.benchmark_run import write_metadata
    from compression_lab import runner
    runner.HOST_LOCK = Path(os.environ.get('COMPRESSION_BENCHMARK_LOCK', '/tmp/compression-lab-host-benchmark-v1.lock'))
    methods = entries()
    with contextlib.ExitStack() as stack:
        if args.project_lock:
            lease = stack.enter_context(args.project_lock.open('a')); fcntl.flock(lease, fcntl.LOCK_EX)
        for ds in args.datasets:
            folder = final / ds
            if folder.exists(): raise RuntimeError('Existing attempt must be inspected, not replayed: ' + str(folder))
            folder.mkdir()
            pins = load(ROOT / f'datasets/{ds}.json')
            for pin in pins:
                path = data / ds / pin['name']
                if path.stat().st_size != pin['bytes'] or sha(path) != pin['sha256']: raise RuntimeError('Input mismatch: ' + str(path))
            write_metadata(folder, protocol='final-paper-replay', inputs={p['name']: data / ds / p['name'] for p in pins},
                cpu=args.cpu, dataset=ds, parameters={'warmups':1, 'trials':7, 'threads':1,
                'row_protocol':'unchanged Lab driver, three outer replays, 100 warmups/100 calls, seed123'}, artifacts=list(drivers.glob('*-generic')))
            if ds == 'dbtext':
                bulk_points = []
                labels = ['FSST','FSST-column','LZ4-paper-1000','OnPair+','Uncompressed',*BASELINES]
                with runner.HOST_LOCK.open('a') as lease:
                    fcntl.flock(lease, fcntl.LOCK_EX)
                    for name in labels:
                        print(ds, name, flush=True)
                        profile = 'fsst' if name.startswith('FSST') else 'onpair' if name == 'OnPair+' else 'generic'
                        point = measure_original(name, data / ds, pins, folder / name, drivers / ('dbtext-' + profile), args.cpu)
                        bulk_points.append({'arm':'baseline','name':name,'stage':0,**point})
                    for entry in [r for r in methods if r['dataset'] == ds]:
                        print(ds, entry['id'], flush=True)
                        arm, stage = identity(entry)
                        manifest = work / 'build' / entry['id'] / 'manifest.json'
                        evidence = folder / entry['id']
                        point = measure_original(entry['id'], data / ds, pins, evidence, drivers / 'dbtext-generic', args.cpu, manifest)
                        shutil.copyfile(manifest.parent / load(manifest)['decoder'], evidence / 'decoder.so')
                        (evidence / 'archives').mkdir()
                        trial_records = []
                        for pin in pins:
                            archive = evidence / pin['name'] / 'archive.bin'
                            shutil.copyfile(archive, evidence / 'archives' / (pin['sha256'] + '.bin'))
                            trial_records.append({'column':pin['name'], 'archive_sha256':sha(archive)})
                        (evidence / 'trials.jsonl').write_text(''.join(json.dumps(r)+'\n' for r in trial_records))
                        spec = importlib.util.spec_from_file_location('final_rows', HERE / 'rows.py')
                        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
                        selective = module.measure(data / ds, pins, evidence, evidence / 'decoder.so', drivers / 'rows', args.cpu)
                        point['selective'] = selective['selective']
                        bulk_points.append({'arm':arm,'name':('G' if arm=='glm' else 'A')+str(stage),'stage':stage,**point})
                # Native control row loops remain their published seven-replay protocols.
                row_build = final / 'row-controls'
                command([sys.executable, ROOT / 'benchmarks/paper/build.py', '--out', row_build,
                         '--row-framing','lf','--methods','fsst'], work / 'logs/row-control-build.log')
                command([sys.executable, ROOT / 'benchmarks/fsst-paper/run.py','--data-dir',data / ds,
                    '--build-dir',row_build,'--out',folder / 'control-rows','--methods','fsst','lz4','uncompressed','--cpu',args.cpu], work / 'logs/control-rows.log')
                onpair = ROOT / 'synthesized-code/dbtext/astra/onpair16-row-baseline'
                command([sys.executable,onpair / 'build.py','--out',final / 'onpair16'], work / 'logs/onpair16-build.log')
                command([sys.executable,onpair / 'run.py','--data-dir',data / ds,'--build-dir',final / 'onpair16',
                    '--out',folder / 'onpair16-rows','--cpu',args.cpu], work / 'logs/onpair16-rows.log')
                update_dbtext(figures, bulk_points, folder)
            else:
                build = final / (ds + '-baselines')
                command([sys.executable, ROOT / 'benchmarks/paper/build.py','--out',build,
                         '--row-framing','nul' if ds=='sqlstorm-tpcds' else 'lf'], work / ('logs/'+ds+'-baseline-build.log'))
                selected = [r for r in methods if r['dataset'] == ds]
                argv = [sys.executable,ROOT / 'benchmarks/paper/run.py','--dataset',ds,'--data-dir',data / ds,
                        '--build-dir',build,'--out',folder / 'native','--cpu',args.cpu]
                if args.deps_prefix: argv += ['--deps-prefix',args.deps_prefix]
                for entry in selected: argv += ['--candidate',work / 'build' / entry['id'] / 'manifest.json']
                command(argv, work / ('logs/'+ds+'-bulk.log'))
                extra = []
                with runner.HOST_LOCK.open('a') as lease:
                    fcntl.flock(lease, fcntl.LOCK_EX)
                    for name, profile in [('FSST-column','fsst'),('OnPair+','onpair')]:
                        print(ds,name,flush=True)
                        extra.append(measure_original(name,data / ds,pins,folder / name,drivers / ('bulk-'+profile),args.cpu))
                update_bulk(figures, ds, folder / 'native/paper-results.csv', selected, extra)
            save(folder / 'COMPLETE.json', {'dataset':ds, 'full':True, 'byte_exact':True})
            print(ds,'complete',flush=True)
    print('Fresh plot inputs:', figures, flush=True)

def update_bulk(figures, ds, result_file, selected, extra):
    rows = list(csv.DictReader(result_file.open()))
    mapping = {'LZ4 default':'lz4','LZ4 HC-9':'lz4hc9','LZ4 HC-12':'lz4hc12',
               **{'Zstd-'+str(n):'zstd'+str(n) for n in [1,3,19,20,21,22]},
               'Brotli-5':'brotli5','Brotli-9':'brotli9','Brotli-11 (default)':'brotli11',
               'zlib-1':'zlib1','zlib-6 (default)':'zlib6','zlib-9':'zlib9',
               'bzip2-1':'bzip2-1','bzip2-9 (default)':'bzip2-9',
               'XZ-0':'xz0','XZ-6 (default)':'xz6','XZ-9':'xz9'}
    target = figures / 'bulk/data' / (ds + '-baseline.csv')
    original = list(csv.DictReader(target.open()))
    for r in original:
        if r['method'].startswith('FSST') or r['method']=='OnPair+':
            p = next(p for p in extra if p['method'] == ('FSST-column' if r['method'].startswith('FSST') else 'OnPair+'))
            factor, speed, size = p['ratio'],p['median'],p['package_bytes']
        else:
            p = next(p for p in rows if p['method']==mapping[r['method'].replace('Zstd-3 (default)','Zstd-3')])
            factor, speed, size = p['package_factor'],p['decode_MB_s'],p['package_bytes']
        r.update(compression_factor=factor,decompression_median_MB_s=speed,package_bytes=size,result_id='fresh-server-replay')
    csvwrite(target,original)
    points = []
    for entry in selected:
        name = load(result_file.parent.parent.parent.parent / 'build' / entry['id'] / 'manifest.json')['name']
        p = next(r for r in rows if r['method']==name)
        points.append({'run':identity(entry)[1], 'compression_factor':p['package_factor'], 'decompression_median_MB_s':p['decode_MB_s']})
    csvwrite(figures / 'bulk/data' / (ds + '-generated.csv'), points)

def update_dbtext(figures, points, folder):
    target = figures / 'dbtext/data/bulk-points.csv'
    original = [r for r in csv.DictReader(target.open()) if r['arm'] in {'baseline','from-scratch','boundary-only','tools-allowed','glm'} and int(r['stage']) <= 4]
    for r in original:
        p = next(p for p in points if p['arm']==r['arm'] and (p['name']==r['name'] if r['arm']=='baseline' else p['stage']==int(r['stage'])))
        r.update(package_bytes=p['package_bytes'],ratio=p['ratio'],median=p['median'],low=p['low'],high=p['high'],source='fresh-final-benchmark',result_id='fresh-server-replay')
    csvwrite(target, original)
    base = figures / 'random-access/data'
    for rel, glm in [('synthesized-code/dbtext/astra/data/row-points.csv',False),
                     ('synthesized-code/glm-5.3-flash/dbtext/tools-allowed/data/row-points.csv',True)]:
        path = base / rel
        rows = [r for r in csv.DictReader(path.open()) if int(r['stage']) <= 4
                and (r['arm'] != 'baseline' or r['name'] in {'FSST','LZ4 block','OnPair+ row adapter'})]
        controls = load(folder / 'control-rows/summary.json')['krows_per_second']
        onpair = load(folder / 'onpair16-rows/summary.json')['krows_per_second']
        control_trials = load(folder / 'control-rows/trials.json')
        onpair_trials = load(folder / 'onpair16-rows/trials.json')
        for r in rows:
            if r['arm']=='baseline':
                values = onpair if r['name']=='OnPair+ row adapter' else controls[{'FSST':'fsst','LZ4 block':'lz4','Uncompressed':'uncompressed'}[r['name']]]
                r['median'] = values[r['percent']] / 1000
                trials = onpair_trials if r['name']=='OnPair+ row adapter' else [t for t in control_trials
                    if t['method']=={'FSST':'fsst','LZ4 block':'lz4'}[r['name']]]
                rates = [t['krows_per_second'][r['percent']] / 1000 for t in trials]
                r.update(low=min(rates),high=max(rates))
                if r['name']=='OnPair+ row adapter':
                    r['package_bytes'] = sum(p.stat().st_size for p in (folder / 'onpair16-rows/1/archives').glob('*.bin')) + (folder.parent / 'onpair16/librow-adapter.so').stat().st_size
                else:
                    label = {'FSST':'FSST','LZ4 block':'LZ4-paper-1000','Uncompressed':'Uncompressed'}[r['name']]
                    r['package_bytes'] = next(p['package_bytes'] for p in points if p['arm']=='baseline' and p['name']==label)
            else:
                p = next(p for p in points if p['arm']==r['arm'] and p['stage']==int(r['stage']))
                r['package_bytes'] = p['package_bytes']
                r['median'] = p['selective'][r['percent']]['median_rows_M_s']
                r.update(low=p['selective'][r['percent']]['min_rows_M_s'],
                         high=p['selective'][r['percent']]['max_rows_M_s'])
            r.update(source='fresh-final-benchmark',result_id='fresh-server-replay')
        csvwrite(path, rows)
        if glm:
            for stage in range(1,5):
                path = base / f'synthesized-code/glm-5.3-flash/dbtext/tools-allowed/G{stage}/provenance.json'
                r=load(path); p=next(p for p in points if p['arm']=='glm' and p['stage']==stage)
                r.update(package_bytes=p['package_bytes'],result_id='fresh-server-replay'); save(path,r)
    for stage in range(1,5):
        path=base / f'ablation/stage-{stage:02d}/QUALIFICATION.json'; r=load(path)
        p=next(p for p in points if p['arm']=='boundary-only' and p['stage']==stage)
        r.update(package_bytes=p['package_bytes'],result_id='fresh-server-replay',rows_M_s={k:{'median':v['median_rows_M_s'],
            'min':v['min_rows_M_s'],'max':v['max_rows_M_s']} for k,v in p['selective'].items()}); save(path,r)

if __name__ == '__main__': main()
