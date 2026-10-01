#!/usr/bin/env python3
"""Self-compile fixed point for the wyrm compiler, on the C VM. No pypoc.

Generation 0 is stage0: the compiler images embedded in the binary under
test (src/embed/**.c, committed; regenerate with scripts/regen_builtins.py
after a src/embed change - that regeneration is the "regenerate the stage0
reference" step).
Generation 1: the binary compiles the compiler's own current sources
(running src/embed/wyrm/tools/compiler_main.wy in-process, itself compiled by
stage0) into a mirror tree (out1).
Generation 2: generation 1's own compiled entry point
(out1/wyrm/tools/compiler_main.wyd), running on the C VM, compiles the
same sources again (out2).

Bar: byte-identical trees (gen1 == gen2), or failing that the weaker
functional fixed point (gen2 == gen3). Then the convergence check against
stage0: what the fresh tree emits for the embedded builtin table must equal
the committed src/embed/ sources byte-for-byte
(regen_builtins.check_tree), so a src/embed change that is not folded back into
stage0 fails here.

Every self-source is compiled, including wyrm/parser.wy (86 `@accept` sites,
expanded in throwaway VMs by wyrm/compiler/expand.wy) and the expander itself.

Skipped (77) when the build tree's wyrm binary is not built.
"""

import filecmp
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wytest_env

ROOT = wytest_env.REPO_ROOT
WYRM = wytest_env.LOCAL_WYRM

# The compiler's own sources, relative to src/embed/ - the front end and
# compiler modules, plus the package marker, the package root, and the entry
# point itself.
SELF_SOURCES = [
    "std/pairs.wy",
    "std/ctype.wy",
    "wyrm/bjson.wy",
    "wyrm/opcodes.wy",
    "wyrm/image.wy",
    "wyrm/tokenizer.wy",
    "wyrm/decode.wy",
    "wyrm/ast.wy",
    "wyrm/template.wy",
    "wyrm/_dsl.wy",
    "wyrm/compiler/analysis.wy",
    "wyrm/compiler/context.wy",
    "wyrm/compiler/expressions.wy",
    "wyrm/compiler/handlers.wy",
    "wyrm/compiler/statements.wy",
    "wyrm/compiler/functions.wy",
    "wyrm/compiler/classes.wy",
    "wyrm/compiler/verify.wy",
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
    """The C VM resolves the `wyrm` package root via <root>/wyrm.wyd."""
    src = os.path.join(tree, "wyrm", "__init__.wyd")
    if os.path.exists(src):
        shutil.copyfile(src, os.path.join(tree, "wyrm.wyd"))


def wy_roots():
    """Source import roots for running the compiler from src/embed/ (the
    sources use package-relative imports, so `wyrm/` and `wyrm/compiler/` are
    roots too)."""
    embed = wytest_env.EMBED_ROOT
    return [embed, os.path.join(embed, "wyrm"), os.path.join(embed, "wyrm", "compiler")]


def tree_roots(tree):
    """The import roots that make a compiled tree importable."""
    return [tree, os.path.join(tree, "wyrm"), os.path.join(tree, "wyrm", "compiler")]


def compile_tree(driver, roots, out_tree, src_root, cache_dir=None):
    """Run compiler `driver` (a .wy source or a compiled .wyd) with import
    `roots`, mirroring SELF_SOURCES from src_root into out_tree.

    `wyrm/` is a second root: the sources use package-relative imports
    (`import bjson::*` in image.wy), which the C VM resolves as top-level
    names, so the sibling modules must be findable from inside the package.
    out_tree comes before the roots that supply the compiler itself:
    parser.wy's decorator expansion loads the modules it imports (tokenizer,
    ast, _dsl, ...) from the tree being built, which SELF_SOURCES orders
    ahead of it, so they carry this compiler's tree shapes."""
    os.makedirs(out_tree, exist_ok=True)
    ensure_dirs(out_tree, SELF_SOURCES)
    args = [WYRM]
    args += ["-I" + out_tree, "-I" + os.path.join(out_tree, "wyrm")]
    args += ["-I" + r for r in roots]
    if cache_dir:
        args += ["--cache-dir", cache_dir]
    args += [driver, "--mirror", src_root, out_tree] + SELF_SOURCES
    r = subprocess.run(args, capture_output=True, text=True, timeout=600)
    sys.stdout.write(r.stdout)
    if r.returncode != 0:
        print(r.stderr, file=sys.stderr)
        sys.exit("self-compile: generation failed (exit %d)" % r.returncode)
    refused = [line for line in r.stdout.splitlines() if line.startswith("REFUSED ")]
    if refused:
        for line in refused:
            print("refused: " + line, file=sys.stderr)
        sys.exit("self-compile: the compiler refused one of its own sources")
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
            # .wyd since epic 11 M1: the port writes .wyd, pypoc .wyc.
            if f.endswith(".wyd"):
                path = os.path.join(root, f)
                out[os.path.relpath(path, tree)] = path
    return out


def check_embedded_images(tree):
    """Epic 11 M1: the checked-in builtin table sources (src/embed/)
    must match what this fresh tree's compiler produces from src/embed/ - the meson
    staleness check for the embedded images. Runs after the fixed point so a
    drifted embed list fails the test, not just the build."""
    import regen_builtins
    return regen_builtins.check_tree(tree)


def gen1_tree(work):
    """Generation 1: the binary's embedded stage0 compiles the current src/embed
    sources (running compiler_main.wy in-process). Returns the tree."""
    out1 = os.path.join(work, "gen1")
    compile_tree(os.path.join(wytest_env.EMBED_ROOT, "wyrm", "tools", "compiler_main.wy"),
                 wy_roots(), out1, wytest_env.EMBED_ROOT,
                 cache_dir=os.path.join(work, "cache"))
    return out1


def main():
    if not os.path.exists(WYRM):
        print(f"self-compile: {WYRM} not built - skipped")
        return wytest_env.SKIP

    import contextlib
    keep = os.environ.get("SELFCOMPILE_KEEP")
    cm = contextlib.nullcontext(keep) if keep else tempfile.TemporaryDirectory(dir="/tmp")
    if keep:
        os.makedirs(keep, exist_ok=True)
    with cm as work:
        wy_root = wytest_env.EMBED_ROOT
        out1 = gen1_tree(work)
        out2 = os.path.join(work, "gen2")

        # Generation 2: generation 1's own compiler_main, on the C VM,
        # importing generation 1's module tree, compiling the same sources.
        gen2_entry = os.path.join(out1, "wyrm", "tools", "compiler_main.wyd")
        compile_tree(gen2_entry, tree_roots(out1), out2, wy_root)

        w1 = collect_wycs(out1)
        w2 = collect_wycs(out2)

        missing = sorted(set(w1) ^ set(w2))
        differing = sorted(name for name in sorted(set(w1) & set(w2))
                           if not filecmp.cmp(w1[name], w2[name], shallow=False))

        print("\n=== self-compile fixed point ===")
        print("generation 1: %d images, generation 2: %d images" % (len(w1), len(w2)))
        if not missing and not differing:
            print("fixed point: CONVERGED byte-for-byte (diff -r empty)")
            return 0 if check_embedded_images(out1) else 1

        for name in missing:
            print("  missing in one tree: " + name)
        for name in differing:
            print("  differs: " + name)

        if missing:
            print("fixed point: FAILED (image sets differ)")
            return 1

        # Weaker bar: generation 2's compiler recompiles the sources into a
        # tree identical to ITS OWN input tree (gen2 == gen3): the tree is a
        # fixed point of the compilation function.
        out3 = os.path.join(work, "gen3")
        gen3_entry = os.path.join(out2, "wyrm", "tools", "compiler_main.wyd")
        compile_tree(gen3_entry, tree_roots(out2), out3, wy_root)
        w3 = collect_wycs(out3)
        gen23_diff = [name for name in sorted(set(w2) & set(w3))
                      if not filecmp.cmp(w2[name], w3[name], shallow=False)]
        if set(w2) == set(w3) and not gen23_diff:
            print("fixed point: CONVERGED functionally - gen3 is byte-identical "
                  "to its own input tree (gen2); gen1 vs gen2 differs only in "
                  "compiler provenance")
            for name in differing:
                print("  (gen1/gen2 provenance delta: %s)" % name)
            return 0 if check_embedded_images(out2) else 1
        print("fixed point: FAILED (generation 2 -> 3 does not reproduce)")
        for name in gen23_diff:
            print("  differs: " + name)
        return 1


if __name__ == "__main__":
    sys.exit(main())
