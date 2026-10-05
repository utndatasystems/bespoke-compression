"""Trusted parent-death guard, executed before unshare (no Python site imports)."""
import ctypes,os,signal,sys
libc=ctypes.CDLL(None,use_errno=True)
parent=int(sys.argv[1])
if libc.prctl(1,signal.SIGKILL,0,0,0)!=0:raise OSError(ctypes.get_errno(),'PR_SET_PDEATHSIG')
if os.getppid()!=parent:raise SystemExit(125)
os.execv(sys.argv[2],sys.argv[2:])
