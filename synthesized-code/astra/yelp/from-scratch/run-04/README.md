# Run 4

Compression factor: **12.101538×**

Decompression: **4452.270 MB/s (median)**

Dataset-specific JSON field decomposition, six archive dictionaries, packed business IDs and coordinate fractions, custom LZ command streams for dictionary/literals and eight Huffman streams for row metadata. Decoder reconstructs all original JSON bytes.

The encoder and independent decoder reconstruct the complete original 100,000,488-byte Yelp input. `build-spec.json` gives the compiler commands and block-based interface; `/source` and `/output` are the build sandbox mounts. The target is Ice Lake Server.

Package size: 8,263,453 bytes, including the archive and custom reconstruction code. Measurements use server CPU 8.
