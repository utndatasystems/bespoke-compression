"""Fixed native RAM workloads for string columns; host owns the test protocol.

This supplements the existing Lab export/correctness gates. A timing result is
not an automatic certification of a candidate's row-access mechanism.
"""
from __future__ import annotations
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import random
import shutil
import statistics
import struct
import subprocess
import sys
import tempfile
import time
import uuid

from . import candidate, runner
from .util import Error, canonical, digest, load, save, sha

SELECTIVITIES = (1, 3, 10, 30, 100)
ROW_FRAMINGS = ('lf', 'nul', 'none')


def execution_mode(config):
    mode = config.get('execution_mode', 'isolated')
    if mode not in ('isolated', 'server-local'):
        raise Error('strings_invalid_execution_mode')
    return mode


def rows(raw, framing='lf'):
    """Split only at the commissioned delimiter, preserving it and a final tail."""
    if framing not in ('lf', 'nul'): raise Error('strings_rows_not_configured')
    separator = b'\n' if framing == 'lf' else b'\0'
    parts = raw.split(separator)
    return [p+separator for p in parts[:-1]] + ([parts[-1]] if parts[-1] else [])


def selections(count, seed):
    order = list(range(count)); random.Random(seed).shuffle(order)
    return {str(p): sorted(order[:math.ceil(count*p/100)]) for p in SELECTIVITIES}


def init(workspace, columns, driver, libraries, cpu, *, row_framing='lf', driver_sources=None):
    """Host-only provisioning. All names, binaries and inputs are pinned here."""
    root = Path(workspace).resolve(); root.mkdir(parents=True, exist_ok=True)
    if (root/'strings.json').exists(): raise Error('strings_already_initialized')
    if cpu not in os.sched_getaffinity(0): raise Error('strings_cpu_unavailable')
    if row_framing not in ROW_FRAMINGS: raise Error('strings_invalid_row_framing')
    pins = []
    for name, path in columns:
        path = Path(path).resolve(); raw = path.read_bytes()
        pins.append(dict(name=name, path=str(path), bytes=len(raw), sha256=sha(path),
                         rows=len(rows(raw, row_framing)) if row_framing != 'none' else None))
    if not pins or len({p['name'] for p in pins}) != len(pins): raise Error('strings_invalid_columns')
    for directory in ('results', 'evidence', 'workbench'): (root/directory).mkdir(exist_ok=True)
    config = dict(schema_version=1, workload='strings-v1', columns=pins, cpu=cpu,
                  row_framing=row_framing,
                  trials=7, warmups=1, seed=20260916, memory_bytes=2*1024**3,
                  timeout_seconds=None, selectivities=[] if row_framing=='none' else list(SELECTIVITIES),
                  driver=dict(path=str(Path(driver).resolve()), sha256=sha(Path(driver))),
                  libraries=libraries, encode_floor_bytes_per_second=None,
                  primary_objectives=['package_bytes', 'decode_seconds'],
                  compression_speed='competitive with matched baselines; report all dataset-dependent work',
                  full_decode='fresh setup plus reconstruction; warm operation separately',
                  row_decode=None if row_framing=='none' else
                      'fixed nested random subsets, sorted row IDs; fresh setup and warm operation separately',
                  boundaries='one complete original column per block; '+
                      ('opaque bytes; no row-access workload' if row_framing == 'none' else
                       row_framing.upper()+'-preserving strings; no duplicated rows'))
    if driver_sources:
        config['driver']['sources']={name:dict(path=str(Path(path).resolve()),sha256=sha(path))
                                     for name,path in driver_sources.items()}
    save(root/'strings.json', config, 0o444)
    return {'workspace':str(root), 'workload_digest':digest(config), 'original_bytes':sum(p['bytes'] for p in pins)}


def config(root):
    c = load(Path(root)/'strings.json')
    if c['schema_version'] != 1 or c['workload'] != 'strings-v1': raise Error('strings_bad_protocol')
    execution_mode(c)
    # Do not add defaults to legacy dictionaries: their digests identify sealed
    # workloads and results. Missing framing means the original LF protocol.
    if c.get('row_framing', 'lf') not in ROW_FRAMINGS: raise Error('strings_invalid_row_framing')
    if sha(Path(c['driver']['path'])) != c['driver']['sha256']: raise Error('strings_driver_changed')
    for source in c['driver'].get('sources',{}).values():
        if sha(source['path']) != source['sha256']: raise Error('strings_driver_source_changed')
    for row in c['columns']:
        if sha(Path(row['path'])) != row['sha256']: raise Error('strings_input_changed', row['name'])
    for library in c['libraries'].values():
        if sha(Path(library['path'])) != library['sha256']: raise Error('strings_library_changed')
    return c


def _closure(paths, c):
    known = candidate.libraries()
    known.update({name:Path(row['path']) for name,row in c['libraries'].items()})
    pending=list(map(Path,paths)); seen=set(); entries=[]; mounts={}
    while pending:
        info=candidate.elf(pending.pop())
        if info['interpreter']:
            p=Path(info['interpreter']).resolve(); mounts[info['interpreter']]=str(p)
        for name in info['needed']:
            if name in seen: continue
            seen.add(name)
            if name not in known: raise Error('strings_dependency_unavailable', name)
            p=Path(known[name]); platform=name in candidate.PLATFORM
            if not platform and name not in c['libraries']: raise Error('strings_dependency_not_commissioned',name)
            entries.append(dict(soname=name,path=str(p),sha256=sha(p),bytes=p.stat().st_size,platform=platform))
            mounts['/lib/'+name]=str(p); pending.append(p)
    return entries,mounts


def _run(c, library, operation, source, output, capacity, ids=None):
    _, mounts=_closure([c['driver']['path'],library],c)
    mounts['/candidate/driver']=c['driver']['path'];mounts['/candidate/codec.so']=str(library)
    mounts['/input/data']=str(source)
    if ids: mounts['/input/ids']=str(ids)
    argv=['/candidate/driver','/candidate/codec.so',operation,
          '/input/data','/output/data',str(capacity),'/input/ids']
    mode=execution_mode(c)
    if mode=='server-local':
        # Use the same timer and pinned dependencies on hosts without namespaces.
        # The owner selects this mode; it is never an automatic fallback.
        libraries=':'.join(sorted({str(Path(path).parent) for name,path in mounts.items()
                                   if name.startswith('/lib/')}))
        argv=['/usr/bin/env','LD_LIBRARY_PATH='+libraries,c['driver']['path'],str(library),
              operation,str(source),str(Path(output)/'data'),str(capacity),str(ids) if ids else '/dev/null']
    measurement=runner.execute(argv,output=output,
        runtime_files=mounts,timeout=None,memory=c['memory_bytes'],
        output_limit=512*1024**2,threads=1,cpus=[c['cpu']],
        mode='exploratory' if mode=='server-local' else 'required')
    try: timing=json.loads(measurement['stdout'])
    except (ValueError,TypeError) as error: raise Error('strings_invalid_driver_output') from error
    if set(timing)!={'setup_seconds','operation_seconds','warm_seconds','cleanup_seconds','output_bytes'}:
        raise Error('strings_invalid_driver_fields')
    for name in ('setup_seconds','operation_seconds','warm_seconds','cleanup_seconds'):
        if not isinstance(timing[name],(int,float)) or not math.isfinite(timing[name]) or timing[name]<0:
            raise Error('strings_invalid_time')
    if timing['operation_seconds']<=0 or timing['output_bytes']!=(Path(output)/'data').stat().st_size:
        raise Error('strings_invalid_output_size')
    return timing, {key:measurement[key] for key in ('elapsed_ns','peak_rss_bytes','cpu_user_seconds',
        'cpu_system_seconds','max_observed_native_live_tasks','resources')}


def _summary(trials, field, work):
    values=[t[field] for t in trials]; middle=statistics.median(values)
    return dict(median_seconds=middle, min_seconds=min(values), max_seconds=max(values),
                relative_MAD=statistics.median(abs(x-middle) for x in values)/middle if middle else None,
                work_per_second=work/middle if middle else None)


def evaluate(workspace, manifest, quick=False, *, run=None, wait_for_lease=False):
    root=Path(workspace).resolve();c=config(root);m=load(Path(manifest))
    if set(m)!={'name','variant','encoder','decoder'} or m['variant'] not in ('bulk','rows'):
        raise Error('strings_invalid_candidate_manifest')
    if not isinstance(m['name'],str) or not m['name'].replace('-','').replace('_','').isalnum():
        raise Error('strings_invalid_candidate_name')
    framing=c.get('row_framing','lf')
    if framing=='none' and m['variant']=='rows':raise Error('strings_rows_not_configured')
    execute=run or _run
    serial=uuid.uuid4().hex; evidence=root/'evidence'/serial;evidence.mkdir()
    binaries={}
    for role in ('encoder','decoder'):
        path=Path(manifest).resolve().parent/m[role]
        if (path.is_symlink() or not path.is_file() or
            not path.resolve().is_relative_to(Path(manifest).resolve().parent)):
            raise Error('strings_bad_library')
        target=evidence/(role+'.so');shutil.copyfile(path,target);target.chmod(0o444);binaries[role]=target
    dependencies,_=_closure([binaries['decoder']],c)
    custom=binaries['decoder'].stat().st_size
    # Only separately pinned conventional libraries can be excluded. The entire
    # submitted decoder remains charged, including statically linked code.
    nonplatform=sum(d['bytes'] for d in dependencies if not d['platform'])
    excluded=sum(d['bytes'] for d in dependencies if not d['platform'] and c['libraries'][d['soname']]['standard_codec'])
    identity={role:sha(path) for role,path in binaries.items()}
    result=dict(schema_version=1, workload_digest=digest(c), name=m['name'], variant=m['variant'],
                candidate=identity, dependencies=dependencies, evidence=str(evidence),
                depth='quick' if quick else 'full', quality_passed=False, eligible=False,
                research_certification='Requires existing Lab build/corruption/diagnostic/export gates and source review of row-access mechanism',
                original_bytes=sum(x['bytes'] for x in c['columns']), columns=[],
                encode_floor_bytes_per_second=None)
    trials=[];archives={};count=1 if quick else c['trials'];current={}
    raw_path=evidence/'trials.jsonl'
    try:
        # Same host lease as existing Lab measurements. Do not benchmark against
        # another candidate concurrently; a busy lease is retryable.
        with open(runner.HOST_LOCK,'a') as lease:
            # Background native jobs wait here, outside the measured interval.
            try:fcntl.flock(lease,fcntl.LOCK_EX | (0 if wait_for_lease else fcntl.LOCK_NB))
            except BlockingIOError as error:raise Error('benchmark_lease_busy') from error
            with raw_path.open('w') as stream, tempfile.TemporaryDirectory(dir=evidence) as tmp:
                tmp=Path(tmp)
                for trial in range(count+1):
                    for col in c['columns']:
                        raw=Path(col['path']).read_bytes()
                        if m['variant']=='rows':
                            values=rows(raw,framing)
                            seed=int(col['sha256'][:16],16)^c['seed']
                            ids_by_percent=selections(len(values),seed)
                        for child in tmp.iterdir():
                            if child.is_dir():shutil.rmtree(child)
                            else:child.unlink()
                        current=dict(column=col['name'],trial=trial,operation='encode')
                        enc, enc_resources=execute(c,binaries['encoder'],'encode',Path(col['path']),tmp/'encode',
                                                  min(4*len(raw)+8*1024**2,512*1024**2))
                        archive=tmp/'encode/data'; archive_hash=sha(archive)
                        if col['name'] in archives and archives[col['name']]['sha256']!=archive_hash:
                            raise Error('strings_nondeterministic_archive',col['name'])
                        archives[col['name']]={'sha256':archive_hash,'bytes':archive.stat().st_size}
                        current['operation']='decode'
                        dec,dec_resources=execute(c,binaries['decoder'],'decode',archive,tmp/'decode',len(raw)+32)
                        if (tmp/'decode/data').read_bytes()!=raw: raise Error('strings_reconstruction_mismatch',col['name'])
                        row=dict(trial=trial,column=col['name'],archive_bytes=archive.stat().st_size,archive_sha256=archive_hash,
                                 encode_seconds=enc['operation_seconds'],
                                 decode_seconds=dec['setup_seconds']+dec['operation_seconds'],
                                 decode_setup_seconds=dec['setup_seconds'],decode_warm_seconds=dec['warm_seconds'],
                                 decoder_cleanup_seconds=dec['cleanup_seconds'],exact=True,selective={},
                                 resources={'encode':enc_resources,'decode':dec_resources})
                        if m['variant']=='rows':
                            for percent, selected in ids_by_percent.items():
                                current.update(operation='rows',selectivity=percent)
                                ids=tmp/'ids';ids.write_bytes(struct.pack('<'+'Q'*len(selected),*selected))
                                output=tmp/('rows-'+percent)
                                timing,resources=execute(c,binaries['decoder'],'rows',archive,output,len(raw)+32,ids)
                                expected=b''.join(values[i] for i in selected)
                                lengths=[0]
                                for i in selected:lengths.append(lengths[-1]+len(values[i]))
                                if ((output/'data').read_bytes()!=expected or
                                    (output/'data.offsets').read_bytes()!=struct.pack('<'+'Q'*len(lengths),*lengths)):
                                    raise Error('strings_row_mismatch',col['name']+':'+percent)
                                row['selective'][percent]=dict(rows=len(selected),bytes=len(expected),ids_sha256=sha(ids),
                                    setup_seconds=timing['setup_seconds'],
                                    seconds=timing['setup_seconds']+timing['operation_seconds'],
                                    warm_seconds=timing['warm_seconds'],resources=resources)
                        stream.write(json.dumps(row,sort_keys=True)+'\n');stream.flush();trials.append(row)
                        if trial==count:
                            retained=evidence/'archives';retained.mkdir(exist_ok=True)
                            shutil.copyfile(archive,retained/(col['sha256']+'.bin'))
        measured=[r for r in trials if r['trial']>0]
        for col in c['columns']:
            records=[r for r in measured if r['column']==col['name']]
            item={**{k:col[k] for k in ('name','bytes','sha256','rows')},'archive_bytes':archives[col['name']]['bytes']}
            for field in ('encode_seconds','decode_seconds','decode_warm_seconds'):
                item[field]=_summary(records,field,col['bytes'])
            item['selective']={}
            if m['variant']=='rows':
                for percent in map(str,SELECTIVITIES):
                    values=[r['selective'][percent] for r in records]
                    item['selective'][percent]={'cold':_summary(values,'seconds',values[0]['rows']),
                        'warm':_summary(values,'warm_seconds',values[0]['rows']),
                        'rows':values[0]['rows'],'bytes':values[0]['bytes'],'ids_sha256':values[0]['ids_sha256']}
            result['columns'].append(item)
        corpus=[]
        for trial in range(1,count+1):
            records=[r for r in measured if r['trial']==trial]
            corpus.append({field:sum(r[field] for r in records) for field in
                          ('encode_seconds','decode_seconds','decode_warm_seconds')})
        result['timing']={field:_summary(corpus,field,result['original_bytes']) for field in corpus[0]}
        result['selective']={}
        if m['variant']=='rows':
            for percent in map(str,SELECTIVITIES):
                totals=[];work=0
                for trial in range(1,count+1):
                    records=[r['selective'][percent] for r in measured if r['trial']==trial]
                    work=sum(r['rows'] for r in records)
                    totals.append({field:sum(r[field] for r in records) for field in ('seconds','warm_seconds')})
                result['selective'][percent]={'cold':_summary(totals,'seconds',work),'warm':_summary(totals,'warm_seconds',work),'rows':work}
        archive_total=sum(a['bytes'] for a in archives.values())
        result['accounting']=dict(archive_bytes=archive_total,custom_decoder_bytes=custom,
            nonplatform_dependency_bytes=nonplatform,excluded_standard_codec_bytes=excluded,
            package_bytes=archive_total+custom+nonplatform-excluded,
            strict_deployment_bytes=archive_total+custom+nonplatform)
        result.update(quality_passed=True,status='measured',roundtrips=len(trials),
                      selective_checks=sum(len(r['selective']) for r in trials))
    except Exception as error:
        result.update(status='failed',error=str(error),reason_code=getattr(error,'code',type(error).__name__),failure_context=current)
        if hasattr(error,'measurement'):
            result['failed_execution']={k:error.measurement.get(k) for k in ('returncode','stderr','reason_code','elapsed_ns')}
    if raw_path.exists():result['raw_trials_sha256']=sha(raw_path)
    result['result_id']='s-'+digest(result)
    save(root/'results'/(result['result_id']+'.json'),result,0o444)
    return result


def compare(workspace, ids, include_columns=False):
    root=Path(workspace);records=[]
    for rid in ids:
        if not rid.startswith('s-') or len(rid)!=66 or any(x not in '0123456789abcdef' for x in rid[2:]):
            raise Error('strings_bad_result_id')
        row=load(root/'results'/(rid+'.json'))
        if 's-'+digest({k:v for k,v in row.items() if k!='result_id'})!=rid:raise Error('strings_result_changed')
        if not row['quality_passed'] or row['depth']!='full':raise Error('strings_full_results_required')
        if sha(Path(row['evidence'])/'trials.jsonl')!=row['raw_trials_sha256']:raise Error('strings_evidence_changed')
        records.append(row)
    if not records or len({r['workload_digest'] for r in records})!=1:raise Error('strings_unmatched_workloads')
    fields=('result_id','name','variant','original_bytes','accounting','timing','selective')
    if include_columns:fields+=('columns',)
    references=load(root/'references.json') if (root/'references.json').exists() else {}
    deltas=[]
    for row in records:
        for name in references.get(row.get('variant'),[]):
            rid=references['results'][name]
            if rid==row['result_id']:continue
            baseline=load(root/'results'/(rid+'.json'))
            if ('s-'+digest({k:v for k,v in baseline.items() if k!='result_id'})!=rid or
                baseline['workload_digest']!=row['workload_digest'] or not baseline['quality_passed'] or
                baseline['depth']!='full' or baseline['variant']!=row['variant']):raise Error('strings_invalid_reference')
            if sha(Path(baseline['evidence'])/'trials.jsonl')!=baseline['raw_trials_sha256']:raise Error('strings_evidence_changed')
            timing=row['timing']['decode_seconds'];ref=baseline['timing']['decode_seconds']
            smaller=row['accounting']['package_bytes']<baseline['accounting']['package_bytes']
            faster=timing['max_seconds']<ref['min_seconds']
            deltas.append({'candidate_result':row['result_id'],'baseline':name,'baseline_result':rid,
                'package_delta_bytes':row['accounting']['package_bytes']-baseline['accounting']['package_bytes'],
                'decode_speedup':ref['median_seconds']/timing['median_seconds'],
                'compression_speedup':baseline['timing']['encode_seconds']['median_seconds']/row['timing']['encode_seconds']['median_seconds'],
                'smaller_package':smaller,'decode_trials_strictly_faster':faster,
                'simultaneous_win_supported_by_observed_ranges':smaller and faster})
    return {'results':[{k:r[k] for k in fields} for r in records],'against_supplied_references':deltas,
            'claim_rule':'Compare one same mode on size and decode speed; report per-column losses and timing spread. Row and bulk variants have separate targets. Compression speed is reported, without a fixed floor.'}


def profile(workspace):
    root=Path(workspace);c=config(root)
    references=load(root/'references.json') if (root/'references.json').exists() else {}
    return {'protocol':c,'workload_digest':digest(c),'references':references,
            'api_header':(Path(__file__).parent/'data/strings/codec.h').read_text(),
            'research_certification':'RAM measurements supplement the existing Lab correctness, diagnostic, reproducibility and export gates. Source review is required for the row-access claim.'}


def _process_identity(pid):
    try:
        fields=Path('/proc/'+str(pid)+'/stat').read_text().rsplit(')',1)[1].split()
        return fields[19] if fields[0] not in ('Z','X') else None
    except (OSError,IndexError):return None


def status(workspace, job_id):
    if len(job_id)!=32 or any(x not in '0123456789abcdef' for x in job_id):raise Error('strings_bad_job_id')
    root=Path(workspace);state=load(root/'jobs'/job_id/'state.json')
    if state['status'] in ('queued','running') and (not state.get('process_start') or _process_identity(state.get('pid',0))!=state.get('process_start')):
        return {**state,'status':'interrupted','reason':'Worker process is no longer present; inspect the retained job log before resubmitting.'}
    if state.get('result_id'):
        result=load(root/'results'/(state['result_id']+'.json'))
        if 's-'+digest({k:v for k,v in result.items() if k!='result_id'})!=state['result_id']:raise Error('strings_result_changed')
        state['result_summary']={k:result.get(k) for k in ('name','variant','status','depth','quality_passed','error',
            'failure_context','failed_execution','accounting','timing','selective','roundtrips','selective_checks')}
    return state


def submit(workspace, manifest, quick=False):
    root=Path(workspace).resolve();config(root)
    jobs=root/'jobs';jobs.mkdir(exist_ok=True)
    with (jobs/'submit.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        active=jobs/'active.json'
        if active.exists():
            previous=status(root,load(active)['job_id'])
            if previous['status'] in ('queued','running'):return {**previous,'already_running':True}
        job_id=uuid.uuid4().hex;folder=jobs/job_id;folder.mkdir()
        save(folder/'request.json',{'manifest':str(Path(manifest).resolve()),'quick':bool(quick)})
        # state is written before the child can acquire this same submit lock.
        with (folder/'worker.log').open('wb') as log:
            process=subprocess.Popen([sys.executable,'-m','compression_lab.strings',str(root),job_id],
                stdin=subprocess.DEVNULL,stdout=log,stderr=log,start_new_session=True,close_fds=True)
        state={'job_id':job_id,'pid':process.pid,'process_start':_process_identity(process.pid),'status':'queued'}
        save(folder/'state.json',state);save(active,{'job_id':job_id});return state


def _work(workspace, job_id):
    root=Path(workspace);folder=root/'jobs'/job_id
    with (root/'jobs/submit.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        state=load(folder/'state.json');state['status']='running';save(folder/'state.json',state)
    request=load(folder/'request.json')
    try:
        result=evaluate(root,request['manifest'],request['quick'])
        state.update(status=result['status'],result_id=result['result_id'],
                     quality_passed=result['quality_passed'],reason_code=result.get('reason_code'))
    except Exception as error:state.update(status='failed',reason_code=getattr(error,'code',type(error).__name__),error=str(error))
    save(folder/'state.json',state)


if __name__=='__main__':_work(sys.argv[1],sys.argv[2])
