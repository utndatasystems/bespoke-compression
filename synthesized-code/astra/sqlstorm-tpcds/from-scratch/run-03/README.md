# SQLStorm TPC-DS · Run 3

Compression factor: **11.172290289×**. Decompression median: **6656.041904 MB/s**; seven-trial range 6475.244323–6846.305660 MB/s. Package: 1,650,923 bytes from 18,444,591 original bytes.

Byte-pair grammar with 24000 nodes, 64-byte maximum phrases and 15-bit token stream; independent AVX2 decoder. Grammar construction occurs in lab_encode.

Online encoding median: 20.915586540 s. No separate fitted payload is needed; data-dependent construction is inside online encoding. Data-independent compiler time is separate.

Full byte reconstruction includes setup on pinned server CPU 8, sibling 32 excluded. Complete archive, custom decoder and non-exempt dependencies are charged; platform runtime and encoder-only artifacts are excluded by the sealed contract. No encoding-speed floor was commissioned. Source review, full original-input reconstruction, 16 diagnostics and reproducible builds passed. Exact-corpus and pinned-host results only; AVX support requirements follow the retained build.

`build-spec.json` contains exact commands using `/source` and `/output`; `source/interface/codec.h` defines the ABI. Sources are unchanged; `provenance.json` records hashes and result identities.
