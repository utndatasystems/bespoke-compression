# Yelp · GLM G2

Maximum compression. Solo GLM-5.3-Flash, Max, tools allowed.

Compression factor **14.170838258×**; decompression median **1995.907970 MB/s**. Seven-trial range: 1985.142709–2027.797398 MB/s. Charged package 7,056,780 bytes; strict deployment total 8,005,796 bytes. Target met.

Two-pass structural JSON columnar encoder, serialized vocabularies and value streams, raw 16-byte identifiers, Zstd level22 compression of column blobs, independent template reconstruction.

Encoding median: 5.978779997 s per complete original input, including preprocessing performed inside lab_encode; data-independent compilation is separate. The sealed experiment has no encoding-speed floor. No new end-to-end fitting-cost claim is made.

Full input: 100,000,488 bytes. Decompression includes fresh setup and complete byte reconstruction on pinned server CPU8 with sibling32 excluded. All payloads, fitted dictionaries, framing and the complete custom decoder are charged; only exact pinned standard-codec libraries and platform runtime are exempt. The strict total restores non-platform standard-library bytes.

The source is copied byte-for-byte from the qualified build export. `build-spec.json` gives compiler commands with `/source` and `/output` sandbox paths. The codec.h source defines the native interface. Result, binary-export and source identities are recorded in [provenance.json](provenance.json). Correctness scope is the complete supplied corpus; the required diagnostics do not prove arbitrary hostile-input safety.
