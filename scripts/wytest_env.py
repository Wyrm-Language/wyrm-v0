"""Shared environment for the test scripts that need an *external* wyrm.

The external ("reference") interpreter is what behavioral expectations are
checked against; it is deliberately not the build tree under test. Selection,
in order:

  1. $WYRM          - exported path to the interpreter
  2. pypoc          - pypoc/.venv/bin/wyrm, when that checkout is present
  3. `wyrm` on $PATH

$WYRM_FLAGS overrides the interpreter's leading arguments (shell-split, `~`
expanded), e.g.  WYRM=/bin/wyrm WYRM_FLAGS="-I~/wy" scripts/run_wy_tests.py
When unset it defaults to `-I<repo>/wy`. Set it empty for no flags.

The build tree under test is $WYRM_BUILD_DIR (default <repo>/buildDir), which
meson sets to its own tree for the tests it runs.
"""

import os
import shlex
import shutil

SKIP = 77  # meson's "skipped" exit code

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WY_ROOT = os.path.join(REPO_ROOT, "wy")
PYPOC_WYRM = os.path.join(REPO_ROOT, "pypoc", ".venv", "bin", "wyrm")
BUILD_DIR = os.environ.get("WYRM_BUILD_DIR") or os.path.join(REPO_ROOT, "buildDir")
LOCAL_WYRM = os.path.join(BUILD_DIR, "src", "wyrm", "wyrm")


def _expand(token):
    """expanduser, also for `-I~/x` (a flag glued to its path)."""
    if token.startswith("-I"):
        return "-I" + os.path.expanduser(token[2:])
    return os.path.expanduser(token)


def reference_wyrm():
    """The external interpreter's argv prefix (binary plus flags), or None
    when no interpreter is available."""
    exe = os.environ.get("WYRM")
    if not exe:
        if os.path.isfile(PYPOC_WYRM):
            exe = PYPOC_WYRM
        else:
            exe = shutil.which("wyrm")
    if not exe:
        return None
    exe = os.path.expanduser(exe)
    if "WYRM_FLAGS" in os.environ:
        flags = [_expand(t) for t in shlex.split(os.environ["WYRM_FLAGS"])]
    else:
        flags = ["-I" + WY_ROOT]
    return [exe] + flags


def local_wyrm():
    """The build tree's own binary argv prefix, or None when not built."""
    if not os.path.isfile(LOCAL_WYRM):
        return None
    return [LOCAL_WYRM, "-I" + WY_ROOT]


def require_reference(tool):
    """reference_wyrm() or print a skip notice and return None."""
    ref = reference_wyrm()
    if ref is None:
        print(f"{tool}: no external wyrm (set WYRM, or install pypoc or a wyrm "
              "on PATH) - skipped", flush=True)
    return ref
