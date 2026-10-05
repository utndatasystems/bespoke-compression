# DBText tailored lossless codec

This implementation was written from scratch. It links only to the platform C/C++ runtime; no compression implementation or library is called.

Each lab_encode call produces exactly one independent archive. A four-byte mode identifies an encoding. No external dictionary, uncharged input-derived code, file, service, or reconstructed column is used by the decoder. All trained dictionaries, constants, models, indexes, and values are serialized in the archive. The custom decoder binary is also charged.

The encoder checks exact syntax and uses these special cases:
* Customer names: learned fixed prefix plus small-range decimal integers, radix packed in groups of five.
* Genome: a stored four-character alphabet and two bits per base.
* Hex: binary 32-bit values rendered without leading zeroes.
* UUID: stored middle fields and constant masks, a learned timestamp base and greatest common divisor, and packed variable suffix bits using BMI2.
* Locations: exact IEEE double values using fitted ranges and sparse high-bit exceptions. Every input decimal is verified by parsing and shortest fixed-format rendering before selecting this representation. The NULL row is recorded.
* URLs: repeated numeric skeletons, stored templates, packed integer deltas or small value dictionaries, a Huffman-coded row-type map, and a text-coded residual sequence.

General text uses a learned RePair binary phrase grammar, minimum-bit dynamic-programming reparsing, and canonical Huffman token streams. The encoder compares fixed byte classes and learned clusters of preceding-byte token distributions, and stores the smallest candidate with its mapping. Repeated model lengths use run-length and Huffman coding. Grammar children and index deltas are bit packed; phrase expansions are derived during measured decoder setup.

The general text stream includes every original byte, including LF, CR, empty rows and any unterminated final row. Tokens do not cross row boundaries. Checkpoints every 32 rows permit selection without scanning earlier blocks. A partial query only scans token metadata within the relevant bounded blocks, emits selected bytes, and writes output offsets. Sorted requests reuse a cursor within each block. Query results and reconstructed columns are never cached. At full decompression the entire stream is reconstructed inside the timed call.

Decoder setup retains only phrase dictionaries, Huffman tables, numeric template metadata and indexes. Bulk timing includes this setup. The URL type map and location exception ranks are indexes, not reconstructed output.

Compilation targets Intel Ice Lake server (the reported Xeon Gold 5318Y instruction set). BMI2 is used explicitly; compiler-generated AVX2/AVX-512 instructions may occur. All fitting is inside lab_encode. There is no external fitting, preprocessing or data-dependent compilation.

The source-build manifest is codec.json. It produces separate encoder.so and decoder.so. The same decoder and per-column archives serve both full and selected-row calls.
