# Tool-only Compression Lab

This is the stdio MCP facade used by the later runs, around the preserved
**0.7.3.dev1** runtime. `runtime-lock.json` pins the 15 required runtime/license files; 99 unused legacy files and two unused
agent-guidance documents are explicitly excluded from this release. It exposes
**17 tools**, no prompts, resources, skills or model instructions. No model
client, private experiment workspace or credentials are bundled.

## Install and connect an existing CLI

First follow the native dependencies and `sh setup.sh` in
[REPRODUCING.md](../REPRODUCING.md). From the repository root:

```sh
. .venv/bin/activate
python compression-lab-isolated/lab doctor
python compression-lab-isolated/lab --workspace workspaces/example init --cpu 8 --inputs /absolute/path/to/input
codex mcp add compression_lab -- "$PWD/.venv/bin/python" "$PWD/compression-lab-isolated/lab" --workspace "$PWD/workspaces/example" mcp
codex mcp list
```

Choose an allowed CPU shown by `doctor`; CPU 8 is only an example. `init` creates
a new workspace and refuses an existing one. Public inputs and the interface
are copied in; jobs and receipts remain owner-only. For another MCP client,
`python compression-lab-isolated/lab --workspace workspaces/example mcp-config`
prints the equivalent stdio JSON. Restart an already-running CLI after adding
the server.

The source workflow is `build -> evaluate -> validate -> export -> finish`;
a sealed joint DBText workspace uses `evaluate_dbtext`. Candidate manifests
provide source files and compiler argv arrays. Builds are frozen, compiled twice
and compared; full evaluation checks exact reconstructed bytes. Validation
checks capacity, malformed input and memory diagnostics before finish. Owner
sealing and target policy are in `src/lab_interface/scoring.py`; a newly
initialized workspace is explicitly unscored until its owner seals a policy.

## Isolation and server installation

The default backend uses Bubblewrap for research/build paths and the retained
namespace/seccomp runner for measurements. It requires unprivileged Linux user
namespaces. On a server that disables them, initialize explicitly with:

```sh
python compression-lab-isolated/lab --workspace workspaces/server-example init --server-local --cpu 8 --inputs /absolute/path/to/input
```

This needs Landlock ABI 3 or later, libseccomp and Ubuntu's x86-64 PRoot 5.1
package. PRoot supplies virtual paths; mandatory Landlock enforces access.
Measured decoders run directly through the retained Landlock/seccomp launcher,
without PRoot timing overhead. Its embedded loader is relocated into an
owner-only policy directory, avoiding global temporary-directory write access.
`COMPRESSION_LAB_PROOT=/absolute/path/proot` supports a project-local installation.
There is no unrestricted fallback.

The tools are `environment`, `execute`, `build`, `evaluate`, `evaluate_dbtext`,
`validate`, `submit`, `status`, `result`, `jobs`, `cancel`, `artifact`, `compare`,
`profiler_info`, `profile`, `export`, and `finish`. Researcher file operations go
through `execute`; an additional unrestricted host shell defeats this boundary.
Keep owner configuration outside Git. Historical server locations in receipts
are provenance only; access to the original server is not required.

## Verify

```sh
PYTHONPATH=compression-lab-isolated/src python -m unittest discover -s compression-lab-isolated/tests -v
```

Use `LAB_TEST_SERVER_LOCAL=1` for the explicit server backend. Tests exercise
real source builds, complete/selected decoding, MCP transport, private-file
isolation, bad outputs, sanitizer faults and receipt integrity without a model.
Hardware-counter profiling requires host `perf` permissions; unavailable
counters remain unavailable rather than being reported as zero. Core
evaluation and validation do not depend on performance counters.

Frozen paper reproduction uses `reproduce.py`, rather than a newly initialized
Lab: the sealed historical protocols and final driver hashes are retained
separately.
