#!/usr/bin/env python3
"""Generate the images the wyrm binary embeds (project
design/plans/I-generated-images.md). Meson runs this; nothing it writes is committed.

--mode bootstrap: --wyrm may be older than the sources. It compiles them into a gen1
  tree, and gen1, on that binary's VM, writes the images, so they are the new
  compiler's output. On a wyrm one image format behind, the binary also writes what
  its own VM has to load (embed_build and the expansion scopes), because gen1
  writes the new format.
--mode self: --wyrm was built from these sources, so its embedded compiler writes
  the images directly.
--probe [--min FILE]: print the image version --wyrm writes; fail if it can't write
  images, or is older than the version in FILE.
--compare A B: fail unless two generated trees hold the same bytes.
--regen BUILD DIR...: delete generated trees, then rebuild the binary.
--dist DIR [TARGET]: for `meson dist`: build TARGET, then copy generated tree DIR to
  src/embed/prebuilt in the tarball.
"""

import argparse
import filecmp
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

GENERATED_ROOT = ("embedded_images.c", "wyrm_builtins_table.c")
IMAGE_PREAMBLE = b"\x82\x64WYCR\x1a"
MIN_FILE = "wyrm_bootstrap_min.txt"
HOST_ONLY_IMPORT = "std::io"  # expansion.wy's HOST_ONLY_IMPORTS


def die(message):
    sys.exit("embed_stage: " + message)


def read_modules(embed):
    """src/embed/modules.txt: paths relative to src/embed, in dependency order."""
    with open(os.path.join(embed, "modules.txt")) as rows:
        return [line.strip() for line in rows if line.strip()]


def module_path(rel):
    """tree_build.wy's module_path_of."""
    stem = rel[:-3]
    if stem.endswith("/__init__"):
        stem = stem[:-len("/__init__")]
    return stem.replace("/", "::")


def source_imports(path):
    """The module paths a source imports at top level."""
    with open(path, encoding="utf-8") as source:
        lines = source.read().splitlines()
    return [m.group(1) for m in (re.match(r"import\s+([\w:]+?)(?:::\*|::\(.*\))?\s*$", line) for line in lines) if m]


def check_order(embed, rows):
    """The exports of a module listed too late would silently come from the running
    compiler again (D5), so an out-of-order list is an error."""
    position = {module_path(rel): i for i, rel in enumerate(rows)}
    for i, rel in enumerate(rows):
        for dep in source_imports(os.path.join(embed, rel)):
            if position.get(dep, -1) > i:
                die("modules.txt: %s imports %s, which is listed after it" % (rel, dep))


def tree_roots(tree):
    """The import roots that make a compiled tree or a source tree importable (the
    sources use package-relative imports, so `wyrm/` and `wyrm/compiler/` are roots)."""
    return [tree, os.path.join(tree, "wyrm"), os.path.join(tree, "wyrm", "compiler")]


def check_output(cmd, want=None):
    """Run a compiler step; die on a failure, a refusal or an unexpected stub (only a
    `@template` definition may stub, which prints TSTUB)."""
    r = subprocess.run(cmd, capture_output=True, text=True)
    lines = r.stdout.splitlines()
    if r.returncode != 0 or (want and want not in r.stdout) or "EMBED-FAIL" in r.stdout \
            or any(line.startswith(("REFUSED ", "STUB ")) for line in lines):
        die("failed: %s\n%s\n%s" % (" ".join(cmd), r.stdout[-2000:], r.stderr[-2000:]))
    return r.stdout


def compile_mirror(wyrm, driver, roots, out_tree, embed, rows, cache_dir=None):
    """Run compiler `driver` with import `roots`, mirroring `rows` from `embed` into
    out_tree. out_tree comes before the roots that supply the compiler itself:
    parser.wy's decorator expansion loads the modules it imports from the tree being
    built, so they carry this compiler's tree shapes."""
    for rel in rows:
        os.makedirs(os.path.dirname(os.path.join(out_tree, rel)), exist_ok=True)
    args = [wyrm, "-I" + out_tree, "-I" + os.path.join(out_tree, "wyrm")]
    args += ["-I" + r for r in roots]
    if cache_dir:
        args += ["--cache-dir", cache_dir]
    return check_output(args + [driver, "--mirror", embed, out_tree] + rows, "compiler_main: done")


def make_out_dirs(out, rows):
    for rel in rows:
        os.makedirs(os.path.dirname(os.path.join(out, rel)), exist_ok=True)


def generated_files(root):
    """Generated C sources under a generated tree, relative to it."""
    found = []
    for dirpath, _dirs, files in os.walk(root):
        found += [os.path.relpath(os.path.join(dirpath, f), root) for f in files if f.endswith(".c")]
    return sorted(found)


def compare_trees(a, b):
    """Print every difference between two generated trees. True when they match."""
    in_a, in_b = generated_files(a), generated_files(b)
    bad = sorted(set(in_a) ^ set(in_b))
    bad += [f for f in sorted(set(in_a) & set(in_b))
            if not filecmp.cmp(os.path.join(a, f), os.path.join(b, f), shallow=False)]
    for name in bad:
        print("embedded images differ: " + name)
    return not bad


def emit_from_tree(wyrm, root, tree, out, scratch):
    """Run embed_build from a compiled tree, with that tree's own compiler building
    the tool, writing the generated files into `out`."""
    roots = ["-I" + r for r in tree_roots(tree)]
    tool = os.path.join(root, "wy", "wyrm", "tools", "embed_build.wy")
    entry = os.path.join(tree, "wyrm", "tools", "compiler_main.wyd")
    check_output([wyrm] + roots + [entry, scratch, tool], "OK embed_build")
    embed = os.path.join(root, "src", "embed")
    make_out_dirs(out, read_modules(embed))
    check_output([wyrm] + roots + [os.path.join(scratch, "embed_build.wyd"), embed, out], "embed_build: done")


def source_version(embed):
    with open(os.path.join(embed, "wyrm", "image.wy"), encoding="utf-8") as source:
        found = re.search(r"^WYC_VERSION := (\d+)", source.read(), re.M)
    if not found:
        die("no WYC_VERSION in wyrm/image.wy")
    return int(found.group(1))


def write_scopes(wyrm, embed, rows, scopes):
    """Have the bootstrap write the expansion scope of each module with decorators
    (expansion.wy's _write_scope), so its VM can load them (D6)."""
    source_roots = ["-I" + r for r in tree_roots(embed)]
    seen = set()
    os.makedirs(os.path.join(scopes, "src"))
    for rel in rows:
        path = os.path.join(embed, rel)
        with open(path, encoding="utf-8") as source:
            if not re.search(r"^\s*@", source.read(), re.M):
                continue
        stem = os.path.basename(rel)[:-3]
        if stem in seen:
            die("two modules with decorators share the name " + stem)
        seen.add(stem)
        imports = ["wyrm::template::*"]
        with open(path, encoding="utf-8") as source:
            for line in source:
                m = re.match(r"import\s+(\S.*?)\s*$", line)
                if m and not re.match(HOST_ONLY_IMPORT + r"(::|$)", m.group(1)):
                    imports.append(m.group(1))
        scope_source = os.path.join(scopes, "src", stem + ".wy")
        with open(scope_source, "w", encoding="utf-8") as out:
            out.write("".join("import %s\n" % i for i in imports))
        check_output([wyrm] + source_roots + ["--build-bc", "-o", scopes, scope_source])


def generate(mode, wyrm, root, out):
    embed = os.path.join(root, "src", "embed")
    rows = read_modules(embed)
    check_order(embed, rows)
    tool = os.path.join(root, "wy", "wyrm", "tools", "embed_build.wy")
    work = out + ".work"
    new = out + ".new"
    shutil.rmtree(work, ignore_errors=True)
    shutil.rmtree(new, ignore_errors=True)
    os.makedirs(work)
    os.makedirs(new)
    start = time.time()
    if mode == "self":
        make_out_dirs(new, rows)
        check_output([wyrm, "--cache-dir", os.path.join(work, "cache"), tool, embed, new], "embed_build: done")
    else:
        tree = os.path.join(work, "gen1")
        compile_mirror(wyrm, os.path.join(embed, "wyrm", "tools", "compiler_main.wy"), tree_roots(embed),
                       tree, embed, rows, os.path.join(work, "cache"))
        print("embed_stage: gen1 tree in %.0fs" % (time.time() - start), flush=True)
        version = probe_version(wyrm)
        if version == source_version(embed):
            emit_from_tree(wyrm, root, tree, new, work)
        else:
            print("embed_stage: bootstrap writes image version %d, the sources %d" % (version, source_version(embed)),
                  flush=True)
            scopes = os.path.join(work, "scopes")
            write_scopes(wyrm, embed, rows, scopes)
            check_output([wyrm] + ["-I" + r for r in tree_roots(embed)] + ["--build-bc", "-o", work, tool])
            make_out_dirs(new, rows)
            check_output([wyrm] + ["-I" + r for r in tree_roots(tree)]
                         + [os.path.join(work, "embed_build.wyd"), embed, new, scopes], "embed_build: done")
    # Replace the outputs together: the unity file includes the per-module files.
    for name in os.listdir(new):
        dst = os.path.join(out, name)
        if os.path.isdir(dst):
            shutil.rmtree(dst)
        os.replace(os.path.join(new, name), dst)
    shutil.rmtree(new)
    shutil.rmtree(work)
    print("embed_stage: %s images in %.0fs" % (mode, time.time() - start), flush=True)


def probe_version(wyrm):
    """Works on any wyrm that can write an image, including one with no --version."""
    with tempfile.TemporaryDirectory() as scratch:
        source = os.path.join(scratch, "probe.wy")
        with open(source, "w") as f:
            f.write("x := 1\n")
        r = subprocess.run([wyrm, "--build-bc", "-o", scratch, "--emit", "wyc", source],
                           capture_output=True, text=True)
        image = os.path.join(scratch, "probe.wyd")
        if r.returncode != 0 or not os.path.exists(image):
            die("%s cannot write images: %s" % (wyrm, (r.stderr or r.stdout).strip()[-400:]))
        with open(image, "rb") as f:
            raw = f.read(11)
    if not raw.startswith(IMAGE_PREAMBLE):
        die("%s wrote something that isn't an image" % wyrm)
    return int.from_bytes(raw[len(IMAGE_PREAMBLE):], "big")


def probe(wyrm, min_file):
    version = probe_version(wyrm)
    if min_file:
        with open(min_file) as f:
            rows = [line.strip() for line in f if line.strip() and not line.startswith("#")]
        if version < int(rows[0]):
            die("%s writes image version %d, older than the minimum %s in %s; build a newer wyrm_bootstrap"
                % (wyrm, version, rows[0], MIN_FILE))
    print(version)
    return 0


def regen(build, dirs):
    for tree in dirs:
        for name in GENERATED_ROOT:
            path = os.path.join(tree, name)
            if os.path.exists(path):
                os.remove(path)
    # The same build graph, run once more: this target itself only deletes outputs.
    return subprocess.call(["ninja", "-C", build, os.path.join("src", "wyrm", "wyrm")])


def dist(tree, target):
    if target:
        subprocess.check_call(["ninja", "-C", os.environ["MESON_PROJECT_BUILD_ROOT"], target])
    dst = os.path.join(os.environ["MESON_DIST_ROOT"], "src", "embed", "prebuilt")
    for rel in generated_files(tree):
        os.makedirs(os.path.dirname(os.path.join(dst, rel)), exist_ok=True)
        shutil.copyfile(os.path.join(tree, rel), os.path.join(dst, rel))
    return 0


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--regen":
        return regen(sys.argv[2], sys.argv[3:])
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", action="store_true")
    parser.add_argument("--min")
    parser.add_argument("--compare", nargs=2)
    parser.add_argument("--dist", nargs="+")
    parser.add_argument("--mode", choices=("bootstrap", "self"))
    parser.add_argument("--wyrm")
    parser.add_argument("--src")
    parser.add_argument("--out")
    o = parser.parse_args()
    if o.compare:
        same = compare_trees(*o.compare)
        print("embed-check: " + ("stage1 == stage2" if same else "the stages differ"))
        return 0 if same else 1
    if o.dist:
        return dist(o.dist[0], o.dist[1] if len(o.dist) > 1 else None)
    if o.probe:
        return probe(o.wyrm, o.min)
    generate(o.mode, o.wyrm, os.path.abspath(o.src), os.path.abspath(o.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
