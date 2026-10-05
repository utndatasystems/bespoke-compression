# Improve both

Compression factor: **3.1907×** · Decompression speed (median): **1,550.32 MB/s**

## Approach

Numeric columns use packed values and templates; URLs combine numeric templates with
indexed phrase-coded residuals. Other text uses learned phrase dictionaries and
dynamic-programming parsing, with fixed-width identifiers for most columns and canonical
Huffman codes for three text columns. Stored row offsets locate the requested token
spans in the same archive used for block-based decoding.

Original algorithm notes: [README.md](source/README.md).

## Build and run

See [dependencies and folder structure](../../README.md).

From the repository root:

```sh
python3 synthesized-code/build.py synthesized-code/dbtext/astra/from-scratch/05-improve-both
python3 synthesized-code/run.py synthesized-code/dbtext/astra/from-scratch/05-improve-both /path/to/dbtext/data
```

`source/` contains the codec and C interface; `build-spec.json` contains the
compiler commands. Compiled libraries are written to `build/`.
