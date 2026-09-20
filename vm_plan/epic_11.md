# Epic 11 — Bootstrap integration

## Goal

Turn the in-repo `wyrm` binary into a self-sufficient toolchain: embed the now-ported
wyrm compiler (epic 10) plus `wy/std` and `wy/wyrm` into the binary itself, so
`wyrm script.wy` compiles and runs a `.wy` file from source with no Python anywhere on
the machine. `src/wyrm/main.c` today (pre-epic-5) is a 60-line demo harness that ignores
its input file; by the end of epic 5 it became a real `wyrm [-I dir] file.wyc` CLI for
running pre-compiled images. This epic extends that CLI to also accept `.wy` source,
compile it in-process using the embedded compiler, cache the result in `__wycache__/`,
and exposes the same flag surface pypoc's `wypoc/cli.py` offers today (`-I`, `-m`,
`--check`, `--build-bc`, `--emit`, `--strip`), so pypoc becomes optional: still useful for
regenerating golden fixtures against a second, independent implementation, but no longer
required to run or build anything in this repo.

**Exit criterion:**
```sh
env -i PATH=/usr/bin:/bin ./buildDir/src/wyrm/wyrm -Iwy test/bytecode/hello.wy
```
expected: prints `Hello World` (or whatever `hello.wy`'s actual output is), with no
`python`/`python3` reachable on `PATH` and no `pypoc/` directory needed at all, this is
the meson end-to-end test's shape (see M6), run against a stripped `PATH` to prove the
binary is genuinely self-sufficient rather than accidentally shelling out.

## Module resolution, provenance and cache contract (added 2026-09-18, user decisions)

These rules are fixed; milestones below implement them.

**Extensions = provenance.** `.wyc` is a compiled image produced by the Python POC (pypoc).
`.wyd` is the same container format (`pypoc/doc/wyc-format.md` is still normative) produced
by the wyrm-hosted compiler (the epic 10/10a port running on the C VM). Loaders accept both;
the extension only records who built it. The `wyrm` compiler writes `.wyd`, never `.wyc`.
(`.wyd` is unused anywhere in the repo as of this date.) The epic 10/10a tools
(`compiler_main.wy`, `run_corpus_sweep.py`, `run_selfcompile.py`) still write `.wyc`; M1
renames their output to `.wyd` and makes their comparisons extension-agnostic. Committed
golden fixtures under `test/bytecode/` stay `.wyc` (they are pypoc output).

**Import search order** for `import a::b`, first hit wins:
1. each `-I` root, in order (then any default roots): `a/b.wy` source (compiled through the
   cache below), or a precompiled `a/b.wyd` / `a/b.wyc` sitting beside it;
2. **the built-in module table, last.** A module present in step 1 overrides the built-in
   one, so a developer can shadow `wyrm::parser` or `std::io` from disk.

**Built-in modules (fixed form).** Each built-in is a **`wy_c` image**: the static
`wy_module_image` C source that `to_c()` emits (`wy/wyrm/image.wy`'s writer; pypoc's
`image.py::to_c()` for the bootstrap seed), exactly the form `test/bytecode/embedded/range_1.c`
already has. These `.c` files are **checked in as source** and compiled directly into the
binary through `cwyrm_sources` (as `range_1.c` is in `src/builtin/meson.build`); the build
never needs a compiler, Python, or a built `wyrm` to produce them. Loading is
`wy_module_load_image` on the static image, no parsing of bytes. A generated **builtin
table** source lists `{virtual path, &image}` rows, e.g. `{"wyrm::compiler::module", &...}`. A
maintainer script (`scripts/regen_builtins.*`) regenerates the `.c` files and the table from
`wy/`; a meson check fails if they are stale relative to `wy/`.

Two tiers, one table:
- **Compiler tier** (`wy/wyrm/compiler/*.wy`, the front end `tokenizer`/`parser`/`ast`/
  `decode`/`_dsl`, `bjson`/`opcodes`/`image`, `std::eval` support, and whatever of `wy/std`
  they import). These must be loaded before anything can be compiled, so they are loaded
  **directly from the table at startup/first use, never through source lookup, cache or
  JIT** - that path would need the compiler to compile itself. This is the table's primary
  purpose.
- **Library tier** (the rest of `wy/std` and `wy/wyrm`). Treat these as **a given**: `import
  std::...` / `import wyrm::...` is assumed to resolve, from the table unless a disk module
  overrides it. Not finding one is an internal packaging error, not a normal "module not
  found".

A table hit (either tier) goes through the same link path as any module once loaded.

**Cache.** Same rules and directory structure as pypoc's (`cache.py`): for source
`/dir/foo.wy` the cache file is `/dir/__wycache__/foo.wyd`; valid iff it is not older than the
source (mtime); any I/O failure is silently skipped (best-effort, never fatal). Cache lookups
consider **only `.wyd`**: a Python-produced `__wycache__/foo.wyc` is ignored, so the two
toolchains never consume each other's caches. (An explicit `.wyc`/`.wyd` argument, or one
found beside a source under a `-I` root, still loads; that is precompiled distribution, not
cache.)

**Cache directory override (`--cache-dir DIR`, absolute).** Replaces the `__wycache__`
directory with a *prefix*: the source's absolute directory is appended to `DIR`.
`/home/andrew/foo.wy` caches to `DIR/home/andrew/foo.wyd` (`DIR=/tmp/cache` gives
`/tmp/cache/home/andrew/foo.wyd`) instead of `/home/andrew/__wycache__/foo.wyd`. No
`__wycache__` component under the prefix; intermediate directories are created on write.
Relative source paths are made absolute first. Meant for compile-only runs (`--check`,
`--build-bc`) but accepted in every mode, and used for reads as well as writes (otherwise
it could never hit). Distinct from the deferred `global_cache` hash scheme.

**Verbose loading (`-v` / `--verbose`).** Extra diagnostics on stderr, one line per module
resolution naming where it came from and why: `cache <path>.wyd`, `jit <source>.wy (cache
absent|stale|unwritable)`, `precompiled <path>.wyc|.wyd`, or `builtin <virtual path>`, plus
`cache write <path>` / `cache write skipped (<reason>)`. Implemented in the hosted import
layer so the same lines cover the entry script and every transitive import.

## Inputs

- `vm_plan/epic_10a_report.md`: how decorator expansion landed (`std::eval`, `expand.wy`).
  **Epic 11 depends on 10a, not only 10:** M2's in-process compile of a user `.wy` must run
  decorator expansion, so the embedded image set must include `std::eval`'s natives and the
  expansion pass, and the embedded `parser` is the one 10a M5 compiled (true self-hosted).
- `vm_plan/epic_10_report.md`: which fixtures are byte-identical vs. semantic-diff, the
  self-compile fixed point's actual convergence status, the final shape/entry point of
  `wy/wyrm/compiler/module.wy` (or wherever `compiler.wy`'s replacement landed), and any
  `_dsl.wy` decorator gaps noted there that might affect compiling `wy/std` or `wy/wyrm`
  itself at embed time.
- `vm_plan/epic_9_report.md`: the section-id enum and `wy_module_image` struct shape
  actually shipped, since M1 here embeds images built that way.

State-scan checklist:
1. Read `src/wyrm/main.c` as it stands after epic 5/6 (not the pre-epic-5 60-line demo
   described in the plan's "Context" section), confirm its current flag parsing, whether
   it already has an `-I` option, and how it currently locates/loads a `.wyc` file.
2. Read `pypoc/wypoc/cli.py`'s flag table in full (`grep -n "elif opt =="` around
   ~470-600) to get the exact flag surface and their interactions/conflicts (e.g.
   `--check` cannot combine with `--build-bc`) as currently implemented, not as
   remembered from the plan.
3. Confirm whether `pypoc/wypoc/compiler_bc/image.py`'s `to_c()` (or epic 9's
   `wy/wyrm/image.wy` equivalent) is the mechanism to be reused for embedding, or whether
   this epic needs a different embedding path (a raw byte array of the `.wyc` bytes is
   simpler than the section-array `wy_module_image` struct `to_c()` produces, decide and
   justify in M1, this is explicitly a plan-time choice the executor may revisit).
4. Confirm `pypoc/wypoc/cache.py`'s `__wycache__` scheme (`CACHE_DIR_NAME`, `CACHE_EXT`,
   mtime-based invalidation, silent-skip-on-failure policy) as the model to port to C;
   read its `.wyc`-caching half (the module's second half, for `--vm`) not just the
   `.wy_ast` half.
5. Confirm what `wy/std` currently contains (`ls wy/std/`) and whether it already compiles
   cleanly with the epic-10 compiler, since it is one of the three things this epic
   embeds (compiler + std + `wy/wyrm`).
6. Check `AGENTS.md`'s current wording around "Prefer the default 'wyrm' in user $PATH"
   (~39-41), this is the exact sentence epic 11 must invert once the in-repo binary is
   self-sufficient.
7. Check `doc/EXPLAINER.md`'s "Bytecode / VM state" section for whatever it currently says
   about the `.wy` sources under `wy/` "not yet wired to the C loader", that sentence
   becomes false at the end of this epic and must be updated.
8. Confirm meson's test infrastructure (`src/test/meson.build`, `test/samples/`,
   `test/wy/`) so the new end-to-end test (M6) fits the existing pattern rather than
   inventing a new one.
9. Run `meson test -C buildDir` and record the pass count for the report's "before" line.

## Context to load

Read:
1. `src/wyrm/main.c` in full, current state (length and shape will have changed
   substantially since epics 2-6; read whatever is there now, not the demo harness quoted
   in the plan's background). ~1-2k tokens depending on how large it has grown.
2. `pypoc/wypoc/cli.py`: the module docstring/usage block (~1-120, the `--help` text is
   the spec for flag semantics), the flag-parsing loop (~470-600), `--build-bc`'s handler
   (~296-345), `--vm`'s handler and its `__wycache__` interaction (~346-420). ~3k tokens.
3. `pypoc/wypoc/cache.py` in full (module docstring plus both halves: `.wy_ast` caching
   and `.wyc` caching), the mtime-check contract this epic ports to C. ~1.5k tokens.
4. `pypoc/wypoc/compiler_bc/image.py`'s `to_c()` (~504-550) and epic 9's
   `wy/wyrm/image.wy` equivalent (once epic 9 has landed; read whichever is the actual
   embedding source of truth per state-scan item 3). ~1.5k tokens.
5. `AGENTS.md` in full (currently ~100-150 lines), this is one of the two files M6
   updates, so read it completely rather than by grep.
6. `doc/EXPLAINER.md` in full (222 lines at plan time), the other file M6 updates.
7. Skim `src/meson.build` and `src/wyrm/meson.build` for how the `wyrm` executable target
   is currently defined, since M1's embedding step adds a build-time dependency (compile
   the images, then compile them into the binary) that has to fit meson's dependency
   graph without introducing a circular one (the compiler that compiles the embedded
   images cannot itself depend on the binary being embedded into).

Grep-only: `pypoc/wypoc/cli.py`'s `--check` recursive-import-following logic (~186-296,
only if M4 needs to replicate the recursion, not just the single-file check); any existing
`custom_target(...)` calls elsewhere in the meson tree, as a style reference.

## Assumptions

- Epic 10 landed a compiler that can compile `wy/std` and `wy/wyrm` (parser, tokenizer,
  the compiler's own sources) without manual intervention, since M1 needs to compile all
  three to images at build time. *(verify in scan)*
- `src/wyrm/main.c` by epic 11's start already has a working `-I` flag and `.wyc` loading
  path from epic 5/6's CLI work, so this epic extends rather than builds a CLI from
  scratch. *(verify in scan)*
- **Decided (user, 2026-09-18):** the embedded modules are checked-in `wy_c` image `.c` files
  compiled directly in (precedent: `range_1.c` in `src/builtin/meson.build`); no
  `custom_target`, no build-time compile step. *(verify in scan that `to_c()`/`image.wy`'s
  writer output still matches `wy_module_image` in `include/wyrm/image.h`)*
- `__wycache__/*.wyc` mtime-check semantics mirror pypoc's `cache.py`: compare the
  source file's mtime against a stored value, skip the cache silently on any read/write
  failure (read-only filesystem, permissions), never raise into the caller.
  *(verify in scan)*
- pypoc's exit codes (0 success, non-zero per failure category, grep `sys.exit` in
  `cli.py` for the actual set) are the contract this epic's C CLI should match, so
  existing shell-level tooling that checks `wyrm`'s exit code behaves the same regardless
  of which binary ran. *(verify in scan, the plan did not enumerate the exact codes)*

## Milestones

### M1 — Embed compiler + `wy/std` + `wy/wyrm` images into the binary

**Re-cut (2026-09-19, scan outcomes — supersedes the paragraphs below where they
conflict).** The scan confirmed the contract and fixed its open mechanics:

- **Hook signature.** The import hook returns *bytes*, but the contract fixes
  table loading as `wy_module_load_image` on static images ("no parsing of
  bytes"). `wy_import_hook` (include/wyrm/context.h) therefore gains a
  `const wy_module_image** out_image` out-param; `wy_link_import` (src/link.c)
  branches to `wy_module_load_image` when the hook sets it, else
  `wy_module_load_bytes` as today. Callers to update: `wy_import_fs_hook`, the
  two test hooks (test_link.cpp, test_bytecode_golden.cpp).
- **Table placement.** The table rides `wy_import_fs_search_path`
  (`builtins`/`builtin_count` fields) and is consulted at the hosted hook's
  terminal miss (import_fs.c's `return WY_ERR_UNBOUND`). This makes the
  expansion child inherit it for free (expand_native.c already copies
  `import_hook`/`import_ud`), which is how the child finds
  `wyrm::compiler::expand` once M1 lands. Mechanism in libcwyrm (import_fs.c);
  the generated table + image `.c` payload compile into the `wyrm` executable
  and `test_cwyrm` only — libcwyrm stays payload-free.
- **Embed set = 23 images.** SELF_SOURCES minus `wyrm/__init__.wy` and
  `wyrm/compiler.wy` (nothing imports them). `wy/std/io.wy` and the
  `wy/std/expand.wy` stub are deliberately NOT embedded: `std::io` and
  `std::expand` are host modules (installed by src/wyrm/main.c), and a
  table-sourced `std::io` would silently break D10 (expansion VMs must fail on
  host modules). Package-relative spellings get alias rows (`bjson`, `opcodes`
  — image.wy:66-67 imports them without the `wyrm::` prefix).
- **In-root extension order: `.wyd` before `.wyc`.** Mixed dirs hold a stale
  pypoc artifact beside a fresh port one (build_expander recompiles ast/_dsl
  over pypoc's copies); `.wyc`-first would load the wrong shape.
- **Provenance: all `.wyd`, no pypoc seed.** The gen1 tree is fully
  port-built since 10a M5, so every embedded image is wyrm-hosted-compiler
  output; only the gen0 amalgam driver (not embedded) is pypoc-built.
- **Regen without a build-time compiler.** `scripts/regen_builtins.py`
  (maintainer-time, Python today — same tier as the sweep tests) rebuilds the
  gen1 tree via the existing sweep helpers, compiles the new
  `wy/wyrm/tools/embed_build.wy` driver with the tree's own compiler_main,
  and runs it: embed_build compiles the embed list in-process and emits one
  `to_c()` `.c` per module plus the table `.c` (image.wy's writer — the
  wy-side writer, not pypoc's). `--check` diffs against `src/wyrm/embedded/`.
  The meson staleness check is folded into the selfcompile test (which already
  builds the tree): after the fixed point it runs embed_build from the fresh
  tree and fails on drift. Post-M2 the check can shrink to the built binary's
  own embedded compiler.
- Generated sources land in `src/wyrm/embedded/`: per-module image `.c` + a
  generated unity `embedded_images.c` + `wyrm_builtins_table.c` (so the meson
  file lists exactly two generated files and never changes on regen), plus a
  hand-written `builtins.h`. M1's acceptance doctest lives in test_link.cpp
  (table hit with no filesystem roots; `-I` shadow wins; miss stays
  WY_ERR_UNBOUND), with a tiny committed pypoc-built shadow fixture under
  `test/bytecode/embedded/shadow/`.

**Scope (original).** Implement the embedding mechanism fixed by the contract (checked-in `wy_c`
image `.c` files). Produce
`.c` arrays (via `wy/wyrm/image.wy`'s `to_c()`) for every module the runtime needs before
it can compile anything itself: the compiler's own modules
(`wy/wyrm/compiler/*.wy`), the front end (`tokenizer.wy`, `parser.wy`, `ast.wy`,
`decode.wy`, `_dsl.wy`), `bjson.wy`/`image.wy`/`opcodes.wy` from epic 9, and `wy/std`.
Produce a checked-in `wy_c` image `.c` per module (the fixed form in the contract, precedent
`range_1.c`; no raw-byte alternative) plus the builtin table, compiled into `libcwyrm` or a
new `wyrm_embedded` static lib through the normal meson source list. Wire it into the hosted import hook as the **last** resolution step (contract
above): filesystem hits, including cache, override it; a table hit loads through the
ordinary load/link path. Also switch the epic 10/10a tools' output to `.wyd` and record which
table modules were built by pypoc (bootstrap seed, `.wyc` provenance) vs. by the wyrm-hosted
compiler (`.wyd`); the steady state is all `.wyd`.

**Files.** New: the checked-in image `.c` files and builtin table (likely under a new
`src/wyrm/embedded/`), `scripts/regen_builtins.*`, and the staleness meson check. Edit:
`src/wyrm/meson.build` (or `src/builtin/meson.build`) to add the sources to `cwyrm_sources`.

**Acceptance.** The build produces a `wyrm` binary with the embedded images linked in;
a small doctest (C++ bindings, per `AGENTS.md`'s testing convention) confirms
`import wyrm::compiler::module` (or whatever the embedded compiler's module name is)
resolves from the builtin table with no file on disk, **and** that a same-named module in a
`-I` root takes precedence over it.

**Model.** Opus. This is a first-of-kind build-system and bootstrapping decision (the
chicken-and-egg of "the tool that compiles the images is the tool being built") with no
reference implementation in this repo to copy.

**Fan-out.** None; the embedding mechanism has to be one coherent decision, not several
agents each making their own choices about the table's shape and the two tiers.

### M2 — `wyrm script.wy`: compile in-process, then run

**Scope.** Extend `src/wyrm/main.c`'s file-handling to detect a `.wy` extension (vs.
`.wyc`) and, when found, invoke the embedded compiler (via the same `wy_vm_call_sync`
path `wy_module_run_init` uses per `design_c_vm.md` §A.5) to produce an in-memory image,
then run it exactly as a pre-compiled `.wyc` would be run. No caching yet (M3).

**Files.** Edit: `src/wyrm/main.c`.

**Acceptance.** `./buildDir/src/wyrm/wyrm -Iwy test/bytecode/hello.wy` runs correctly
(same output as running `test/bytecode/hello.wyc` directly), with `pypoc/` present but
unused (confirm via `strace`/`ltrace` or a stripped `PATH` per-command, not just "it
worked", the point is proving no shell-out happened).

**Model.** Sonnet (the contract, "compile then run", is fully specified by how
`wy_module_load_bytes` and the compiler's entry point already work; this is wiring, not
design).

**Fan-out.** None.

### M3 — `__wycache__/*.wyd` cache, `--cache-dir`, `-v`

**Scope.** Port `cache.py`'s image-caching half to C, per the contract above: before
compiling a `.wy` file, check `<script_dir>/__wycache__/<name>.wyd` (or, with `--cache-dir`,
the prefix-mapped path; `global_cache` stays deferred)
against the source file's mtime; on a hit, load the cached bytes directly; on a miss,
compile and write the cache; any I/O failure anywhere in this path is silently skipped
(cache is best-effort, never fatal), matching pypoc's stated policy exactly.
Also implement `--cache-dir DIR` (prefix mapping, read and write) and `-v`/`--verbose` (one
stderr line per resolution naming its source). The resolver and cache-path logic live in one
module (`src/wyrm/cache.c`/`.h` or the hosted import layer) so the entry script and every
transitive import share it.

**Files.** Edit: `src/wyrm/main.c`, or a new `src/wyrm/cache.c`/`.h` if the logic is
substantial enough to warrant its own module (consistent with `AGENTS.md`'s module
layout convention).

**Acceptance.** Running `wyrm -Iwy test/bytecode/hello.wy` twice: the second run is
measurably faster (or, more robustly, instrumented to confirm the compile step was
skipped) and `__wycache__/hello.wyd` exists after the first run. Touching `hello.wy`
(updating its mtime) forces a recompile on the next run. With `--cache-dir /tmp/cache` the
file appears at `/tmp/cache/<abs dir of hello.wy>/hello.wyd` and no `__wycache__` is created;
a Python-written `__wycache__/hello.wyc` is ignored; `-v` prints `cache ...wyd` on the second
run and `jit ...` on the first and after the touch; a `-I` shadow of a builtin module wins
over the table (checked via `-v`).

**Model.** Sonnet.

**Fan-out.** None.

### M4 — `-I`, `--check`, `--build-bc`, `-m`, exit codes

**Scope.** Bring the CLI flag surface to parity with `pypoc/wypoc/cli.py`: `-I path`
(stacking, gcc-style attached or detached, ahead of any `WYRM_PATH`-equivalent search
order this repo defines), `--check` (parse/compile-check only, no run, matching pypoc's
recursive-import-following semantics if that is judged worth porting, otherwise
single-file only, noted as a scope reduction), `--build-bc [-o dir] [--emit wya,wyc,c]
[--strip]` (compile to one or more containers via `wy/wyrm/image.wy`'s writers from epic
9/10, without running), `-m mod::sub` (resolve via the same search-path logic as
`import`), `--cache-dir DIR` and `-v`/`--verbose` (implemented in M3, surfaced and tested
here with the rest of the flags), `--build-bc` writing `.wyd`, and an exit-code contract matching pypoc's (per the Assumptions item on exit
codes, confirmed in scan).

**Files.** Edit: `src/wyrm/main.c` (flag parsing), possibly split into
`src/wyrm/cli.c`/`.h` if `main.c` is getting large (judgment call for the milestone
owner, consistent with keeping files single-purpose per `AGENTS.md`).

**Acceptance.** Each flag has at least one meson test exercising it: `-I` resolves an
import from a non-default directory; `--check` reports success/failure without running
side effects; `--build-bc --emit wyc,wya` produces both containers matching what epic 9/10
already validated for byte-identity; `-m` runs a module found via search path; a
deliberately invalid script produces the documented non-zero exit code.

**Model.** Sonnet (mechanical CLI work against a fully specified reference,
`wypoc/cli.py`'s existing `--help` text and flag-handling code).

**Fan-out.** Once the flag-parsing skeleton exists (first half of the milestone), the
remaining flags are largely independent and can fan out: one Sonnet subagent for
`--check`, one for `--build-bc`/`--emit`/`--strip`, one for `-m`, each adding its own
meson test, provided they agree on the flag-parsing skeleton's shape first (sequence
this as skeleton-then-fan-out, not fully parallel from the start).

### M5 — pypoc becomes optional; docs updated

**Scope.** Confirm (and where needed, adjust) that nothing under `meson.build`,
`AGENTS.md`'s Commands section, or any `meson test` invocation requires `pypoc/` to be
present. pypoc's remaining role is regenerating golden fixtures under `test/bytecode/`
and `test/samples/` when someone deliberately wants a second, independent
implementation's output to diff against (documented as such, not silently relied upon by
anything in the default build/test path).

**Files.** Edit: `AGENTS.md` (the "Prefer the default 'wyrm' in user $PATH" sentence
from state-scan item 6, inverted to prefer the in-repo binary now that it is
self-sufficient; the "Commands" section if it implied a Python dependency anywhere).
Possibly: a `scripts/regen_goldens.sh` or similar, documenting pypoc's now-optional role
explicitly, if no such entry point exists yet.

**Acceptance.** `meson test -C buildDir` passes in an environment with `pypoc/` renamed
out of the way (or `PATH` stripped of `python`/`python3`, whichever proves the point more
directly) as well as in the normal environment.

**Model.** Sonnet.

**Fan-out.** None.

### M6 — `doc/EXPLAINER.md` update and the final end-to-end meson test

**Scope.** Update `doc/EXPLAINER.md`'s "Bytecode / VM state" section (the sentence about
`.wy` sources "not yet wired to the C loader" is now false) and any other section whose
facts this epic changed (the `wyrm` binary is now the preferred interpreter, not a
loader-only tool). Add the final meson test: compile and run a `.wy` sample from source
with `PATH` stripped of Python (`env -i PATH=/usr/bin:/bin` or equivalent), asserting
correct output, as the exit criterion above specifies.

**Files.** Edit: `doc/EXPLAINER.md`. New: a meson test target (likely under
`src/test/meson.build` or `test/`) implementing the stripped-`PATH` run.

**Acceptance.** The exit criterion command block, run exactly as written, succeeds in CI
(or under `meson test -C buildDir --test-case="*bootstrap*"` or whatever name the new
test target gets).

**Model.** Sonnet.

**Fan-out.** None; this is the closing integration milestone and should be done by
whoever last touched M1-M5 with full context of what actually landed, not delegated cold.

## Out of scope / deferred

- (`--cache-dir` is in scope; it is a plain path prefix. The hashed per-user cache below is not.)
- The `global_cache` variant of `__wycache__` (per-user cache keyed by a hash of the
  absolute path), pypoc's opt-in via `~/.wyrm/config`; its config parsing is a separate
  concern from the mtime-cache mechanism, deferred unless M3's owner finds it trivial.
- `--dump-wys`, `-c` (inline script), and any other pypoc CLI flag not explicitly listed
  in this epic's milestones; add only if a fixture or explicit user need requires it.
- REPL support; nothing in this epic's exit criterion or milestones touches it.
- Performance tuning of the embedded-image lookup path; correctness is the bar, not speed.
- Cross-compilation concerns for the embedded `.c` arrays (endianness swap for a
  big-endian target, per `wyc-format.md` §2's "Endianness" note); flag as a known gap if
  the target list ever includes a big-endian host, but do not block this epic on it.

## Risks

- **Bootstrap / staleness of checked-in images.** The compiler tier cannot be produced by the
  build itself (it would need itself), so the checked-in `.c` files can drift from `wy/`.
  Mitigation: the regen script is the only way they change, a meson check fails when they are
  stale relative to `wy/`, and the first table is seeded from pypoc-built images (`.wyc`
  provenance) then regenerated by the wyrm-hosted compiler once it can self-compile (epic
  10a M5). Document the seed/regen procedure prominently; later milestones depend on it.
- **`meson test` needing network/filesystem access to `pypoc/.venv`** would silently
  reintroduce a Python dependency through the back door. Mitigation: M5's acceptance
  explicitly tests with `pypoc/` unavailable, not just "assume it's fine."
- **CLI flag semantics drift from pypoc in a way nobody notices** until a script that
  worked under pypoc's `wyrm` behaves differently under this repo's `wyrm`. Mitigation:
  M4's acceptance ties each flag to a concrete meson test rather than eyeballing
  `--help` text parity.
- **Exit-code contract undocumented in the plan.** Mitigation: state-scan item confirms
  the actual codes pypoc uses before M4 starts; if pypoc's codes turn out to be
  under-specified (e.g. everything non-zero is just `1`), match that rather than
  inventing a richer scheme that then has nothing to be consistent with.
- **The stripped-`PATH` test is flaky in CI** if the CI image resolves `wyrm`-adjacent
  tools (a linker, `cc`) through paths that also happen to expose a `python3` shim.
  Mitigation: M6 should assert the *absence* of a working `python`/`python3` on the test's
  `PATH` as a precondition of the test itself (fail loudly if the test can't prove
  Python is actually gone, rather than silently passing because Python happened to be
  reachable anyway and just wasn't needed).

- **Stale builtin table vs. disk.** A forgotten copy of `wyrm::parser` in a `-I` root silently
  shadows the built-in one, or the reverse (`wy/` edited, binary still uses the built-in
  copy). Mitigation: `-v` names the source of every resolution, and a meson check rebuilds
  the table from `wy/` and fails if the generated table is out of date.
- **Two toolchains, one cache directory.** Mitigation: the `.wyd`-only cache rule; an M3
  test places a `.wyc` cache file and asserts it is ignored.

## Report

Write `vm_plan/epic_11_report.md` per `vm_plan/README.md`'s template. Beyond the standard
sections, record explicitly:
- The builtin table's format, and how bootstrap provenance (`.wyc` seed vs `.wyd`) was resolved.
- Confirmation the table is checked-in `wy_c` `.c`, the regen/staleness procedure, and which
  modules are compiler-tier vs library-tier.
- The final exit-code table implemented, for anything downstream that scripts against it.
- Whether `--check`'s recursive-import-following was ported or scoped down to single-file.
- Confirmation (with the actual command run) that the stripped-`PATH` end-to-end test
  passed, plus the CI environment's actual Python-absence guarantee if this ran in CI.
- Any remaining place in the repo that still assumes `pypoc/` is present, flagged for a
  follow-up rather than silently left in place.
