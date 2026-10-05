# Run 2

Compression factor: **15.326483×**

Decompression: **2164.512 MB/s (median)**

Exact Yelp JSONL field codec: SIMD packed IDs, byte-pair/Huffman text, Huffman location tuples, location-predicted lossless decimal integers, Huffman attribute/category phrases and hour schedules; all dictionaries in charged archive.

The encoder and independent decoder reconstruct the complete original 100,000,488-byte Yelp input. `build-spec.json` gives the compiler commands and block-based interface; `/source` and `/output` are the build sandbox mounts. The target is Ice Lake Server.

Package size: 6,524,686 bytes, including the archive and custom reconstruction code. Measurements use server CPU 8.
