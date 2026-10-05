# Maximum compression

Compression factor: **3.8874×** · Decompression speed (median): **23.18 MB/s**

## Approach

A per-column plan selects structural bit packing, sorted prefix sharing, or word and
UTF-8 token dictionaries. Prefix sharing stores each sorted row as a common-prefix
length and a suffix, with an encoded permutation restoring the original order. PPMd or
Brotli compresses the text representation; opening expands its intermediate structures,
then selected rows are reconstructed through prefix links or indexed token ranges.

## Build and run

See [dependencies and folder structure](../../README.md).

From the repository root:

```sh
python3 synthesized-code/build.py synthesized-code/dbtext/astra/tools-allowed/02-maximum-compression
python3 synthesized-code/run.py synthesized-code/dbtext/astra/tools-allowed/02-maximum-compression /path/to/dbtext/data
```

`source/` contains the codec and C interface; `build-spec.json` contains the
compiler commands. Compiled libraries are written to `build/`.
