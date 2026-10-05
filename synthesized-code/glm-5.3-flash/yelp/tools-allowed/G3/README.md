# Yelp · GLM G3

Throughput at an intermediate compression target. Solo GLM-5.3-Flash, Max, tools allowed.

Compression factor **11.185150130×**; decompression median **1082.692053 MB/s**. Seven-trial range: 1066.674261–1118.653236 MB/s. Charged package 8,940,469 bytes; strict deployment total 9,696,333 bytes. Target missed (valid completed result).

Serialized JSON skeleton, packed business identifiers, zstd-compressed merged field pools, per-object pair vocabularies, Huffman category/hour dictionaries and adjacent-repeat bits.

Encoding median: 3.259922091 s per complete original input, including preprocessing performed inside lab_encode; data-independent compilation is separate. The sealed experiment has no encoding-speed floor. No new end-to-end fitting-cost claim is made.

Full input: 100,000,488 bytes. Decompression includes fresh setup and complete byte reconstruction on pinned server CPU8 with sibling32 excluded. All payloads, fitted dictionaries, framing and the complete custom decoder are charged; only exact pinned standard-codec libraries and platform runtime are exempt. The strict total restores non-platform standard-library bytes.

The source is copied byte-for-byte from the qualified build export. `build-spec.json` gives compiler commands with `/source` and `/output` sandbox paths. The codec.h source defines the native interface. Result, binary-export and source identities are recorded in [provenance.json](provenance.json). Correctness scope is the complete supplied corpus; the required diagnostics do not prove arbitrary hostile-input safety.
