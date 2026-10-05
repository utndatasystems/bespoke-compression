# Maximum decompression

Compression factor: **2.4789×** · Decompression speed (median): **5,661.94 MB/s**

## Approach

Structured columns use packed numeric fields and stored templates. General text uses
learned byte-pair phrases, dynamic-programming parsing, and 14-bit or 16-bit phrase
identifiers; short-name columns use one-byte dictionary codes. Block offsets and per-row
token counts locate selected rows in the same archive used for block-based decoding.

Original algorithm notes: [FORMAT.md](source/FORMAT.md).

## Build and run

See [dependencies and folder structure](../../README.md).

From the repository root:

```sh
python3 synthesized-code/build.py synthesized-code/dbtext/astra/from-scratch/01-maximum-decompression
python3 synthesized-code/run.py synthesized-code/dbtext/astra/from-scratch/01-maximum-decompression /path/to/dbtext/data
```

`source/` contains the codec and C interface; `build-spec.json` contains the
compiler commands. Compiled libraries are written to `build/`.
