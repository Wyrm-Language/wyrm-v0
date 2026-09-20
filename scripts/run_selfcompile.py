#!/usr/bin/env python3
"""Epic 10 M7: self-compile fixed point for the ported wyrm compiler.

Generation 1: the pypoc-compiled compiler driver (the corpus sweep's
amalgam) compiles the compiler's own sources into a mirror tree (out1).
Generation 2: generation 1's own compiled entry point
(out1/wyrm/tools/compiler_main.wyc), running on the C VM, compiles the
same sources again (out2).

The bar per the epic's approved deviation: functionally equivalent
images. In practice the port is deterministic, so the report also records
whether the stronger bar - byte-identical trees (the epic's original
`diff -r`) - came free. Exit status is nonzero when neither bar holds.

Epic 10a M5: every self-source is compiled by the port, including
wyrm/parser.wy (86 `@accept` sites, expanded in throwaway VMs by
wyrm/compiler/expand.wy) and the expander itself. The interim "pypoc builds
parser.wyc" arrangement of epic 10 M7 is retired; the only pypoc-built code
is generation 0 (the amalgam driver). gen0's expansion support (wyrm/_dsl.wy,
wyrm/ast.wy) is rebuilt by gen0 itself (run_corpus_sweep.build_expander) so
its tree shape is the port's.

Skipped entirely when pypoc/.venv is absent.
"""

import filecmp
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from run_corpus_sweep import build_amalgam, build_expander, ROOT, WYRM

# The compiler's own sources, relative to wy/ - everything the driver
# amalgam concatenates, plus the package marker, the package root, and
# the entry point itself.
SELF_SOURCES = [
    "std/pairs.wy",
    "std/ctype.wy",
    "wyrm/bjson.wy",
    "wyrm/opcodes.wy",
    "wyrm/image.wy",
    "wyrm/tokenizer.wy",
    "wyrm/decode.wy",
    "wyrm/ast.wy",
    "wyrm/_dsl.wy",
    "wyrm/compiler/analysis.wy",
    "wyrm/compiler/context.wy",
    "wyrm/compiler/expressions.wy",
    "wyrm/compiler/handlers.wy",
    "wyrm/compiler/statements.wy",
    "wyrm/compiler/functions.wy",
    "wyrm/compiler/classes.wy",
    "wyrm/compiler/verify.wy",
    "wyrm/compiler/predefined.wy",
    "wyrm/compiler/expand.wy",
    "wyrm/compiler/module.wy",
    "wyrm/compiler/expansion.wy",
    "wyrm/parser.wy",
    "wyrm/compiler.wy",
    "wyrm/__init__.wy",
    "wyrm/tools/compiler_main.wy",
]

def ensure_dirs(tree, rels):
    for rel in rels:
        os.makedirs(os.path.join(tree, os.path.dirname(rel)), exist_ok=True)


def root_package_alias(tree):
    """The C VM resolves the `wyrm` package root via <root>/wyrm.wyc."""
    src = os.path.join(tree, "wyrm", "__init__.wyc")
    if os.path.exists(src):
        shutil.copyfile(src, os.path.join(tree, "wyrm.wyc"))


def compile_tree(driver_wyc, extra_include, out_tree, src_root):
    os.makedirs(out_tree, exist_ok=True)
    ensure_dirs(out_tree, SELF_SOURCES)
    # `wyrm/` is a second root: the sources use package-relative imports
    # (`import bjson::*` in image.wy), which the C VM resolves as top-level
    # names, so the sibling modules must be findable from inside the package.
    # out_tree is on the path last: parser.wy's decorator expansion loads
    # the modules it imports (tokenizer, ...) from the tree being built,
    # which SELF_SOURCES orders ahead of it.
    args = [WYRM, "-I" + extra_include, "-I" + os.path.join(extra_include, "wyrm"),
            "-I" + os.path.join(extra_include, "wyrm", "compiler"),
            "-I" + out_tree, "-I" + os.path.join(out_tree, "wyrm"), driver_wyc,
            "--mirror", src_root, out_tree] + SELF_SOURCES
    r = subprocess.run(args, capture_output=True, text=True, timeout=600)
    sys.stdout.write(r.stdout)
    if r.returncode != 0:
        print(r.stderr, file=sys.stderr)
        sys.exit("self-compile: generation failed (exit %d)" % r.returncode)
    # Epic 10a M0: the port compiles strictly now. A STUB line is a body
    # that failed to lower and stubbed anyway - only a `@template`
    # definition may stub (TSTUB; the expected set is _dsl.wy's two
    # `this`-using plain-fn templates), so a bare STUB fails the
    # generation.
    stubs = [line for line in r.stdout.splitlines() if line.startswith("STUB ")]
    if stubs:
        for line in stubs:
            print("unexpected stub: " + line, file=sys.stderr)
        sys.exit("self-compile: strict compile produced a non-template stub")
    tstubs = [line for line in r.stdout.splitlines() if line.startswith("TSTUB ")]
    for line in tstubs:
        print("  (template stub: %s)" % line.strip())
    root_package_alias(out_tree)
    return out_tree


def collect_wycs(tree):
    out = {}
    for root, _dirs, files in os.walk(tree):
        for f in files:
            if f.endswith(".wyc"):
                path = os.path.join(root, f)
                out[os.path.relpath(path, tree)] = path
    return out


def main():
    if not os.path.exists(os.path.join(ROOT, "pypoc", ".venv", "bin", "wyrm")):
        print("self-compile: pypoc/.venv not set up - skipped")
        return 0

    import contextlib
    keep = os.environ.get("SELFCOMPILE_KEEP")
    cm = contextlib.nullcontext(keep) if keep else tempfile.TemporaryDirectory(dir="/tmp")
    if keep:
        os.makedirs(keep, exist_ok=True)
    with cm as work:
        gen1_driver = build_amalgam(work)

        wy_root = os.path.join(ROOT, "wy")
        out1 = os.path.join(work, "gen1")
        out2 = os.path.join(work, "gen2")

        # Generation 1: the pypoc-compiled amalgam driver.
        build_expander(work, gen1_driver)
        compile_tree(gen1_driver, work, out1, wy_root)

        # Generation 2: generation 1's own compiler_main, on the C VM,
        # importing generation 1's module tree, compiling the same sources.
        gen2_entry = os.path.join(out1, "wyrm", "tools", "compiler_main.wyc")
        compile_tree(gen2_entry, out1, out2, wy_root)

        w1 = collect_wycs(out1)
        w2 = collect_wycs(out2)

        missing = sorted(set(w1) ^ set(w2))
        differing = sorted(name for name in sorted(set(w1) & set(w2))
                           if not filecmp.cmp(w1[name], w2[name], shallow=False))

        print("\n=== self-compile fixed point ===")
        print("generation 1: %d images, generation 2: %d images" % (len(w1), len(w2)))
        if not missing and not differing:
            print("fixed point: CONVERGED byte-for-byte (diff -r empty)")
            return 0

        for name in missing:
            print("  missing in one tree: " + name)
        for name in differing:
            print("  differs: " + name)

        if missing:
            print("fixed point: FAILED (image sets differ)")
            return 1

        # The bar the epic actually requires: functionally equivalent
        # images. Both trees are complete compilers - the strongest
        # available check here is that generation 2's compiler recompiles
        # the sources into a tree identical to ITS OWN input tree
        # (generation 2 == generation 3 means the tree is a fixed point
        # of the compilation function, which is the property the epic
        # wants; byte-identity between gen1 and gen2 is the stronger,
        # incidental form of the same thing).
        out3 = os.path.join(work, "gen3")
        gen3_entry = os.path.join(out2, "wyrm", "tools", "compiler_main.wyc")
        compile_tree(gen3_entry, out2, out3, wy_root)
        w3 = collect_wycs(out3)
        gen23_diff = [name for name in sorted(set(w2) & set(w3))
                      if not filecmp.cmp(w2[name], w3[name], shallow=False)]
        if set(w2) == set(w3) and not gen23_diff:
            print("fixed point: CONVERGED functionally - generation 2's "
                  "output (gen3) is byte-identical to its own input tree "
                  "(gen2); gen1 vs gen2 differs only in compiler "
                  "provenance (amalgam vs linked module set)")
            for name in differing:
                print("  (gen1/gen2 provenance delta: %s)" % name)
            return 0
        print("fixed point: FAILED (generation 2 -> 3 does not reproduce)")
        for name in gen23_diff:
            print("  differs: " + name)
        return 1


if __name__ == "__main__":
    sys.exit(main())
