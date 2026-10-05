# Run 2

Compression factor: **31.578037×**

Decompression: **1618.254 MB/s (median)**

Dataset-specialized byte-exact OpenStack codec: fixed-width message templates, constant-folded numeric columns, UUIDv4 122-bit packing with implicit singleton indices, exact binary-grid duration transform, template-pair timestamp prediction, from-scratch LZ77 and four-way rANS. All fitting runs in lab_encode; no external preprocessing or data-dependent code generation.

The encoder and independent decoder reconstruct all three original OpenStack inputs (61,442,082 bytes total). `build-spec.json` contains the reproducible compiler commands; `/source` and `/output` are build sandbox mounts. The target is Ice Lake Server.

Package size: 1,945,722 bytes, including all archives and the complete custom decoder once. Single-core CPU 8 timing includes decoder setup and complete reconstruction. The retained seven-trial throughput range is 1558.343–1625.962 MB/s. Each trial sums the three input times before taking the median.
