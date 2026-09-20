#!/usr/bin/env python3
"""Epic 11 M1: regenerate the checked-in builtin module table sources.

The wyrm binary embeds the ported compiler and the library modules it needs
as static wy_module_image C sources under src/wyrm/embedded/. They are
checked in - the build never produces them - so this script is the only way
they change, and meson test fails when they drift from wy/ (run_selfcompile
calls check_tree after its fixed point).

Flow (needs pypoc/.venv today, like the sweep tests; once `wyrm script.wy`
compiles in-process (epic 11 M2) step 1 can shrink to the built binary's own
embedded compiler):

1. Build the gen1 compiler tree exactly like run_selfcompile.py: the gen0
   amalgam driver is the only pypoc-built artifact, and it is not embedded -
   every image emitted below is .wyd provenance (port-built).
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
from run_corpus_sweep import ROOT, WYRM, build_amalgam, build_expander
from run_selfcompile import compile_tree

EMBEDDED_DIR = os.path.join(ROOT, "src", "wyrm", "embedded")


def tree_roots(tree):
    """The -I roots a driver or driver-built tool needs to run from `tree`."""
    return ["-I" + tree,
            "-I" + os.path.join(tree, "wyrm"),
            "-I" + os.path.join(tree, "wyrm", "compiler")]


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
        r = subprocess.run(
            [WYRM] + tree_roots(tree) + [embed_build, wy_root, out_dir],
            capture_output=True, text=True, timeout=600)
        sys.stdout.write(r.stdout)
        if r.returncode != 0 or "embed_build: done" not in r.stdout \
                or "EMBED-FAIL" in r.stdout:
            print(r.stderr, file=sys.stderr)
            sys.exit("regen_builtins: embed_build failed")
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


def generated_files(out_dir):
    return sorted(f for f in os.listdir(out_dir) if f.endswith(".c"))


def check_tree(tree):
    """Compare a fresh emit against the checked-in sources. True when they
    match byte-for-byte; prints every drift."""
    out = tempfile.mkdtemp(prefix="regen_check_", dir="/tmp")
    try:
        emit_from_tree(tree, out)
        fresh = generated_files(out)
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
                print("embedded images: %s is stale (wy/ moved since it was generated)" % name)
                ok = False
        if ok:
            print("embedded images: %d files up to date with wy/" % len(fresh))
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

    if not os.path.exists(os.path.join(ROOT, "pypoc", ".venv", "bin", "wyrm")):
        print("regen_builtins: pypoc/.venv not set up - cannot rebuild the seed tree")
        return 1

    work = keep or tempfile.mkdtemp(prefix="regen_work_", dir="/tmp")
    try:
        wy_root = os.path.join(ROOT, "wy")
        tree = os.path.join(work, "gen1")
        if os.path.isdir(os.path.join(tree, "wyrm", "tools")):
            # A kept tree from a previous run: reuse it. Pass --fresh after
            # editing anything under wy/ that the amalgam or the expander
            # build consumes.
            print("regen_builtins: reusing tree " + tree)
        else:
            driver = build_amalgam(work)
            build_expander(work, driver)
            compile_tree(driver, work, tree, wy_root)

        if check:
            return 0 if check_tree(tree) else 1

        out = tempfile.mkdtemp(prefix="regen_emit_", dir="/tmp")
        emit_from_tree(tree, out)
        os.makedirs(EMBEDDED_DIR, exist_ok=True)
        names = generated_files(out)
        for name in names:
            shutil.copyfile(os.path.join(out, name), os.path.join(EMBEDDED_DIR, name))
        print("regen_builtins: wrote %d files to %s" % (len(names), EMBEDDED_DIR))
        print("now: meson compile -C buildDir && meson test -C buildDir, then commit")
        return 0
    finally:
        if not keep:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
