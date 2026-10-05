# Run 3

Compression factor: **19.454138×**

Decompression: **11708.731 MB/s (median)**

Independent from-scratch OpenStack codec.

The encoder and independent decoder reconstruct all three original OpenStack inputs (61,442,082 bytes total). `build-spec.json` contains the reproducible compiler commands; `/source` and `/output` are build sandbox mounts. The target is Ice Lake Server.

Package size: 3,158,304 bytes, including all archives and the complete custom decoder once. Single-core CPU 8 timing includes decoder setup and complete reconstruction. The retained seven-trial throughput range is 10676.539–11949.007 MB/s. Each trial sums the three input times before taking the median.
