"""Immutable native candidate inventory, deterministic build and ELF closure."""
from __future__ import annotations
import hashlib,time,os,shutil,stat,struct,subprocess,tempfile,zipfile,zlib
from pathlib import Path
from .util import Error,load,save,safe,rel,sha,digest,secure_directory,secure_load,secure_names
from .runner import execute,fingerprint
PLATFORM={'libc.so.6','libm.so.6','libstdc++.so.6','libgcc_s.so.1','ld-linux-x86-64.so.2','libpthread.so.0','librt.so.1','libdl.so.2'}
TOOLS={'gcc','g++','clang','clang++','rustc','ar','make','cmake'}
KEYS={'schema_version','candidate_id','language','input_domain','deterministic','threads','source_paths','artifact_paths','runtime_paths','build_commands','sanitizer_build_commands','build_output_paths','executable','commands','toolchain','dependencies','hypothesis','training'}
V2_KEYS={'decoder','diagnostics'}
DECODER_COMMAND_FILE='decoder-command.json'

def blank_manifest(name):
 """Complete C++ v2 interface, with no algorithm, framing helper or codec source."""
 from .util import ident
 ident(name);compiler=shutil.which('g++',path='/usr/bin:/bin')
 if not compiler:raise Error('blocked_toolchain','g++')
 compiler=Path(compiler).resolve()
 commands={op:['{runtime}/'+('decoder' if op.startswith('decode') else 'codec'),op.replace('_','-')]+
           (['{input_dir}','{output_dir}'] if op.endswith('_dir') else [])
           for op in ('encode_dir','decode_dir','encode_stream','decode_stream')}
 def builds(flags):
  return [['g++','-std=c++17',*flags,'-ffile-prefix-map=/source=source','-ffile-prefix-map=/output=build',
           '{source}/'+name+'.cpp','-o','{build}/'+name] for name in ('codec','decoder')]
 return manifest({'schema_version':2,'candidate_id':name,'language':'c++','input_domain':'opaque_bytes',
  'deterministic':True,'threads':1,'source_paths':['codec.cpp','decoder.cpp'],'artifact_paths':[],
  'runtime_paths':[],'build_commands':builds(['-O3','-DNDEBUG']),
  'sanitizer_build_commands':builds(['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']),
  'build_output_paths':['codec','decoder'],'executable':'codec','commands':commands,
  'decoder':{'executable':'decoder','build_output_paths':['decoder'],'artifact_paths':[],'runtime_paths':[]},
  'diagnostics':{'kind':'cxx-asan-ubsan-v1'},'toolchain':{'g++':{'path':str(compiler),'sha256':sha(compiler)}},
  'dependencies':[],'training':{'kind':'data-independent'}})

def check_limits(limits=None,cancel=None):
 """Cooperative preprocessing boundary; native calls use the process runner."""
 limits=limits or {};cancel=cancel if cancel is not None else limits.get('_cancel')
 if cancel is not None and Path(cancel).exists():raise Error('cancelled')

def execution_timeout(limits=None,default=None,cancel=None):
 check_limits(limits,cancel)
 return None

def checked_sha(path,limits=None):
 h=hashlib.sha256();check_limits(limits)
 with Path(path).open('rb') as source:
  while True:
   b=source.read(1048576);check_limits(limits)
   if not b:break
   h.update(b)
 return h.hexdigest()

def checked_copy(source,dest,limits=None):
 check_limits(limits)
 with Path(source).open('rb') as src,Path(dest).open('wb') as out:
  while True:
   b=src.read(1048576);check_limits(limits)
   if not b:break
   out.write(b);check_limits(limits)

def copy_import(root,name,dest,limits=None,limit=512*1048576):
 """Retain descriptor-relative import safety while copying bounded chunks."""
 check_limits(limits);rel(name);path=Path(root)/name
 with secure_directory(path.parent) as directory:
  fd=os.open(path.name,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK,dir_fd=directory)
  try:
   st=os.fstat(fd)
   if not stat.S_ISREG(st.st_mode) or st.st_nlink!=1 or st.st_size>limit:raise Error('unsafe_import_file')
   size=0
   with Path(dest).open('wb') as out:
    while True:
     b=os.read(fd,min(1048576,limit-size+1));check_limits(limits)
     if not b:break
     size+=len(b)
     if size>limit:raise Error('oversized_import')
     out.write(b);check_limits(limits)
  finally:os.close(fd)

def write_source_archive(src,names,out,limits=None):
 check_limits(limits)
 with zipfile.ZipFile(out,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
  for name in sorted(names):
   check_limits(limits);path=safe(src,name)
   entry=zipfile.ZipInfo(name,(1980,1,1,0,0,0));entry.external_attr=0o100644<<16;entry.create_system=3
   entry.compress_type=zipfile.ZIP_DEFLATED;entry._compresslevel=9;entry.file_size=path.stat().st_size
   with path.open('rb') as source,archive.open(entry,'w') as dest:
    while True:
     chunk=source.read(1048576);check_limits(limits)
     if not chunk:break
     dest.write(chunk);check_limits(limits)
 check_limits(limits)

def decoder_commands(m):
 return {'schema_version':1,'commands':{op:m['commands'][op] for op in ('decode_dir','decode_stream')}}

def decoder_role(m):
 return m.get('decoder', {k:m[k] for k in ('executable','build_output_paths','artifact_paths','runtime_paths')})

def diagnostic_kind(m):
 return m.get('diagnostics',{}).get('kind','cxx-asan-ubsan-v1')

def executables(m):
 return sorted({m['executable'],decoder_role(m)['executable']}|({m['offline']['executable']} if m.get('offline') else set()))
def scan(root,names,limits=None):
 check_limits(limits);root=Path(root);names=set(names);actual=set();total=0
 if root.is_symlink():raise Error('symlink_path')
 for p in root.rglob('*'):
  check_limits(limits)
  if p.is_symlink():raise Error('symlink_path',str(p.relative_to(root)))
  if p.is_dir():continue
  s=p.relative_to(root).as_posix();safe(root,s);actual.add(s)
 if actual!=names:raise Error('undeclared_or_missing_files',str({'undeclared':sorted(actual-names)[:8],'missing':sorted(names-actual)[:8]}))
 out=[]
 for s in sorted(names):
  p=safe(root,s);total+=p.stat().st_size
  if total>512*1048576 or len(names)>20000:raise Error('oversized_candidate')
  out.append({'path':s,'bytes':p.stat().st_size,'sha256':checked_sha(p,limits)})
 return out

def scan_selected(root,names,limits=None):
 return [{'path':n,'bytes':safe(root,n).stat().st_size,'sha256':checked_sha(safe(root,n),limits)} for n in sorted(set(names))]

def manifest(m):
 if not isinstance(m,dict) or set(m)-(KEYS|V2_KEYS|{'offline'} if m.get('schema_version')==2 else KEYS) or not KEYS-{'hypothesis','training'}<=set(m):raise Error('invalid_candidate_manifest')
 if type(m['schema_version']) is not int or m['schema_version'] not in (1,2) or m['language'] not in ('c','c++','rust') or type(m['threads']) is not int or not 1<=m['threads']<=64 or (m['schema_version']==1 and m['threads']!=1) or m['deterministic'] is not True:raise Error('invalid_candidate_contract')
 if m['input_domain'] not in ('opaque_bytes','hcb1-arbitrary-content','rlb1-lexical'):raise Error('invalid_input_domain')
 for k in ('source_paths','artifact_paths','runtime_paths','build_output_paths'):
  if not isinstance(m[k],list):raise Error('invalid_path_list')
  for p in m[k]:rel(p)
 if m['executable'] not in m['build_output_paths']:raise Error('missing_executable')
 if m['schema_version']==2:
  if not V2_KEYS<=set(m) or not isinstance(m['decoder'],dict) or set(m['decoder'])!={'executable','build_output_paths','artifact_paths','runtime_paths'}:raise Error('invalid_decoder_role')
  for k in ('build_output_paths','artifact_paths','runtime_paths'):
   if not isinstance(m['decoder'][k],list) or len(set(m['decoder'][k]))!=len(m['decoder'][k]) or not set(m['decoder'][k])<=set(m[k]):raise Error('invalid_decoder_role')
  if m['decoder']['executable'] not in m['decoder']['build_output_paths']:raise Error('missing_decoder_executable')
  if not isinstance(m['diagnostics'],dict) or set(m['diagnostics'])!={'kind'} or diagnostic_kind(m)!=('rust-checked-v1' if m['language']=='rust' else 'cxx-asan-ubsan-v1'):raise Error('unsupported_diagnostics')
 if 'offline' in m:
  offline=m['offline']
  if not isinstance(offline,dict) or set(offline)!={'executable','command','artifact_paths','rebuild'}:raise Error('invalid_offline_stage')
  if offline['executable'] not in m['build_output_paths'] or type(offline['rebuild']) is not bool:raise Error('invalid_offline_stage')
  paths=offline['artifact_paths']
  if not isinstance(paths,list) or not paths or len(set(paths))!=len(paths) or set(paths)!=set(m['artifact_paths']):raise Error('incomplete_offline_artifacts')
  if m.get('training',{}).get('kind') not in ('train-only','whole-dataset'):raise Error('missing_offline_provenance')
  cmd=offline['command']
  if not isinstance(cmd,list) or not cmd or cmd[0]!='{runtime}/'+offline['executable'] or any(not isinstance(a,str) or '\0' in a for a in cmd):raise Error('non_native_offline_command')
 if set(m['commands'])!={'encode_dir','decode_dir','encode_stream','decode_stream'}:raise Error('incomplete_interfaces')
 for op,cmd in m['commands'].items():
  exe=decoder_role(m)['executable'] if op.startswith('decode') else m['executable']
  if not isinstance(cmd,list) or not cmd or cmd[0]!='{runtime}/'+exe or any(not isinstance(a,str) or '\0' in a for a in cmd):raise Error('non_native_command')
 for k in ('build_commands','sanitizer_build_commands'):
  if not m[k]:raise Error('blocked_toolchain','Normal and ASan/UBSan build recipes required')
  for cmd in m[k]:
   if not isinstance(cmd,list) or not cmd or cmd[0] not in TOOLS or cmd[0] not in m['toolchain'] or any(not isinstance(a,str) or '\0' in a for a in cmd):raise Error('invalid_build_command')
 # v1 accepts explicit C/C++ diagnostic compiler recipes only. A recipe label
 # is not evidence of instrumentation; omission/disable flags fail closed.
 for cmd in m['sanitizer_build_commands']:
  if m['schema_version']==2 and m['language']=='rust':
   if cmd[0]!='rustc' or any(a.startswith(('@','-Z')) for a in cmd):raise Error('unsafe_diagnostic_recipe')
   # Last value wins in rustc; reject every conflicting spelling/override.
   flags={};i=1
   while i<len(cmd):
    a=cmd[i]
    if a in ('-C','--codegen'):
     i+=1
     if i==len(cmd):raise Error('invalid_build_command')
     a='-C'+cmd[i]
    elif a.startswith('--codegen='):a='-C'+a.split('=',1)[1]
    if a.startswith('-C') and '=' in a[2:]:
     key,value=a[2:].split('=',1);key=key.replace('_','-')
     if key in ('debug-assertions','overflow-checks') and value not in ('yes','on','true','y'):raise Error('unsafe_diagnostic_recipe')
     flags[key]=value
    i+=1
   if not {'debug-assertions','overflow-checks'}<=set(flags):raise Error('missing_checked_instrumentation')
   continue
  if cmd[0] not in ('gcc','g++','clang','clang++') or m['language'] not in ('c','c++'):
   raise Error('blocked_toolchain','v1 sanitizer certification requires explicit C/C++ compiler commands')
  if any(a.startswith(('-fno-sanitize','@')) for a in cmd):raise Error('unsafe_sanitizer_recipe')
  enabled=set()
  for a in cmd:
   if a.startswith('-fsanitize='):enabled.update(a.split('=',1)[1].split(','))
  if not {'address','undefined'}<=enabled:raise Error('missing_sanitizer_instrumentation')
 for d in m['dependencies']:
  if set(d)!={'soname','sha256','version','license','build_path'} or '/' in d['soname']:raise Error('invalid_dependency_lock')
 return m

def rust_toolchain():
 """Only the evaluator may configure a host compiler root; never the candidate."""
 value=os.environ.get('COMPRESSION_LAB_RUST_TOOLCHAIN')
 if not value or not Path(value).is_absolute():raise Error('blocked_toolchain','Set an evaluator-owned COMPRESSION_LAB_RUST_TOOLCHAIN')
 root=Path(value).resolve()
 if not (root/'bin/rustc').is_file() or not (root/'lib/rustlib').is_dir():raise Error('blocked_toolchain','Invalid configured Rust sysroot')
 return root

def rust_identity(limits=None):
 check_limits(limits);root=rust_toolchain();files=[]
 for base in (root/'bin',root/'lib'):
  for p in base.rglob('*'):
   # Include compiler drivers, metadata and target linkers, not just .rlib/.so.
   if p.is_file() and not p.is_relative_to(root/'lib/rustlib/src'):
    if not p.resolve().is_relative_to(root):raise Error('untrusted_rust_sysroot_link')
    files.append({'path':p.relative_to(root).as_posix(),'bytes':p.stat().st_size,'sha256':checked_sha(p,limits)})
 files.sort(key=lambda r:r['path'])
 return {'path':str(root/'bin/rustc'),'sha256':checked_sha(root/'bin/rustc',limits),'sysroot_digest':digest(files),'sysroot_files':files}

def check_tools(m,limits=None):
 check_limits(limits);known=libraries(limits);check_limits(limits)
 for name,row in m['toolchain'].items():
  p=rust_toolchain()/'bin/rustc' if name=='rustc' and m['schema_version']==2 else shutil.which(name,path='/usr/bin:/bin')
  if name not in TOOLS or not p or checked_sha(Path(p).resolve(),limits)!=row['sha256']:raise Error('blocked_toolchain',name)
  if name=='rustc' and m['schema_version']==2 and row.get('sysroot_digest')!=rust_identity(limits)['sysroot_digest']:raise Error('stale_rust_sysroot')
 for d in m['dependencies']:
  if d['soname'] not in known or Path(d['build_path']).absolute()!=known[d['soname']]:raise Error('untrusted_dependency_path',d['soname'])
  p=known[d['soname']]
  if not p.is_file() or checked_sha(p.resolve(),limits)!=d['sha256']:raise Error('dependency_digest_mismatch',d['soname'])

def libraries(limits=None):
 try:r=subprocess.run([shutil.which('ldconfig') or '/sbin/ldconfig','-p'],capture_output=True,text=True,timeout=execution_timeout(limits,5),env={'PATH':'/usr/sbin:/usr/bin:/sbin:/bin','LANG':'C'})
 except subprocess.TimeoutExpired as error:
  if limits is None:raise
  raise Error('timeout') from error
 check_limits(limits)
 out={}
 for line in r.stdout.splitlines():
  p=line.split()
  if len(p)>=4 and '=>' in p and 'x86-64' in line:out.setdefault(p[0],Path(p[-1]).resolve())
 return out

def elf(path):
 b=Path(path).read_bytes()
 if len(b)<64 or b[:6]!=b'\x7fELF\x02\x01':raise Error('not_native_elf')
 h=struct.unpack_from('<16sHHIQQQIHHHHHH',b)
 if h[2]!=62 or h[1] not in (2,3) or h[9]!=56 or h[10]>4096 or h[5]+h[9]*h[10]>len(b):raise Error('invalid_elf')
 loads=[];dyn=[];interp=None
 for i in range(h[10]):
  t,flags,off,vaddr,paddr,fs,ms,al=struct.unpack_from('<IIQQQQQQ',b,h[5]+i*56)
  if off+fs>len(b):raise Error('invalid_elf')
  if t==1:loads.append((vaddr,fs,off))
  if t==2:
   if fs%16:raise Error('invalid_elf')
   dyn=[struct.unpack_from('<qQ',b,j) for j in range(off,off+fs,16)]
  if t==3:interp=b[off:off+fs].rstrip(b'\0').decode('ascii')
 if not dyn:return {'needed':[],'interpreter':interp}
 d=dict(dyn);v=d.get(5);size=d.get(10,0);tables=[o+v-a for a,s,o in loads if v is not None and a<=v<a+s and v+size<=a+s]
 if not tables:raise Error('invalid_elf')
 tab=b[tables[0]:tables[0]+size]
 def string(n):
  end=tab.find(b'\0',n)
  if end<0 or n>=len(tab):raise Error('invalid_elf')
  return tab[n:end].decode('ascii')
 needed=[string(v) for k,v in dyn if k==1];rpaths=[string(v) for k,v in dyn if k in (15,29)]
 if any('/' in n or not n for n in needed) or any(p not in ('$ORIGIN/lib','$ORIGIN') for p in rpaths):raise Error('unsafe_elf_paths')
 return {'needed':needed,'interpreter':interp}

def closure(binary,m,sanitizer=False,limits=None):
 check_limits(limits);known=libraries(limits);decl={d['soname']:d for d in m['dependencies']};todo=list(binary) if isinstance(binary,(list,tuple)) else [binary];seen=set();rows=[];interp=None
 while todo:
  check_limits(limits);info=elf(todo.pop());check_limits(limits);interp=info['interpreter'] or interp
  for name in info['needed']:
   if name in seen:continue
   seen.add(name)
   if name not in known:raise Error('missing_native_dependency',name)
   p=known[name];h=checked_sha(p,limits);platform=name in PLATFORM or (sanitizer and name.startswith(('libasan.so','libubsan.so')))
   if not platform and (name not in decl or h!=decl[name]['sha256']):raise Error('undeclared_runtime_dependency',name)
   rows.append({'soname':name,'path':str(p),'sha256':h,'bytes':p.stat().st_size,'platform':platform,'version':decl.get(name,{}).get('version','hash-pinned platform'),'license':decl.get(name,{}).get('license','platform runtime license')});todo.append(p)
 if interp:
  if interp not in ('/lib64/ld-linux-x86-64.so.2','/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2','/usr/lib64/ld-linux-x86-64.so.2','/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2'):raise Error('untrusted_elf_interpreter')
  p=Path(interp).resolve()
  if str(p) not in [r['path'] for r in rows]:rows.append({'soname':p.name,'path':str(p),'sha256':checked_sha(p,limits),'bytes':p.stat().st_size,'platform':True,'version':'hash-pinned loader','license':'LGPL'})
 check_limits(limits)
 return {'libraries':rows,'interpreter':interp}

def runtime_mounts(dep,diagnostic=False,limits=None):
 check_limits(limits);out={};known=libraries(limits);check_limits(limits)
 for r in dep['libraries']:
  if r['platform']:
   allowed=r['soname'] in PLATFORM or (diagnostic and r['soname'].startswith(('libasan.so','libubsan.so')))
   if not allowed or r['soname'] not in known or str(known[r['soname']])!=r['path']:raise Error('untrusted_runtime_dependency')
   if checked_sha(known[r['soname']],limits)!=r['sha256'] or known[r['soname']].stat().st_size!=r['bytes']:raise Error('stale_runtime_dependency',r['soname'])
   out['/lib/'+r['soname']]=str(known[r['soname']])
 interp=dep['interpreter']
 if interp:
  if interp not in ('/lib64/ld-linux-x86-64.so.2','/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2','/usr/lib64/ld-linux-x86-64.so.2','/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2'):raise Error('untrusted_elf_interpreter')
  out[interp]=str(Path(interp).resolve())
 return out

def build(src,m,out,mode='required',sanitizer=False,limits=None,cancel=None,*,cpus=None):
 limits=dict(limits or {});cancel=cancel if cancel is not None else limits.get('_cancel');limits['_cancel']=cancel
 check_limits(limits);check_tools(m,limits);check_limits(limits);out=Path(out);out.mkdir();logs=[]
 native_sanitizer=sanitizer and diagnostic_kind(m)=='cxx-asan-ubsan-v1';ro={'/source':src}
 if m['language']=='rust':ro['/toolchain']=rust_toolchain()
 if native_sanitizer:
  policy='const char *__asan_default_options(void){return "detect_leaks=0:symbolize=0:abort_on_error=1:allocator_may_return_null=1";}\nconst char *__ubsan_default_options(void){return "halt_on_error=1:print_stacktrace=0";}\n'
  (out/'__lab_policy.c').write_text(policy)
  logs.append(execute(['gcc','-c','/output/__lab_policy.c','-o','/output/__lab_policy.o'],output=out,readonly={'/source':src},timeout=execution_timeout(limits),memory=limits.get('memory_bytes',2*1024**3),build=True,mode=mode,cancel=cancel,cpus=cpus));check_limits(limits)
 for cmd in m['sanitizer_build_commands' if sanitizer else 'build_commands']:
  check_limits(limits)
  args=[a.replace('{source}','/source').replace('{build}','/output') for a in cmd]
  if cmd[0]=='rustc':
   if any(a=='--sysroot' or a.startswith('--sysroot=') for a in args):raise Error('rust_sysroot_is_evaluator_owned')
   args[0]='/toolchain/bin/rustc';args+=['--sysroot','/toolchain']
  if native_sanitizer and any('/output/'+p in args for p in executables(m)):
   if cmd[0] in ('gcc','g++','clang','clang++'):args.append('/output/__lab_policy.o')
   elif cmd[0]=='rustc':args+=['-C','link-arg=/output/__lab_policy.o']
   else:raise Error('blocked_toolchain','v1 sanitizer policy injection requires a direct compiler link command')
  logs.append(execute(args,output=out,readonly=ro,build=True,timeout=execution_timeout(limits),memory=limits.get('memory_bytes',2*1024**3),mode=mode,cancel=cancel,cpus=cpus));check_limits(limits)
 if native_sanitizer:
  (out/'__lab_policy.c').unlink();(out/'__lab_policy.o').unlink()
 files=scan(out,m['build_output_paths'],limits);p=safe(out,m['executable']);elf(p);check_limits(limits)
 if not os.access(p,os.X_OK):raise Error('nonexecutable_binary')
 decoder=decoder_role(m);dp=safe(out,decoder['executable']);elf(dp);check_limits(limits)
 if not os.access(dp,os.X_OK):raise Error('nonexecutable_decoder')
 binaries=[safe(out,name) for name in executables(m)]
 if any(not os.access(binary,os.X_OK) for binary in binaries):raise Error('nonexecutable_binary')
 dep=closure(binaries,m,native_sanitizer,limits)
 dd=closure(dp,m,native_sanitizer,limits)
 if native_sanitizer:
  for role_dep in (closure(binary,m,True,limits) for binary in binaries):
   names={r['soname'] for r in role_dep['libraries']}
   if not any(n.startswith('libasan.so') for n in names) or not any(n.startswith('libubsan.so') for n in names):
    raise Error('blocked_toolchain','Every encoder and decoder diagnostic ELF must load pinned ASan and UBSan runtimes; static/other runtimes are unvalidated')
 return {'files':files,'dependencies':dep,'decoder_dependencies':dd,'diagnostic_kind':diagnostic_kind(m),'logs':logs}

def make_runtime(src,builddir,m,record,out,decoder=False,limits=None):
 check_limits(limits);out.mkdir()
 role=decoder_role(m) if decoder else m
 for name in set(role['artifact_paths']+role['runtime_paths']):
  p=out/name;p.parent.mkdir(parents=True,exist_ok=True);checked_copy(safe(src,name),p,limits)
 for name in role['build_output_paths']:
  p=out/name
  if p.exists():raise Error('runtime_path_collision')
  p.parent.mkdir(parents=True,exist_ok=True);checked_copy(safe(builddir,name),p,limits);p.chmod(0o555 if name in executables(m) else 0o444)
 (out/'lib').mkdir(exist_ok=True)
 for r in record['decoder_dependencies' if decoder else 'dependencies']['libraries']:
  if not r['platform']:checked_copy(r['path'],out/'lib'/r['soname'],limits)
 if decoder and m['schema_version']==2:
  if (out/DECODER_COMMAND_FILE).exists():raise Error('reserved_decoder_command_path')
  save(out/DECODER_COMMAND_FILE,decoder_commands(m),0o444)
 for p in out.rglob('*'):
  check_limits(limits)
  if p.is_file() and not p.stat().st_mode&0o111:p.chmod(0o444)

def source_zip(src,m,out,limits=None):
 names=(set(m['source_paths'])|{'candidate.json'})-set(m['artifact_paths'])-set(m['runtime_paths'])
 write_source_archive(src,names,out,limits)
 return {'rule':'zip-deflate-9-v1, fixed timestamp/mode, sorted paths','zlib_version':zlib.ZLIB_RUNTIME_VERSION,'bytes':out.stat().st_size,'sha256':checked_sha(out,limits)}

def validate_training(m,allowed_hashes,evaluation_mode='split'):
 kind=m.get('training',{}).get('kind')
 if kind in ('train-only','whole-dataset'):
  expected='whole-dataset' if evaluation_mode=='whole_dataset' else 'train-only'
  hashes=set(m['training'].get('object_sha256',[]))
  if kind!=expected or not hashes or not hashes<=set(allowed_hashes):raise Error('training_split_violation','Artifact provenance must match the declared fitting corpus')
 elif m['artifact_paths'] and kind!='data-independent':raise Error('missing_artifact_provenance')

def register(root,path,mode='required',limits=None,train_hashes=(),cache_info=None,*,evaluation_mode='split'):
 limits=limits or {};check_limits(limits)
 root=Path(root);path=Path(path);actual=secure_names(path);m=manifest(secure_load(path,'candidate.json'));names=set(m['source_paths']+m['artifact_paths']+m['runtime_paths'])|{'candidate.json'}
 check_limits(limits)
 if actual!=names:raise Error('undeclared_or_missing_files')
 validate_training(m,train_hashes,evaluation_mode)
 (root/'candidates').mkdir(exist_ok=True)
 with tempfile.TemporaryDirectory(prefix='.register-',dir=root/'candidates') as tmp:
  tmp=Path(tmp);src=tmp/'source';src.mkdir()
  for name in sorted(names):
   p=src/name;p.parent.mkdir(parents=True,exist_ok=True);copy_import(path,name,p,limits)
  check_limits(limits);save(src/'candidate.json',m,0o444);records=scan(src,names,limits)
  key=digest({'source_files':records,'runtime_digest':fingerprint(limits=limits)['runtime_digest'],'mode':mode,'zlib_version':zlib.ZLIB_RUNTIME_VERSION})
  check_limits(limits);cache=root/'registration-cache'/(key+'.json')
  if cache.exists():
   cid=load(cache)['candidate_digest'];_,cached=verify(root/'candidates'/cid,limits)
   if cached['source_files']!=records:raise Error('stale_registration_cache')
   if cache_info is not None:cache_info['hit']=True
   return cached
  b=build(src,m,tmp/'build',mode,limits=limits,cancel=limits.get('_cancel'));z=source_zip(src,m,tmp/'source.zip',limits);make_runtime(src,tmp/'build',m,b,tmp/'runtime',limits=limits)
  r={'source_digest':digest(records),'source_files':records,'build_files':b['files'],'dependencies':b['dependencies'],'source_package':z,'runtime_files':scan(tmp/'runtime',[p.relative_to(tmp/'runtime').as_posix() for p in (tmp/'runtime').rglob('*') if p.is_file()],limits)}
  if m['schema_version']==2:
   make_runtime(src,tmp/'build',m,b,tmp/'decoder-runtime',decoder=True,limits=limits)
   r.update(decoder_dependencies=b['decoder_dependencies'],decoder_runtime_files=scan(tmp/'decoder-runtime',[p.relative_to(tmp/'decoder-runtime').as_posix() for p in (tmp/'decoder-runtime').rglob('*') if p.is_file()],limits),diagnostic_kind=b['diagnostic_kind'])
  r['candidate_digest']=digest(r);dest=root/'candidates'/r['candidate_digest'];save(tmp/'registration.json',r);save(tmp/'build-log.json',b['logs']);shutil.rmtree(tmp/'build')
  for p in tmp.rglob('*'):
   check_limits(limits)
   if p.is_file() and not p.stat().st_mode&0o111:p.chmod(0o444)
  check_limits(limits)
  if dest.exists():verify(dest,limits)
  else:os.rename(tmp,dest)
  save(cache,{'candidate_digest':r['candidate_digest']})
  if cache_info is not None:cache_info['hit']=False
  return r

def verify(path,limits=None):
 check_limits(limits);path=Path(path);r=load(safe(path,'registration.json'));m=manifest(load(safe(path/'source','candidate.json')))
 if digest({k:v for k,v in r.items() if k!='candidate_digest'})!=r['candidate_digest'] or path.name!=r['candidate_digest']:raise Error('stale_candidate_digest')
 if scan(path/'source',set(m['source_paths']+m['artifact_paths']+m['runtime_paths'])|{'candidate.json'},limits)!=r['source_files']:raise Error('stale_source_digest')
 if scan(path/'runtime',[r['path'] for r in r['runtime_files']],limits)!=r['runtime_files']:raise Error('stale_binary_or_artifact_digest')
 if checked_sha(path/'source.zip',limits)!=r['source_package']['sha256']:raise Error('stale_source_package')
 if digest(r['source_files'])!=r['source_digest']:raise Error('stale_source_digest')
 if scan_selected(path/'runtime',m['build_output_paths'],limits)!=r['build_files']:raise Error('stale_build_inventory')
 check_tools(m,limits)
 binaries=[path/'runtime'/name for name in executables(m)]
 if closure(binaries,m,limits=limits)!=r['dependencies']:raise Error('stale_dependency_inventory')
 if m['schema_version']==2:
  role=decoder_role(m);expected=set(role['build_output_paths']+role['artifact_paths']+role['runtime_paths'])|{DECODER_COMMAND_FILE}|{'lib/'+d['soname'] for d in r['decoder_dependencies']['libraries'] if not d['platform']}
  if scan(path/'decoder-runtime',expected,limits)!=r['decoder_runtime_files']:raise Error('stale_decoder_runtime')
  if load(path/'decoder-runtime'/DECODER_COMMAND_FILE)!=decoder_commands(m):raise Error('stale_decoder_command')
  if scan_selected(path/'decoder-runtime',role['build_output_paths'],limits)!=[f for f in r['build_files'] if f['path'] in role['build_output_paths']]:raise Error('stale_decoder_build_inventory')
  if closure(path/'decoder-runtime'/role['executable'],m,limits=limits)!=r['decoder_dependencies']:raise Error('stale_decoder_dependency_inventory')
  for d in r['decoder_dependencies']['libraries']:
   if not d['platform']:
    p=path/'decoder-runtime/lib'/d['soname']
    if checked_sha(p,limits)!=d['sha256'] or p.stat().st_size!=d['bytes']:raise Error('stale_decoder_dependency_inventory')
  runtime_mounts(r['decoder_dependencies'],limits=limits)
 for dep in r['dependencies']['libraries']:
  if not dep['platform']:
   p=path/'runtime/lib'/dep['soname']
   if checked_sha(p,limits)!=dep['sha256'] or p.stat().st_size!=dep['bytes']:raise Error('stale_dependency_inventory')
 with tempfile.TemporaryDirectory() as t:
  z=source_zip(path/'source',m,Path(t)/'source.zip',limits)
  if z!=r['source_package']:raise Error('noncanonical_source_accounting')
 runtime_mounts(r['dependencies'],limits=limits);check_limits(limits);return m,r

def costs(m,r):
 files={f['path']:f for f in r['source_files']};fixed=set(m['artifact_paths']+m['runtime_paths']);deps={x['sha256']:x for x in r['dependencies']['libraries'] if not x['platform']}
 result={'packed_source_bytes':r['source_package']['bytes'],'raw_source_config_bytes':sum(v['bytes'] for k,v in files.items() if k not in fixed),'fixed_bytes':sum(files[p]['bytes'] for p in fixed),'config_bytes':files['candidate.json']['bytes'],'binary_bytes':sum(f['bytes'] for f in r['build_files']),'nonplatform_dependency_bytes':sum(v['bytes'] for v in deps.values()),'platform_dependency_bytes_reported_not_charged':sum(x['bytes'] for x in r['dependencies']['libraries'] if x['platform']),'dependency_inventory':r['dependencies'],'installed_runtime_policy':'Only libc, libm, libstdc++, libgcc and loader are assumed installed. They are hashed and reported. Other runtime libraries are shipped and charged.'}
 role=decoder_role(m);dd=r.get('decoder_dependencies',r['dependencies']);runtime_files=r.get('decoder_runtime_files',r['runtime_files'])
 artifacts=set(role['artifact_paths']+role['runtime_paths']);builds=set(role['build_output_paths'])
 result['decoder']={'compiled_decoder_bytes':sum(f['bytes'] for f in r['build_files'] if f['path'] in builds),'required_decoder_artifact_bytes':sum(files[p]['bytes'] for p in artifacts),'nonplatform_decoder_dependency_bytes':sum(d['bytes'] for d in dd['libraries'] if not d['platform']),'platform_dependency_bytes_reported_not_charged':sum(d['bytes'] for d in dd['libraries'] if d['platform']),'dependency_inventory':dd,'runtime_files':runtime_files,'executable':role['executable'],'runtime_directory':'decoder-runtime' if m['schema_version']==2 else 'runtime','role_separation':m['schema_version']==2,'combined_executable':role['executable']==m['executable']}
 command_bytes=sum(f['bytes'] for f in runtime_files if m['schema_version']==2 and f['path']==DECODER_COMMAND_FILE)
 result['decoder']['decoder_command_bytes']=command_bytes
 result['decoder']['required_decoder_artifact_bytes']+=command_bytes
 if sum(f['bytes'] for f in runtime_files)!=sum(result['decoder'][k] for k in ('compiled_decoder_bytes','required_decoder_artifact_bytes','nonplatform_decoder_dependency_bytes')):raise Error('decoder_accounting_inventory_mismatch')
 result['encoder_only_binary_bytes_reported_separately']=sum(f['bytes'] for f in r['build_files'] if f['path'] not in builds)
 result['encoder_only_artifact_bytes_reported_separately']=sum(files[p]['bytes'] for p in fixed-artifacts)
 return result
