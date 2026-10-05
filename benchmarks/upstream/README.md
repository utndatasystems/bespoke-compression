# Upstream code

| Component | Pinned source | How it is used |
|---|---|---|
| FSST and paper `filtertest.cpp` | [cwida/fsst at e638d4c](https://github.com/cwida/fsst/tree/e638d4cf8c26129d73c242a4127b42b975de5b63) | The required source files are preserved under `fsst/`; [MIT license](fsst/LICENSE). |
| OnPair+ | [umbra-db/token-vldb2026 at e202e36](https://github.com/umbra-db/token-vldb2026/tree/e202e36e2b33a4768d1122f96880b64416234175/src/compressor/onpair_advanced) | `fetch.py` downloads and hash-checks the exact [44-file dependency snapshot](onpair-lock.json) into ignored `token/`. |
| LZ4 / Zstd / LZMA / xxHash | Installed native libraries | Linked by the wrappers; not copied into the repository. Versions used in the recorded tests are documented with the protocols. |

The FSST benchmark source is unchanged; `../fsst-paper/wrapper.cpp` is ours and
adds the `lab_*` interface to its existing runner. We use the original FSST/LZ4
runners and selection/timing/aggregation logic. We do not claim to use the
OnPair+ paper's original benchmark harness. Its encoder calls the upstream
implementation. Its decoder is an ABI adaptation in `onpair.cpp`, following the
authors' dictionary expansion and four-token decode loop, with added row offsets
and capacity checks. This adaptation is part of what the review should inspect.

`../dbtext/reference.cpp` and `../dbtext/onpair.cpp` implement that ABI, framing,
indexes and state management. `onpair-base.cpp` provides only the three abstract
base-class methods needed to link the upstream implementation.

Astra's selected source snapshots use the same pinned OnPair+ tree, with a small
set of files changed by the model. These are explicit `vendor-overrides/`
directories under each applicable candidate. `dbtext/build.py` copies upstream
into the build directory and overlays those files. The baseline uses the
unmodified upstream files. Upstream authorship and copyright remain upstream;
the toolkit's license does not relicense these dependencies.
