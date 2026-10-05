"""Separate reproducibility, capacity and malformed-archive diagnostics."""
from pathlib import Path
import shutil

from . import PROJECT
from .diagnostics import diagnostic_failure
from .workflow import build_commands
from compression_lab import candidate, runner, strings
from compression_lab.util import load, save, sha


def validate(lab, job, state):
    measured=lab.result(state['source_result_id'])
    built=lab.result(measured['build_id'])
    build_job=lab.owner/'jobs'/built['job_id']
    source_job=lab.owner/'jobs'/measured['job_id']
    shutil.copytree(build_job/'source', job/'source')
    spec=load(build_job/'build-spec.json')
    shutil.copyfile(build_job/'build-spec.json',job/'build-spec.json')
    row=dict(kind='validation',source_result_id=measured['result_id'],build_id=built['result_id'],
             passed=False,gates=[dict(name='byte_exact_full_measurement',status='passed'),
                                dict(name='two_independent_reproducible_builds',status='passed')],case_count=0,
             scope='Capacity and malformed archive checks; not a proof for arbitrary hostile archives or a source audit.')
    sanitizable=any(Path(cmd[0]).name in ('g++','gcc','c++','cc','clang','clang++') for cmd in spec['commands'])
    build_commands(lab,job,spec,job/'diagnostic-build',sanitizer=sanitizable)
    row['diagnostic_instrumentation']='ASan/UBSan rebuild plus guard pages' if sanitizable else 'guard pages around exact input/output spans; candidate is not sanitizer-instrumented'
    shutil.copyfile(PROJECT/'include/validate.cpp',job/'source/validate.cpp')
    argv=['g++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
          '-I/source/interface','/source/validate.cpp','-ldl','-o','/output/validate']
    build_commands(lab,job,{'commands':[argv]},job/'diagnostic-driver')
    joint='dbtext' in lab.config['protocol']
    rows_driver=None
    if joint:
        shutil.copyfile(PROJECT/'include/dbtext_rows.cpp',job/'source/dbtext_rows.cpp')
        row_argv=[a.replace('/source/validate.cpp','/source/dbtext_rows.cpp').replace('/output/validate','/output/rows') for a in argv]
        build_commands(lab,job,{'commands':[row_argv]},job/'diagnostic-rows-driver')
        rows_driver=job/'diagnostic-rows-driver/rows'
    cfg=strings.config(source_job/'engine')
    known=candidate.libraries()
    # Pin every sanitizer or candidate dependency independently of benchmark binaries.
    known.update({k:Path(v['path']) for k,v in cfg['libraries'].items()})
    decoder=job/'diagnostic-build'/spec['decoder']; driver=job/'diagnostic-driver/validate'
    pending=[decoder,driver]+([rows_driver] if rows_driver else []);mounts={};seen=set()
    while pending:
        item=candidate.elf(pending.pop())
        if item['interpreter']:mounts[item['interpreter']]=str(Path(item['interpreter']).resolve())
        for soname in item['needed']:
            if soname in seen:continue
            seen.add(soname); path=known[soname]
            mounts['/lib/'+soname]=str(path);pending.append(path)
    passed=True
    if lab.config.get('remote'):
        from .remote import diagnostics
        library_paths={Path(target).name:Path(source) for target,source in mounts.items() if target.startswith('/lib/')}
        returned=diagnostics(lab,job,kind='validation',driver=driver,decoder=decoder,
            archives=source_job/'archives',libraries=library_paths,columns=lab.config['columns'],rows_driver=rows_driver)
        # Remote bulk diagnostics and row diagnostics share one durable job and lock.
        records=load(returned/'CALLS.json')
        row['case_count']=len(records)
        bulk_count=16*len(lab.config['columns'])
        failure=next(((i,r,diagnostic_failure(r)) for i,r in enumerate(records[:bulk_count])
                      if diagnostic_failure(r)),None)
        passed=len(records)>=bulk_count and failure is None
        if not joint:passed=passed and len(records)==bulk_count
        if failure:
            i,r,code=failure
            row['failure']=dict(r,code=code,column=lab.config['columns'][i//16]['name'],case=i%16)
        elif not passed:
            row['failure']=dict(code='diagnostic_count_mismatch',expected=bulk_count,actual=len(records))
        row['gates'].append(dict(name='asan_ubsan_capacity_and_malformed_archives',status='passed' if passed else 'failed'))
        if passed and joint:
            import json
            from .dbtext import check_queries
            passed=len(records)==bulk_count+len(lab.config['columns'])
            if not passed:
                row['failure']=dict(code='diagnostic_count_mismatch',
                    expected=bulk_count+len(lab.config['columns']),actual=len(records))
            for i,col in enumerate(lab.config['columns']):
                if not passed:break
                r=records[bulk_count+i]
                failure=diagnostic_failure(r)
                if failure:
                    passed=False;row['failure']=dict(r,code=failure,column=col['name'],case='rows');break
                try:
                    check_queries(returned/'checks'/str(bulk_count+i),json.loads(r['stdout']),
                                  (lab.public/'inputs'/col['name']).read_bytes())
                except ValueError as e:
                    passed=False;row['failure']={'code':str(e)};break
            row['gates'].append(dict(name='selected_rows_capacity_and_memory',status='passed' if passed else 'failed'))
        row['passed']=passed
        return row
    for column in lab.config['columns']:
        archive=source_job/'archives'/(column['sha256']+'.bin')
        for which in range(16):
            output=job/'checks'/column['sha256']/str(which)
            files={**mounts,'/candidate/validate':str(driver),'/candidate/decoder.so':str(decoder),'/input/archive':str(archive)}
            args=['/candidate/validate','/candidate/decoder.so','/input/archive',str(column['bytes']),str(which)]
            if lab.config.get('server_local'):
                from .remote_worker import secure_execute
                actual=[str(driver),str(decoder),str(archive),str(column['bytes']),str(which)]
                record=secure_execute(actual,output,[*mounts.values(),str(driver),str(decoder),str(archive),'/dev/null'],
                    list(mounts.values()),lab.config['cpu'],sanitizer=True,timeout=None)
            else:
                record=runner.execute(args,output=output,runtime_files=files,sanitizer=True,
                    timeout=None,memory=2*1024**3,output_limit=1024**2,cpus=[lab.config['cpu']],check=False,cancel=job/'cancel')
            save(output/'receipt.json',record)
            row['case_count']+=1
            failure=diagnostic_failure(record)
            if failure:
                passed=False
                row['failure']=dict(code=failure,column=column['name'],case=which,returncode=record['returncode'],reason=record['reason_code'],stderr=record['stderr'][-4000:])
                break
        if not passed:break
    row['gates'].append(dict(name='asan_ubsan_capacity_and_malformed_archives',status='passed' if passed else 'failed'))
    if passed and joint:
        from .dbtext import execute, check_queries
        # Diagnostic-only sanitizer dependencies are distinct from charged runtime dependencies.
        for target,source in mounts.items():
            if target.startswith('/lib/'):
                cfg['libraries'][Path(target).name]=dict(path=source,bytes=Path(source).stat().st_size,
                    sha256=sha(source),standard_codec=False)
        try:
            for column in lab.config['columns']:
                output=job/'row-checks'/column['sha256']
                record=execute(cfg,rows_driver,decoder,source_job/'archives'/(column['sha256']+'.bin'),
                               column,output,'validate',sanitizer=True,remote=lab.config.get('server_local',False))
                checks=check_queries(output,record,(lab.public/'inputs'/column['name']).read_bytes())
                row['case_count']+=len(checks)
        except Exception as e:
            passed=False;row['failure']={'code':getattr(e,'code',type(e).__name__)}
            if hasattr(e,'measurement'):row['failure']['execution']=e.measurement
        row['gates'].append(dict(name='selected_rows_capacity_and_memory',status='passed' if passed else 'failed'))
    row['passed']=passed
    return row
