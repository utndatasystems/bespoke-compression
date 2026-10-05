#!/bin/sh
# Native system packages are listed in REPRODUCING.md; no sudo or credentials here.
set -eu
cd "$(dirname "$0")"
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-plots.txt
.venv/bin/python -m pip install --require-hashes -r compression-lab-isolated/requirements-mcp.lock
.venv/bin/python reproduce.py check --require-original-recipes
printf '%s\n' 'Installed. Regenerate the submitted plots: .venv/bin/python reproduce.py plots'
