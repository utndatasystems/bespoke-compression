# Final benchmark drivers and adapters

The root `reproduce.py benchmark` command assembles these existing kernels into
one complete paper replay. It records the exact source, input and binary hashes.

| Component | Source |
|---|---|
| Final DBText bulk operation timers and baselines | `final/sources/dbtext-measure.cpp` |
| Final raw-byte FSST-column and OnPair+ structured baselines | `final/sources/bulk-measure.cpp` |
| Original seven-pass strings-v1 structured driver | `dbtext/benchmark.cpp`, `paper/run.py`, `dbtext/run.py` |
| Submitted synthesized DBText selective-row kernel | `../compression-lab-isolated/include/dbtext_rows.cpp`, `final/rows.py` |
| Original FSST/LZ4/uncompressed row loop | `upstream/fsst/paper/filtertest.cpp`, `fsst-paper/run.py` |
| Final OnPair16 row bridge | `../synthesized-code/dbtext/astra/onpair16-row-baseline/` |
| Final orchestration and portable builds | `final/run.py`, `final/build.py`, `paper/build.py` |

`final/sources/SOURCES.json` pins the original server driver bytes and records
historical source paths as provenance, not execution dependencies. The final
FSST source copy includes the original one-line histogram-bound repair used in
the paper; its diff, regression source and original repair receipts are retained.
No encoder search or new tuning is performed during this replay.

OnPair author sources are fetched at the revision and hashes in `upstream/onpair-lock.json`.
The conventional adapters and their source pins remain in `SOURCE_MAP.json` and
`paper/SOURCE.json`. Installation and full commands are in [REPRODUCING.md](../REPRODUCING.md).
