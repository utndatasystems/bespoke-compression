# Maximum decompression

Compression factor: **2.4817×** · Decompression speed (median): **5,724.19 MB/s**

## Approach

Structured columns use packed numeric fields and decimal digits. Selected string columns
use shared-prefix or shared-suffix dictionaries; other text uses learned byte-pair
phrases represented by 12-bit identifiers. LZ4 compresses dictionary data, and row
records or block indexes locate the requested rows for dictionary expansion.

## Build and run

See [dependencies and folder structure](../../README.md).

From the repository root:

```sh
python3 synthesized-code/build.py synthesized-code/dbtext/astra/tools-allowed/01-maximum-decompression
python3 synthesized-code/run.py synthesized-code/dbtext/astra/tools-allowed/01-maximum-decompression /path/to/dbtext/data
```

`source/` contains the codec and C interface; `build-spec.json` contains the
compiler commands. Compiled libraries are written to `build/`.
