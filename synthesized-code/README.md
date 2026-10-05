# Frozen synthesized codecs

[`INDEX.json`](INDEX.json) lists all 42 retained source snapshots and their datasets.
Each has unchanged `source/` files, an original `build-spec.json`, source hashes
and historical provenance. The generic builder verifies and relocates those
recipes into a new build directory; the runner checks exact complete inputs.

Use the root [reproduction guide](../REPRODUCING.md) to build all implementations
or one ID. The final measurement command selects the 32 paper implementations:
16 structured-dataset Astra runs, 12 DBText Astra runs and four DBText GLM runs.
The two A5 codecs, four Luna codecs and four GLM Yelp codecs are supplements.

The canonical plotting code is now under [`paper/figures/`](../paper/figures/README.md),
with its map in [`FIGURES.json`](FIGURES.json). Historical raw CSVs, trials,
qualification notes and provenance remain under the corresponding `data/`
directories. Obsolete plotting entry points and draft PDFs have been removed.
Source-internal scripts and notes needed to explain or rebuild frozen algorithms
remain part of their hashed snapshots.
