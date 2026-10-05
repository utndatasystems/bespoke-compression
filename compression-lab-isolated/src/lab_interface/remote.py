"""Credential-free tool surface; owner-only SSH transfers and durable server jobs."""
import io
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tarfile
import time

from .api import _clean_env
from compression_lab.util import Error, load, save, sha


def _ssh(config, command, **kwargs):
    if config.get('transport')=='same-host':
        return subprocess.run(['/bin/sh','-c',command],check=True,timeout=None,env=_clean_env(),**kwargs)
    return subprocess.run([*config['ssh'],command],check=True,timeout=None,**kwargs)


def call(lab,job,plan,files):
    remote=lab.config['remote']
    destination=remote['root']+'/jobs/'+job.name
    packet=job/'remote-packet';packet.mkdir()
    for name,source in files.items():
        target=packet/name;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source,target)
    plan.update(cpu=remote['cpu'],locks=remote['locks'])
    save(packet/'PLAN.json',plan)
    pins={str(p.relative_to(packet)):sha(p) for p in packet.rglob('*') if p.is_file()}
    save(packet/'PINS.json',pins)
    # Each local durable job owns exactly one remote directory; never retry a launch.
    archive=job/'transfer.tar'
    with tarfile.open(archive,'w') as t:
        for p in packet.rglob('*'):
            if p.is_file():t.add(p,arcname=str(p.relative_to(packet)))
    receipt=job/'remote-receipt.json'
    save(receipt,dict(remote_directory=destination,plan_sha256=sha(packet/'PLAN.json'),phase='uploading'))
    with archive.open('rb') as data:
        _ssh(remote, 'umask 077; mkdir '+shlex.quote(destination)+' && tar -C '+shlex.quote(destination)+' -xf -',stdin=data,capture_output=True)
    launcher='''import json,os,subprocess,sys
from pathlib import Path
root=Path(sys.argv[1]);runtime=sys.argv[2];python=sys.argv[3]
if (root/'LAUNCH.json').exists():raise SystemExit('already launched')
with (root/'worker.log').open('wb') as log:
 p=subprocess.Popen([python,runtime+'/remote-entry.py',str(root)],stdin=subprocess.DEVNULL,stdout=log,stderr=log,start_new_session=True)
 fields=Path('/proc/'+str(p.pid)+'/stat').read_text().rsplit(')',1)[1].split()
 (root/'LAUNCH.json').write_text(json.dumps({'pid':p.pid,'process_start':fields[19]}))
'''
    command='python3 -c '+shlex.quote(launcher)+' '+shlex.quote(destination)+' '+shlex.quote(remote['runtime'])+' '+shlex.quote(remote['python'])
    _ssh(remote,command,capture_output=True)
    save(receipt,dict(remote_directory=destination,plan_sha256=sha(packet/'PLAN.json'),phase='launched'))
    probe='''import json,sys
from pathlib import Path
r=Path(sys.argv[1]);s=r/'STATUS.json'
state=json.loads(s.read_text()) if s.exists() else {'state':'starting'}
launch=json.loads((r/'LAUNCH.json').read_text());p=Path('/proc')/str(launch['pid'])/'stat'
if state['state'] not in ('complete','failed'):
 live=p.exists() and p.read_text().rsplit(')',1)[1].split()[19]==launch['process_start'] and p.read_text().rsplit(')',1)[1].split()[0] not in ('Z','X')
 if not live:state.update(state='failed',reason='worker_interrupted')
print(json.dumps(state))
'''
    while True:
        result=_ssh(remote,'python3 -c '+shlex.quote(probe)+' '+shlex.quote(destination),capture_output=True,text=True)
        state=json.loads(result.stdout)
        save(job/'remote-status.json',state)
        if state['state'] in ('complete','failed'):break
        if (job/'cancel').exists():
            raise Error('remote_cancellation_pending_owner')
        time.sleep(3)
    returned=job/'remote-results.tar'
    with returned.open('wb') as out:
        _ssh(remote,'tar -C '+shlex.quote(destination)+' -cf - --exclude=inner.lock --exclude=state.lock .',stdout=out,stderr=subprocess.PIPE)
    resultroot=job/'remote-result';resultroot.mkdir()
    with tarfile.open(returned) as tar:
        tar.extractall(resultroot,filter='data')
    # Transfer containers are disposable; retained files are individually hashed.
    archive.unlink();returned.unlink()
    if state['state']!='complete':raise Error('remote_'+state.get('reason','failed'))
    return resultroot


def measure(lab,job,state):
    files={}
    for folder in ('candidate','libraries','harness'):
        for p in (job/folder).rglob('*'):
            if p.is_file():files[str(p.relative_to(job))]=p
    plan=dict(kind='measurement',columns=lab.config['remote']['columns'],
              row_framing=lab.config['row_framing'],quick=state['quick'],dbtext=state.get('dbtext',False))
    returned=call(lab,job,plan,files)
    native=load(returned/'NATIVE.json')
    relative=Path(native['evidence']).relative_to(Path(lab.config['remote']['root'])/'jobs'/job.name)
    native['evidence']=str(returned/relative)
    return native


def diagnostics(lab,job,*,kind,driver,decoder,archives,libraries,columns,rows_driver=None):
    files={'harness/driver':driver,'candidate/decoder.so':decoder}
    for soname,path in libraries.items():files['libraries/'+soname]=path
    calls=[]
    for col in columns:
        name=col['sha256']+'.bin';files['archives/'+name]=archives/name
        for case in range(16) if kind=='validation' else [None]:
            args=['harness/driver','candidate/decoder.so','archives/'+name,str(col['bytes']),str(case)]
            if kind=='profile':
                args=['harness/driver','candidate/decoder.so','decode','archives/'+name,'@OUTPUT@',str(col['bytes']+32),'/dev/null']
            calls.append(dict(argv=args,reads=['harness/driver','candidate/decoder.so','archives/'+name],
                              expected_sha256=col['sha256']))
    if rows_driver is not None:
        files['harness/rows']=rows_driver
        for col in columns:
            name=col['sha256']+'.bin'
            calls.append(dict(argv=['harness/rows','candidate/decoder.so','archives/'+name,
                                    str(col['rows']),str(col['bytes']),'@OUTPUT_DIR@','validate'],
                              reads=['harness/rows','candidate/decoder.so','archives/'+name]))
    return call(lab,job,dict(kind=kind,calls=calls),files)


def cancel(lab,job):
    remote=lab.config['remote'];destination=remote['root']+'/jobs/'+job.name
    script='''import json,os,signal,sys,time
from pathlib import Path
r=Path(sys.argv[1]);launch=r/'LAUNCH.json'
if not launch.exists():raise SystemExit(0)
d=json.loads(launch.read_text());p=Path('/proc')/str(d['pid'])/'stat'
if p.exists() and p.read_text().rsplit(')',1)[1].split()[19]==d['process_start']:
 os.killpg(d['pid'],signal.SIGTERM)
(r/'CANCEL.json').write_text(json.dumps({'cancelled':True,'pid':d['pid']}))
'''
    _ssh(remote,'python3 -c '+shlex.quote(script)+' '+shlex.quote(destination),capture_output=True)
