#!/usr/bin/env python3
"""Behavioral conformance: does the build tree under test run the corpus the
way the language says it should?

Every row of test/corpus/manifest.txt is a .wy source plus its expected
stdout. Each source is run from source (the binary compiles it in-process)
by the local build ($WYRM_BUILD_DIR, default buildDir), and its stdout and
exit status are checked against the committed .out. When an external wyrm is
available (see wytest_env.py: $WYRM / $WYRM_FLAGS, else pypoc, else PATH) the
same source is also run there and the two runs must agree. No bytecode is
compared - only behavior - so the bytecode format and compiler may change
freely.

Manifest columns (tab separated):
  source   expected-stdout|-   status   note
status:
  matches     local must equal the .out (or exit 0 when the .out is `-`), and
              the external wyrm, if present, must agree
  local-only  as `matches`, but the external wyrm cannot run it (it uses
              builtins only this VM defines), so it is never consulted
  DIVERGES    known local gap: expected to differ; passing is a failure (stale row)
  REFUSED     known: the compiler declines the source; accepting it is a failure

--reference-only runs just the external wyrm against the .out (useful to see
whether a given external wyrm is a faithful oracle).

Exit: 0 all as expected, 1 failures, 77 nothing to run (no local build).
"""

import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wytest_env

CORPUS = os.path.join(wytest_env.REPO_ROOT, "test", "corpus")
TIMEOUT = 120


def manifest_rows():
    rows = []
    with open(os.path.join(CORPUS, "manifest.txt")) as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t") + [""] * 4
            rows.append(tuple(parts[:4]))
    return rows


def import_roots(src):
    """A fixture's own directory first (siblings import each other by bare
    name, and two directories may each define e.g. `shapes`), then the
    corpus root."""
    roots = [os.path.dirname(src), CORPUS]
    return ["-I" + r for r in dict.fromkeys(roots)]


def run(prefix, extra, src, cwd):
    try:
        p = subprocess.run(prefix + extra + [src], cwd=cwd, capture_output=True, timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return None, b"", b"timeout"
    return p.returncode, p.stdout, p.stderr


def main():
    reference_only = "--reference-only" in sys.argv[1:]
    local = wytest_env.local_wyrm()
    ref = wytest_env.reference_wyrm()
    if reference_only:
        if ref is None:
            print("run_behavior: no external wyrm available", file=sys.stderr)
            return 1
        local = ref
    elif local is None:
        print(f"run_behavior: {wytest_env.LOCAL_WYRM} not built - skipped")
        return wytest_env.SKIP
    if not reference_only and ref is None:
        print("run_behavior: no external wyrm; checking against committed .out only")

    failures = []
    counts = {}
    with tempfile.TemporaryDirectory() as scratch:
        for source, out, status, note in manifest_rows():
            src = os.path.join(CORPUS, source)
            expected = None
            if out != "-":
                with open(os.path.join(CORPUS, out), "rb") as f:
                    expected = f.read()
            extra = import_roots(src)
            # A private cache keeps __wycache__ out of the corpus tree (the
            # external wyrm ignores nothing here: it is only handed `extra`).
            rc, stdout, stderr = run(local + ["--cache-dir", os.path.join(scratch, "cache")]
                                     if not reference_only else local, extra, src, scratch)
            ok = rc == 0 and (expected is None or stdout == expected)
            verdict, why = "matches", ""

            if status in ("matches", "local-only"):
                if not ok:
                    verdict = "FAIL"
                    why = ("exit %s" % rc) if rc != 0 else "stdout differs from " + out
                elif not reference_only and status == "matches" and ref is not None:
                    rrc, rout, _ = run(ref, extra, src, scratch)
                    if rrc != rc or rout != stdout:
                        verdict = "FAIL"
                        why = "local and external wyrm disagree (external exit %s)" % rrc
            elif status == "DIVERGES":
                verdict = "DIVERGES"
                if ok:
                    verdict, why = "FAIL", "listed DIVERGES but now matches - update the manifest"
            elif status == "REFUSED":
                verdict = "REFUSED"
                if rc == 0:
                    verdict, why = "FAIL", "listed REFUSED but now runs - update the manifest"
            else:
                verdict, why = "FAIL", "unknown status %r" % status

            counts[verdict] = counts.get(verdict, 0) + 1
            if verdict == "FAIL":
                failures.append(source)
                print(f"FAIL {source}: {why}", file=sys.stderr)
                if stderr:
                    print("    " + stderr.decode(errors="replace").strip().splitlines()[-1][:200]
                          if stderr.strip() else "", file=sys.stderr)

    summary = ", ".join(f"{n} {v}" for v, n in sorted(counts.items()))
    print(f"run_behavior: {summary}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
