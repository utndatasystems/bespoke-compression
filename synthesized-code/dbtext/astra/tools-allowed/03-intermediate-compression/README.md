# Intermediate compression

Compression factor: **3.1074×** · Decompression speed (median): **1,618.79 MB/s**

## Approach

Structured columns use packed numeric records. Text uses learned byte-pair phrases
represented by fixed two-byte identifiers or variable one/two-byte codes. Phrase
dictionaries are sorted, prefix-coded, and compressed with LZ4; Zstd compresses
row-length metadata that expands into offsets for direct access to each row's token
span.

## Build and run

See [dependencies and folder structure](../../README.md).

From the repository root:

```sh
python3 synthesized-code/build.py synthesized-code/dbtext/astra/tools-allowed/03-intermediate-compression
python3 synthesized-code/run.py synthesized-code/dbtext/astra/tools-allowed/03-intermediate-compression /path/to/dbtext/data
```

`source/` contains the codec and C interface; `build-spec.json` contains the
compiler commands. Compiled libraries are written to `build/`.
