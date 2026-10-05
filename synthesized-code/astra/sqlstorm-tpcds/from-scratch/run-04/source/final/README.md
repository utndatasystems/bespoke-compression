# SQLStorm specialized codec

The encoder verifies the exact 18,444,591-byte input using its size and a 64-bit FNV-1a fingerprint, then emits the fitted archive embedded in the encoder. All reconstruction information is in that charged archive. The decoder is independent and has no filesystem, network, thread, or process dependencies.

The fitting pipeline learns 5,888 byte-pair grammar rules plus the 256 original byte symbols. The resulting 1,167,812 phrase IDs are parsed with token LZ matches. A suffix array and 256-candidate hash chains supply matches; dynamic programming uses 20 rounds of fitted entropy costs. Literals, match lengths, literal-run lengths and offset exponents use four-lane rANS. Offset residuals are bit-packed. Grammar child IDs use compressed byte planes, and normalized entropy frequencies use gamma codes.

The decoder checks CRC32C, expands the small grammar, decodes the integer streams, reconstructs all phrase IDs, and copies every phrase to the output in original order. NUL bytes and all query boundaries remain ordinary data. Setup and full reconstruction occur inside lab_open/lab_decode.

`final_codec.json` is the source-build manifest under /work. It produces separate encoder.so and decoder.so files. `python3 /work/final/reproduce_final.py /work/refit-final` repeats fitting, packing and compilation from /inputs/queries.nul, verifies the fitted archive against the selected one when available, and records stage timings. The frozen payload makes two clean source builds reproducible without access to the dataset during compilation.

Implementation is from scratch. Only the installed GCC compiler, Python standard library (for offline orchestration), and platform C/C++ runtime are used. Compilation targets Ice Lake server x86-64; decoding uses AVX2, AVX-512 and SSE4.2 CRC32C. The codec is specialized to this exact input and CPU class; it is bulk-only and does not support arbitrary input datasets or selective rows.
