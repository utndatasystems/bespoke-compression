# Run 3

Compression factor: **9.786010×**

Decompression: **3429.745 MB/s (median)**

Fourteen-field structural JSON codec with serialized dictionaries, category/hour token programs, attribute pairs, binary business identifiers, lexical numeric nibble packing and a custom LZ dictionary compressor.

The encoder and independent decoder reconstruct the complete original 100,000,488-byte Yelp input. `build-spec.json` gives the compiler commands and block-based interface; `/source` and `/output` are the build sandbox mounts. The target is Ice Lake Server.

Package size: 10,218,719 bytes, including the archive and custom reconstruction code. Measurements use server CPU 8.
