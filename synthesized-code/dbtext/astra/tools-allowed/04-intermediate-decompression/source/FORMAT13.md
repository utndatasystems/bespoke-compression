# DBText row codec
Each input column has one self-contained archive. The same decoder and archive serve full output and arbitrary selected rows.

Four synthetic columns use packed numeric representations (customer IDs and nine DNA bases:18bits/row; UUID:78varyingbits/row; hexadecimal integers:32bits). Their fixed textualsyntax is implemented in the charged decoder. Locations use13-byte records containing exact decimalfractions andoriginalprecision.

Other columns use dataset-trained byte-pair phrases up to32bytes, reparsed by dynamicprogramming. Small columns use12/13-bit-limited canonicalHuffman codes. Larger columns use four learned contexts based on the preceding byte and14-bit-limited codes. Every phrase, contextmap, codebook androwbit-length index is serialized in its own column archive. Dictionaries and byte-transposed indexes are compressed with Zstd.

Opening expands dictionaries/indexes and scans token codes to validate them and derive raw rowlength metadata; it does not reconstruct output bytes or cache queryoutputs. Rowqueries begin atstored bit offsets and decode only requested rows. Four independent rowstreams use exact AVX-512 masked stores. Full decoding uses the same representation and includes setup in measuredbulk time.

The encoder performs all fitting and preprocessing in lab_encode; no external fitting or data-dependent compilation is required. Builds use g++ -O3 -march=icelake-server. The decoder needs the pinned exempt libzstd.so.1 and platform C/C++ runtime libraries; the complete customdecoder is charged. The codec is tuned for the supplied dataset and x86-64 IceLake server, using AVX2, BMI2 and AVX-512 BW/VL instructions. It runs on one thread.

Runtime dictionaries pack phrases contiguously with 32 bytes of trailing padding; masked copies emit only the actual phrase length. This changes no archive bytes. The encoder retains its internal padded dictionary layout for phrase identifiers.

Selected Huffman rows assign output offsets as independent decoding lanes finish; the assignment order follows the selection order. Structural converters batch DNA and location rows and specialize the common eight-digit hexadecimal case. Every row ID and output capacity remains checked.

Full plain-Huffman output uses a contiguous token prefix, reserving at least 32 bytes of output for a capacity-checked tail. This keeps overlapping vector copies within the supplied buffer even at exact capacity.

UUID counters use the exact common step of ten, storing 18 quotient bits plus 60 tail bits. The encoder checks this property for every row.

The URLs column uses numeric templates with packed decimal digits. Other URL rows use a shared prefix and FSST-coded suffix. Zstd compresses the shared templates, prefix strings and FSST table together. Row codes are self-delimiting; opening scans code metadata and terminators to construct an index without emitting or retaining output strings. The same index and payload serve full and selective output. AVX-512 VBMI2 expands packed digits into template positions.

FSST build sources are included. FSST fitting code is linked only into the encoder; the inline suffix decoder is part of the charged custom decoder. Runtime dependencies remain the pinned Zstd library and platform C/C++ runtimes. Public symbols are the required C entry points; internal symbols and unused linker sections are omitted.
