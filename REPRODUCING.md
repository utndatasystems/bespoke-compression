# Reproduce the submitted paper

## Install

The native replay is tested on Ubuntu 24.04 x86-64, Python 3.12 and GCC 13.
Several frozen codecs use AVX-512 or `-march=icelake-server`; use a compatible
CPU. Replacing these flags changes the submitted build. Plotting alone does
not need those CPU features or native libraries.

```sh
sudo apt-get update
sudo apt-get install -y git build-essential binutils clang cmake python3-venv bubblewrap proot util-linux libseccomp2 liblz4-dev libzstd-dev libbrotli-dev zlib1g-dev libbz2-dev liblzma-dev libboost-dev
sh setup.sh
. .venv/bin/activate
```

`setup.sh` creates a project-local environment, installs pinned plot dependencies
and the hash-locked MCP SDK, then checks every retained source hash and original
build recipe. It does not install a model client or change account settings.

## Regenerate the submitted figures

```sh
python reproduce.py plots --work work/figures-01
```

Outputs are under `work/figures-01/plots/`: three data-driven PDFs (Figures 3-5)
and four schematic PDFs (Figures 1, 2, 6, 7). The data-driven plots use the final
submitted layout, measurements and labels. Schematic PDF artwork is copied;
Figures 1, 6 and 7 also have editable standalone TikZ sources. See the
[figure map](paper/figures/README.md). Matplotlib may write different PDF metadata
across runs; reproduction means identical recorded data and layout.

## Acquire exact inputs and rebuild every frozen codec

All five exact prepared datasets are included as ZIPs in `datasets/archives/`.
Extract them with the standard-library helper; no separate download is required.

```sh
python datasets/fetch.py --out work/data
python reproduce.py all --data work/data --work work/replay-01
```

`fetch.py` verifies each ZIP and extracted file by byte count and SHA-256.
The manifests cover 29 files: 23 DBText columns, three OpenStack logs and one file
each for Python, Yelp and SQLStorm TPC-DS. SQLStorm has NUL separators; all other
row-oriented inputs retain their original LF boundaries. Details and individual
preparation commands are in [datasets/README.md](datasets/README.md).

`all` verifies, builds and checks **all 42** retained codecs, including the ten
supplementary implementations not plotted in the submitted paper. Every codec
must reconstruct every complete file exactly; row-capable implementations also
reconstruct selected queries and the complete row sequence with correct offsets.
Builds use relocated copies of the original commands and files, leaving frozen
source bytes untouched. GNU `objcopy --redefine-sym` preserves the names of
binary-embedded objects when relocating paths. Existing fitted artifacts are
retained: this is a replay of submitted implementations, not a new synthesis run.

Every build, subprocess log and round-trip receipt is retained in the selected
work directory. Build and plot output directories must be new. Failures stop
certification; inspect their evidence before retrying with a new directory.

## Fresh measurements and the same figure layouts

After `all` has passed, select one allowed CPU (`python -c 'import os; print(sorted(os.sched_getaffinity(0)))'`):

```sh
python reproduce.py benchmark --data work/data --work work/replay-01 --cpu 8
```

This replays the **32 paper codecs**, the plotted conventional configurations,
and all DBText row controls, then renders the same three data-driven figure
layouts into `work/replay-01/fresh-plots/`. Raw trials, archive hashes, compiler
receipts, timings and both archive/package and dependency-inclusive size
accounting are retained under `final-benchmarks/`. New inputs for the plots go
into `fresh-figure-inputs/`; the saved paper measurements are never overwritten.

The unchanged native kernels use one discarded warmup and seven measured bulk
decodes, with exact reconstruction checked on every pass. DBText synthesized
row curves use the unchanged selection driver (seed 123, sorted nested subsets,
100 warmups and 100 timed calls) and three outer replays. Conventional row controls
retain their original seven-replay FSST and OnPair16 loops. Bulk throughput and
selected-row throughput measure different operations.

Use `--datasets dbtext` for a first-stage measurement; the complete fresh plots
are produced only once all five datasets are complete. Existing attempt directories
are not silently replayed. On a shared server add `--project-lock /path/to/shared.lock`;
the runner also acquires `/tmp/compression-lab-host-benchmark-v1.lock`.
`--deps-prefix /path/to/usr` supports project-local native development libraries.

If a structured stage stops, `benchmarks/final/resume_structured.py` can explicitly
continue its existing sealed workload. It verifies retained result IDs, raw-trial
hashes, input and binary identities, then measures only missing configurations.
Failed methods require an explicit choice and receive one retry with unchanged
trial counts. Original failed receipts remain intact.
For example, inspect a stopped Yelp attempt before continuing it:

```sh
python benchmarks/final/resume_structured.py --dataset yelp --data work/data --work work/replay-01 --cpu 8 --project-lock work/shared.lock --retry-failed brotli11 --inspect
```

Remove `--inspect` to execute that verified plan once. Successful measurements
are never repeated. A further failure stops completion; the helper does not
hide failures or substitute saved values. After all five stages
are complete, render their fresh inputs into a new output directory:

```sh
python -c 'from pathlib import Path; import reproduce; reproduce.plots(Path("work/replay-01/fresh-plots"), Path("work/replay-01/fresh-figure-inputs"))'
```

Fresh speeds depend on hardware, compiler, dependencies and load; equality with
paper timings is not promised. The replay launchers preserve the original
single-core limits and operation timers for the reviewed frozen codecs. They
are not a filesystem sandbox for arbitrary untrusted submissions. The separate
MCP Lab enforces filesystem isolation and validation when researching new codecs.
A replay round trip does not erase historical source-review or qualification caveats.

Builds, compression, decompression, validation and MCP jobs have no wall-clock or
CPU-time timeout. They run until completion or explicit cancellation, including
slow Brotli configurations. CPU affinity, memory limits and byte-exact checks
remain in place.

## Run one codec

Choose an ID from [`synthesized-code/INDEX.json`](synthesized-code/INDEX.json):

```sh
python reproduce.py build --work work/one-01 --only astra-openstack-from-scratch-run-01
python reproduce.py verify --data work/data --work work/one-01 --only astra-openstack-from-scratch-run-01
```

The resulting `build/<ID>/manifest.json` names the encoder and decoder shared
libraries. [`include/codec.h`](compression-lab-isolated/include/codec.h) defines
the native ABI. The generic runner checks all files in that codec's exact dataset.

## Tool-only MCP installation

Follow [the Lab README](compression-lab-isolated/README.md) for a new isolated
workspace and the exact `codex mcp add` command. Frozen reproduction does not
require MCP or an LLM. Namespace isolation needs unprivileged Linux user
namespaces; an explicit Landlock/seccomp server backend is provided for hosts
that disable them. No unrestricted fallback is used.

## Size and provenance boundaries

The paper convention charges all archive bytes, headers, indexes, dictionaries
and the whole custom decoder once per dataset. Its standard-library exemptions
are retained; dependency-inclusive deployment sizes are reported separately. DBText native
codec ledgers include the entire decoder dependency closure. Baseline ledgers
that use the combined measurement driver explicitly report a deployment upper
bound: that driver includes multiple codecs and encoders. This applies to the
DBText conventional baselines and the structured FSST-column/OnPair+ controls;
it does not claim a minimal standalone decoder deployment size.
Bespoke fitted tables and embedded model bytes remain charged in their stored
artifacts. A compression ratio is input bytes divided by charged bytes; bits per
byte is eight divided by that ratio. Baseline encoder/decoder installations are
shared under the paper convention, not silently counted as zero in deployment
accounting.

The Python corpus is described as PyTorrent in the paper, but the recorded
experiment does not establish the original PyTorrent selection or file boundaries.
Its exact author-supplied bytes are pinned and bundled, so the submitted input
can be reproduced. The original upstream selection remains undocumented; we
do not invent that provenance or replace the corpus with CPython.
