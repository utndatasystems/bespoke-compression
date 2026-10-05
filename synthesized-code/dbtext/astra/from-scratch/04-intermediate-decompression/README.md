# Intermediate decompression

Compression factor: **3.2180×** · Decompression speed (median): **1,652.39 MB/s**

## Approach

Structured columns use packed numeric fields; URLs use date and numeric templates plus
dictionaries for titles, prefixes, and generic suffixes. General text uses per-column
phrase dictionaries learned by pair merging or RePair, followed by shortest-token
parsing and fixed-width phrase identifiers. Prefix-coded dictionaries are compressed
with a custom Huffman coder, while compact row-token counts expand into offsets for
selective decoding.

Original algorithm notes: [FORMAT.md](source/FORMAT.md).

## Build and run

See [dependencies and folder structure](../../README.md).

From the repository root:

```sh
python3 synthesized-code/build.py synthesized-code/dbtext/astra/from-scratch/04-intermediate-decompression
python3 synthesized-code/run.py synthesized-code/dbtext/astra/from-scratch/04-intermediate-decompression /path/to/dbtext/data
```

`source/` contains the codec and C interface; `build-spec.json` contains the
compiler commands. Compiled libraries are written to `build/`.
