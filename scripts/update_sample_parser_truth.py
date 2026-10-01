#!/usr/bin/env python3
"""Regenerate parser AST truth files for test/samples/parser/*.wy.

Each `.wy.ast` is the build tree's parser output in D2 Scheme form
(`parse_dump --scheme`). Check the new trees against the project's
conformance corpus (or the aligned wypoc's `--dump-ast`) before committing:
the truth files pin the parser, they don't define it."""

import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wytest_env

REPO_ROOT = wytest_env.REPO_ROOT
SAMPLES_DIR = os.path.join(REPO_ROOT, "test", "samples", "parser")


def main():
    if wytest_env.local_wyrm() is None:
        print("update_sample_parser_truth: build the tree first", file=sys.stderr)
        return 1
    for name in sorted(os.listdir(SAMPLES_DIR)):
        if not name.endswith(".wy"):
            continue
        path = os.path.join(SAMPLES_DIR, name)
        result = subprocess.run(
            wytest_env.parse_dump_argv(path),
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            print(f"error: {path}: {result.stderr}", file=sys.stderr)
            continue
        out_path = path + ".ast"
        with open(out_path, "w") as f:
            f.write(result.stdout)
        print(f"wrote {out_path}")


if __name__ == "__main__":
    sys.exit(main())
