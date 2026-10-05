# SQLStorm TPC-DS · Run 4

Compression factor: **16.087835687×**. Decompression median: **1243.482427 MB/s**; seven-trial range 1187.855857–1262.485658 MB/s. Package: 1,146,493 bytes from 18,444,591 original bytes.

Offline byte-pair grammar and token LZ parsing with 20 entropy fits, four-lane rANS and independent AVX512 reconstruction. Encoder verifies exact input and emits its fitted archive. All reconstruction information is in that charged archive.

Online encoding median: 0.023969797 s. Retained offline fitting/packing: 28.581672 s; recorded compilation: 13.119333 s (including required fitted-encoder compilation). Combined historical fitting/build cost plus measured online median: 41.724975 s, approximately 0.442052 MB/s. This is not a fresh end-to-end benchmark and does not establish 100 MB/s end-to-end encoding. See [fitting-costs.json](fitting-costs.json). Refit source preserves its original `/inputs/queries.nul`, `/work` and `/interface` paths; mount the original input and source interface there when running `source/final/reproduce_final.py`.

Full byte reconstruction includes setup on pinned server CPU 8, sibling 32 excluded. Complete archive, custom decoder and non-exempt dependencies are charged; platform runtime and encoder-only artifacts are excluded by the sealed contract. No encoding-speed floor was commissioned. Source review, full original-input reconstruction, 16 diagnostics and reproducible builds passed. Exact-corpus and pinned-host results only; AVX support requirements follow the retained build.

`build-spec.json` contains exact commands using `/source` and `/output`; `source/interface/codec.h` defines the ABI. Sources are unchanged; `provenance.json` records hashes and result identities.
