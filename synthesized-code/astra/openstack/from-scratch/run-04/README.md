# Run 4

Compression factor: **25.517924×**

Decompression: **5185.946 MB/s (median)**

From-scratch template patch codec with packed UUID dictionary, timestamp deltas, greedy constant-field template splitting, LZ77 metadata and four-stream Huffman payload.

The encoder and independent decoder reconstruct all three original OpenStack inputs (61,442,082 bytes total). `build-spec.json` contains the reproducible compiler commands; `/source` and `/output` are build sandbox mounts. The target is Ice Lake Server.

Package size: 2,407,801 bytes, including all archives and the complete custom decoder once. Single-core CPU 8 timing includes decoder setup and complete reconstruction. The retained seven-trial throughput range is 5069.971–5647.541 MB/s. Each trial sums the three input times before taking the median.
