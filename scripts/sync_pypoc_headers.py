#!/usr/bin/env python3
"""Adopt pypoc's opcode.h and image.h verbatim, and regenerate the opcode
mnemonic table from them.

pypoc/wypoc/compiler_bc/opcodes.py is the single source of truth for the v1
instruction set (vm_plan/README.md: "opcode.h and image.h are copied
verbatim from pypoc/wypoc/compiler_bc/include/wyrm/ and drift is a test
failure", checked by src/test/test_headers_sync.cpp). Run after any change
to pypoc's opcode table:

    scripts/sync_pypoc_headers.py

Idempotent - a second run makes no changes.
"""
import os
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PYPOC_ROOT = os.path.join(REPO_ROOT, "pypoc")
PYPOC_PYTHON = os.path.join(PYPOC_ROOT, ".venv", "bin", "python")
PYPOC_INCLUDE = os.path.join(PYPOC_ROOT, "wypoc", "compiler_bc", "include", "wyrm")
DEST_INCLUDE = os.path.join(REPO_ROOT, "include", "wyrm")

PROVENANCE = (
    "/* Synced verbatim from pypoc/wypoc/compiler_bc/include/wyrm/{name} by\n"
    " * scripts/sync_pypoc_headers.py. Do not hand-edit here - edit the pypoc\n"
    " * source (wypoc/compiler_bc/opcodes.py for opcode.h) and rerun. */\n"
)


def sync_header(name: str) -> None:
    src_path = os.path.join(PYPOC_INCLUDE, name)
    dest_path = os.path.join(DEST_INCLUDE, name)
    with open(src_path) as f:
        content = f.read()
    generated = PROVENANCE.format(name=name) + content
    existing = None
    if os.path.exists(dest_path):
        with open(dest_path) as f:
            existing = f.read()
    if existing == generated:
        print(f"{dest_path}: already up to date")
        return
    with open(dest_path, "w") as f:
        f.write(generated)
    print(f"{dest_path}: synced")


def main() -> int:
    if not os.path.exists(PYPOC_PYTHON):
        print(f"sync_pypoc_headers.py: {PYPOC_PYTHON} missing; run scripts/setup_pypoc.sh first", file=sys.stderr)
        return 1

    # pypoc's own opcode.h must reflect its own opcodes.py before we copy it.
    subprocess.run(
        [PYPOC_PYTHON, os.path.join(PYPOC_ROOT, "tools", "generate_opcode_header.py")],
        cwd=PYPOC_ROOT, check=True,
    )

    sync_header("opcode.h")
    sync_header("image.h")

    names_out = os.path.join(DEST_INCLUDE, "opcode_names.h")
    subprocess.run(
        [PYPOC_PYTHON, os.path.join(PYPOC_ROOT, "tools", "generate_opcode_names.py"), names_out],
        cwd=PYPOC_ROOT, check=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
