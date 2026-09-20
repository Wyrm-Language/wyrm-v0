#!/usr/bin/env python3
"""Regenerate the committed, Python-free conformance corpus under
test/bytecode/ from pypoc's own fixtures and samples.

Run with pypoc/.venv/bin/python scripts/build_corpus.py (scripts/setup_pypoc.sh
sets up that venv first). For each pypoc/test/bytecode/**/*.wy fixture and
each pypoc/wypoc/samples/*.wy sample, this:

  - compiles it with wypoc.compiler_bc.compile_module (catching CompileError
    -> REFUSED, mirroring pypoc/test/test_vm_samples.py's REFUSED handling);
  - writes the compiled image as <name>.wyc and <name>.wy_a next to a mirror
    of its source path under test/bytecode/;
  - writes the expected output as <name>.out: the tree walker's stdout,
    except for the fixtures pypoc/test/test_vm_run.py's DIVERGENCES names
    (the multi-value gap), where the checked-in *.vm.out is VM-correct and
    copied instead, and except samples in test_vm_samples.py's DIVERGES,
    which still get the walker's output (the VM is what disagrees, not this
    corpus);
  - records every source in test/bytecode/manifest.txt.

See vm_plan/epic_1.md M1 for the contract this implements.
"""
import contextlib
import io
import os
import shutil
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
PYPOC_ROOT = os.path.join(REPO_ROOT, "pypoc")
OUT_DIR = os.path.join(REPO_ROOT, "test", "bytecode")

sys.path.insert(0, os.path.join(PYPOC_ROOT, "test"))
sys.path.insert(0, PYPOC_ROOT)

import conftest  # noqa: E402  (pypoc/test/conftest.py)
import test_vm_run  # noqa: E402
import test_vm_samples  # noqa: E402
from wypoc import wyrm_eval_parse_tree as ev  # noqa: E402
from wypoc import wyrm_io  # noqa: E402
from wypoc import wyrm_modules  # noqa: E402
from wypoc.compiler_bc import compile_module  # noqa: E402
from wypoc.compiler_bc.errors import CompileError  # noqa: E402
from wypoc.parse import parse  # noqa: E402

# Fixtures whose VM output is checked in beside the source (multi-value
# gap): use that instead of the walker's stdout as the expected output.
FIXTURE_DIVERGENCES = test_vm_run.DIVERGENCES

# Samples the compiler refuses outright, and samples that compile but whose
# run under the (Python reference) VM cannot match the walker.
SAMPLE_REFUSED = test_vm_samples.REFUSED
SAMPLE_DIVERGES = test_vm_samples.DIVERGES

ManifestRow = tuple  # (name, wyc_path, out_path, status, reason)


def capture_stdout(run):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        wyrm_io._reset_std_handles()
        try:
            run()
        finally:
            wyrm_io._reset_std_handles()
    return out.getvalue()


def run_under_walker(source: str, filename: str, search_root: str) -> str:
    previous = wyrm_modules.set_script_root(search_root)
    try:
        ctx = ev.Scope()
        ev.populate_globals(ctx)
        ev.expose(ctx, "__ARGS", ())
        return capture_stdout(lambda: ev.eval_program(parse(source, filename=filename), ctx))
    finally:
        wyrm_modules.set_script_root(previous)


def rel_out_path(dest_stem: str) -> str:
    return os.path.relpath(dest_stem, REPO_ROOT)


def write_image(image, dest_stem: str) -> None:
    os.makedirs(os.path.dirname(dest_stem), exist_ok=True)
    with open(dest_stem + ".wyc", "wb") as f:
        f.write(image.to_wyc())
    with open(dest_stem + ".wy_a", "w") as f:
        f.write(image.to_wya())


def build_bytecode_fixtures(rows: list) -> None:
    for rel in conftest.bytecode_fixture_names():
        src_path = os.path.join(conftest.BYTECODE_DIR, rel)
        with open(src_path) as f:
            source = f.read()
        name = os.path.splitext(rel)[0]
        dest_stem = os.path.join(OUT_DIR, name)

        try:
            image = conftest.compile_bytecode_fixture(rel)
        except CompileError as e:
            rows.append((rel, "-", "-", "REFUSED", str(e)))
            continue

        write_image(image, dest_stem)

        search_root = os.path.dirname(src_path)
        if rel in FIXTURE_DIVERGENCES:
            checked_in = os.path.join(conftest.BYTECODE_DIR, name + ".vm.out")
            with open(checked_in) as f:
                expected = f.read()
            reason = FIXTURE_DIVERGENCES[rel]
        else:
            expected = run_under_walker(source, rel, search_root)
            reason = ""

        with open(dest_stem + ".out", "w") as f:
            f.write(expected)

        rows.append((
            rel,
            rel_out_path(dest_stem + ".wyc"),
            rel_out_path(dest_stem + ".out"),
            "matches",
            reason,
        ))


def build_samples(rows: list) -> None:
    for fname in sorted(n for n in os.listdir(conftest.SAMPLES_DIR) if n.endswith(".wy")):
        src_path = os.path.join(conftest.SAMPLES_DIR, fname)
        with open(src_path) as f:
            source = f.read()
        module_name = fname[:-3]
        rel = os.path.join("samples", fname)
        dest_stem = os.path.join(OUT_DIR, "samples", module_name)

        previous = wyrm_modules.set_script_root(conftest.SAMPLES_DIR)
        try:
            image = compile_module(parse(source, filename=fname), module_name, fname)
        except CompileError as e:
            wyrm_modules.set_script_root(previous)
            reason = SAMPLE_REFUSED.get(fname, str(e))
            rows.append((rel, "-", "-", "REFUSED", reason))
            continue
        finally:
            wyrm_modules.set_script_root(previous)

        write_image(image, dest_stem)
        expected = run_under_walker(source, fname, conftest.SAMPLES_DIR)
        with open(dest_stem + ".out", "w") as f:
            f.write(expected)

        status = "DIVERGES" if fname in SAMPLE_DIVERGES else "matches"
        reason = SAMPLE_DIVERGES.get(fname, "")
        rows.append((
            rel,
            rel_out_path(dest_stem + ".wyc"),
            rel_out_path(dest_stem + ".out"),
            status,
            reason,
        ))


def build_embedded_c() -> None:
    """Regenerate the embedded-image .c fixtures used by src/test/test_module.cpp."""
    embedded_dir = os.path.join(OUT_DIR, "embedded")
    os.makedirs(embedded_dir, exist_ok=True)
    for stem in ("hello_1", "hello_2", "hello_3"):
        src_path = os.path.join(conftest.BYTECODE_DIR, stem + ".wy")
        with open(src_path) as f:
            source = f.read()
        image = conftest.compile_bytecode_fixture(stem + ".wy")
        with open(os.path.join(embedded_dir, stem + ".c"), "w") as f:
            f.write(image.to_c())


def write_manifest(rows: list) -> None:
    manifest_path = os.path.join(OUT_DIR, "manifest.txt")
    with open(manifest_path, "w") as f:
        for name, wyc_path, out_path, status, reason in rows:
            f.write(f"{name}\t{wyc_path}\t{out_path}\t{status}\t{reason}\n")


def main() -> int:
    if os.path.isdir(OUT_DIR):
        shutil.rmtree(OUT_DIR)
    os.makedirs(OUT_DIR, exist_ok=True)

    rows: list = []
    build_bytecode_fixtures(rows)
    build_samples(rows)
    build_embedded_c()
    write_manifest(rows)

    counts: dict = {}
    for row in rows:
        counts[row[3]] = counts.get(row[3], 0) + 1
    print(f"wrote {len(rows)} manifest rows: {counts}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
