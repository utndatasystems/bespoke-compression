
from pathlib import Path
import struct,sys
p=Path(sys.argv[1]).read_bytes()
n,ns,nl=struct.unpack_from('<QQQ',p)
assert len(p)==24+ns*12+nl
Path(sys.argv[2]).write_bytes(struct.pack('<QQ',n,ns)+p[24:24+ns*12])
