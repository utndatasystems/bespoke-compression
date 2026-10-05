DBText Phrase Mosaic

This implementation encodes all 23 original columns independently. Each resulting archive is used unchanged for full decompression and every selected-row query. Every line-feed, carriage return, UTF-8 byte, final boundary and original row order is retained.

General text: the encoder learns adjacent phrases, limited to sixteen bytes, by counted pair substitution. A dynamic-programming pass reparses the complete input over the learned vocabulary to minimize the number of symbols. The archive contains the literal phrase dictionary, phrase lengths, a symbol stream (14-bit packed IDs or 16-bit slots selected per column), and row indexing. No fitting files or generated data-dependent source code are needed. Decoder setup expands only the small phrase dictionary into padded slots, not the column or query outputs. Row addressing uses an absolute symbol offset for each block of sixteen rows and a one-byte symbol count per row; a bounded sixteen-byte sum addresses any requested row directly.

Four short-name columns use an independently trained one-byte symbol dictionary whose entries contain up to seven literal bytes and a byte count. Their indexes use byte counts, with nibble counts where they fit.

Special formats retain archive-resident templates: customer names store the constant prefix and decimal triplet indexes; genomes pack nine bases into eighteen bits; hexadecimal rows store their exact unsigned values; UUIDs store eleven varying bytes and the fixed template. Location rows store exact fractional decimal digits in BCD plus a template ID preserving all other bytes. Decimal values are never converted through floating-point.

The encoder and decoder are separate C++17 shared libraries implementing /interface/codec.h. Decoder dependencies are the installed platform C runtime and loader. There is no linked, copied, or invoked existing compression library. Build for Intel Ice Lake server (-march=icelake-server); specialized expansion uses AVX2 and AVX-512 BW/VL, VBMI and VBMI2, with BMI2 used by the short-name index.

The decoder has no threads or external I/O. Partial queries reconstruct only requested rows. It never retains reconstructed columns or query outputs. All custom code, dictionaries, templates, row indexes, headers and padding are included in package accounting. Sources and the encoder are excluded as specified by the protocol.

This codec deliberately specializes to the supplied DBText inputs and server instruction set. Its encoder need not support other datasets; the decoder supports arbitrary valid sorted row selections for these archives.

All dictionary training and parsing are performed within lab_encode and included in the reported encoding time. External fitting, preprocessing and data-dependent compilation cost are zero. Ordinary source compilation is not data-dependent.
