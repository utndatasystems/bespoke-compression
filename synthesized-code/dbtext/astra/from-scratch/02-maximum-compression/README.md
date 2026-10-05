# Maximum compression

Compression factor: **3.7879×** · Decompression speed (median): **192.68 MB/s**

## Approach

Structured columns use numeric packing and URL templates. General text uses a RePair
phrase grammar, minimum-bit parsing, and canonical Huffman codes with models selected by
the preceding byte. Checkpoints every 32 rows let the decoder locate a selected row by
scanning token metadata within its block.

Original algorithm notes: [FORMAT.md](source/FORMAT.md).

## Build and run

See [dependencies and folder structure](../../README.md).

From the repository root:

```sh
python3 synthesized-code/build.py synthesized-code/dbtext/astra/from-scratch/02-maximum-compression
python3 synthesized-code/run.py synthesized-code/dbtext/astra/from-scratch/02-maximum-compression /path/to/dbtext/data
```

`source/` contains the codec and C interface; `build-spec.json` contains the
compiler commands. Compiled libraries are written to `build/`.
