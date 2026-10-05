"""Real fresh native process timing and constrained Linux execution."""
from __future__ import annotations
import functools,hashlib,json,math,os,platform,shutil,signal,subprocess,sys,tempfile,time
from pathlib import Path
from .util import Error,save,sha,digest
from .resources import resolve_resources,THREAD_ENV
HOST_LOCK=Path('/home/iachnyk/compression-research/results/benchmark-cpu10.lock')

def process_tree(pid):
 """Use PPid from proc stat: CONFIG_CHECKPOINT_RESTORE/children is optional."""
 parents={}
 for q in Path('/proc').iterdir():
  if not q.name.isdigit():continue
  try:
   fields=(q/'stat').read_text().rsplit(')',1)[1].split()
   parents.setdefault(int(fields[1]),[]).append(int(q.name))
  except (OSError,ValueError,IndexError):continue
 todo=[pid];seen=set()
 while todo and len(seen)<4096:
  p=todo.pop()
  if p in seen:continue
  seen.add(p);todo.extend(parents.get(p,[]))
 return seen

def usage(pid):
 rss=n=0
 for p in process_tree(pid):
  try:
   for line in Path(f'/proc/{p}/status').read_text().splitlines():
    if line.startswith('VmRSS:'):rss+=int(line.split()[1])*1024
   n+=1
  except (OSError,ValueError):pass
 return rss,n

def live_tasks(pid,reported):
 """Exclude kernel exit cleanup, which can outlive pthread_join's wakeup."""
 if reported<=1:return reported
 try:
  tasks=list(Path(f'/proc/{pid}/task').iterdir());live=0
  for task in tasks:
   try:fields=(task/'stat').read_text().rsplit(')',1)[1].split()
   except FileNotFoundError:continue
   # proc stat field 9: PF_EXITING is set before clear_child_tid wakes join.
   if fields[0] not in ('Z','X','x') and not int(fields[6])&0x4:live+=1
  return live
 except (OSError,ValueError,IndexError):return reported

def observe(pid,wrapper=True):
 """Sample each TGID once for RSS and its Threads count for aggregate tasks.

 RSS is shared by pthreads: summing per-thread RSS would multiply memory use.
 Native values exclude only the known unshare waiter, not compiler children.
 """
 result={'rss_bytes':0,'processes':0,'tasks':0,'native_rss_bytes':0,
         'native_processes':0,'native_tasks':0,'native_live_tasks':0,'max_threads_per_process':0}
 for p in process_tree(pid):
  try:
   status=dict(line.split(':',1) for line in Path(f'/proc/{p}/status').read_text().splitlines() if ':' in line)
   rss=int(status.get('VmRSS','0 kB').split()[0])*1024
   threads=int(status.get('Threads','1'))
  except (OSError,ValueError):continue
  result['rss_bytes']+=rss;result['processes']+=1;result['tasks']+=threads
  result['max_threads_per_process']=max(result['max_threads_per_process'],threads)
  if not wrapper or p!=pid:
   result['native_rss_bytes']+=rss;result['native_processes']+=1;result['native_tasks']+=threads
   result['native_live_tasks']+=live_tasks(p,threads)
 return result


def execute(argv,*,output,readonly=None,stdin=None,stdout=None,timeout=None,memory=2*1024**3,output_limit=8*1024**3,build=False,sanitizer=False,mode='required',cancel=None,runtime_files=None,check=True,threads=1,cpus=None):
 if platform.system()!='Linux' or platform.machine()!='x86_64':raise Error('unsupported_execution_platform')
 if mode not in ('required','exploratory'):raise Error('invalid_runner_mode')
 if not argv or any(not isinstance(a,str) or '\0' in a for a in argv):raise Error('invalid_argv')
 allocation=resolve_resources(threads,cpus);cpus=allocation['cpus'];cpu=cpus[0]
 # Retain the argument for older callers; published executions have no time cap.
 timeout=None
 if type(memory) is not int or memory<=0:raise Error('invalid_memory_limit')
 if type(output_limit) is not int or output_limit<=0:raise Error('invalid_output_limit')
 cpu_seconds=None
 enclosing=cgroup_limits()
 output=Path(output).absolute();output.mkdir(parents=True,exist_ok=True)
 ro={k:str(Path(v).absolute()) for k,v in (readonly or {}).items()}
 allowed_mounts={'/candidate','/source','/input'}|({'/toolchain'} if build else set())
 if set(ro)-allowed_mounts:raise Error('invalid_mount')
 with tempfile.TemporaryDirectory(prefix='compression-lab-process-') as tmp:
  tmp=Path(tmp);op=Path(stdout) if stdout else tmp/'stdout';ep=tmp/'stderr'
  if mode=='required':
   unshare=shutil.which('unshare')
   if not unshare:raise Error('sandbox_unavailable','Install util-linux and libseccomp2')
   c={'argv':argv,'root':str(tmp/'root'),'readonly':ro,'runtime_files':runtime_files,'output':str(output),'memory':memory,'output_limit':output_limit,'timeout':timeout,'build':build,'sanitizer':sanitizer,'cpu':cpu,'cpus':cpus,'threads':threads}
   save(tmp/'config.json',c);cmd=[unshare,'--user','--map-root-user','--mount','--net','--pid','--ipc','--uts','--fork','--kill-child=KILL',sys.executable,'-I','-S',str(Path(__file__).with_name('_sandbox.py')),str(tmp/'config.json')]
  else:
   def subst(a):
    for k,v in {**ro,'/output':str(output)}.items():
     if a==k or a.startswith(k+'/'):return v+a[len(k):]
    return a
   taskset=shutil.which('taskset')
   if not taskset:raise Error('affinity_unavailable')
   cmd=[taskset,'--cpu-list',','.join(map(str,cpus)),*map(subst,argv)]
  cmd=[sys.executable,'-I','-S',str(Path(__file__).with_name('_launch.py')),str(os.getpid()),*cmd]
  env={'PATH':'/usr/bin:/bin','HOME':str(tmp),'TMPDIR':str(tmp),'LANG':'C','LC_ALL':'C','OMP_NUM_THREADS':'1','OPENBLAS_NUM_THREADS':'1','ASAN_OPTIONS':'abort_on_error=1:detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'}
  env.update({name:str(threads) for name in THREAD_ENV})
  if mode=='exploratory' and build and '/toolchain' in ro:env['LD_LIBRARY_PATH']=str(Path(ro['/toolchain'])/'lib')
  peak=tree_peak=count=task_peak=thread_peak=native_peak=native_task_peak=native_live_peak=native_process_peak=0
  reason=None;directory_bytes=directory_files=0;last_scan=0.;samples=0;max_poll_gap=0.;previous_poll=None
  with open(stdin or os.devnull,'rb') as fin,open(op,'wb') as fout,open(ep,'wb') as ferr:
   start=time.perf_counter_ns();p=subprocess.Popen(cmd,stdin=fin,stdout=fout,stderr=ferr,cwd=output,env=env,start_new_session=True,close_fds=True)
   try:
    while True:
     pid,st,ru=os.wait4(p.pid,os.WNOHANG)
     if pid:p.returncode=os.waitstatus_to_exitcode(st);peak=max(peak,int(ru.ru_maxrss)*1024);break
     now=time.monotonic()
     if previous_poll is not None:max_poll_gap=max(max_poll_gap,now-previous_poll)
     previous_poll=now;samples+=1
     observation=observe(p.pid,wrapper=mode=='required')
     rss=observation['rss_bytes'];n=observation['processes']
     peak=max(peak,rss);tree_peak=max(tree_peak,rss);count=max(count,n)
     task_peak=max(task_peak,observation['tasks']);thread_peak=max(thread_peak,observation['max_threads_per_process'])
     native_peak=max(native_peak,observation['native_rss_bytes'])
     native_task_peak=max(native_task_peak,observation['native_tasks']);native_process_peak=max(native_process_peak,observation['native_processes'])
     native_live_peak=max(native_live_peak,observation['native_live_tasks'])
     if time.monotonic()-last_scan>.05:
      directory_bytes=directory_files=0
      for base,dirs,files in os.walk(output,followlinks=False):
       for f in files:
        try:directory_bytes+=os.lstat(Path(base)/f).st_size;directory_files+=1
        except FileNotFoundError:pass
      last_scan=time.monotonic()
     if (rss if build else observation['native_rss_bytes'])>memory:reason='memory_limit'
     elif n>(132 if build else 2 if mode=='required' else 3):reason='process_limit'
     elif not build and observation['native_live_tasks']>threads:reason='thread_limit'
     elif build and observation['tasks']>132:reason='task_limit'
     elif directory_bytes>output_limit or directory_files>100000:reason='output_limit'
     elif cancel and Path(cancel).exists():reason='cancelled'
     elif op.stat().st_size>output_limit or ep.stat().st_size>32768:reason='output_limit'
     if reason:
      try:os.killpg(p.pid,signal.SIGKILL)
      except ProcessLookupError:pass
      _,st,ru=os.wait4(p.pid,0);p.returncode=os.waitstatus_to_exitcode(st);peak=max(peak,int(ru.ru_maxrss)*1024);break
     time.sleep(.002)
   except BaseException:
    try:os.killpg(p.pid,signal.SIGKILL);os.wait4(p.pid,0)
    except (ProcessLookupError,ChildProcessError):pass
    raise
   elapsed=time.perf_counter_ns()-start
  if op.stat().st_size>output_limit or ep.stat().st_size>32768:reason='output_limit'
  with ep.open('rb') as f:err=f.read(32768).decode('utf-8','replace')
  out=None
  if not stdout:
   with op.open('rb') as f:out=f.read(32768).decode('utf-8','replace')
  enforcement={'affinity':'sched_setaffinity plus seccomp denial of changes' if mode=='required' and not build else 'initial affinity; changes not prohibited',
   'memory':('shared-process RLIMIT_AS and native aggregate RSS polling' if not build else 'per-process RLIMIT_AS and process-tree RSS polling') if mode=='required' and not sanitizer else 'aggregate RSS polling only',
   'threads':('seccomp denies all clones' if threads==1 else 'UID-scoped RLIMIT_NPROC emergency ceiling with task-cleanup headroom plus aggregate live-task polling; not an exact per-call kernel task cap') if mode=='required' and not build else 'aggregate live-task polling',
   'processes':'seccomp denies process creation' if mode=='required' and not build else 'process-tree polling',
   'cpu_time':'unlimited',
   'dedicated_per_call_cgroup':False,
   'measurement_limits':'Polling can miss transients and overshoot limits; intervals include scan time and scheduler delay. Native samples include the namespace helper before exec. RSS sums one value per TGID, may double-count shared pages across processes, and excludes page cache/kernel memory. wait4 RSS is the largest process high-water mark including bootstrap, not an aggregate peak. Enclosing cgroup limits are shared with evaluator/controller and are not per-call measurements.'}
  resource_record={**allocation,'memory_limit_bytes':memory,'wall_time_limit_seconds':timeout,
   'rlimit_cpu_seconds':cpu_seconds if mode=='required' else None,
   'rlimit_as_bytes':memory if mode=='required' and not sanitizer else None,
   'rlimit_nproc':(128 if build or threads>1 else 1) if mode=='required' else None,
   'effective_cgroup_limits':enclosing,'enforcement':enforcement,
   'runtime_identity':{'kernel_release':platform.release(),'architecture':platform.machine(),
    'libc':list(platform.libc_ver()),'python_binary_sha256':sha(Path(sys.executable).resolve()),
    'runner_sha256':sha(Path(__file__)),'resources_helper_sha256':sha(Path(__file__).with_name('resources.py')),
    'sandbox_helper_sha256':sha(Path(__file__).with_name('_sandbox.py')) if mode=='required' else None,
    'launcher_sha256':sha(Path(__file__).with_name('_launch.py')),
    'seccomp_sha256':sha(Path('/usr/lib/x86_64-linux-gnu/libseccomp.so.2').resolve()) if mode=='required' and Path('/usr/lib/x86_64-linux-gnu/libseccomp.so.2').exists() else None}}
  resource_record['execution_resource_digest']=digest(resource_record)
  r={'argv':argv,'elapsed_ns':elapsed,'returncode':p.returncode,'peak_rss_bytes':peak,'observed_tree_peak_rss_bytes':tree_peak,'wait4_peak_rss_bytes':int(ru.ru_maxrss)*1024,'rss_poll_seconds':.002,'output_tree_poll_seconds':.05,'max_observed_processes':count,'cpu':cpu,'cpus':cpus,'threads':threads,'sandbox':mode,'stderr':err,'stdout':out,'stdout_bytes':op.stat().st_size,'reason_code':reason,
   'resources':resource_record,'cpu_topology':allocation['cpu_topology'],
   'observed_native_peak_rss_bytes':native_peak,'max_observed_tasks':task_peak,
   'max_observed_threads':thread_peak,'max_observed_native_tasks':native_task_peak,
   'max_observed_native_live_tasks':native_live_peak,
   'max_observed_native_processes':native_process_peak,'resource_observations':samples,
   'max_observed_poll_gap_seconds':max_poll_gap,
   'cpu_user_seconds':ru.ru_utime,'cpu_system_seconds':ru.ru_stime,
   'cpu_total_seconds':ru.ru_utime+ru.ru_stime,
   'cpu_time_scope':'wait4 launcher and waited-for descendants, including namespace/native bootstrap'}
  code=reason or ('sandbox_unavailable' if p.returncode==125 and mode=='required' else 'native_failed' if check and p.returncode else None)
  if code:e=Error(code,err or code);e.measurement=r;raise e
  return r

@functools.lru_cache(maxsize=1)
def probe():
 with tempfile.TemporaryDirectory(prefix='compression-lab-private-probe-') as tmp:
  tmp=Path(tmp);secret=tmp/'canary';secret.write_text('SYNTHETIC')
  code='''import os,socket,json
r={}
for k,fn in [('private_canary_denied',lambda:open(%r).read()),('runtime_readonly',lambda:open('/usr/denied-write','w')),('network_denied',lambda:socket.socket())]:
 try:fn();r[k]=False
 except OSError:r[k]=True
try:
 p=os.fork()
 if p==0:os._exit(0)
 os.waitpid(p,0);r['fork_denied']=False
except OSError:r['fork_denied']=True
r['pid_namespace']=os.getpid()==1
r['host_proc_absent']=not os.path.exists('/proc/1/environ')
print(json.dumps(r))
'''%str(secret)
  try:
   r=execute(['/usr/bin/python3','-I','-S','-c',code],output=tmp/'out',timeout=None);checks=json.loads(r['stdout'])
   if not all(checks.values()):raise Error('sandbox_probe_failed',str(checks))
   return {'status':'available','backend':'util-linux namespaces, chroot, seccomp','checks':checks,'measurement':r}
  except Exception as e:return {'status':'blocked','reason_codes':[getattr(e,'code','sandbox_probe_failed')],'detail':str(e)[:2000]}

def cgroup_limits(root=Path('/sys/fs/cgroup'),membership=Path('/proc/self/cgroup')):
 """Effective cgroup-v2 ceilings, including the current scope's ancestors.

 Root-level controller files alone miss limits applied by systemd or a
 container parent. Ephemeral scope names are excluded from runtime identity.
 """
 try:
  root=Path(root);lines=Path(membership).read_text().splitlines()
  scope=next(line.split(':',2)[2] for line in lines if line.startswith('0::'))
  relative=Path(scope.lstrip('/'))
  if '..' in relative.parts:raise ValueError('unmapped cgroup namespace')
  leaf=root/relative
  if not leaf.is_dir():raise ValueError('process cgroup unavailable')
  quotas=[];memories=[];tasks=[]
  for directory in [leaf,*leaf.parents]:
   if directory!=root and root not in directory.parents:break
   cpu=directory/'cpu.max';memory=directory/'memory.max'
   if cpu.exists():
    quota,period=cpu.read_text().split()
    if quota!='max':quotas.append(int(quota)/int(period))
   if memory.exists():
    value=memory.read_text().strip()
    if value!='max':memories.append(int(value))
   pids=directory/'pids.max'
   if pids.exists():
    value=pids.read_text().strip()
    if value!='max':tasks.append(int(value))
   if directory==root:break
  cpuset=leaf/'cpuset.cpus.effective'
  return {'status':'verified','version':2,'cpu_quota_cores':min(quotas,default=None),
          'memory_max_bytes':min(memories,default=None),'pids_max':min(tasks,default=None),
          'cpuset_cpus':cpuset.read_text().strip() if cpuset.exists() else None}
 except (OSError,ValueError,StopIteration,ZeroDivisionError):
  return {'status':'unverified','reason':'effective_cgroup_limits_unavailable'}

def fingerprint(*,threads=1,cpus=None,limits=None):
 operation_limits=limits or {};bounded=limits is not None
 def guard():
  if not bounded:return
  cancel=operation_limits.get('_cancel')
  if cancel is not None and Path(cancel).exists():raise Error('cancelled')
 def file_sha(path):
  if not bounded:return sha(path)
  guard();h=hashlib.sha256()
  with Path(path).open('rb') as source:
   while True:
    chunk=source.read(1048576);guard()
    if not chunk:break
    h.update(chunk)
  return h.hexdigest()
 guard()
 allocation=resolve_resources(threads,cpus)
 cpu=Path('/proc/cpuinfo').read_text() if Path('/proc/cpuinfo').exists() else ''
 tools={}
 for n in ('gcc','g++','unshare'):
  guard()
  p=shutil.which(n,path='/usr/bin:/bin')
  if p:
   p=Path(p).resolve()
   try:r=subprocess.run([str(p),'--version'],capture_output=True,text=True,timeout=None)
   except subprocess.TimeoutExpired as error:
    if not bounded:raise
    raise Error('timeout') from error
   guard();tools[n]={'path':str(p),'sha256':file_sha(p),'version':r.stdout.splitlines()[0]}
 limits={}
 for name in ('/sys/fs/cgroup/cpu.max','/sys/fs/cgroup/memory.max','/sys/fs/cgroup/cpuset.cpus.effective','/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor'):
  guard()
  limits[name]=Path(name).read_text().strip() if Path(name).exists() else 'unavailable'
 engine=digest({p.relative_to(Path(__file__).parent).as_posix():file_sha(p) for p in Path(__file__).parent.rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix!='.pyc'})
 r={'os':platform.platform(),'architecture':platform.machine(),'cpu_model':next((l.split(':',1)[1].strip() for l in cpu.splitlines() if l.startswith('model name')),''),'pinned_cpu':allocation['cpus'][0],'native_resources':allocation,'python':platform.python_version(),'python_binary_sha256':file_sha(Path(sys.executable).resolve()),'seccomp_sha256':file_sha(Path('/usr/lib/x86_64-linux-gnu/libseccomp.so.2').resolve()) if Path('/usr/lib/x86_64-linux-gnu/libseccomp.so.2').exists() else None,'host_lock':str(HOST_LOCK),'tools':tools,'hardware_limits':limits,'engine_sha256':engine,
 'timing_policy':'Complete fresh namespace/helper/native startup plus native I/O; no warmups or trial trimming. Memory conservatively includes fork/bootstrap.'}
 r['effective_cgroup_limits']=cgroup_limits()
 r['runtime_digest']=digest(r);guard();return r


def require_fingerprint(expected,limits=None):
 """Check stored metadata, then match the selected CPU and pinned runtime."""
 if not isinstance(expected,dict) or digest({k:v for k,v in expected.items() if k!='runtime_digest'})!=expected.get('runtime_digest'):
  raise Error('invalid_runtime_fingerprint')
 allocation=expected.get('native_resources',{})
 actual=fingerprint(threads=allocation.get('threads',1),cpus=allocation.get('cpus'),**({'limits':limits} if limits is not None else {}))
 if actual['runtime_digest']!=expected['runtime_digest']:
  raise Error('stale_runtime_digest')
 return actual
