# Yelp · GLM G1

Maximum decompression throughput. Solo GLM-5.3-Flash, Max, tools allowed.

Compression factor **5.857293138×**; decompression median **7121.825640 MB/s**. Seven-trial range: 7004.165497–7244.044736 MB/s. Charged package 17,072,816 bytes; strict deployment total 17,072,816 bytes. Target met.

Source-reviewed specialized reconstruction; implementation is preserved below.

Encoding median: 0.459170859 s per complete original input, including preprocessing performed inside lab_encode; data-independent compilation is separate. The sealed experiment has no encoding-speed floor. No new end-to-end fitting-cost claim is made.

Full input: 100,000,488 bytes. Decompression includes fresh setup and complete byte reconstruction on pinned server CPU8 with sibling32 excluded. All payloads, fitted dictionaries, framing and the complete custom decoder are charged; only exact pinned standard-codec libraries and platform runtime are exempt. The strict total restores non-platform standard-library bytes.

The source is copied byte-for-byte from the qualified build export. `build-spec.json` gives compiler commands with `/source` and `/output` sandbox paths. The codec.h source defines the native interface. Result, binary-export and source identities are recorded in [provenance.json](provenance.json). Correctness scope is the complete supplied corpus; the required diagnostics do not prove arbitrary hostile-input safety.
