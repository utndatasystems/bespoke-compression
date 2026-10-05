"""Landlock and seccomp launcher for servers that disable user namespaces.

Policy is owner-generated. No filesystem fallback and no provider credentials.
"""
import ctypes
import errno
import json
import os
from pathlib import Path
import resource
import sys


def restrict(read_paths, output, *, cpu, sanitizer=False):
    from compression_lab._sandbox import filter_setup
    library,context=filter_setup(False,1,pin_affinity=True)
    libc=ctypes.CDLL(None,use_errno=True)
    abi=libc.syscall(444,0,0,1)
    if abi < 3:
        raise RuntimeError('Landlock ABI 3 or later required')
    # Execute, read/write, directory creation/removal, refer and truncate.
    handled=(1<<15)-1
    class Ruleset(ctypes.Structure):
        _fields_=[('handled_access_fs',ctypes.c_uint64)]
    class PathRule(ctypes.Structure):
        _pack_=1
        _fields_=[('allowed_access',ctypes.c_uint64),('parent_fd',ctypes.c_int32)]
    fd=libc.syscall(444,ctypes.byref(Ruleset(handled)),ctypes.sizeof(Ruleset),0)
    if fd<0:raise OSError(ctypes.get_errno(),'landlock_create_ruleset')
    try:
        for path,write in [(p,False) for p in read_paths]+[(str(output),True)]:
            path=Path(path).resolve(strict=True)
            access=handled if write else (1|4|8 if path.is_dir() else 1|4)
            pfd=os.open(path,os.O_PATH|os.O_CLOEXEC)
            try:
                rule=PathRule(access,pfd)
                if libc.syscall(445,fd,1,ctypes.byref(rule),0):
                    raise OSError(ctypes.get_errno(),'landlock_add_rule')
            finally:os.close(pfd)
        if libc.prctl(38,1,0,0,0) or libc.syscall(446,fd,0):
            raise OSError(ctypes.get_errno(),'landlock_restrict_self')
    finally:os.close(fd)
    os.sched_setaffinity(0,{cpu})
    # Prevent same-UID interference with owner/controller processes.
    for name in ('kill','tkill','tgkill','pidfd_open','pidfd_getfd','pidfd_send_signal'):
        number=library.seccomp_syscall_resolve_name(name.encode())
        if number>=0 and library.seccomp_rule_add(context,0x50000|errno.EPERM,number,0):
            raise RuntimeError('seccomp_signal_rule')
    if library.seccomp_load(context):raise OSError(ctypes.get_errno(),'seccomp_load')
    library.seccomp_release(context)
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    inherited=resource.getrlimit(resource.RLIMIT_FSIZE)[1]
    maximum=min(512*1024**2,inherited) if inherited>=0 else 512*1024**2
    resource.setrlimit(resource.RLIMIT_FSIZE,(maximum,maximum))
    if not sanitizer:resource.setrlimit(resource.RLIMIT_AS,(2*1024**3,2*1024**3))


def main():
    spec=json.loads(Path(sys.argv[1]).read_text())
    reads=list(spec['reads'])
    if spec.get('sanitizer'):
        reads.extend(['/proc/self/maps','/proc/self/stat','/proc/self/status','/proc/meminfo'])
    restrict(reads,spec['output'],cpu=spec['cpu'],sanitizer=spec.get('sanitizer',False))
    environment={'PATH':'/usr/bin:/bin','LANG':'C','LC_ALL':'C','HOME':'/nonexistent',
                 'LD_LIBRARY_PATH':spec['library_path'],
                 'ASAN_OPTIONS':'detect_leaks=0:abort_on_error=1:symbolize=0',
                 'UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=0',
                 'OMP_NUM_THREADS':'1','OPENBLAS_NUM_THREADS':'1'}
    os.execve(spec['argv'][0],spec['argv'],environment)


if __name__=='__main__':main()
