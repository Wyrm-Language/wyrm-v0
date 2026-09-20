#!/usr/bin/env python3
"""Epic 8 M5: run every test/wy/*.wy under the external wyrm (see
wytest_env.py: $WYRM / $WYRM_FLAGS, else pypoc, else PATH) and fail if any
of them reports trouble.

Each test/wy/*.wy file now calls exit(1) itself at the point of failure
(confirmed to propagate as the real process exit code - see
wyrm_builtins.py's exit_()), so a failing script's own exit code is the
primary signal. This script also greps stdout for a stray "FAIL" line as
a redundant check, in case some future test prints FAIL without also
exiting non-zero.

Skips cleanly (meson's skip code, 77) when no external wyrm is available.
"""

import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wytest_env

REPO_ROOT = wytest_env.REPO_ROOT
WY_TEST_DIR = os.path.join(REPO_ROOT, "test", "wy")


def main():
    wyrm = wytest_env.require_reference("run_wy_tests")
    if wyrm is None:
        return wytest_env.SKIP

    failures = []
    ran = 0
    for name in sorted(os.listdir(WY_TEST_DIR)):
        if not name.endswith(".wy"):
            continue
        path = os.path.join(WY_TEST_DIR, name)
        result = subprocess.run(
            wyrm + [path],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
        )
        ran += 1
        saw_fail_text = "FAIL" in result.stdout or "FAIL" in result.stderr
        if result.returncode != 0 or saw_fail_text:
            failures.append(name)
            print(f"FAIL: {name} (exit {result.returncode})", file=sys.stderr)
            if result.stdout:
                print(result.stdout, file=sys.stderr)
            if result.stderr:
                print(result.stderr, file=sys.stderr)

    if failures:
        print(f"run_wy_tests: {len(failures)}/{ran} file(s) failed: {', '.join(failures)}", file=sys.stderr)
        return 1
    print(f"run_wy_tests: {ran}/{ran} passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
