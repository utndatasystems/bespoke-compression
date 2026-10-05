"""Server research/build filesystem: PRoot paths plus mandatory Landlock access.

PRoot provides the reviewed virtual paths; Landlock, not PRoot, enforces access.
This wrapper is never in the measured native decoder operation.
"""
import ctypes
import errno
import json
import os
from pathlib import Path
import resource
import shutil
import struct
import sys
import uuid

from . import PROJECT

def proot_path():
    return Path(os.environ.get('COMPRESSION_LAB_PROOT') or shutil.which('proot') or '/usr/bin/proot').resolve()

def extract_loader(proot, target):
    """Relocate PRoot's embedded x86-64 loader into the owner policy directory.

    PRoot 5.1 otherwise writes its loader into the host /tmp, which is outside
    the Landlock write policy. No host-wide temporary-directory access is needed.
    """
    binary=Path(proot).read_bytes(); offset=1
    while (offset:=binary.find(b'\x7fELF',offset)) >= 0:
        if binary[offset+4:offset+7]==b'\x02\x01\x01' and struct.unpack_from('<H',binary,offset+18)[0]==62:
            end=struct.unpack_from('<Q',binary,offset+40)[0]
            size,count=struct.unpack_from('<HH',binary,offset+58)
            length=end+size*count
            if end >= 64 and size==64 and count>0 and offset+length<=len(binary):
                target.write_bytes(binary[offset:offset+length]);target.chmod(0o555);return
        offset+=4
    raise RuntimeError('Unsupported PRoot package: embedded x86-64 loader not found')

SYSTEM = ['/usr', '/bin', '/sbin', '/lib', '/lib64', '/etc/ld.so.cache',
          '/etc/resolv.conf', '/etc/hosts', '/etc/nsswitch.conf', '/etc/ssl/certs',
          '/etc/passwd', '/etc/group', '/proc/cpuinfo', '/proc/meminfo',
          '/sys/devices/system/cpu', '/dev/null', '/dev/zero', '/dev/urandom', '/dev/random']

def landlock(reads, writes):
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.syscall(444, 0, 0, 1) < 3:
        raise RuntimeError('Landlock ABI >= 3 is required')
    handled = (1 << 15) - 1
    class Ruleset(ctypes.Structure):
        _fields_ = [('handled_access_fs', ctypes.c_uint64)]
    class Rule(ctypes.Structure):
        _pack_ = 1
        _fields_ = [('allowed_access', ctypes.c_uint64), ('parent_fd', ctypes.c_int32)]
    fd = libc.syscall(444, ctypes.byref(Ruleset(handled)), ctypes.sizeof(Ruleset), 0)
    if fd < 0: raise OSError(ctypes.get_errno(), 'landlock_create')
    try:
        for item, write in [(x, False) for x in reads] + [(x, True) for x in writes]:
            p = Path(item).resolve(strict=True)
            access = handled if p.is_dir() and write else (1|2|4|(1<<14) if write else 1|4|(8 if p.is_dir() else 0))
            pfd = os.open(p, os.O_PATH | os.O_CLOEXEC)
            try:
                if libc.syscall(445, fd, 1, ctypes.byref(Rule(access, pfd)), 0):
                    raise OSError(ctypes.get_errno(), 'landlock_add')
            finally: os.close(pfd)
        if libc.prctl(38, 1, 0, 0, 0) or libc.syscall(446, fd, 0):
            raise OSError(ctypes.get_errno(), 'landlock_restrict')
    finally: os.close(fd)

def no_network():
    sec = ctypes.CDLL('libseccomp.so.2', use_errno=True)
    sec.seccomp_init.argtypes=[ctypes.c_uint32];sec.seccomp_init.restype=ctypes.c_void_p
    sec.seccomp_rule_add.argtypes=[ctypes.c_void_p,ctypes.c_uint32,ctypes.c_int,ctypes.c_uint]
    sec.seccomp_arch_add.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
    sec.seccomp_load.argtypes=[ctypes.c_void_p];sec.seccomp_release.argtypes=[ctypes.c_void_p]
    ctx=sec.seccomp_init(0x7fff0000)
    for arch in (0x40000003,0x4000003e):
        if sec.seccomp_arch_add(ctx,arch):raise RuntimeError('seccomp_compat_arch')
    for call in (b'socket',b'connect',b'bind',b'listen',b'accept',b'accept4'):
        number=sec.seccomp_syscall_resolve_name(call)
        if number >= 0 and sec.seccomp_rule_add(ctx,0x50000|errno.EPERM,number,0):
            raise RuntimeError('seccomp_network_rule')
    if sec.seccomp_load(ctx): raise RuntimeError('seccomp_network_load')
    sec.seccomp_release(ctx)

def command(policy_dir, argv, mounts, *, writable, cwd, cpus, network=True, env=None, proot=None):
    directory=Path(policy_dir)/uuid.uuid4().hex
    directory.mkdir(parents=True)
    executable=Path(proot or proot_path())
    extract_loader(executable,directory/'loader')
    spec=dict(argv=argv,mounts={k:str(v) for k,v in mounts.items()},writable=writable,
              cwd=cwd,cpus=cpus,network=network,env=env or {},proot=str(executable))
    (directory/'policy.json').write_text(json.dumps(spec))
    return [sys.executable,str(PROJECT/'server-shell-entry.py'),str(directory/'policy.json')]

def main():
    policy=Path(sys.argv[1]);spec=json.loads(policy.read_text())
    proot=Path(spec['proot'])
    root=policy.parent/'root';tmp=policy.parent/'tmp'
    root.mkdir();tmp.mkdir()
    mounts={p:p for p in SYSTEM if Path(p).exists()}
    mounts.update(spec['mounts']);mounts['/tmp']=str(tmp)
    mounts[str(tmp)]=str(tmp)
    for guest,host in mounts.items():
        dest=root/guest.lstrip('/');dest.parent.mkdir(parents=True,exist_ok=True)
        if Path(host).is_dir():dest.mkdir(exist_ok=True)
        else:dest.touch(exist_ok=True)
    libraries=proot.parent.parent/'lib/x86_64-linux-gnu'
    loader=policy.parent/'loader'
    reads=list(mounts.values())+[str(proot),str(root),str(loader)]
    if libraries.is_dir(): reads.append(str(libraries))
    writes=[mounts[p] for p in spec['writable']]+[str(tmp),'/dev/null','/dev/zero']
    env={'PATH':'/work/bin:/usr/local/bin:/usr/bin:/bin','HOME':'/work',
         'LANG':'C.UTF-8','TMPDIR':str(tmp),
         'PROOT_LOADER':str(loader),
         'PROOT_NO_SECCOMP':'1',
         'LD_LIBRARY_PATH':str(libraries)}
    env.update(spec['env'])
    argv=[str(proot),'-r',str(root),'-w',spec['cwd']]
    for guest,host in mounts.items():argv+=['-b',str(Path(host).resolve())+':'+guest]
    argv+=spec['argv']
    os.sched_setaffinity(0,set(spec['cpus']))
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    landlock(reads,writes)
    if not spec['network']:no_network()
    os.execve(proot,argv,env)
