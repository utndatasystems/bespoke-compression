# Native full-corpus benchmark

`benchmark.cpp` owns encoding, initialization-inclusive decoding, reused-state
full decoding and exact output comparisons. `run.py` uses the preserved Lab
runtime, verifies complete input manifests, selects a CPU, and records one
warmup followed by seven measured passes. Cache flushing is not performed.

Use the root [reproduction guide](../../REPRODUCING.md). Generated codecs are
loaded through their built `manifest.json`; historical candidates have been
removed. `build.py` retains conventional DBText controls and pinned source checks.
