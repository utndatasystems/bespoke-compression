# Reproduction evidence

The 2026-10-05 server replay uses frozen implementations and SHA-256-pinned
complete datasets. All 42 codecs built successfully and passed 534 complete-file
round trips, including selected-row reconstruction where supported. The tool-only
MCP suites passed 36/36 independently under WSL and the server sandbox. All seven
saved paper figures regenerated successfully.

`roundtrips/` contains the exact reconstruction receipts; `verification.json`
records the measured checks. Source, compiler, input and artifact identities are
retained alongside the raw results. Linux commands are in the root reproduction
guide. Dataset acquisition checks and project-local native package hashes are
included in the transcripts.

Fresh DBText measurements cover 41 bulk configurations and 16 synthesized row
configurations plus the original controls. OpenStack covers 27 configurations
and 648 exact file decodes; Python, Yelp and SQLStorm each cover 27 configurations and
216 exact file decodes. Their raw numeric records, readable summaries and fresh
figure inputs are in the corresponding ZIPs, verification JSON files and `fresh/`.
Historical paper measurements remain separately preserved.

`private-candidate-test/` records a second independent checkout test with 42
builds, 534 round trips, seven figure PDFs and 36 MCP tests. All 42 decoder hashes
matched the measurement builds; 81 of 84 encoder/decoder binary hashes matched.
Three encoder libraries differ in absolute assertion-header build paths, as
recorded in `independent-binary-identity.json`.

Original failed attempts and recovery receipts are retained with the numerical
evidence. Plot placement repairs preserve measured points and the saved paper
previews, with collision checks enforced. Full-file replay validates these frozen
inputs; it does not establish robustness or generalization beyond this corpus.
Matched hardware does not promise identical timings.

`public-checkout-test/` records an independent checkout of the public unlimited
runtime: all 42 builds, 534 exact full-file round trips, both selected-row checks
where supported, 36 MCP tests, bundled extraction and seven saved/fresh PDFs.
All five fresh server measurements completed and their exported members were
SHA-256 verified. Fresh figures and their exact generator identities are in
`fresh/plots/` and `public-checkout-test/fresh-generator-identities.json`.

Yelp retains two historical 300-second Brotli failures. Its final Brotli replay
ran with unlimited wall-clock and CPU time while preserving all 24 successful
configurations. Production execution has no time caps. Historical runtime hashes
remain separate from the public runtime; the source difference classification
records cap removal, newline normalization and documentation changes. Native
timed kernels, codec algorithms and original build recipes are unchanged.
