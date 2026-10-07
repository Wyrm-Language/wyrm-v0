#!/usr/bin/env python3
"""Compile the corpus into .wyd images for the C++ tests.

usage: build_fixtures.py WYRM_BINARY OUT_DIR

The C++ loader/link/golden tests exercise images, but the images are never
committed: they are produced here by the build tree's own compiler, so they
always match the bytecode format under test. Layout mirrors test/corpus:
OUT_DIR/<dir>/<stem>.wyd for

  * every manifest row that is expected to run (status matches/local-only),
  * everything under test/corpus/expand/ (expansion-native fixtures; they
    locate their sibling images through __ARGS[0]),
  * everything under test/corpus/embedded/ (import-resolution fixtures),
  * everything under test/corpus/packages/ (the packages a manifest row
    imports: design/modules.md M1).

Writes OUT_DIR/STAMP last. Exit 1 if any expected-to-compile source fails.
"""

import os
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORPUS = os.path.join(REPO, "test", "corpus")


def sources():
    seen = []
    with open(os.path.join(CORPUS, "manifest.txt")) as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            src, _out, status = (line.split("\t") + ["", ""])[:3]
            if status in ("matches", "local-only"):
                seen.append(src)
    for sub in ("expand", "embedded", "packages"):
        for dirpath, dirs, files in os.walk(os.path.join(CORPUS, sub)):
            dirs.sort()
            for name in sorted(files):
                if name.endswith(".wy"):
                    seen.append(os.path.relpath(os.path.join(dirpath, name), CORPUS))
    return list(dict.fromkeys(seen))


def main():
    wyrm, out_dir = sys.argv[1], sys.argv[2]
    # A compiled image bakes in what its wildcard imports offered when it
    # was compiled (design/modules.md M2), and the .wyd cache is keyed on
    # the source's own mtime only, so a cached importer can go stale when a
    # dependency changes. A fresh build is well under a second: start clean.
    shutil.rmtree(os.path.join(out_dir, ".cache"), ignore_errors=True)
    failures = 0
    built = 0
    for rel in sources():
        src = os.path.join(CORPUS, rel)
        dest = os.path.join(out_dir, os.path.dirname(rel))
        os.makedirs(dest, exist_ok=True)
        # Compiling reads each dependency's exports (design/modules.md M2),
        # so the roots must find them: the source's own directory, the top
        # directory it sits under in the corpus (a package's root, for
        # packages/pkg/impl.wy), and the corpus itself.
        top = os.path.join(CORPUS, rel.split(os.sep)[0]) if os.sep in rel else CORPUS
        roots = list(dict.fromkeys([os.path.dirname(src), top, CORPUS]))
        cmd = [wyrm] + ["-I" + r for r in roots] + [
               "--cache-dir", os.path.join(out_dir, ".cache"),
               "--build-bc", "-o", dest, "--emit", "wyd", src]
        p = subprocess.run(cmd, capture_output=True, text=True)
        if p.returncode != 0:
            failures += 1
            print(f"build_fixtures: {rel}: {p.stderr.strip() or p.stdout.strip()}", file=sys.stderr)
        else:
            built += 1
    if failures:
        return 1
    with open(os.path.join(out_dir, "STAMP"), "w") as f:
        f.write(f"{built} fixtures\n")
    print(f"build_fixtures: {built} fixtures -> {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
