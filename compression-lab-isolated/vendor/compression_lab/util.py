"""Bounded canonical I/O and durable single-writer state."""
from __future__ import annotations
import contextlib, hashlib, json, os, stat, tempfile, datetime, re
from pathlib import Path, PurePosixPath

class Error(ValueError):
    def __init__(self,code,message=''):
        self.code=code; super().__init__(message or code)

def canonical(x):return json.dumps(x,sort_keys=True,separators=(',',':'),ensure_ascii=True,allow_nan=False).encode()
def digest(x):return hashlib.sha256(canonical(x)).hexdigest()
def sha(p):
    h=hashlib.sha256()
    with Path(p).open('rb') as f:
        for b in iter(lambda:f.read(1048576),b''):h.update(b)
    return h.hexdigest()
def pairs(values):
    d={}
    for k,v in values:
        if k in d:raise Error('duplicate_json_key',k)
        d[k]=v
    return d
def load(p,limit=16*1048576):
    p=Path(p)
    if p.stat().st_size>limit:raise Error('oversized_json')
    return json.loads(p.read_text(),object_pairs_hook=pairs,parse_constant=lambda x:(_ for _ in ()).throw(Error('nonfinite_json')))
def fsync_dir(p):
    if os.name=='posix':
        fd=os.open(p,os.O_RDONLY|os.O_DIRECTORY)
        try:os.fsync(fd)
        finally:os.close(fd)
def atomic(p,data,mode=0o600):
    p=Path(p);p.parent.mkdir(parents=True,exist_ok=True)
    fd,tmp=tempfile.mkstemp(prefix='.'+p.name,dir=p.parent)
    try:
        os.fchmod(fd,mode)
        with os.fdopen(fd,'wb') as f:f.write(data);f.flush();os.fsync(f.fileno())
        os.replace(tmp,p);fsync_dir(p.parent)
    finally:
        if os.path.exists(tmp):os.unlink(tmp)
def save(p,x,mode=0o600):atomic(p,canonical(x)+b'\n',mode)
def rel(s):
    if not isinstance(s,str) or not s or '\x00' in s or '\\' in s:raise Error('unsafe_path')
    p=PurePosixPath(s)
    if p.is_absolute() or str(p)!=s or any(x in ('','..','.') for x in p.parts):raise Error('unsafe_path',s)
    return s
def safe(root,name,exists=True):
    p=Path(root).absolute();rel(name)
    for part in [None,*PurePosixPath(name).parts]:
        if part:p=p/part
        if p.is_symlink():raise Error('symlink_path',name)
    if exists:
        st=p.stat()
        if not stat.S_ISREG(st.st_mode) or st.st_nlink!=1:raise Error('nonregular_file',name)
    return p
@contextlib.contextmanager
def lock(p,nonblocking=False,host=False):
    if os.name!='posix':raise Error('unsupported_lock_platform','Mutating evaluation state currently requires POSIX')
    import fcntl
    p=Path(p);p.parent.mkdir(parents=True,exist_ok=True)
    old=os.umask(0)
    try:fd=os.open(p,os.O_RDWR|os.O_CREAT|getattr(os,'O_NOFOLLOW',0),0o666 if host else 0o600)
    finally:os.umask(old)
    try:
        st=os.fstat(fd)
        if not stat.S_ISREG(st.st_mode) or st.st_nlink!=1:raise Error('unsafe_lock')
        try:fcntl.flock(fd,fcntl.LOCK_EX|(fcntl.LOCK_NB if nonblocking else 0))
        except BlockingIOError:raise Error('busy')
        yield
    finally:fcntl.flock(fd,fcntl.LOCK_UN);os.close(fd)
def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def ident(s):
    if not isinstance(s,str) or not re.fullmatch(r'[a-zA-Z0-9][a-zA-Z0-9_-]{0,127}',s):raise Error('invalid_id')
    return s
def reply(run_id=None,status='ok',**kw):
    d={'schema_version':1,'run_id':run_id,'job_id':None,'candidate_digest':None,'status':status,'reason_codes':[],'metrics':{},'artifacts':{}}
    d.update(kw);return d

NEXT={'created':{'public_search'},'public_search':{'public_search','public_ready','cancelled','failed'},'public_ready':{'public_search','public_ready','frozen','cancelled','failed'},'frozen':{'private_started','cancelled'},'private_started':{'private_started','complete','failed','cancelled'},'complete':set(),'failed':set(),'cancelled':set()}
class Ledger:
    """Append-first journal is authoritative; state.json is a recoverable cache."""
    def __init__(self,root):self.root=Path(root);self.journal=self.root/'events.jsonl'
    def _read(self):
        raw=self.journal.read_bytes();prev='0'*64;state=None;n=0;good=0
        for line in raw.splitlines(keepends=True):
            if not line.endswith(b'\n'):break # interrupted final append only
            try:e=json.loads(line);h=e.pop('event_hash')
            except (ValueError,KeyError):raise Error('journal_corrupt')
            if e['event_id']!=n+1 or e['previous_hash']!=prev or digest(e)!=h or e['previous_state']!=(state or {}).get('stage'):raise Error('journal_corrupt')
            state=e['state'];prev=h;n+=1;good+=len(line)
        if state is None:raise Error('journal_empty')
        if good<len(raw):
            with self.journal.open('r+b') as f:f.truncate(good);f.flush();os.fsync(f.fileno())
        save(self.root/'state.json',state);return state,n,prev
    def read(self):
        with lock(self.root/'state.lock'):return self._read()[0]
    def _append(self,s,old,n,prev,op):
        e={'event_id':n+1,'previous_hash':prev,'previous_state':(old or {}).get('stage'),'timestamp':now(),'operation':op,'candidate_digest':s.get('candidate_digest'),'card_digest':s.get('card_digest'),'runtime_digest':s.get('runtime_digest'),'result_ref':s.get('result_id'),'state':s}
        e['event_hash']=digest(e)
        with self.journal.open('ab') as f:f.write(canonical(e)+b'\n');f.flush();os.fsync(f.fileno())
        fsync_dir(self.root);save(self.root/'state.json',s);return s
    def create(self,s):
        self.root.mkdir(parents=True,exist_ok=True)
        with lock(self.root/'state.lock'):
            if self.journal.exists():raise Error('already_initialized')
            return self._append(s,None,0,'0'*64,'create')
    def update(self,op,fn):
        with lock(self.root/'state.lock'):
            old,n,h=self._read();s=fn(dict(old))
            if old['stage'] in ('complete','failed','cancelled') and not(op=='disclose' and s['stage']==old['stage']):raise Error('terminal_latch')
            if s['stage']!=old['stage'] and s['stage'] not in NEXT[old['stage']]:raise Error('invalid_transition')
            return self._append(s,old,n,h,op)
    def transition(self,stage,**kw):
        def fn(s):s.update(kw);s['stage']=stage;return s
        return self.update('transition:'+stage,fn)

@contextlib.contextmanager
def secure_directory(path):
    """Open an absolute directory through no-follow descriptors."""
    parts=Path(path).absolute().parts[1:]
    if '..' in parts:raise Error('unsafe_import_directory')
    fd=os.open('/',os.O_RDONLY|os.O_DIRECTORY)
    try:
        for part in parts:
            try:new=os.open(part,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW,dir_fd=fd)
            except OSError as ex:raise Error('unsafe_import_directory',str(ex)) from ex
            os.close(fd);fd=new
        yield fd
    finally:os.close(fd)

def secure_names(root,owner_uid=None,max_files=20000,max_bytes=512*1048576):
    """Inventory metadata without following links or opening file contents."""
    names=set();total=0;entries=0
    def walk(fd,prefix='',depth=0):
        nonlocal total,entries
        if depth>64:raise Error('oversized_import_tree')
        if owner_uid is not None and os.fstat(fd).st_uid!=owner_uid:raise Error('foreign_owned_import')
        for name in sorted(os.listdir(fd)):
            rel(name);st=os.stat(name,dir_fd=fd,follow_symlinks=False);entries+=1
            if max_files is not None and entries>2*max_files:raise Error('oversized_import_tree')
            if owner_uid is not None and st.st_uid!=owner_uid:raise Error('foreign_owned_import')
            if stat.S_ISDIR(st.st_mode):
                child=os.open(name,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW,dir_fd=fd)
                try:walk(child,prefix+name+'/',depth+1)
                finally:os.close(child)
            elif stat.S_ISREG(st.st_mode) and st.st_nlink==1:
                total+=st.st_size;names.add(prefix+name)
                if (max_files is not None and len(names)>max_files) or (max_bytes is not None and total>max_bytes):raise Error('oversized_import_tree')
            else:raise Error('unsafe_import_file',prefix+name)
    with secure_directory(root) as fd:walk(fd)
    return names

def secure_load(root,name,limit=16*1048576):
    return json.loads(secure_read(root,name,limit),object_pairs_hook=pairs,
                      parse_constant=lambda x:(_ for _ in ()).throw(Error('nonfinite_json')))

def secure_read(root,name,limit=512*1048576):
    """Descriptor-relative no-symlink read for importing agent-owned snapshots.

    Every parent is opened with O_NOFOLLOW, not just inspected before a later
    open. Concurrent edits produce a digest mismatch, never a private-path read.
    """
    rel(name);path=Path(root)/name
    with secure_directory(path.parent) as fd:
        f=os.open(path.name,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK,dir_fd=fd)
        try:
            st=os.fstat(f)
            if not stat.S_ISREG(st.st_mode) or st.st_nlink!=1 or st.st_size>limit:raise Error('unsafe_import_file')
            chunks=[];size=0
            while True:
                b=os.read(f,min(1048576,limit-size+1))
                if not b:break
                size+=len(b)
                if size>limit:raise Error('oversized_import')
                chunks.append(b)
            return b''.join(chunks)
        finally:os.close(f)
