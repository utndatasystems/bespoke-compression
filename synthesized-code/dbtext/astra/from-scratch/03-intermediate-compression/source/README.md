# DBText mixed codec

The source-build entry point is codec.json (variant: rows). Its two g++ commands build encoder.so and decoder.so against interface/codec.h. All sources are self-contained; no downloaded build dependencies or existing compression libraries are used. The decoder uses only the installed platform C/C++ runtimes.

Each column is independently encoded into one archive. Full decoding and every row selection use that same archive. All original bytes, including LF, CR, UTF-8, punctuation, empty rows, and original order are retained.

* Customer IDs and genomes use 18-bit records with archive-owned templates/alphabet.
* Hex integers use binary values and exact uppercase digit reconstruction.
* UUIDs retain an archive-owned common template plus an 81-bit varying payload per row (57-bit stream and three-byte tail).
* Coordinates use 13-byte records: packed decimal pairs for the common form and binary fractions for exceptional forms. Fraction lengths preserve every original digit.
* Text uses independently trained greedy byte-pair phrases of at most 32 bytes. Small columns use one/two-byte phrase codes. Larger columns use canonical Huffman codes and a reconstructible phrase grammar. Dictionary metadata uses a small custom LZ parser. All dictionaries and codebooks are archived.
* Compressed row-length indexes preserve direct row access. Huffman bit counts can be predicted from raw lengths, with lossless residuals. Index Huffman streams have independent checkpoints to accelerate setup.

lab_open expands dictionaries, Huffman lookup tables and indexes. It does not reconstruct a column or cache query outputs. Full Huffman decoding interleaves eight independent ranges on one core. Partial queries interleave six selected rows and write bytes and boundaries inside the call; they do not scan preceding rows. Byte-coded columns decode directly from indexed spans. The 100% selection may use full decoding, still returning row boundaries within the timed call.

The build targets Intel Ice Lake server x86-64. Required instructions include AVX2, BMI2 and AVX-512 F/BW/VL/VBMI/VBMI2. There is no portable CPU fallback. The format is specialized to the supplied DBText dataset, not a general-purpose interchange format.

All fitting and preprocessing occur inside lab_encode. There is no external fitting phase, external dictionary, or data-dependent compilation. Ordinary source compilation is separate from encoding.

The research directory also contains benchmark and experimental files. Only codec.json's listed sources define the submitted configuration. Official full measurement, validation and export identities are reported with the final result.
