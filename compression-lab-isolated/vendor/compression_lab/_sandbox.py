"""Trusted Linux-only namespace launcher. No host /proc, /etc, HOME or /sys."""
import ctypes,errno,json,os,resource,sys
libc=ctypes.CDLL(None,use_errno=True)
libc.mount.argtypes=[ctypes.c_char_p,ctypes.c_char_p,ctypes.c_char_p,ctypes.c_ulong,ctypes.c_char_p]
def mount(src,dst,kind=None,flags=0,data=None):
 b=lambda x:x.encode() if isinstance(x,str) else x
 if libc.mount(b(src),b(dst),b(kind),flags,b(data)):raise OSError(ctypes.get_errno(),os.strerror(ctypes.get_errno()),dst)
def bind(src,dst,ro=True):
 if os.path.isdir(src):os.makedirs(dst,exist_ok=True)
 else:
  os.makedirs(os.path.dirname(dst),exist_ok=True)
  with open(dst,'wb'):pass
 mount(src,dst,flags=4096|16384)
 if ro:mount(None,dst,flags=4096|32|1|2|4)
def diagnostic_maps(root):
 # ASan discovers the main stack through live maps, including exception unwind.
 # This proc belongs to our new PID namespace. Retain only this process's maps,
 # then unmount the full proc before exec; no PID directories, fd or environ.
 private=root+'/.diagnostic-proc';os.mkdir(private)
 mount('proc',private,'proc',2|4|8)
 target=root+'/proc/self/maps';os.makedirs(os.path.dirname(target),exist_ok=True)
 with open(target,'wb'):pass
 mount(private+'/self/maps',target,flags=4096)
 # Preserve proc's nosuid/nodev/noexec flags when making the file read-only.
 mount(None,target,flags=4096|32|1|2|4|8)
 if libc.umount2(private.encode(),0):raise OSError(ctypes.get_errno(),'private proc unmount')
 os.rmdir(private)
def filter_setup(fork_allowed,threads=1,*,pin_affinity=False):
 # Mutable sessions call this with only False: their no-clone contract remains.
 lib=ctypes.CDLL('libseccomp.so.2',use_errno=True)
 lib.seccomp_init.argtypes=[ctypes.c_uint32];lib.seccomp_init.restype=ctypes.c_void_p
 lib.seccomp_syscall_resolve_name.argtypes=[ctypes.c_char_p];lib.seccomp_syscall_resolve_name.restype=ctypes.c_int
 lib.seccomp_rule_add.argtypes=[ctypes.c_void_p,ctypes.c_uint32,ctypes.c_int,ctypes.c_uint]
 class Compare(ctypes.Structure):
  _fields_=[('arg',ctypes.c_uint),('op',ctypes.c_uint),('datum_a',ctypes.c_uint64),('datum_b',ctypes.c_uint64)]
 lib.seccomp_rule_add_array.argtypes=[ctypes.c_void_p,ctypes.c_uint32,ctypes.c_int,ctypes.c_uint,ctypes.POINTER(Compare)]
 lib.seccomp_load.argtypes=[ctypes.c_void_p];lib.seccomp_release.argtypes=[ctypes.c_void_p]
 ctx=lib.seccomp_init(0x7fff0000)
 if not ctx:raise RuntimeError('seccomp_init')
 denied='socket socketpair connect accept accept4 bind listen mount umount2 pivot_root setns unshare ptrace process_vm_readv process_vm_writev bpf perf_event_open reboot kexec_load kexec_file_load open_by_handle_at name_to_handle_at userfaultfd keyctl add_key request_key io_uring_setup io_uring_enter io_uring_register'.split()
 if not fork_allowed:
  denied+='fork vfork'.split()
  if pin_affinity:denied.append('sched_setaffinity')
  if threads==1:denied+='clone clone3'.split()
 for s in denied:
  n=lib.seccomp_syscall_resolve_name(s.encode())
  if n>=0 and lib.seccomp_rule_add(ctx,0x50000|errno.EPERM,n,0):raise RuntimeError('seccomp_rule')
 if not fork_allowed and threads>1:
  # clone3's flags live behind a pointer, which classic seccomp cannot inspect.
  # ENOSYS is the libc-supported fallback to the inspectable legacy clone ABI.
  n=lib.seccomp_syscall_resolve_name(b'clone3')
  if n>=0 and lib.seccomp_rule_add(ctx,0x50000|errno.ENOSYS,n,0):raise RuntimeError('seccomp_clone3')
  n=lib.seccomp_syscall_resolve_name(b'clone')
  required=0x100|0x800|0x10000 # VM, SIGHAND, THREAD: one shared address space/TGID
  allowed=required|0x200|0x400|0x40000|0x80000|0x100000|0x200000|0x400000|0x1000000
  # Reject missing required flags and every bit outside the pthread allowlist,
  # including the exit signal, all namespace bits and unknown future flags.
  for bit in range(64):
   mask=1<<bit
   if required&mask or not allowed&mask:
    comparison=Compare(0,7,mask,0 if required&mask else mask) # SCMP_CMP_MASKED_EQ
    if lib.seccomp_rule_add_array(ctx,0x50000|errno.EPERM,n,1,ctypes.byref(comparison)):raise RuntimeError('seccomp_clone_flags')
 return lib,ctx
def drop_caps():
 class H(ctypes.Structure):_fields_=[('version',ctypes.c_uint32),('pid',ctypes.c_int)]
 class D(ctypes.Structure):_fields_=[('effective',ctypes.c_uint32),('permitted',ctypes.c_uint32),('inheritable',ctypes.c_uint32)]
 for i in range(41):libc.prctl(24,i,0,0,0)
 if libc.capset(ctypes.byref(H(0x20080522,0)),(D*2)()):raise OSError(ctypes.get_errno(),'capset')
 if libc.prctl(38,1,0,0,0):raise OSError(ctypes.get_errno(),'no_new_privs')
def main():
 with open(sys.argv[1]) as f:c=json.load(f)
 threads=c.get('threads',1);cpus=c.get('cpus',[c.get('cpu',0)])
 lib,ctx=filter_setup(c['build'],threads,pin_affinity=True);root=c['root'];os.makedirs(root)
 mount(None,'/',flags=16384|(1<<18));mount('tmpfs',root,'tmpfs',2|4,f'size={max(16*1024**2,min(c["memory"]//4,256*1024**2))},mode=755')
 if c['runtime_files'] is None:
  for p in ('/usr','/lib','/lib64','/bin','/sbin'):
   if not os.path.exists(p):continue
   if os.path.islink(p):os.symlink(os.readlink(p),root+p)
   else:bind(p,root+p)
 else:
  for dst,src in c['runtime_files'].items():bind(src,root+dst)
 for p in ('tmp','proc','sys','dev','input','source','candidate','output'):os.makedirs(root+'/'+p,exist_ok=True)
 os.chmod(root+'/tmp',0o1777)
 for p in ('null','zero','urandom','random'):bind('/dev/'+p,root+'/dev/'+p,False)
 for dst,src in c['readonly'].items():bind(src,root+dst)
 bind(c['output'],root+'/output',False)
 if c['sanitizer']:diagnostic_maps(root)
 os.chroot(root);os.chdir('/output');os.sched_setaffinity(0,set(cpus))
 # NPROC also counts the unshare waiter and kernel task cleanup after join.
 # An exact threads+1 ceiling rejects successive legal pthread pools. Keep a
 # finite emergency ceiling; the runner checks the declared live thread count.
 nproc=128 if c['build'] or threads>1 else 1
 for res,val in ((resource.RLIMIT_CORE,0),(resource.RLIMIT_FSIZE,c['output_limit']),(resource.RLIMIT_NOFILE,64),(resource.RLIMIT_NPROC,nproc)):resource.setrlimit(res,(val,val))
 if not c['sanitizer']:resource.setrlimit(resource.RLIMIT_AS,(c['memory'],c['memory']))
 drop_caps()
 if lib.seccomp_load(ctx):raise OSError(ctypes.get_errno(),'seccomp_load')
 lib.seccomp_release(ctx)
 env={'PATH':'/usr/bin:/bin','HOME':'/tmp','TMPDIR':'/tmp','LANG':'C','LC_ALL':'C','TZ':'UTC','SOURCE_DATE_EPOCH':'0','OMP_NUM_THREADS':'1','OPENBLAS_NUM_THREADS':'1','MKL_NUM_THREADS':'1','RAYON_NUM_THREADS':'1','ASAN_OPTIONS':'abort_on_error=1:detect_leaks=0:allocator_may_return_null=1:symbolize=0','UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'}
 # Kept inline: this trusted helper runs with -I -S and then in a minimal chroot.
 for name in ('COMPRESSION_LAB_THREADS','OMP_NUM_THREADS','OMP_THREAD_LIMIT','OPENBLAS_NUM_THREADS','MKL_NUM_THREADS','BLIS_NUM_THREADS','VECLIB_MAXIMUM_THREADS','NUMEXPR_NUM_THREADS','RAYON_NUM_THREADS'):env[name]=str(threads)
 if c['build'] and '/toolchain' in c['readonly']:env['LD_LIBRARY_PATH']='/toolchain/lib'
 if c['runtime_files'] is not None:env['LD_LIBRARY_PATH']='/candidate/lib:/lib'
 # execvpe imports warnings lazily for PATH lookup. The host interpreter's
 # standard library is intentionally absent from a minimal runtime chroot.
 executable=c['argv'][0]
 if '/' not in executable:
  executable=next((p+'/'+executable for p in ('/usr/bin','/bin') if os.access(p+'/'+executable,os.X_OK)),None)
  if executable is None:raise FileNotFoundError(c['argv'][0])
 os.execve(executable,c['argv'],env)
if __name__=='__main__':
 try:main()
 except BaseException as e:print('sandbox_setup_failed: '+str(e),file=sys.stderr);sys.exit(125)
