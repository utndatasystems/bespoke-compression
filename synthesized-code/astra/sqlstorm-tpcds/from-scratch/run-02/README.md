# SQLStorm TPC-DS · Run 2

Compression factor: **20.956476144×**. Decompression median: **46.491260 MB/s**; seven-trial range 45.894100–46.805206 MB/s. Package: 880,138 bytes from 18,444,591 original bytes.

Cyclic BWT, delayed half-front rank transform, zero-run coding and adaptive range coding. All construction occurs in lab_encode.

Online encoding median: 10.298799098 s. No separate fitted payload is needed; data-dependent construction is inside online encoding. Data-independent compiler time is separate.

Full byte reconstruction includes setup on pinned server CPU 8, sibling 32 excluded. Complete archive, custom decoder and non-exempt dependencies are charged; platform runtime and encoder-only artifacts are excluded by the sealed contract. No encoding-speed floor was commissioned. Source review, full original-input reconstruction, 16 diagnostics and reproducible builds passed. Exact-corpus and pinned-host results only; AVX support requirements follow the retained build.

`build-spec.json` contains exact commands using `/source` and `/output`; `source/interface/codec.h` defines the ABI. Sources are unchanged; `provenance.json` records hashes and result identities.
