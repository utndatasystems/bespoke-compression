# Saved baseline trials

[baselines.json](baselines.json) contains 107 conventional configurations from
the recorded server measurements: input hashes, archive hashes and sizes, exact
reconstruction flags, one warmup, seven measured passes, library identities and
size accounting. For 27 configurations it also retains the separate decode
replay and verifies that the decoder binary matches the original sweep.

This is a numeric export of retained evidence. Host-specific paths are omitted;
`original_result_sha256` and `original_trials_sha256` identify the original
receipts, not this JSON projection. Model trials and source pins are alongside
each implementation in Codec.

[clp-trials.json](clp-trials.json) retains all 56 OpenStack CLI passes, including
the warmups, per-file times and complete archive inventories.

Run `python3 results/reproduce.py` from the repository root to check the saved
measurements against the tables and paper plots. New measurements are stored in
new output directories and do not replace these recorded trials.
