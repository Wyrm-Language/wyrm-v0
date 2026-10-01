"""Shared environment for the test scripts that need an *external* wyrm.

The external ("reference") interpreter is what behavioral expectations are
checked against; it is deliberately not the build tree under test. Selection,
in order:

  1. $WYRM          - exported path to the interpreter
  2. pypoc          - pypoc/.venv/bin/wyrm, when that checkout is present
  3. `wyrm` on $PATH

$WYRM_FLAGS overrides the interpreter's leading arguments (shell-split, `~`
expanded), e.g.  WYRM=/bin/wyrm WYRM_FLAGS="-I~/wy" scripts/run_wy_tests.py
When unset it defaults to `-I<repo>/src/embed -I<repo>/wy`. Set it empty for
no flags.

The build tree under test is $WYRM_BUILD_DIR (default <repo>/buildDir), which
meson sets to its own tree for the tests it runs.
"""

import os
import shlex
import shutil

SKIP = 77  # meson's "skipped" exit code

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# Import roots for wyrm sources: src/embed holds the modules compiled into the
# binary (std::*, wyrm::*), wy/ the tools and drivers that are not.
EMBED_ROOT = os.path.join(REPO_ROOT, "src", "embed")
WY_ROOT = os.path.join(REPO_ROOT, "wy")
SOURCE_ROOTS = [EMBED_ROOT, WY_ROOT]
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
        flags = ["-I" + r for r in SOURCE_ROOTS]
    return [exe] + flags


def local_wyrm():
    """The build tree's own binary argv prefix, or None when not built."""
    if not os.path.isfile(LOCAL_WYRM):
        return None
    # No -I: the std/wyrm modules come from the binary's own embedded table.
    return [LOCAL_WYRM]


def parse_dump_argv(path):
    """The argv that prints `path`'s tree in D2 Scheme form with the build
    tree's own parser (the one the binary embeds), or None when not built.
    The conformance corpus and test/samples/parser use this form."""
    local = local_wyrm()
    if local is None:
        return None
    return local + ["-I" + WY_ROOT, "-m", "wyrm::tools::parse_dump", "--scheme", path]


def require_reference(tool):
    """reference_wyrm() or print a skip notice and return None."""
    ref = reference_wyrm()
    if ref is None:
        print(f"{tool}: no external wyrm (set WYRM, or install pypoc or a wyrm "
              "on PATH) - skipped", flush=True)
    return ref
