#!/usr/bin/env python3
"""Epic 11 M1: regenerate the checked-in builtin module table sources.

The wyrm binary embeds the ported compiler and the library modules it needs
as static wy_module_image C sources under src/embed/, each `<name>.c` beside
its `<name>.wy` (src/embed/std, src/embed/wyrm, ...), plus the generated
table and unity files at the src/embed root. They are checked in - the build
never produces them - so this script is the only way they change, and meson
test fails when they drift from the sources (run_selfcompile calls
check_tree after its fixed point). Hand-written natives beside a module
(`<name>_native.c`) are not generated and are never touched.

Flow (no pypoc: the binary's own embedded compiler is stage0):

1. Build the gen1 compiler tree exactly like run_selfcompile.py: the binary
   compiles the current wy/ sources; every image emitted below is .wyd
   provenance (port-built). Regenerating replaces stage0 with this output.
2. Compile wy/wyrm/tools/embed_build.wy with the tree's own compiler_main
   into a scratch dir (never into the tree: the fixed-point comparison
   walks it).
3. Run embed_build: it compiles the embed list in-process and writes one
   image .c per module plus the generated wyrm_builtins_table.c and the
   unity embedded_images.c.
4. Default: move them into src/wyrm/embedded/. --check: stop after
   comparing with what is checked in; exit nonzero on any drift.

Usage: python3 scripts/regen_builtins.py [--check] [--keep DIR]
"""

import filecmp
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wytest_env
from run_selfcompile import gen1_tree, tree_roots as _tree_roots

ROOT = wytest_env.REPO_ROOT
WYRM = wytest_env.LOCAL_WYRM

EMBEDDED_DIR = wytest_env.EMBED_ROOT
ROOT_GENERATED = ("embedded_images.c", "wyrm_builtins_table.c")


def tree_roots(tree):
    """The -I roots a driver or driver-built tool needs to run from `tree`."""
    return ["-I" + r for r in _tree_roots(tree)]


def emit_from_tree(tree, out_dir):
    """Run embed_build from a self-compiled tree, writing the generated .c
    files into out_dir. Fails the script on any refusal."""
    wy_root = os.path.join(ROOT, "wy")
    scratch = tempfile.mkdtemp(prefix="regen_embed_", dir="/tmp")
    try:
        # embed_build itself is compiled by the tree's own compiler_main,
        # into the scratch dir: the fixed-point walk must not see it.
        entry = os.path.join(tree, "wyrm", "tools", "compiler_main.wyd")
        r = subprocess.run(
            [WYRM] + tree_roots(tree) + [entry, scratch,
             os.path.join(wy_root, "wyrm", "tools", "embed_build.wy")],
            capture_output=True, text=True, timeout=300)
        if r.returncode != 0 or "OK embed_build" not in r.stdout:
            print(r.stdout, r.stderr, file=sys.stderr)
            sys.exit("regen_builtins: embed_build failed to compile")
        embed_build = os.path.join(scratch, "embed_build.wyd")
        # embed_build writes <out>/<dir>/<name>.c and does not create dirs.
        for sub_dir in EMBED_DIRS:
            os.makedirs(os.path.join(out_dir, sub_dir), exist_ok=True)
        r = subprocess.run(
            [WYRM] + tree_roots(tree) + [embed_build, EMBEDDED_DIR, out_dir],
            capture_output=True, text=True, timeout=600)
        sys.stdout.write(r.stdout)
        if r.returncode != 0 or "embed_build: done" not in r.stdout \
                or "EMBED-FAIL" in r.stdout:
            print(r.stderr, file=sys.stderr)
            sys.exit("regen_builtins: embed_build failed")
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


EMBED_DIRS = ("std", "wyrm", os.path.join("wyrm", "compiler"), os.path.join("wyrm", "tools"))


def generated_files(root, all_c=False):
    """Paths (relative to root) of the generated C sources under it: the two
    root-level files, and every `<name>.c` that has a `<name>.wy` beside it.
    Hand-written `*_native.c` files have no such sibling and are excluded.
    all_c: treat every .c as generated (a fresh emit dir has no .wy beside)."""
    found = []
    for dirpath, _dirs, files in os.walk(root):
        for f in files:
            if not f.endswith(".c"):
                continue
            full = os.path.join(dirpath, f)
            rel = os.path.relpath(full, root)
            if all_c or rel in ROOT_GENERATED or os.path.exists(full[:-2] + ".wy"):
                found.append(rel)
    return sorted(found)


def check_tree(tree):
    """Compare a fresh emit against the checked-in sources. True when they
    match byte-for-byte; prints every drift."""
    out = tempfile.mkdtemp(prefix="regen_check_", dir="/tmp")
    try:
        emit_from_tree(tree, out)
        fresh = generated_files(out, all_c=True)
        checked_in = generated_files(EMBEDDED_DIR)
        ok = True
        for name in sorted(set(fresh) | set(checked_in)):
            new = os.path.join(out, name)
            old = os.path.join(EMBEDDED_DIR, name)
            if not os.path.exists(old):
                print("embedded images: %s is generated but not checked in" % name)
                ok = False
            elif not os.path.exists(new):
                print("embedded images: %s is checked in but no longer generated" % name)
                ok = False
            elif not filecmp.cmp(new, old, shallow=False):
                print("embedded images: %s is stale (its source or the compiler moved since it was generated)" % name)
                ok = False
        if ok:
            print("embedded images: %d files up to date with src/embed" % len(fresh))
        else:
            print("embedded images: STALE - run scripts/regen_builtins.py and commit")
        return ok
    finally:
        shutil.rmtree(out, ignore_errors=True)


def main():
    check = "--check" in sys.argv
    keep = None
    if "--keep" in sys.argv:
        keep = sys.argv[sys.argv.index("--keep") + 1]
        os.makedirs(keep, exist_ok=True)

    if not os.path.exists(WYRM):
        print(f"regen_builtins: {WYRM} not built")
        return 1

    work = keep or tempfile.mkdtemp(prefix="regen_work_", dir="/tmp")
    try:
        wy_root = os.path.join(ROOT, "wy")
        tree = os.path.join(work, "gen1")
        if os.path.isdir(os.path.join(tree, "wyrm", "tools")):
            # A kept tree from a previous run: reuse it. Delete it after
            # editing anything under wy/.
            print("regen_builtins: reusing tree " + tree)
        else:
            tree = gen1_tree(work)

        if check:
            return 0 if check_tree(tree) else 1

        out = tempfile.mkdtemp(prefix="regen_emit_", dir="/tmp")
        emit_from_tree(tree, out)
        os.makedirs(EMBEDDED_DIR, exist_ok=True)
        names = generated_files(out, all_c=True)
        for name in names:
            os.makedirs(os.path.dirname(os.path.join(EMBEDDED_DIR, name)), exist_ok=True)
            shutil.copyfile(os.path.join(out, name), os.path.join(EMBEDDED_DIR, name))
        print("regen_builtins: wrote %d files to %s" % (len(names), EMBEDDED_DIR))
        print("now: meson compile -C buildDir && meson test -C buildDir, then commit")
        return 0
    finally:
        if not keep:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
