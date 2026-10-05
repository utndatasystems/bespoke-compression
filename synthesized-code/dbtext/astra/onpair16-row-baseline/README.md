# DBText: OnPair+ with the OnPair16 row adapter

Result `onpairplus-onpair16-rows-069b2393d94fb3cd`. Published comparison; the original model-run measurements are unchanged.

The encoder and the timed per-row decoder are unchanged author implementations. The bridge loads the OnPair+ dictionary into OnPair16 and supplies explicit row boundaries. This is a commissioned hybrid, not the unpublished Umbra field implementation.

## Random-access results

Seven fresh passes per method; median of the geometric mean across all 23 original columns. Units: **1M rows / s**. Plots show medians only; observed trial minima and maxima remain in the data files.

| Selected rows | OnPair+ with OnPair16 row adapter | Previous OnPair+ block adapter | FSST | LZ4 block |
|---:|---:|---:|---:|---:|
| 1% | 93.14 | 1.13 | 54.17 | 1.16 |
| 3% | 77.03 | 3.36 | 53.08 | 3.42 |
| 10% | 80.29 | 10.48 | 51.60 | 10.66 |
| 30% | 82.90 | 27.41 | 51.55 | 26.74 |
| 100% | 80.06 | 61.46 | 47.63 | 59.24 |

At 1% selectivity the hybrid is **82.7×** faster than the previous block adapter in this replay. This comparison changes both the access method and table granularity: the hybrid shares one table per column, whereas the old adapter used independent 1000-row blocks.

## Measurement scope

- Same pinned server and CPU8, both shared benchmark locks; sibling32 excluded from affinity. Other research continued on separate research cores. See per-trial CPU counters in the experiment receipts.
- Exact FSST-paper query function: seed123, sorted random row IDs, 100 warm calls and 100 timed calls per selectivity. All 39,841,347 original bytes and 1,492,936 rows are covered.
- Resident dictionary setup is outside query timing, matching FSST. Selected bytes and restored newlines are fully reconstructed inside the timer. No decoded-row cache.
- All 23 saved archives reconstructed independently in a separate ASan/UBSan decoder process and matched original input SHA-256 hashes. Every measured output was checked. The failed initial empty-input bridge diagnostic remains in the private experiment evidence.
- Astra curves retain their existing qualified measurements (including original qualifications); the refreshed baseline curves have seven new passes. Block-compression measurements are byte-for-byte unchanged in the CSV. This row experiment does not create a new block-compression result.

## Size and setup accounting

The hybrid archives total **27,646,020 bytes**, including original OnPair+ dictionaries/headers/payloads and all 64-bit row offsets. The complete custom shared decoder adds **67,480 bytes** once; primary package **27,713,500 bytes**, compression factor **1.4376×**. The standalone CLI adds 22,568 bytes to the strict custom deployment total (**27,736,068 bytes**). Installed standard runtime libraries follow the existing excluded-library convention.
Median cold setup summed across all columns: **36.40 ms**. Median raw-strings-to-complete-archive preparation: **0.918 s**, including training and bridge construction; this is not an encoding-speed optimization experiment. Expanded tables and runtime offsets are itemized in QUALIFICATION.json.

## Review files

- Figures and plotted CSV files are in the parent directory.
- `../plots/dbtext-random-access-baseline.png`: old/new OnPair+ comparison with FSST and LZ4.
- `../plots/dbtext-random-access-{from-scratch,tools-allowed}.png`: updated Astra comparisons.
- `../plots/dbtext-block-comparison.png`: combined from-scratch/tools-allowed block-compression comparison.
- Every figure has annotated PNG/PDF/SVG and a separate `-paper-ready.pdf` without title, subtitle or grey notes.
- `../data/row-points.csv`, `../data/bulk-points.csv`: complete plotted points. `python3 ../plotting/plot.py` regenerates all figures.
- Full bridge, build commands, source hashes, raw timings, checks and failure receipts: the retained experiment `onpairplus-onpair16-rows-2026-09-27`; source hashes are recorded here. Adapter source is included in this folder. Full raw local histories are not published.

## Primary sources

- [Supplied OnPair16 row decoder](https://github.com/umbra-db/token-vldb2026/blob/e202e36e2b33a4768d1122f96880b64416234175/thirdparty/onpair/src/onpair16.cpp)
- [OnPair+ encoder](https://github.com/umbra-db/token-vldb2026/blob/e202e36e2b33a4768d1122f96880b64416234175/src/compressor/onpair_advanced/OnPairAdvancedCompressor.cpp)
- [Author dictionary replay example](https://github.com/umbra-db/token-vldb2026/blob/e202e36e2b33a4768d1122f96880b64416234175/tools/onpairplus_train.cpp)
Accessed 2026-09-27; exact revision and SHA-256 pins retained.

Plot legends abbreviate the current row adapter to **OnPair+** and the block
baseline to **LZ4**. LZ4 and the previous OnPair+ adapter use 1,000-row blocks;
FSST and the current OnPair+ adapter use direct indexed row access. FSST's
`blockSizeIgnored` constructor confirms that the shared harness argument is not
a common block size for all methods. Random-access plots use zero-based linear
throughput axes in **1M rows / s** and retain all original median measurements.
