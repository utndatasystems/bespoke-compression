# Submitted figure map

These assets correspond to **The Case for Synthesized Data Compression**, submitted
2026-10-05. Run `python reproduce.py plots` from the repository root; it makes a
new output tree and writes a hash receipt for all seven PDFs.

| Figure | Canonical source | Output |
|---|---|---|
| 1: OpenStack A1 example | `schematics/openstack-run1-full-row.tex` and `.tikz` | `figure-1.pdf` |
| 2: Codec synthesizer | `schematics/sysdiag-crop.pdf` (original static artwork) | `figure-2.pdf` |
| 3: Four structured bulk Pareto panels | `bulk/plot.py`, `bulk/data/` | `figure-3/bulk-datasets-grouped.pdf` |
| 4: Four DBText random-access panels | `random-access/plot.py`, `random-access/data/` | `figure-4/dbtext-random-access-comparison.pdf` |
| 5: Combined DBText bulk Pareto plot | `dbtext/plotting/plot.py`, `dbtext/data/` | `figure-5/dbtext-astra-glm-paper-ready.pdf` |
| 6: Python indentation schematic | `schematics/indentation_buckets.tex` and `.tikz` | `figure-6.pdf` |
| 7: UUID schematic | `schematics/dbtext-uuid.tex` and `.tikz` | `figure-7.pdf` |

The saved plot records retain their original result IDs and qualifications.
`IMPORT.json` records the historical source-work-area paths and hashes from the
final paper edits. These paths are not needed at runtime. `FILES.json` pins the
portable figure inputs and code. Derived fronts and preview images go into the
new output tree, never back into these immutable inputs.

To edit a schematic, use its standalone `.tex` file in the same directory as the
corresponding `.tikz` file. Existing PDFs are provided so TeX is optional for
paper-figure reproduction. Figure 6's histogram and Figure 7's UUID are illustrative;
they are not additional measured experiments.
