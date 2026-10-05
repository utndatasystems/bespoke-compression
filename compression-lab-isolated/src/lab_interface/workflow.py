"""Source-first builds and final submissions, with immutable durable receipts."""
import base64
import os
from pathlib import Path
import signal
import time
import uuid

from .api import ACTIVE, SCORING, _identity
from compression_lab import candidate, runner
from compression_lab.util import Error, digest, ident, load, lock, now, rel, save, secure_load, sha


def _queue(lab, request_id, request, prepare=None):
    ident(request_id)
    with lock(lab.owner / 'submit.lock'):
        receipt = lab.owner / 'requests' / (request_id + '.json')
        if receipt.exists():
            previous = load(receipt)
            if previous['request'] != request:
                raise Error('request_id_conflict')
            return lab.status(previous['job_id'])
        if (lab.owner/'FINISH.json').exists():raise Error('workspace_finished')
        import shutil
        if shutil.disk_usage(lab.root).free<6*1024**3:raise Error('local_disk_reserve')
        job_id = 'j-' + uuid.uuid4().hex
        job = lab.owner / 'jobs' / job_id
        job.mkdir()
        try:
            if prepare:
                prepare(job)
        except Exception:
            save(job / 'status.json', dict(job_id=job_id, status='failed', reason='snapshot_failed'))
            raise
        state = dict(job_id=job_id, status='queued', created_utc=now(), request_id=request_id, **request)
        save(receipt, dict(request=request, job_id=job_id))
        lab._launch(state)
        return lab.status(job_id)


def queue_build(lab, manifest, request_id):
    lab.environment()
    rel(manifest)
    def prepare(job):
        spec = secure_load(lab.public / 'work', manifest)
        required = {'name', 'variant', 'sources', 'commands', 'encoder', 'decoder'}
        if not isinstance(spec, dict) or not required <= spec.keys() or spec.keys() - required - {'libraries', 'description'}:
            raise Error('invalid_build_manifest')
        ident(spec['name'])
        if spec['variant'] not in ('bulk', 'rows') or not isinstance(spec['sources'], list) or not spec['sources']:
            raise Error('invalid_build_manifest')
        if not isinstance(spec['commands'], list) or not 1 <= len(spec['commands']) <= 64:
            raise Error('invalid_build_commands')
        for argv in spec['commands']:
            if not isinstance(argv, list) or not argv or any(not isinstance(a, str) or '\0' in a for a in argv):
                raise Error('invalid_build_commands')
        source = job / 'source'
        source.mkdir()
        total = 0
        for name in sorted(set(rel(x) for x in spec['sources'])):
            if name.startswith('interface/'):
                raise Error('reserved_source_path')
            target = source / name
            target.parent.mkdir(parents=True, exist_ok=True)
            candidate.copy_import(lab.public / 'work' / Path(manifest).parent, name, target)
            total += target.stat().st_size
            if total > 512 * 1024**2:
                raise Error('oversized_sources')
            target.chmod(0o444)
        (source / 'interface').mkdir()
        import shutil
        shutil.copyfile(lab.public / 'interface/codec.h', source / 'interface/codec.h')
        for name in [spec['encoder'], spec['decoder'], *spec.get('libraries', {}).values()]:
            rel(name)
        save(job / 'build-spec.json', spec, 0o444)
    return _queue(lab, request_id, dict(kind='build', manifest=manifest), prepare)


def queue_evaluation(lab, build_id, request_id, quick, *, dbtext=False):
    built = lab.result(build_id)
    if not build_id.startswith('b-') or not built['reproducible']:
        raise Error('reproducible_build_required')
    source_job = lab.owner / 'jobs' / built['job_id']
    if dbtext and load(source_job/'package/manifest.json')['variant']!='rows':
        raise Error('dbtext_row_capable_codec_required')
    def prepare(job):
        lab._snapshot('manifest.json', job, source_root=source_job / 'package',
                      manifest_data=load(source_job / 'package/manifest.json'))
    return _queue(lab, request_id, dict(kind='measurement', build_id=build_id, quick=bool(quick), dbtext=dbtext), prepare)


def queue_validation(lab, result_id, request_id):
    row = lab.result(result_id)
    if not result_id.startswith('r-') or not row['correctness']['byte_exact'] or row['depth'] != 'full' or not row.get('build_id'):
        raise Error('full_source_built_measurement_required')
    return _queue(lab, request_id, dict(kind='validation', source_result_id=result_id))


def retain(lab, job, row, prefix):
    ignored = {'worker.log', 'status.json', 'state.lock', 'retained-files.json', 'export.lock', 'export.json', 'cancel'}
    files = {str(p.relative_to(job)): sha(p) for p in job.rglob('*') if p.is_file() and p.name not in ignored}
    save(job / 'retained-files.json', files, 0o444)
    row.update(job_id=job.name, protocol_id=lab.config['protocol']['protocol_id'], completed_utc=now(),
               evidence_manifest_sha256=sha(job / 'retained-files.json'))
    row['result_id'] = prefix + '-' + digest(row)
    save(lab.owner / 'results' / (row['result_id'] + '.json'), row, 0o444)
    return row


def build_commands(lab, job, spec, output, *, sanitizer=False):
    records = []
    for i, command in enumerate(spec['commands']):
        argv = list(command)
        if sanitizer and Path(argv[0]).name in ('g++', 'gcc', 'clang++', 'clang', 'c++', 'cc'):
            argv += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-g']
        output.mkdir(parents=True,exist_ok=True)
        options={'readonly':{'/source':job/'source'}}
        if lab.config.get('server_local'):
            from .server_shell import command as server_command
            argv=server_command(job/'build-policies',argv,{'/source':job/'source','/output':output},
                                writable=['/output'],cwd='/output',cpus=[lab.config['build_cpu']],network=False,
                                proot=lab.config['proot'])
            options={'mode':'exploratory'}
        record = runner.execute(argv, output=output, **options,
                                build=True, timeout=None, memory=3*1024**3, output_limit=512*1024**2,
                                cpus=[lab.config.get('build_cpu', lab.config['cpu'])], cancel=job/'cancel', check=False)
        records.append(record)
        save(output.parent / (output.name + '-commands.json'), records)
        if record['returncode'] or record['reason_code']:
            error=Error('build_command_failed');error.measurement=record;raise error
    return records


def run_build(lab, job, state):
    import shutil
    spec = load(job / 'build-spec.json')
    for label in ('build-a', 'build-b'):
        build_commands(lab, job, spec, job / label)
    outputs = [spec['encoder'], spec['decoder'], *spec.get('libraries', {}).values()]
    hashes = {}
    for label in ('build-a', 'build-b'):
        hashes[label] = {}
        for name in outputs:
            path = job / label / rel(name)
            if path.is_symlink() or not path.is_file() or not path.resolve().is_relative_to((job/label).resolve()):
                raise Error('invalid_build_output')
            candidate.elf(path)
            hashes[label][name] = sha(path)
    reproducible = hashes['build-a'] == hashes['build-b']
    if reproducible:
        package = job / 'package'
        package.mkdir()
        for name in outputs:
            target = package / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(job / 'build-a' / name, target)
        shutil.copytree(job/'source', package/'source')
        shutil.copyfile(job/'build-spec.json', package/'build-spec.json')
        manifest = {k: spec[k] for k in ('name', 'variant', 'encoder', 'decoder')}
        manifest.update(sources=['build-spec.json'] + ['source/'+s for s in spec['sources']],
                        libraries=spec.get('libraries', {}), description=spec.get('description', ''))
        save(package/'manifest.json', manifest)
    return retain(lab, job, dict(kind='build', name=spec['name'], reproducible=reproducible,
                                binary_hashes=hashes, scoring=lab.config['protocol']['scoring']), 'b')


def run_validation(lab, job, state):
    from .validation import validate
    return retain(lab, job, validate(lab, job, state), 'v')


def cancel(lab, job_id):
    ident(job_id)
    job = lab.owner/'jobs'/job_id
    lab.status(job_id)
    with lock(job/'state.lock'):
        state = load(job/'status.json')
        if state['status'] not in ACTIVE:
            return {k:v for k,v in state.items() if k not in ('pid','process_start')}
        (job/'cancel').touch()
        if lab.config.get('remote') and (job/'remote-receipt.json').exists():
            from .remote import cancel as remote_cancel
            remote_cancel(lab,job)
        pid, start = state.get('pid'), state.get('process_start')
        if pid and start and _identity(pid) == start:
            # The worker's handler cancels its bounded child and preserves evidence.
            os.kill(pid, signal.SIGTERM)
        state.update(status='cancelling', cancellation_requested_utc=now())
        save(job/'status.json', state)
    return lab.status(job_id)


def artifact(lab, result_id, path, offset, limit):
    row = lab.result(result_id)
    job = lab.owner/'jobs'/row['job_id']
    inventory = load(job/'retained-files.json')
    if path is None:
        return {'files':[dict(path=n,sha256=h,bytes=(job/n).stat().st_size) for n,h in inventory.items()]}
    rel(path)
    if path not in inventory or type(offset) is not int or offset < 0 or type(limit) is not int or not 1 <= limit <= 65536:
        raise Error('invalid_artifact_request')
    with (job/path).open('rb') as f:
        f.seek(offset); data=f.read(limit)
    try:
        content=data.decode('utf-8');encoding='utf-8'
    except UnicodeDecodeError:
        content=base64.b64encode(data).decode();encoding='base64'
    return dict(path=path,offset=offset,bytes=len(data),total_bytes=(job/path).stat().st_size,
                encoding=encoding,content=content,sha256=inventory[path])


def compare(lab, result_ids):
    if not isinstance(result_ids,list) or not 1 <= len(result_ids) <= 64:
        raise Error('invalid_result_list')
    rows=[lab.result(r) for r in result_ids]
    if any(not r['result_id'].startswith('r-') for r in rows):
        raise Error('measurements_required')
    return {'scoring':lab.config['protocol']['scoring'],'results':[
        {k:r[k] for k in ('result_id','name','depth','sizes','measurements','scoring','correctness')} for r in rows]}


def finish(lab, result_id, validation_id, request_id):
    ident(request_id)
    row,check=lab.result(result_id),lab.result(validation_id)
    if not validation_id.startswith('v-') or check['source_result_id'] != result_id or not check['passed']:
        raise Error('passed_matching_validation_required')
    if any(j['status'] in ACTIVE for j in lab.jobs()['jobs']):
        raise Error('jobs_still_active')
    destination=lab.owner/'FINISH.json'
    request=dict(result_id=result_id,validation_id=validation_id,request_id=request_id)
    with lock(lab.owner/'finish.lock'):
        if destination.exists():
            prior=load(destination)
            if prior['request'] != request:raise Error('already_finished')
            return prior
        exports={'measurement':lab.export(result_id),'validation':lab.export(validation_id),
                 'build':lab.export(row['build_id'])}
        receipt=dict(status='submitted_for_owner_review',request=request,exports=exports,
                     target_claim_verified=False,measurement_target_met=row['scoring'].get('target_met'),
                     finished_utc=now())
        save(destination,receipt,0o444)
        return receipt
