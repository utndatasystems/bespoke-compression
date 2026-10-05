# Intermediate decompression

Compression factor: **3.4201×** · Decompression speed (median): **429.80 MB/s**

## Approach

Structured columns use packed integers and exact decimal fields. Text uses learned
byte-pair phrases, dynamic-programming parsing, and length-limited Huffman codes, with
four preceding-byte contexts for larger columns. URLs combine numeric templates, shared
prefixes, and FSST-coded suffixes; Zstd compresses dictionaries and metadata, while row
indexes support selective reconstruction.

Original algorithm notes: [FORMAT13.md](source/FORMAT13.md).

## Build and run

See [dependencies and folder structure](../../README.md).

From the repository root:

```sh
python3 synthesized-code/build.py synthesized-code/dbtext/astra/tools-allowed/04-intermediate-decompression
python3 synthesized-code/run.py synthesized-code/dbtext/astra/tools-allowed/04-intermediate-decompression /path/to/dbtext/data
```

`source/` contains the codec and C interface; `build-spec.json` contains the
compiler commands. Compiled libraries are written to `build/`.
