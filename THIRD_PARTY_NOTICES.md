# Third-party sources and redistribution

Newly authored toolkit, facade, adapters and tests use the root MIT license.
Frozen synthesized sources retain their original comments and dependency notices;
our license does not relicense third-party codec implementations.

- **FSST**: MIT. Pinned paper/codec sources and LICENSE are under
  `benchmarks/upstream/fsst/` and `benchmarks/final/sources/fsst/`. The latter
  preserves the exact histogram-bound repair used for the final paper replay.
- **OnPair+**: the author snapshot at `umbra-db/token-vldb2026` commit
  `e202e36e2b33a4768d1122f96880b64416234175` is fetched and checked against
  `benchmarks/upstream/onpair-lock.json`. This pinned upstream snapshot has no
  repository-level LICENSE; no permissive license is inferred. Model-modified
  copies are explicit `vendor-overrides/` snapshots and retain upstream attribution.
  Our MIT license does not grant rights to those upstream portions; consult their
  authors for redistribution terms. The ABI/row adapters are identified separately.
- **LZ4, Zstandard, Brotli, zlib, bzip2 and XZ/liblzma**: linked from installed
  development libraries, not redistributed as binaries. Their original licenses
  apply (respectively BSD, BSD/GPL option, MIT, zlib, bzip2, and XZ component terms).
  Frozen sources embedding codec implementations retain their included notices.
- **PPMd8**: the retained 7-Zip 24.09 source files identify Igor Pavlov and
  Dmitry Shkarin and state public-domain status. `source/ppmd/README.origin`
  records the upstream tag and files in the applicable DBText snapshot.
  This is a dependency, not claimed as a new algorithm invention by the model.
- **MCP Python SDK and plot dependencies**: downloaded during setup from their
  pinned package distributions. The MCP dependency hashes are in
  `compression-lab-isolated/requirements-mcp.lock`; plot versions are in
  `requirements-plots.txt`. Their distribution license metadata accompanies them.
- **Linux PRoot, Bubblewrap, libseccomp and platform runtimes**: installed by
  the operator. No binaries are included. PRoot's embedded loader is extracted
  from the installed package into an owner workspace only, for isolated builds.
- **Dataset bytes**: the exact prepared paper inputs are bundled as five ZIPs
  under `datasets/archives/`. Sources and preparation are recorded in
  `datasets/README.md`. Dataset owners' original terms apply, including Yelp's
  dataset terms; the toolkit MIT license does not relicense these corpora.
  The Python corpus's exact bytes are supplied, but its original upstream
  selection and file-boundary provenance were not recorded.

The tool-only Lab retains unchanged, hash-pinned runtime modules and native
string adapters from the historical 0.7.3.dev1 runtime. Unused earlier application
code, agent guidance, reference parsers and unrelated workload fixtures are
excluded; their prior hashes are retained in `runtime-lock.json` as provenance.
No reference material with an undocumented redistribution license is knowingly
included from the earlier implementation brief.

Platform libc, libm, libstdc++, libgcc and the ELF loader are inventoried by the
measurement tools but are supplied by the host. The paper package convention
assumes the platform and declared standard codec libraries are installed;
dependency-inclusive deployment totals are reported separately.
