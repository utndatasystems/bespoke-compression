# Run 1

Compression factor: **11.429913×**

Decompression: **12954.036 MB/s (median)**

OpenStack template inference, packed variable hexadecimal nibbles, compact 128-byte template chunks and AVX-512 VBMI2 byte expansion.

The encoder and independent decoder reconstruct all three original OpenStack inputs (61,442,082 bytes total). `build-spec.json` contains the reproducible compiler commands; `/source` and `/output` are build sandbox mounts. The target is Ice Lake Server.

Package size: 5,375,551 bytes, including all archives and the complete custom decoder once. Single-core CPU 8 timing includes decoder setup and complete reconstruction. The retained seven-trial throughput range is 12573.449–13209.087 MB/s. Each trial sums the three input times before taking the median.
