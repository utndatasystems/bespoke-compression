# Original FSST benchmark with our codec wrapper

Start with **[wrapper.cpp](wrapper.cpp)** (83 lines). This is our code, separated
from the authors' unchanged **[filtertest.cpp](../upstream/fsst/paper/filtertest.cpp)**.
Current table.

The wrapper loads the Astra/OnPair+ encoder and decoder libraries, turns the
paper's string vector into the LF-preserving input expected by our codecs, and
returns the complete archive size. Before selective decoding it opens the
archive and initializes decoder state.

For each timed request it converts row IDs, calls `lab_rows`, and returns the
reconstructed byte count. Conversion and returned-offset production are inside
that timed call. Initialization is outside the paper's selective timer.
Each column/selectivity uses 100 warmup calls followed by 100 timed calls with
the initialized decoder. CPU caches are not flushed.

The authors' code still owns row selection, sorting, warmup, repeated timing,
byte comparisons and geometric-mean aggregation. Their existing FSST/LZ4 runners
remain in that file. [run.py](run.py) enables `DEBUG=1`, pins the CPU and repeats
the whole replay three times with rotated method order.

For a focused review of our additions, read the wrapper and the
[measurement paths](../README.md). Consult the upstream
source when checking one of its inherited choices. It is not mixed into our
wrapper, and the original DBText timer is not used by this path.

`wrapper.cpp` was previously named `adapter.cpp`; its source bytes are unchanged.

The upstream `NoCompressionRunner` uses a `vector<string>` for the uncompressed
control. It reports an estimated size of `4 + sum(string length + 4)` bytes per
column, rather than a serialized archive or measured memory footprint. This
counts more row metadata than the input files' one-byte LF separators, so its
input-size / accounted-size factor can be below 1.

## Run

The [paper reproduction guide](../../REPRODUCING.md#dbtext-row-access) gives
commands for FSST, OnPair+, LZ4, uncompressed strings, corrected Astra, and both
GLM row codecs in one replay. `--candidate NAME=MANIFEST` adds a reviewed
row-capable codec to the authors' loop.

Choose an available CPU and a new output directory. Add `--column uuid` for one
column, or repeat `--column` to select several. Inputs are checked against the
published column hashes. Selection changes only which columns enter the original
benchmark; its timing and aggregation code is unchanged.

The earlier per-column package's `benchmark.py` also calls this runner with its
historical implementations. Its shared source URL is preserved. `summary.json`
reports median throughput in thousands of rows/second; `trials.json` and raw
output retain all seven replays.
