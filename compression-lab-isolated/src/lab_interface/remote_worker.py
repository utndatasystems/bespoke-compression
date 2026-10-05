"""One durable owner-side job on the pinned benchmark server; no model calls."""
import argparse
import contextlib
import fcntl
import json
import os
from pathlib import Path
import shutil
import sys

from . import PROJECT
from .diagnostics import diagnostic_failure
from compression_lab import candidate, runner, strings
from compression_lab.util import Error, load, now, save, sha


def secure_execute(argv, output, reads, libraries, cpu, *, sanitizer=False, timeout=None, profile=None):
    output.mkdir(parents=True,exist_ok=True)
    policy=output.parent/(output.name+'-sandbox.json')
    save(policy,dict(argv=argv,reads=sorted(set(map(str,reads))),output=str(output),cpu=cpu,
                     library_path=':'.join(sorted(set(str(Path(p).parent) for p in libraries))),sanitizer=sanitizer))
    command=[sys.executable,'-m','lab_interface.linux_sandbox',str(policy)]
    # runner supplies a minimal env; use the trusted entrypoint's absolute path.
    command=[sys.executable,str(PROJECT/'remote-entry.py'),'sandbox',str(policy)]
    if profile:
        command=['/usr/bin/perf','stat','-x',';','-o',str(profile),'-e',
                 'cycles:u,instructions:u,cache-references:u,cache-misses:u,branches:u,branch-misses:u','--',*command]
    record=runner.execute(command,output=output,timeout=timeout,memory=2*1024**3,output_limit=512*1024**2,
                          mode='exploratory',sanitizer=sanitizer,build=bool(profile),cpus=[cpu],check=False)
    record['resources']['enforcement'].update(filesystem='Landlock ABI >=3; exact input/runtime files and own output only',
        threads='seccomp denies clones',affinity='CPU pin plus seccomp denial of changes',network='seccomp denied')
    if sanitizer and not record['reason_code']:
        record['reason_code']=diagnostic_failure(record)
    return record


def native_execute(config, library, operation, source, output, capacity, ids=None):
    """The existing server launcher, usable without an SSH hop on that server."""
    _, mounts = strings._closure([config['driver']['path'], library], config)
    driver = config['driver']['path']
    reads = [*mounts.values(), str(library), str(driver), str(source), '/dev/null']
    if ids: reads.append(str(ids))
    argv = [str(driver), str(library), operation, str(source), str(output/'data'),
            str(capacity), str(ids) if ids else '/dev/null']
    record = secure_execute(argv, output, reads, [x['path'] for x in config['libraries'].values()], config['cpu'])
    if record['returncode'] or record['reason_code']:
        error = Error(record['reason_code'] or 'native_failed'); error.measurement = record; raise error
    return json.loads(record['stdout']), {k:record[k] for k in ('elapsed_ns','peak_rss_bytes',
        'cpu_user_seconds','cpu_system_seconds','max_observed_native_live_tasks','resources')}


def main(root):
    root=Path(root).resolve();plan=load(root/'PLAN.json')
    state=dict(state='running',pid=os.getpid(),started_utc=now(),kind=plan['kind'])
    save(root/'STATUS.json',state)
    try:
        if shutil.disk_usage(root).free < 10*1024**3:raise Error('server_disk_reserve')
        for name,h in load(root/'PINS.json').items():
            if sha(root/name)!=h:raise Error('input_pin_changed')
        for item in plan.get('columns',[]):
            if sha(item['path'])!=item['sha256']:raise Error('dataset_changed')
        libraries={}
        for f in (root/'libraries').iterdir():
            libraries[f.name]=dict(path=str(f),bytes=f.stat().st_size,sha256=sha(f),standard_codec=False)
        driver=root/'harness/driver'
        with contextlib.ExitStack() as stack:
            for lockpath in plan['locks']:
                file=stack.enter_context(open(lockpath,'a'));fcntl.flock(file,fcntl.LOCK_EX)
            if plan['kind']=='measurement':
                engine=root/'engine'
                strings.init(engine,[(c['name'],c['path']) for c in plan['columns']],driver,libraries,plan['cpu'],row_framing=plan['row_framing'])
                cfg=load(engine/'strings.json');cfg['execution_mode']='server-local'
                save(engine/'strings.json',cfg)
                # Already holding both host locks. The engine's inner lock is job-local.
                runner.HOST_LOCK=root/'inner.lock'
                def execute(c,library,operation,source,output,capacity,ids=None):
                    _,mounts=strings._closure([c['driver']['path'],library],c)
                    reads=list(mounts.values())+[str(library),str(driver),str(source),'/dev/null']
                    if ids:reads.append(str(ids))
                    argv=[str(driver),str(library),operation,str(source),str(output/'data'),str(capacity),str(ids) if ids else '/dev/null']
                    r=secure_execute(argv,output,reads,[x['path'] for x in libraries.values()],plan['cpu'])
                    if r['returncode'] or r['reason_code']:
                        e=Error(r['reason_code'] or 'native_failed');e.measurement=r;raise e
                    timing=json.loads(r['stdout'])
                    return timing,{k:r[k] for k in ('elapsed_ns','peak_rss_bytes','cpu_user_seconds','cpu_system_seconds','max_observed_native_live_tasks','resources')}
                manifest=root/'candidate/engine-manifest.json'
                if plan.get('dbtext'):
                    from .dbtext import bulk_manifest
                    manifest=bulk_manifest(manifest)
                result=strings.evaluate(engine,manifest,plan['quick'],run=execute,wait_for_lease=True)
                if plan.get('dbtext'):
                    from .dbtext import attach
                    result=attach(strings.config(engine),result,root/'harness/dbtext_rows',quick=plan['quick'],remote=True)
                save(root/'NATIVE.json',result)
                state['quality_passed']=result['quality_passed']
            else:
                records=[]
                for item in plan['calls']:
                    output=root/'checks'/str(len(records))
                    argv=[str(root/p) if p.startswith(('harness/','candidate/','archives/')) else p for p in item['argv']]
                    argv=[str(output/'data') if a=='@OUTPUT@' else a for a in argv]
                    argv=[str(output) if a=='@OUTPUT_DIR@' else a for a in argv]
                    reads=[str(root/p) for p in item['reads']]+['/dev/null']
                    reads.extend(str(f) for f in (root/'libraries').iterdir())
                    # ELF interpreter is a platform runtime, not research data.
                    for binary in [Path(argv[0]),Path(argv[1])]:
                        interpreter=candidate.elf(binary)['interpreter']
                        if interpreter:reads.append(interpreter)
                    r=secure_execute(argv,output,reads,[x['path'] for x in libraries.values()],plan['cpu'],
                        sanitizer=plan['kind']=='validation',timeout=None,
                        profile=output.parent/(output.name+'-perf.csv') if plan['kind']=='profile' else None)
                    save(output/'receipt.json',r);records.append(r)
                    if plan['kind']=='profile' and not r['returncode'] and not r['reason_code']:
                        if sha(output/'data')!=item['expected_sha256']:raise Error('profile_reconstruction_mismatch')
                        (output/'data').unlink()
                    if r['returncode'] or r['reason_code']:break
                save(root/'CALLS.json',records)
        state.update(state='complete',finished_utc=now())
    except Exception as error:
        import traceback
        traceback.print_exc()
        state.update(state='failed',reason=getattr(error,'code',type(error).__name__),finished_utc=now())
    save(root/'STATUS.json',state)


if __name__=='__main__':main(sys.argv[1])
