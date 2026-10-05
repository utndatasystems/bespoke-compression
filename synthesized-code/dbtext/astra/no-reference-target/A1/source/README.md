DBText adaptive lossless codec

Build using build.json. Encoder and decoder are separate shared libraries implementing /interface/codec.h. The decoder requires only the archive bytes and the installed platform C/C++ runtimes. No compression libraries, external datasets, runtime files, threads or subprocesses are used. All reconstruction tables and fitted dictionaries are inside their column archive, with the custom decoder charged once.

Each archive begins with a 32-byte header: magic/version, original byte count, row count, format tag, and payload byte count. LF delimiters remain part of their rows. Input row order, CR bytes, UTF-8 bytes, empty rows and an unterminated final row are preserved by the generic formats. Specialized encoders verify every byte of their recognized layouts before accepting them. The supplied DBText columns all terminate in LF.

Column formats:
* c_name: fixed prefix and a charged decimal expansion table, followed by two ten-bit three-digit indexes in three bytes per row.
* genome: nine two-bit bases in three bytes per row, with charged alphabet/expansion tables.
* hex: four-byte unsigned integers, original uppercase alphabet, and LF; the value determines the original unpadded length.
* uuid: stored common template, alphabet, and eleven binary bytes per row.
* location: sixteen-byte records of decimal nibbles, fractional lengths, and the varying integer field; a distinct NULL record and formatting template preserve exact spelling.
* city, credentials, faust, firstname, street: independently trained 255-entry byte dictionaries with an escape byte. Row byte counts and a block index every sixteen rows locate arbitrary selections.
* email, hamlet, l_comment, ps_comment, urls2: trained dictionary phrases of at most sixteen bytes, minimum-token dynamic-programming parsing, and packed token IDs. Symbols are reordered by frequency. Compact dictionaries expand during timed setup.
* urls: the same approach with phrases of at most thirty-two bytes.
* chinese, japanese, lastname, movies, wiki, wikipedia, yago: phrases of at most eight bytes stored directly as fixed eight-byte dictionary entries with a byte length table. Timed setup validates lengths and references resident archive data without expanding a dictionary.

All multi-byte values are little-endian. Packed token IDs use the minimum whole bit width covering the dictionary. Generic token formats store token counts per row and an absolute token index every thirty-two rows; partial queries sum at most thirty-one count bytes using SIMD and reconstruct only requested rows. Adjacent selections reuse the next compressed position within that call. No reconstructed rows or query results are cached. At a verified complete selection, the decoder reconstructs the full byte stream and scans LF bytes with AVX-512 to return row offsets, both inside lab_rows.

The code is specialized to this dataset and targets Intel Ice Lake server x86-64 (-march=icelake-server). It uses AVX2 and AVX-512, including BW, VL, VBMI and VBMI2. There is no runtime dispatch for older CPUs. All fitting, parsing, dictionaries and field preprocessing are performed in lab_encode; no external preprocessing or data-dependent compilation is required. Compilation is ordinary data-independent codec compilation.

Bounds checks protect supplied capacities, dictionary IDs, selected row IDs and archive regions. Subset reads do not scan earlier rows beyond the bounded index group, reconstruct the full column, or retain reconstructed output. The submitted files are original implementations of dictionary substitution, bit packing and SIMD byte expansion; no existing compression implementation was used.
