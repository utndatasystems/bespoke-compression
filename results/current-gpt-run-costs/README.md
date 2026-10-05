# Current GPT research-run accounting

The **26 published GPT-6 Astra Ultra results from 26–27 September 2026**:
DBText A1–A5 in each of the from-scratch and tools-allowed settings, and
Python, Yelp, OpenStack and SQLStorm TPC-DS A1–A4 from scratch.
Each result is counted once, even when shown in both block and row-access plots.
The [22 September accounting table](../run-costs/README.md) remains a separate,
unchanged historical snapshot. GLM sequences and earlier Astra experiments
are outside this table.

[PDF table](table.pdf) · [LaTeX table](table.tex) · [Exact per-result CSV](runs.csv)
· [Summary CSV](summary.csv) · [Request-level usage](requests.csv)
· [Source and result mapping](sources.json) · [Per-file code counts](code-lines.csv)

<!-- BEGIN SUMMARY -->
| Dataset | Setting | Variants | Recorded API estimate |
|---|---|---:|---:|
| DBText | from-scratch | 5 | $283.11 |
| DBText | tools-allowed | 5 | ChatGPT subscription |
| Python | from-scratch | 4 | $190.89 |
| Yelp | from-scratch | 4 | $142.22 |
| OpenStack | from-scratch | 4 | $130.23 |
| SQLStorm TPC-DS | from-scratch | 4 | $137.29 |
<!-- END SUMMARY -->

**Recorded API estimate: $883.746020 ($883.75 rounded).** This includes original
attempts, failed attempts, all model sessions, restarts and continuations in the five API
campaign ledgers. There is one additional **$8.450125 unresolved reservation**
on DBText from-scratch A3. Its request returned no usage; the reservation is
not a confirmed charge and is excluded from the estimate. The five DBText
tools-allowed results used the ChatGPT subscription; their per-result dollar
cost is unavailable, not zero. No subscription fee has been allocated to them.

## Per-result table

Scratch means from scratch; Tools means tools allowed. All runs used Astra
with Ultra effort. A1–A5 are the labels used in the current figures.

<!-- BEGIN TABLE -->
| Dataset | Setting | Variant | Code lines | Input (M) | Cached (M) | Output (M) | Elapsed (h:mm) | API estimate (USD) |
|---|---|---|---:|---:|---:|---:|---:|---:|
| DBText | Scratch | A1 | 802 | 37.514 | 36.204 | 0.245 | 0:57 | $64.54 |
| DBText | Scratch | A2 | 405 | 33.228 | 32.676 | 0.193 | 0:53 | $48.87 |
| DBText | Scratch | A3 | 672 | 48.361 | 46.967 | 0.353 | 8:10 | $81.26 |
| DBText | Scratch | A4 | 687 | 33.317 | 32.740 | 0.240 | 0:56 | $51.71 |
| DBText | Scratch | A5 | 727 | 21.998 | 21.513 | 0.189 | 1:02 | $36.74 |
| DBText | Tools | A1 | 599 | 28.624 | 27.971 | 0.149 | 0:56 | Subscription |
| DBText | Tools | A2 | 349 | 43.797 | 42.940 | 0.194 | 1:01 | Subscription |
| DBText | Tools | A3 | 326 | 25.399 | 24.872 | 0.118 | 0:33 | Subscription |
| DBText | Tools | A4 | 711 | 68.184 | 66.862 | 0.311 | 1:48 | Subscription |
| DBText | Tools | A5 | 669 | 36.817 | 35.817 | 0.212 | 1:21 | Subscription |
| Python | Scratch | A1 | 100 | 25.984 | 25.354 | 0.223 | 0:38 | $43.74 |
| Python | Scratch | A2 | 834 | 41.031 | 40.281 | 0.326 | 8:51 | $65.43 |
| Python | Scratch | A3 | 231 | 25.107 | 24.540 | 0.215 | 1:36 | $42.04 |
| Python | Scratch | A4 | 574 | 23.827 | 23.330 | 0.209 | 0:51 | $39.68 |
| Yelp | Scratch | A1 | 132 | 13.699 | 13.326 | 0.135 | 0:23 | $24.48 |
| Yelp | Scratch | A2 | 602 | 19.749 | 19.274 | 0.196 | 0:35 | $34.68 |
| Yelp | Scratch | A3 | 157 | 24.428 | 23.906 | 0.159 | 0:30 | $38.13 |
| Yelp | Scratch | A4 | 386 | 27.369 | 26.795 | 0.227 | 0:33 | $44.93 |
| OpenStack | Scratch | A1 | 157 | 22.822 | 22.298 | 0.168 | 0:26 | $36.82 |
| OpenStack | Scratch | A2 | 870 | 13.720 | 13.211 | 0.171 | 8:15 | $27.91 |
| OpenStack | Scratch | A3 | 1,008 | 11.037 | 10.716 | 0.108 | 0:17 | $20.03 |
| OpenStack | Scratch | A4 | 541 | 28.198 | 27.610 | 0.216 | 0:49 | $45.46 |
| SQLStorm TPC-DS | Scratch | A1 | 225 | 28.657 | 28.126 | 0.210 | 0:45 | $44.93 |
| SQLStorm TPC-DS | Scratch | A2 | 119 | 17.215 | 16.781 | 0.162 | 0:25 | $30.06 |
| SQLStorm TPC-DS | Scratch | A3 | 92 | 22.230 | 21.773 | 0.175 | 0:34 | $35.95 |
| SQLStorm TPC-DS | Scratch | A4 | 148 | 15.217 | 14.836 | 0.138 | 0:27 | $26.34 |
<!-- END TABLE -->

Input includes cached input: **do not add those columns together**. Output
already includes reasoning tokens. M denotes one million tokens. Exact token
counts, cache writes, reasoning output, timestamps and result IDs are in the CSV.

**Code lines** are physical source lines containing code, excluding blank lines,
comments and Python docstrings. Counts sum the custom C/C++, headers, Python and
assembly files listed in each published result's `build-spec.json`, including
retained fitting and build scripts. A file shared by the encoder and decoder is
counted once per result. Preprocessor directives and generated executable code
count; imported FSST/PPMd sources, supplied lab interfaces, documentation,
metadata, binaries, and files containing only generated payloads or fitted
tables do not. [code-lines.csv](code-lines.csv) records every selected file,
its hash, count or exclusion reason. This measures the published solution's
source size, not all abandoned candidates or cumulative code written during
the search; physical line counts depend on formatting.

## Cost and timing definitions

API estimates are the sum of settled gateway-ledger charges, recomputed exactly
from returned usage and the **historical campaign rate card** in
[pricing.json](pricing.json). They are not invoices or newly checked list
prices. Cache writes and reads are distinct billing categories; reasoning is
already included in output. All 8,652 settled requests reconcile exactly to
their recorded nanodollar charges. The one uncertain request retains blank
usage and cost fields, rather than invented zeros.

Subscription usage comes from native per-session token events. Repeated
cumulative snapshots are removed; each remaining increment is checked against
the change in the cumulative counter, and its sum against the session's final
counter. The 2,347 usage events include all model sessions. Their
API-equivalent value is deliberately not mixed into spending.

Elapsed is the observed wall-clock span across all model sessions for a result,
including tool execution, compilation, benchmarks, pauses and recovery waits.
For API results it runs from the first request to the last recorded response;
for subscription results, from the first native turn start to the last token
event (request-start times are not exposed there). It excludes subsequent
owner review and offline audits. It is not summed session time or model compute
time. The long A2 times for Python and OpenStack include the pause before their
restarts; DBText A3 includes its recovery pause. Times are descriptive,
not a controlled comparison of model speed.

Specific inclusions:

- DBText A3 includes $25.712176 of continuation usage that was absent from its
  old stage-level snapshot. DBText A5 includes its $2.032900 continuation.
- Python A2 includes the unsuccessful original attempt ($3.297865) plus the
  successful restart ($62.1300295).
- OpenStack A2 includes the unsuccessful original attempt ($4.149337) plus the
  successful restart ($23.764414).
- Baseline measurements, machine/electricity costs, taxes, subscription fees
  and separate supervisor work are outside these model-token estimates.
  Supervisor assistance was not metered in these experiment ledgers; its cost
  is unavailable. Earlier DBText preparation campaigns are outside this scope.

## Reproduce

The sanitized records contain counts, IDs and timestamps, with source digests
and published-result links. They contain no prompts, reasoning text, credentials
or account quota data. The five complete API ledgers were read on 28 September
2026; source hashes bind the snapshots. Original evidence remains unchanged.

Counting uses Pygments 2.17.2 language lexers (Python package `pygments`)
to separate comments from code while preserving string literals and directives.

```sh
python3 results/current-gpt-run-costs/build.py          # verify all records and tables
python3 results/current-gpt-run-costs/build.py --write  # regenerate Markdown, CSV, LaTeX
cd results/current-gpt-run-costs
pdflatex -interaction=nonstopmode -halt-on-error -jobname=table preview.tex
```

The paper needs `\usepackage{booktabs}`, then
`\input{results/current-gpt-run-costs/table.tex}`. The table fits a full-width
paper page; the preview includes the accounting caveats beneath it.
