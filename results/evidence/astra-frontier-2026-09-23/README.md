# Astra LZ4-target additions

Twelve variants from three additional GPT-6 Astra Ultra experiments, added to
the paper figures and full tables on 23 September 2026. Existing points and
measurements are retained. Hollow blue diamonds distinguish these variants.

| Dataset | Paper IDs | Modes, in ID order |
|---|---|---|
| Yelp | A5–A9 | LZ fast; LZ balanced; LZ compact; lexical; lexical compact |
| OpenStack | A7–A9 | Learned templates; templates + LZ4; LZ + packed literals |
| SQLStorm TPC-DS | A6–A9 | BPE balanced; BPE fast decode; BPE compact; LZ fast encode |

[measurements.json](measurements.json) retains input identities, byte accounting,
timing summaries, binary hashes, original qualification IDs, export hashes and
the numerical records for two matched LZ4 controls per candidate. The adjacent
JSONL files contain the unchanged raw CPU 8 trials, including the discarded
warmup. `python3 results/reproduce.py` checks their hashes, byte-exact flags,
CPU affinity, medians, ranges and package arithmetic before checking the tables
and both Pareto frontiers.

All added plot points use the paper's **CPU 8** RAM protocol: one warmup and seven
measured corpus passes, decoder setup and full reconstruction included, without
cache flushing. OpenStack sums matching pass times across its three input files
before taking the median. Original CPU 10 research timings are not plotted.
Custom decoders and all archive data are charged; installed standard libraries
and baseline benchmark wrappers cost zero.

The unchanged exported binaries passed the original full Lab qualification.
The replay review verified exact reconstruction, source-export manifests,
archive and binary identities, and raw timing/accounting evidence. It was not
the separate comprehensive source audit documented for the earlier 43 model
configurations in `codecs/`. This update publishes numerical evidence; it does
not add these twelve implementations to that source collection.

Every selected candidate also beats its freshly replayed HC-9 and HC-12 controls
on package size and decoding throughput, with separated observed timing ranges.
Those matched controls remain here as verification evidence; the paper's
conventional frontier keeps its existing baseline measurements. Encoding
throughput is reported separately in the full tables. Dominated modes remain
in the data even if they do not extend the combined size/decode frontier.

Sources are the retained `astra-frontier-2026-09-23` Yelp/SQLStorm final CPU 8
replays and `astra-openstack-frontier-2026-09-22/cpu8-replay`. Relative source
paths and SHA-256 receipts are included in each measurement. The unrelated
Yelp GLM continuation present in the experiment gallery is not part of this
Astra-only update. Research cost tables retain their separately stated
22 September cohort.
