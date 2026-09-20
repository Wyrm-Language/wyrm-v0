#!/usr/bin/env bash
# Sets up the nested pypoc/ checkout's Python toolchain: venv, dev deps, and
# a sanity run of pypoc's own test suite. Idempotent - safe to re-run.
#
# vm_plan/README.md: "pypoc/ is a nested git checkout, gitignored. Tooling
# uses pypoc/.venv/bin/wyrm."
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
pypoc_dir="$repo_root/pypoc"
venv_dir="$pypoc_dir/.venv"

if [[ ! -d "$pypoc_dir" ]]; then
    echo "setup_pypoc.sh: $pypoc_dir does not exist (expected a nested pypoc git checkout)" >&2
    exit 1
fi

if [[ ! -d "$venv_dir" ]]; then
    python3 -m venv "$venv_dir"
fi

# `dev` omits the `lsp` extra even though test/test_lsp.py imports pygls's
# lsprotocol, so pull it in too or the suite fails to collect.
"$venv_dir/bin/pip" install -q --upgrade pip
"$venv_dir/bin/pip" install -q -e "$pypoc_dir[dev,lsp]"

(cd "$pypoc_dir" && "$venv_dir/bin/pytest" -q)

echo "wyrm: $venv_dir/bin/wyrm"
