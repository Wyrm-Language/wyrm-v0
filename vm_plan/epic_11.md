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

## Inputs

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
- The embedding mechanism is a meson `custom_target` that invokes the *already-built*
  in-repo `wyrm` binary (bootstrapped by pypoc once, or by a previous build's `wyrm`,
  per a chicken-and-egg resolution decided in M1) to produce `.c` arrays via
  `wy/wyrm/image.wy`'s `to_c()`, which are then compiled into the final `wyrm` binary
  in a second build pass. **This is the plan-time default choice; M1 must explicitly
  decide between this and simply checking in pre-generated `.c` files (regenerated by a
  maintainer script, not by the build), and justify whichever is chosen.**
  *(verify in scan: check whether meson's build model in this repo already has any
  precedent for a two-pass "build a tool, then use it to generate sources" pattern before
  assuming custom_target is the natural fit)*
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

**Scope.** Decide and implement the embedding mechanism (see Assumptions: meson
`custom_target` running the already-built binary vs. checked-in generated `.c`). Produce
`.c` arrays (via `wy/wyrm/image.wy`'s `to_c()`) for every module the runtime needs before
it can compile anything itself: the compiler's own modules
(`wy/wyrm/compiler/*.wy`), the front end (`tokenizer.wy`, `parser.wy`, `ast.wy`,
`decode.wy`, `_dsl.wy`), `bjson.wy`/`image.wy`/`opcodes.wy` from epic 9, and `wy/std`.
Link the resulting `.c` files into `libcwyrm` or a new `wyrm_embedded` static lib, and
register each embedded module with `wy_context_module_register` (per Appendix A §5 of
`vm_plan/design_c_vm.md`, "Existing C assets to reuse") so `import` resolves them without
touching the filesystem.

**Files.** New: a meson `custom_target` (or a `scripts/` generator script) plus its
output `.c` files, likely under a new `src/wyrm/embedded/` directory. Edit:
`src/wyrm/meson.build` (or wherever the `wyrm` executable target is defined) to add the
new sources and the two-pass dependency if `custom_target` is chosen.

**Acceptance.** The build produces a `wyrm` binary with the embedded images linked in;
a small doctest (C++ bindings, per `AGENTS.md`'s testing convention) confirms
`wy_context_module_register` finds `wyrm::compiler` (or whatever the embedded compiler's
module name is) without any file on disk.

**Model.** Opus. This is a first-of-kind build-system and bootstrapping decision (the
chicken-and-egg of "the tool that compiles the images is the tool being built") with no
reference implementation in this repo to copy.

**Fan-out.** None; the embedding mechanism has to be one coherent decision, not several
agents each picking a different answer to the custom_target-vs-checked-in question.

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

### M3 — `__wycache__/*.wyc` with mtime check

**Scope.** Port `cache.py`'s `.wyc`-caching half to C: before compiling a `.wy` file,
check `<script_dir>/__wycache__/<name>.wyc` (or the `global_cache` directory equivalent,
if this epic decides to support it, plan-time choice, may be deferred, note in report)
against the source file's mtime; on a hit, load the cached bytes directly; on a miss,
compile and write the cache; any I/O failure anywhere in this path is silently skipped
(cache is best-effort, never fatal), matching pypoc's stated policy exactly.

**Files.** Edit: `src/wyrm/main.c`, or a new `src/wyrm/cache.c`/`.h` if the logic is
substantial enough to warrant its own module (consistent with `AGENTS.md`'s module
layout convention).

**Acceptance.** Running `wyrm -Iwy test/bytecode/hello.wy` twice: the second run is
measurably faster (or, more robustly, instrumented to confirm the compile step was
skipped) and `__wycache__/hello.wyc` exists after the first run. Touching `hello.wy`
(updating its mtime) forces a recompile on the next run.

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
`import`), and an exit-code contract matching pypoc's (per the Assumptions item on exit
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

- **Chicken-and-egg build dependency.** Embedding the compiler into `wyrm` requires
  compiling the compiler first, which requires a compiler. Mitigation: M1 must state
  explicitly how this is broken (a bootstrap binary built once via pypoc and checked in
  as generated `.c`, regenerated by a maintainer script; or a two-stage meson build where
  stage one produces a bootstrap `wyrm` from pypoc-compiled images and stage two
  re-embeds using it). Document the choice prominently; every later milestone depends on it.
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

## Report

Write `vm_plan/epic_11_report.md` per `vm_plan/README.md`'s template. Beyond the standard
sections, record explicitly:
- The exact embedding mechanism chosen in M1 (custom_target vs. checked-in `.c`) and why,
  since this is the plan-time choice most likely to be revisited later.
- The final exit-code table implemented, for anything downstream that scripts against it.
- Whether `--check`'s recursive-import-following was ported or scoped down to single-file.
- Confirmation (with the actual command run) that the stripped-`PATH` end-to-end test
  passed, plus the CI environment's actual Python-absence guarantee if this ran in CI.
- Any remaining place in the repo that still assumes `pypoc/` is present, flagged for a
  follow-up rather than silently left in place.
