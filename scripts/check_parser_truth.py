#!/usr/bin/env python3
"""Diff fresh parser output against test/samples/parser/*.wy.ast truth files.

Epic 8 M4's comparing golden runner. For every `.wy` in
test/samples/parser/, parses it with the self-hosted wyrm parser
(pypoc/.venv/bin/wyrm -Iwy -m wyrm::parser, matching
update_sample_parser_truth.py's invocation) and diffs the output against
the committed `.ast` file. Exits non-zero with a unified diff on any
mismatch, or if a `.wy` is missing its `.ast` counterpart.
"""

import difflib
import os
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.join(SCRIPT_DIR, "..")
SAMPLES_DIR = os.path.join(REPO_ROOT, "test", "samples", "parser")
WYRM = os.path.join(REPO_ROOT, "pypoc", ".venv", "bin", "wyrm")


def main():
    if not os.path.isfile(WYRM):
        print(f"check_parser_truth: {WYRM} not found, skipping", file=sys.stderr)
        return 77  # meson's "skip" exit code

    failures = 0
    checked = 0
    for name in sorted(os.listdir(SAMPLES_DIR)):
        if not name.endswith(".wy"):
            continue
        wy_path = os.path.join(SAMPLES_DIR, name)
        ast_path = wy_path + ".ast"
        if not os.path.isfile(ast_path):
            print(f"MISSING TRUTH: {name} has no {name}.ast", file=sys.stderr)
            failures += 1
            continue

        result = subprocess.run(
            [WYRM, "-Iwy", "-m", "wyrm::parser", wy_path],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
        )
        checked += 1
        actual = result.stdout
        with open(ast_path, "r") as f:
            expected = f.read()

        if actual != expected:
            failures += 1
            print(f"MISMATCH: {name}", file=sys.stderr)
            diff = difflib.unified_diff(
                expected.splitlines(keepends=True),
                actual.splitlines(keepends=True),
                fromfile=f"{name}.ast (expected)",
                tofile=f"{name} (actual parse)",
            )
            sys.stderr.writelines(diff)
            if result.returncode != 0 and result.stderr:
                print(f"  (parser stderr: {result.stderr.strip()})", file=sys.stderr)

    if failures:
        print(f"check_parser_truth: {failures}/{checked} mismatch(es)", file=sys.stderr)
        return 1
    print(f"check_parser_truth: {checked}/{checked} match")
    return 0


if __name__ == "__main__":
    sys.exit(main())
