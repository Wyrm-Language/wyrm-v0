#!/usr/bin/env python3
"""Regenerate parser AST truth files for test/samples/parser/*.wy."""

import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wytest_env

REPO_ROOT = wytest_env.REPO_ROOT
SAMPLES_DIR = os.path.join(REPO_ROOT, "test", "samples", "parser")


def main():
    wyrm = wytest_env.require_reference("update_sample_parser_truth")
    if wyrm is None:
        return 1
    for name in sorted(os.listdir(SAMPLES_DIR)):
        if not name.endswith(".wy"):
            continue
        path = os.path.join(SAMPLES_DIR, name)
        result = subprocess.run(
            wyrm + ["-m", "wyrm::tools::parse_dump", path],
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
