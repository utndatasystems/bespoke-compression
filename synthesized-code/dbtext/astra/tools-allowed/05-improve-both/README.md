# Improve both

Compression factor: **3.1619×** · Decompression speed (median): **1,446.32 MB/s**

## Approach

Structured columns use packed numeric fields and exact decimal representations. General
text uses learned byte-pair phrases, dynamic-programming parsing, and fixed-width
identifiers; Zstd compresses the dictionaries and row-token counts. URLs combine packed
date/time templates with shared prefixes and Huffman-coded phrase suffixes. Row offsets
let the decoder reconstruct selected rows from the same archives used for block-based decoding.

## Build and run

See [dependencies and folder structure](../../README.md).

From the repository root:

```sh
python3 synthesized-code/build.py synthesized-code/dbtext/astra/tools-allowed/05-improve-both
python3 synthesized-code/run.py synthesized-code/dbtext/astra/tools-allowed/05-improve-both /path/to/dbtext/data
```

`source/` contains the codec and C interface; `build-spec.json` contains the
compiler commands. Compiled libraries are written to `build/`.
