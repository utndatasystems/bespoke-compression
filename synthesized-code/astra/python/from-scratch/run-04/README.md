# Python · Run 4

Compression factor: **5.175272586×**. Decompression median: **1168.986121 MB/s**; seven-trial range 1138.572437–1187.755879 MB/s. Charged package: 19,322,651 bytes from 99,999,986 original bytes.

Fitted LZ parsing and block entropy coding with an independent AVX-512 decoder. Retained offline preprocessing and required compilation total 381.607115509 s. The 74,231,164-byte final_trace.bin contains encoder-only parse decisions, checked against input; the independent decoder uses only the complete charged archive. FIT_README.md refers to final_costs.json; the actual retained cost receipt is final_1m_costs.json.

Online encoding median: 0.438007450 s; this excludes offline fitting and builds.
Combining the historical offline cost with measured median online encoding gives approximately 382.045123 s (0.261749 MB/s), not a fresh end-to-end benchmark. No 100 MB/s end-to-end encoding claim.

The sealed commission measures full byte reconstruction including setup on server CPU 8 (sibling 32 excluded), with no encoding-speed floor. All archives, complete custom decoder and non-exempt decoder dependencies are charged; encoder-only artifacts and the platform runtime are excluded. Qualification includes source review, required diagnostics, and independent reconstruction of the complete original input. This is exact-corpus, pinned-host evidence, not a general portability claim.

`build-spec.json` preserves the exact build commands using `/source` and `/output` sandbox paths. `source/interface/codec.h` defines the ABI. Preserve these paths when reproducing builds: some encoder-only binary symbols and assembly depend on them. The source and fitting artifacts are unchanged. Result and source hashes are in `provenance.json`.
