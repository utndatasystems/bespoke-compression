# Run 1

Compression factor: **5.204370×**

Decompression: **8884.410 MB/s (median)**

Independent from-scratch Yelp JSONL codec. Lexical literals, packed head dictionaries, ordered frequency-sorted phrase slots with copy classes, AVX-512/VBMI reconstruction, bounded tail path. All fitting occurs in lab_encode; all fitted data is charged in archive.

The encoder and independent decoder reconstruct the complete original 100,000,488-byte Yelp input. `build-spec.json` gives the compiler commands and block-based interface; `/source` and `/output` are the build sandbox mounts. The target is Ice Lake Server.

Package size: 19,214,714 bytes, including the archive and custom reconstruction code. Measurements use server CPU 8.
