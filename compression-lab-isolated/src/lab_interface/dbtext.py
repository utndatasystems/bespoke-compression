"""Joint DBText measurement on immutable archives, with the paper's row loop."""
import contextlib
import fcntl
import json
import math
from pathlib import Path
import shutil
import statistics
import struct
import subprocess

from . import PROJECT
from .diagnostics import diagnostic_failure
from compression_lab import runner, strings
from compression_lab.util import Error, load, save, sha

PERCENTS = ('1', '3', '10', '30', '100')
PROTOCOL = dict(selection='C++ mt19937(123), std::shuffle of uint32 row IDs, floor(percent*rows/100), sorted',
    selectivities=[1,3,10,30,100], warmup_calls=100, timed_calls=100, outer_replays=3,
    aggregation='geometric mean of per-column selected rows/second, then median of three outer replays',
    timer='resident archive and initialized decoder; complete selected bytes and row boundaries inside each query',
    setup='measured separately, outside repeated row queries; bulk includes fresh decoder setup',
    same_configuration='identical per-column archives and complete decoder binary for bulk and rows',
    source_review='Partial queries must not reconstruct or cache the full column or scan all preceding rows; owner source review required')


def prepare(lab):
    if lab.config['row_framing'] != 'lf' or any(c['rows'] < 100 for c in lab.config['columns']):
        raise Error('dbtext_requires_lf_columns_with_at_least_100_rows')
    folder = lab.owner/'driver'
    shutil.copyfile(PROJECT/'include/dbtext_rows.cpp', folder/'dbtext_rows.cpp')
    command = ['/usr/bin/g++', '-std=c++17', '-O3', '-DNDEBUG', str(folder/'dbtext_rows.cpp'),
               '-ldl', '-o', str(folder/'dbtext_rows')]
    if lab.config.get('server_local'):
        command = ['/usr/bin/taskset', '-c', str(lab.config['build_cpu']), *command]
    r = subprocess.run(command, capture_output=True, text=True, timeout=None)
    save(folder/'dbtext-build.json', dict(argv=command, returncode=r.returncode, stdout=r.stdout, stderr=r.stderr))
    r.check_returncode()
    for name in ('dbtext_rows.cpp', 'dbtext_rows', 'dbtext-build.json'):
        lab.config['driver'][name] = sha(folder/name)


def bulk_manifest(manifest):
    original = load(manifest)
    if original['variant'] != 'rows':
        raise Error('dbtext_row_capable_codec_required')
    target = Path(manifest).with_name('dbtext-bulk-manifest.json')
    save(target, {**original, 'variant':'bulk'}, 0o444)
    return target


def execute(cfg, driver, decoder, archive, column, output, mode, *, remote=False, sanitizer=False):
    _, mounts = strings._closure([driver, decoder], cfg)
    if remote:
        from .remote_worker import secure_execute
        reads = [str(driver), str(decoder), str(archive), *mounts.values()]
        argv = [str(driver), str(decoder), str(archive), str(column['rows']), str(column['bytes']), str(output), mode]
        r = secure_execute(argv, output, reads, list(mounts.values()), cfg['cpu'],
                           sanitizer=sanitizer, timeout=None)
    else:
        mounts.update({'/candidate/rows':str(driver), '/candidate/decoder.so':str(decoder), '/input/archive':str(archive)})
        argv = ['/candidate/rows', '/candidate/decoder.so', '/input/archive', str(column['rows']), str(column['bytes']), '/output', mode]
        r = runner.execute(argv, output=output, runtime_files=mounts, cpus=[cfg['cpu']],
                           sanitizer=sanitizer, timeout=None, memory=2*1024**3, output_limit=512*1024**2, check=False)
    save(output/'execution.json', r)
    if sanitizer and diagnostic_failure(r):
        e = Error(diagnostic_failure(r)); e.measurement = r; raise e
    if r['returncode'] or r['reason_code']:
        e = Error('dbtext_native_query_failed'); e.measurement = r; raise e
    return json.loads(r['stdout'])


def check_queries(output, record, raw):
    """Owner comparison. Raw input is never visible to the decoder process."""
    values = strings.rows(raw, 'lf')
    checked = []
    for q in record['queries']:
        prefix = output/str(q['index'])
        data = prefix.with_suffix('.ids').read_bytes()
        if len(data) != q['rows']*8:
            raise Error('dbtext_invalid_ids')
        ids = struct.unpack('<'+'Q'*q['rows'], data)
        if tuple(sorted(set(ids))) != ids or any(i>=len(values) for i in ids):
            raise Error('dbtext_invalid_ids')
        expected = b''.join(values[i] for i in ids)
        boundaries = [0]
        for i in ids:
            boundaries.append(boundaries[-1]+len(values[i]))
        if q['capacity'] < len(expected):
            if q['written'] != -1:
                raise Error('dbtext_short_capacity_not_rejected')
        elif q['written'] != len(expected) or prefix.with_suffix('.data').read_bytes() != expected or \
                prefix.with_suffix('.offsets').read_bytes() != struct.pack('<'+'Q'*len(boundaries), *boundaries):
            raise Error('dbtext_row_bytes_or_boundaries_mismatch')
        receipt = dict(q, ids_sha256=sha(prefix.with_suffix('.ids')), byte_exact=True)
        if q['written']>=0:
            receipt['output_sha256'] = sha(prefix.with_suffix('.data'))
            receipt['offsets_sha256'] = sha(prefix.with_suffix('.offsets'))
            # Reconstructed source bytes are redundant; their hashes and query IDs remain.
            prefix.with_suffix('.data').unlink()
        checked.append(receipt)
    save(output/'verified-queries.json', checked)
    return checked


def measure(cfg, native, driver, *, quick=False, remote=False):
    evidence = Path(native['evidence'])
    folder = evidence/'dbtext'; folder.mkdir()
    decoder = evidence/'decoder.so'
    initial_decoder = sha(decoder)
    archives = {c['name']:sha(evidence/'archives'/(c['sha256']+'.bin')) for c in cfg['columns']}
    if initial_decoder != native['candidate']['decoder']:
        raise Error('dbtext_decoder_changed')
    bulk_trials = [json.loads(line) for line in (evidence/'trials.jsonl').read_text().splitlines()]
    if any(t['archive_sha256'] != archives[t['column']] for t in bulk_trials):
        raise Error('dbtext_archive_changed')
    records = []
    for replay in range(1 if quick else 3):
        for column in cfg['columns']:
            output = folder/str(replay)/column['name']
            archive = evidence/'archives'/(column['sha256']+'.bin')
            value = execute(cfg, driver, decoder, archive, column, output, 'measure', remote=remote)
            raw = Path(column['path']).read_bytes()
            if sha(column['path']) != column['sha256']:
                raise Error('dbtext_input_changed')
            checked = check_queries(output, value, raw)
            timed = [q for q in checked if q['percent']]
            if [str(q['percent']) for q in timed] != list(PERCENTS) or any(
                q['rows'] != column['rows']*q['percent']//100 or q['calls']!=100 or
                not math.isfinite(q['seconds']) or q['seconds']<=0 for q in timed):
                raise Error('dbtext_query_protocol_mismatch')
            if sha(decoder)!=initial_decoder or sha(archive)!=archives[column['name']]:
                raise Error('dbtext_configuration_changed')
            records.append(dict(replay=replay, column=column['name'], setup_seconds=value['setup_seconds'], queries=checked))
    selective = {}
    for percent in PERCENTS:
        trials = []
        for replay in range(1 if quick else 3):
            values = [q['rows']*q['calls']/q['seconds']/1e6 for r in records if r['replay']==replay
                      for q in r['queries'] if str(q['percent'])==percent]
            trials.append(statistics.geometric_mean(values))
        selective[percent] = dict(median_rows_M_s=statistics.median(trials), min_rows_M_s=min(trials),
                                  max_rows_M_s=max(trials), replay_rows_M_s=trials)
    result = dict(PROTOCOL, byte_exact=True, same_configuration=True, decoder_sha256=initial_decoder,
                  archive_sha256=archives, replays=1 if quick else 3, selective=selective,
                  validated_queries=sum(len(r['queries']) for r in records))
    save(folder/'RAW.json', records); save(folder/'SUMMARY.json', result)
    return result


def attach(cfg, native, driver, *, quick=False, remote=False):
    if native['quality_passed']:
        try:
            # Remote caller holds both shared locks across bulk and row work.
            with contextlib.ExitStack() as stack:
                if not remote:
                    lease=stack.enter_context(open(runner.HOST_LOCK,'a')); fcntl.flock(lease,fcntl.LOCK_EX)
                native['dbtext'] = measure(cfg, native, driver, quick=quick, remote=remote)
        except Exception as e:
            native.update(quality_passed=False, reason_code=getattr(e,'code',type(e).__name__),
                          failure_context={'stage':'dbtext_rows'})
            if hasattr(e,'measurement'): native['failed_execution']=e.measurement
    native['variant'] = 'rows'
    return native
