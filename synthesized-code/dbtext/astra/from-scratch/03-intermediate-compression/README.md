# Intermediate compression

Compression factor: **3.0664×** · Decompression speed (median): **1,146.95 MB/s**

## Approach

Structured columns use packed numeric records and templates. Text uses learned byte-pair
phrases, with byte-oriented codes for smaller columns and canonical Huffman codes for
larger ones; phrase dictionaries are stored as grammars or compressed metadata.
Row-length indexes provide direct access to selected rows, and the decoder interleaves
independent streams on one core.

Original algorithm notes: [README.md](source/README.md).

## Build and run

See [dependencies and folder structure](../../README.md).

From the repository root:

```sh
python3 synthesized-code/build.py synthesized-code/dbtext/astra/from-scratch/03-intermediate-compression
python3 synthesized-code/run.py synthesized-code/dbtext/astra/from-scratch/03-intermediate-compression /path/to/dbtext/data
```

`source/` contains the codec and C interface; `build-spec.json` contains the
compiler commands. Compiled libraries are written to `build/`.
