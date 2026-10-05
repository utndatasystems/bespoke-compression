# SQLStorm TPC-DS · Run 1

Compression factor: **5.888799316×**. Decompression median: **12182.745231 MB/s**; seven-trial range 11352.194170–12464.953410 MB/s. Package: 3,132,148 bytes from 18,444,591 original bytes.

RePair phrase construction and DP reparsing during lab_encode; independent SIMD reconstruction from a compact phrase pool and 24-bit commands.

Online encoding median: 16.016193255 s. No separate fitted payload is needed; data-dependent construction is inside online encoding. Data-independent compiler time is separate.

Full byte reconstruction includes setup on pinned server CPU 8, sibling 32 excluded. Complete archive, custom decoder and non-exempt dependencies are charged; platform runtime and encoder-only artifacts are excluded by the sealed contract. No encoding-speed floor was commissioned. Source review, full original-input reconstruction, 16 diagnostics and reproducible builds passed. Exact-corpus and pinned-host results only; AVX support requirements follow the retained build.

`build-spec.json` contains exact commands using `/source` and `/output`; `source/interface/codec.h` defines the ABI. Sources are unchanged; `provenance.json` records hashes and result identities.
