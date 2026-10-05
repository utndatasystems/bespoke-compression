DBText column grammars

This codec builds independent archives for each LF-framed column. Row delimiters, carriage returns, empty rows, and final unterminated rows are part of the encoded byte strings.

The native encoder and decoder are separate shared libraries. The decoder supports lab_open, lab_decode, lab_rows and lab_close from the strings-v1 ABI. Numeric columns use packed integers with exact string rendering; other columns use trained phrase dictionaries and compact row indexes. Numeric URL templates and their variable fields are stored within the URL archive, alongside an independently indexed phrase stream.

All trained information is stored in each column archive. Literal format constants are compiled into the charged decoder. Sources and the encoder are excluded under the lab policy; the complete decoder binary is charged. No external compression library or implementation is used.

Build: see source.json. Compiler target: Intel Ice Lake server (AVX2, BMI2, AVX-512BW/VL/VBMI where used). Runtime dependencies are the installed C/C++ platform runtime. There are no downloaded build dependencies.

Partial queries reconstruct only the selected rows. Decoder setup may expand dictionaries and offset metadata; it does not reconstruct or retain an uncompressed column. Query outputs are not cached.

Encoding costs include dictionary training performed inside lab_encode. There is no external fitting or data-dependent compilation. Build compilation is independent of the input values.
