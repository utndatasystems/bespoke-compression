# The Case for Synthesized Data Compression

Frozen codecs, measurements and reproduction tools for the submitted paper.
No model account or API key is needed to reproduce its figures or run its codecs.

```sh
sh setup.sh
.venv/bin/python reproduce.py plots
```

The seven figure PDFs appear in `work/reproduction/plots/`. These use the
paper's recorded measurements and final layouts, including the DBText package
sizes in the random-access legends.

For native builds, exact input acquisition and fresh measurements, follow
[REPRODUCING.md](REPRODUCING.md). The tested platform is Ubuntu 24.04 x86-64,
Python 3.12 and GCC 13, with the submitted CPU-specific compiler flags preserved.

| Directory | Contents |
|---|---|
| [`paper/figures/`](paper/figures/README.md) | Final plot code, saved inputs and schematic artwork |
| [`synthesized-code/`](synthesized-code/README.md) | 42 frozen implementations and their original build recipes; 32 appear in the paper |
| [`benchmarks/`](benchmarks/README.md) | Final native drivers, baseline adapters and pinned upstream sources |
| [`datasets/`](datasets/README.md) | Five exact dataset ZIPs, input hashes and byte-preserving preparation |
| [`compression-lab-isolated/`](compression-lab-isolated/README.md) | Tool-only stdio MCP server, without agent prompts or skills |
| [`results/`](results/README.md) | Historical evidence and dated reproduction receipts |

All five exact input corpora are bundled in Git. Extract and verify them with
`.venv/bin/python datasets/fetch.py --out work/data`. Their upstream terms and
provenance are listed in the dataset documentation; the original Python corpus
selection remains undocumented. Private experiment workspaces are excluded.
