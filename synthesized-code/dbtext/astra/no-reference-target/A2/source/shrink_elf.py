import pathlib,struct,sys
# Keep every byte referenced by an ELF program segment. Section headers and
# their string table are link-time metadata, unused by the runtime loader.
for name in sys.argv[1:]:
    p=pathlib.Path(name); data=bytearray(p.read_bytes())
    if data[:6]!=b'\x7fELF\x02\x01': raise ValueError("Expected little-endian ELF64")
    phoff=struct.unpack_from('<Q',data,32)[0]
    ents,num=struct.unpack_from('<HH',data,54)
    if ents!=56 or not num: raise ValueError("Unexpected program headers")
    end=max(64,phoff+ents*num)
    for i in range(num):
        q=phoff+i*ents
        off=struct.unpack_from('<Q',data,q+8)[0]
        size=struct.unpack_from('<Q',data,q+32)[0]
        end=max(end,off+size)
    if end>len(data): raise ValueError("Truncated segment")
    struct.pack_into('<Q',data,40,0)
    struct.pack_into('<HHH',data,58,0,0,0)
    p.write_bytes(data[:end])
