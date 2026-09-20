#!/usr/bin/env python3
"""Epic 10 M7: corpus sweep for the ported wyrm compiler, run on the C VM.

Builds the compiler-driver amalgam (front end + compiler modules +
compiler_main.wy) with pypoc's compiler_bc, then drives it over the
corpus: every manifest row with a `.wy` source. Each fixture's compiled
.wyc is run on the C VM and its stdout diffed against the corpus .out.

Per-fixture verdicts reuse the conformance vocabulary:
  matches   - compiled, ran, output identical to .out
  DIVERGES  - known C VM/reference gap (reason from manifest.txt honored)
  REFUSED   - the ported compiler declines (parse/compile/serialize)
  FAIL      - compiled and ran but output differs, or the run faulted

Exit status is nonzero when anything is neither matches nor a recorded
DIVERGES/REFUSED.

Skipped entirely (like run_wy_tests.py) when pypoc/.venv is absent.
"""

import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WYRM = os.path.join(ROOT, "buildDir", "src", "wyrm", "wyrm")
PYPOC_WYRM = os.path.join(ROOT, "pypoc", ".venv", "bin", "wyrm")
MANIFEST = os.path.join(ROOT, "test", "bytecode", "manifest.txt")

# Concatenation order: std shards, serialization/front end, compiler
# modules, driver main.
AMALGAM_FILES = [
    "wy/std/pairs.wy",
    "wy/std/ctype.wy",
    "wy/wyrm/bjson.wy",
    "wy/wyrm/opcodes.wy",
    "wy/wyrm/image.wy",
    "wy/wyrm/tokenizer.wy",
    "wy/wyrm/decode.wy",
    "wy/wyrm/ast.wy",
    "wy/wyrm/_dsl.wy",
    "wy/wyrm/parser.wy",
    "wy/wyrm/compiler/analysis.wy",
    "wy/wyrm/compiler/context.wy",
    "wy/wyrm/compiler/expressions.wy",
    "wy/wyrm/compiler/handlers.wy",
    "wy/wyrm/compiler/statements.wy",
    "wy/wyrm/compiler/functions.wy",
    "wy/wyrm/compiler/classes.wy",
    "wy/wyrm/compiler/verify.wy",
    "wy/wyrm/compiler/predefined.wy",
    "wy/wyrm/compiler/module.wy",
    "wy/wyrm/compiler/expansion.wy",
    "wy/wyrm/tools/compiler_main.wy",
]

STRIP_IMPORT = r"^import (bjson|opcodes|image|std::pairs|std::ctype|wyrm::(?!_dsl))"

# Kept imports (compiled into the driver) need runtime .wyc twins on the
# C VM's import path: `import wyrm::_dsl::*` stays so pypoc's compile-time
# decorator expansion can resolve `@accept` (parser.wy is full of them);
# _dsl imports wyrm::ast, which imports std::pairs.
RUNTIME_SUPPORT = ["wy/std/pairs.wy", "wy/wyrm/ast.wy", "wy/wyrm/_dsl.wy"]

# Manifest rows whose runtime needs a C VM capability outside this epic's
# scope (epic 6's deferred iterator/coroutine-property gaps, the import
# parent-then-member fallback), or that the compiler itself declines
# (grammar constructs with no lowering: signal/with/decorators).
EXPECTED_DIVERGES = {
    "samples/eval_closures": "no COROUTINE case in wy_iterator_next: `for i in range(...)` never resumes (epic 6 deferral)",
    "samples/eval_coroutines": "cofun.value faults: no property-table entry for wy_coroutine::result (epic 6 deferral)",
    "samples/eval_modules": "import std::io::println as alias: wy_link_import has no parent-then-member fallback",
    "samples/eval_signals": "REFUSED upstream: `signal` in a class body is not lowered (8.5)",
    "samples/lexical": "REFUSED upstream: `with` has been removed from the language",
    "decorators/decorated": "pypoc's declib builds pypoc's 8-field 'fn node; the port's parser emits 'fn_def (see expand/wydecorated, its port-shaped twin)",
}


def build_amalgam(workdir):
    seen = set()
    out = []
    for rel in AMALGAM_FILES:
        text = open(os.path.join(ROOT, rel)).read()
        for line in text.splitlines(keepends=True):
            if re.match(STRIP_IMPORT, line):
                continue
            m = re.match(r"^([A-Z_][A-Z0-9_]*) :=", line)
            if m:
                if m.group(1) in seen:
                    continue
                seen.add(m.group(1))
            out.append(line)
    path = os.path.join(workdir, "wyrm_compiler.wy")
    with open(path, "w") as f:
        f.write("".join(out))

    # Runtime support modules for the kept `import wyrm::_dsl::*`: each
    # compiled to its mirror path so the C VM's import hook finds it. The
    # package root imports each prefix in turn, so `wyrm` itself must
    # resolve: __init__.wy lands as wyrm.wyc.
    # pypoc writes an __init__ module's artifacts beside the source, so
    # compile a copy under a neutral name and rename to the package root.
    init_copy = os.path.join(workdir, "pkgroot.wy")
    with open(init_copy, "w") as f:
        f.write(open(os.path.join(ROOT, "wy", "wyrm", "__init__.wy")).read())
    r2 = subprocess.run(
        [PYPOC_WYRM, "-I" + os.path.join(ROOT, "wy"), "--build-bc", init_copy,
         "-o", workdir],
        capture_output=True, text=True,
    )
    if r2.returncode != 0:
        print(r2.stdout, r2.stderr, file=sys.stderr)
        sys.exit("corpus sweep: wyrm package marker failed to compile")
    os.replace(os.path.join(workdir, "pkgroot.wyc"), os.path.join(workdir, "wyrm.wyc"))
    for rel in RUNTIME_SUPPORT:
        # Mirror the support sources into the workdir under their import
        # paths (relative to the wy/ root) and compile them in place:
        # pypoc resolves their own imports against the -I workdir tree and
        # lands each .wyc exactly where the C VM's import hook looks.
        rel_to_wy = rel.split("/", 1)[1]
        copy = os.path.join(workdir, rel_to_wy)
        os.makedirs(os.path.dirname(copy), exist_ok=True)
        with open(copy, "w") as f:
            f.write(open(os.path.join(ROOT, rel)).read())
        r2 = subprocess.run(
            [PYPOC_WYRM, "-I" + workdir, "--build-bc", copy],
            capture_output=True, text=True,
        )
        if r2.returncode != 0:
            print(r2.stdout, r2.stderr, file=sys.stderr)
            sys.exit("corpus sweep: support module %s failed to compile" % rel)

    r = subprocess.run(
        [PYPOC_WYRM, "-I" + os.path.join(ROOT, "wy"), "--build-bc", path, "-o", workdir],
        capture_output=True, text=True,
    )
    if r.returncode != 0:
        print(r.stdout, r.stderr, file=sys.stderr)
        sys.exit("corpus sweep: driver amalgam failed to compile")
    return os.path.join(workdir, "wyrm_compiler.wyc")


def build_expander(workdir, driver_wyc):
    """Epic 10a M3: compile the decorator expander (wy/wyrm/compiler/expand.wy)
    into <workdir>/wyrm/compiler/expand.wyd, where a throwaway expansion VM's
    import hook finds it. pypoc cannot build it (it does not know the C VM's
    tree_box/bind_message builtins), but the expander has no decorators, so
    the decorator-free gen0 driver compiles it. Epic 11 M1: the driver writes
    .wyd (port provenance), and each recompiled module's stale pypoc .wyc is
    removed - the import hook prefers .wyd, but keeping one extension per
    module makes the tree unambiguous."""
    out = os.path.join(workdir, "wyrm", "compiler")
    os.makedirs(out, exist_ok=True)
    src = os.path.join(ROOT, "wy", "wyrm", "compiler", "expand.wy")
    r = subprocess.run([WYRM, "-I" + workdir, driver_wyc, out, src],
                       capture_output=True, text=True, timeout=300)
    if r.returncode != 0 or not os.path.exists(os.path.join(out, "expand.wyd")):
        print(r.stdout, r.stderr, file=sys.stderr)
        sys.exit("corpus sweep: the expander failed to compile")

    # The decorators an expansion VM runs (wyrm/_dsl.wy and its helper
    # wyrm/ast.wy) must be built by the PORT, not pypoc: their tree shape
    # follows the compiler that built them (ast.wy's WY_SHAPE), and the
    # operands they receive here are the port's. Both are decorator-free, so
    # the gen0 driver compiles them over pypoc's copies.
    wyrm_out = os.path.join(workdir, "wyrm")
    for rel in ("wy/wyrm/ast.wy", "wy/wyrm/_dsl.wy"):
        r = subprocess.run([WYRM, "-I" + workdir, driver_wyc, wyrm_out, os.path.join(ROOT, rel)],
                           capture_output=True, text=True, timeout=300)
        if r.returncode != 0 or ("OK " + os.path.basename(rel)[:-3]) not in r.stdout:
            print(r.stdout, r.stderr, file=sys.stderr)
            sys.exit("corpus sweep: %s failed to compile with the port" % rel)
        stale = os.path.join(wyrm_out, os.path.basename(rel)[:-3] + ".wyc")
        if os.path.exists(stale):
            os.remove(stale)


def manifest_rows():
    rows = []
    for line in open(MANIFEST):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) >= 3 and parts[1].endswith(".wyc") and parts[0].endswith(".wy"):
            rows.append((parts[0], parts[2] if parts[2] != "-" else None))
    return rows


def read_text(path):
    with open(path, "rb") as f:
        return f.read()


def main():
    if not os.path.exists(PYPOC_WYRM):
        print("corpus sweep: pypoc/.venv not set up - skipped")
        return 0
    if not os.path.exists(WYRM):
        print("corpus sweep: buildDir/src/wyrm/wyrm not built - skipped")
        return 0

    with tempfile.TemporaryDirectory(dir="/tmp") as work:
        driver_wyc = build_amalgam(work)
        build_expander(work, driver_wyc)

        # The corpus: every manifest row with a source and a compiled
        # reference. Sources live under pypoc/test/bytecode/; the sweep
        # compiles from there.
        rows = manifest_rows()
        out_dir = os.path.join(work, "out")
        os.makedirs(out_dir)

        sources = []
        expected_out = {}
        for src_rel, out_rel in rows:
            src = os.path.join(ROOT, "pypoc", "test", "bytecode", src_rel)
            if not os.path.exists(src):
                continue
            sources.append(src)
            expected_out[src] = (
                os.path.join(ROOT, "test", "bytecode", out_rel) if out_rel else None
            )
        # Port-only fixtures authored in this repo (test/bytecode/expand):
        # decorators written against the self-hosted parser's tree shapes.
        # wydeclib compiles first: wydecorated's expansion loads it from
        # out_dir. wydecorated has a hand-authored .out.
        for stem_name in ("wydeclib", "wydecorated"):
            path = os.path.join(ROOT, "test", "bytecode", "expand", stem_name + ".wy")
            sources.append(path)
            out = os.path.join(ROOT, "test", "bytecode", "expand", stem_name + ".out")
            expected_out[path] = out if os.path.exists(out) else None
        # shapes.wy (the corelib module eval_modules imports) joins the
        # corpus so that import resolves; it has no .out of its own.
        shapes = os.path.join(ROOT, "pypoc", "wypoc", "corelib", "shapes.wy")
        if os.path.exists(shapes):
            sources.append(shapes)
            expected_out[shapes] = None

        # out_dir is on the path too: a decorated fixture's expansion loads
        # its decorator module (compiled earlier in this same run) from there.
        args = [WYRM, "-I" + work, "-I" + out_dir, driver_wyc, out_dir] + sources
        r = subprocess.run(args, capture_output=True, text=True, timeout=600)
        sys.stdout.write(r.stdout)
        if r.returncode != 0:
            print(r.stderr, file=sys.stderr)

        # Epic 10a M0: the driver compiles strictly now. A STUB line is a
        # body that failed to lower and stubbed anyway - only a `@template`
        # definition may stub (reported as TSTUB), so a bare STUB is a
        # failure however green the runs look.
        stub_lines = [line for line in r.stdout.splitlines()
                      if line.startswith("STUB ")]
        stub_failures = len(stub_lines)
        for line in stub_lines:
            print("unexpected stub: " + line, file=sys.stderr)

        def stem(path):
            base = os.path.basename(path)
            return base[:-3] if base.endswith(".wy") else base

        print("\n=== corpus sweep: run each compiled image against its .out ===")
        failures = 0
        verdicts = []
        for src in sources:
            name = stem(src)
            # The driver writes .wyd since epic 11 M1 (port provenance).
            wyc = os.path.join(out_dir, name + ".wyd")
            if not os.path.exists(wyc):
                verdict = "REFUSED"
                note = EXPECTED_DIVERGES.get(rel_of(src), "")
                verdicts.append((name, verdict, note))
                if not note:
                    failures += 1
                continue
            run = subprocess.run(
                [WYRM, "-I" + out_dir, wyc],
                capture_output=True, timeout=120,
            )
            actual = run.stdout
            exp_path = expected_out[src]
            if exp_path and os.path.exists(exp_path):
                expected = read_text(exp_path)
                if actual == expected:
                    verdict = "matches"
                    note = ""
                else:
                    verdict = "FAIL"
                    note = "output differs from " + out_rel_of(src)
                    failures += 1
            else:
                # No .out to diff: a run that exits cleanly is the bar.
                if run.returncode == 0:
                    verdict = "matches"
                    note = "no .out (clean run)"
                else:
                    verdict = "FAIL"
                    note = "clean-run bar failed (exit %d)" % run.returncode
                    failures += 1
            verdicts.append((name, verdict, note))

        print(f"\n{'fixture':<24} {'verdict':<10} note")
        for name, verdict, note in verdicts:
            mark = "" if verdict in ("matches",) else "  <-- " + note if note else "  <-- unexpected"
            print(f"{name:<24} {verdict:<10}{mark}")

        print(f"\ncorpus sweep: {len(verdicts)} fixtures, "
              f"{sum(1 for _, v, _ in verdicts if v == 'matches')} matches, "
              f"{failures + stub_failures} unexpected failure(s)")
        return 1 if (failures or stub_failures) else 0


def rel_of(src):
    rel = os.path.relpath(src, os.path.join(ROOT, "pypoc", "test", "bytecode"))
    return rel.replace(os.sep, "/")[:-3]


def out_rel_of(src):
    return rel_of(src).replace(".wy", ".out")


import re

if __name__ == "__main__":
    sys.exit(main())
