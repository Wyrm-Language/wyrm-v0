# Epic 11 report — Bootstrap integration

Session date: 2026-09-19 · Model: GLM (opencode) · Commits: `76804ce`..`d7d6d2f`
(handoff note at `epic_11_handoff.md` is superseded by this report; M4–M6 landed
after it was written).

## Landed

- **M1** — Builtin module table + embedded compiler/`wy/std` images
  (`9a278a9`). Acceptance: doctests (`src/test/test_link.cpp`, suite
  `builtin_table`) resolve `wyrm::compiler::module`, `wyrm::compiler::expand`,
  and the `bjson` alias from the table with **no filesystem roots at all**; a
  same-named module in a `-I` root shadows it; a miss stays `WY_ERR_UNBOUND`.
- **M2** — `wyrm script.wy` compiles in-process, then runs (`16a04a1`).
  Acceptance: `./buildDir/src/wyrm/wyrm -Iwy pypoc/test/bytecode/hello.wy`
  prints `Hello World`; `strace -f -e execve,openat` shows the only execve is
  wyrm itself and the only `pypoc/` access is the requested script path.
- **M3** — `__wycache__/*.wyd` cache, `.wy` source imports, `--cache-dir`,
  `-v` (`0466c4d`). Acceptance:
  `./buildDir/src/wyrm/wyrm -Iwy -Itest/bytecode/expand
  test/bytecode/expand/wydecorated.wy` matches its committed `.out` cold
  (`wydeclib.wy` compiled through the hook on a scratch machine, expander from
  the table) and warm (from the cache).
- **M4** — `--check`, `--build-bc [-o DIR] [--emit LIST] [--strip]`, `-m`,
  exit codes (`d603500`, MVP scope — see Deviations). Acceptance: meson test
  `wy-flags` (scripts/check_wy_flags.sh): --check ok/fail exit codes;
  `--build-bc --emit wyd,c --strip -o DIR` byte-identical to the run cache's
  `.wyd` (`cmp`); `-m` from a `-I` source and from the builtin table; `-m`
  miss exits 1.
- **M5** — pypoc optional; docs (`d7d6d2f`). Acceptance: `meson test` with
  `pypoc/` renamed away: **10/10 OK + 1 skipped**, then restored. AGENTS.md
  now prefers the in-repo binary and documents pypoc's optional role.
- **M6** — EXPLAINER/vm_impl updates + the stripped-PATH end-to-end test
  (`d7d6d2f`). Acceptance, run exactly as the exit criterion intends (stronger
  PATH than the epic's draft — an empty dir reaches nothing):
  ```sh
  env -i PATH=<empty dir> sh -c 'command -v python3 || command -v python'   # fails (loud precondition)
  env -i PATH=<empty dir> ./buildDir/src/wyrm/wyrm -Iwy test/bytecode/hello.wy   # prints Hello World
  ```
  automated as part of meson test `wy-entry`.

Not in epic scope but landed on the way: stray `test/bytecode/expand/treelib.c`
removed (`76804ce`, committed by mistake in 10a); four latent compiler bugs
fixed because `to_c`/`to_wya` ran on the C VM for the first time (below).

## Deviations from the epic file

- **M1 re-cut (written into epic_11.md before starting; scan outcomes)**:
  the fixed contract ("loading is `wy_module_load_image`, no parsing of
  bytes") required a `wy_import_hook` signature extension (image out-param);
  the table rides `wy_import_fs_search_path` and is consulted at the hosted
  hook's terminal miss — which is also how the expansion child inherits it.
  Payload compiles into the `wyrm` exe and `test_cwyrm`, not libcwyrm.
- **Embed set**: 26 modules — SELF_SOURCES plus `wyrm/__init__.wy`,
  `wyrm/compiler.wy` (the import opcode imports **every path prefix in
  turn**, so `import wyrm::compiler::module` needs `wyrm` and
  `wyrm::compiler` rows), and `wyrm/tools/compile_source.wy` (M2's in-memory
  entry helper). `wy/std/io.wy` and the `wy/std/expand.wy` stub are
  deliberately absent (host modules; a table-sourced `std::io` would break
  10a D10). Package-relative spellings get alias rows (`bjson`, `opcodes`).
- **Provenance**: steady state already reached — every embedded image is
  `.wyd` (port-built from the gen1 tree); no `.wyc` bootstrap seed was
  needed because 10a M5 finished the parser bring-up. Only the gen0 amalgam
  driver (not embedded) is pypoc-built.
- **`.wy` imports compile on an independent scratch machine**
  (`compile_on_scratch_`, depth-capped 8): the requesting VM is suspended
  mid-import, so compiling through it would break the no-C-recursion rule.
  Related bootstrap rule: **a builtin-table row bars the hook's `.wy` source
  branch** (`-Iwy` + `import wyrm::tools::compile_source` otherwise recurses
  the compiler into itself — found as a 33k-frame segfault). Precompiled
  `.wyd`/`.wyc` may still shadow table rows; sources may not.
- **Per-root resolution order**: `.wy` source → `.wyd` → `.wyc` (the
  contract listed the forms but not the in-root order). `.wy` first because
  the source is authoritative and the cache handles freshness; `.wyd` before
  `.wyc` so a stale pypoc artifact never shadows a fresh port build.
- **M4 MVP reductions** (user-directed): `--check` is single-file (pypoc's
  recursive import walk not ported; decorator expansion still pulls
  transitive imports through the hook). `--emit wya` is refused with a
  pointer to `--disasm`: `image.wy`'s `to_wya` annotation writer still
  carries pypoc-only assumptions and faults on the C VM (native
  `pack_u8`/`str` faults; known gap). `--strip` is a no-op (the port never
  emits a debug section). `-m` seeds `__name__="__main__"` (python -m
  semantics).
- **Latent compiler bugs fixed** (each found by first C-VM exercise):
  `image.wy` `_join` was O(n²) in output size (pairwise join; to_c on
  parser-sized images went from timeout-scale to seconds); `context.wy`
  stored message `_path` as a joined string where pypoc and all three
  consumers expect the parts list; `to_c` comments now escape `*/`
  (image.wy compiling itself puts its own header text, which contains
  `*/`, in a static string constant); `_POSIX_C_SOURCE 200809L` for
  nanosecond cache mtimes (a whole-second mtime cannot see a touch in the
  same second as the cache write).

## Tests

- before: `meson test` 8/8; corpus sweep 23 fixtures / 22 matches / 0
  unexpected; selfcompile gen1==gen2 byte-for-byte (26 images incl.
  parser.wy); wy-tests 7/7.
- after: `meson test` **11/11** (new: `wy-entry` incl. the stripped-PATH
  exit criterion, `wy-cache`, `wy-flags`; doctest suites `import_fs` +7
  cases and `builtin_table` +3). Sweep and selfcompile unchanged, and
  selfcompile now also fails on embedded-image staleness (runs
  `regen_builtins.check_tree` after the fixed point: "embedded images: 28
  files up to date with wy/"). Without `pypoc/`: 10/10 + 1 skipped.

## Exit-code table (implemented; matches pypoc's 0/1/2 contract)

| Code | Meaning |
|---|---|
| 0 | Success (run, `--check` ok, `--build-bc`, `-m`; also `--sections`/`--disasm`) |
| 1 | Compile failure (entry/`--check`/`--build-bc`/import-through-hook), image load failure, run fault, `-m` miss, unreadable input, unresolvable `--cache-dir`, cannot create `-o` dir |
| 2 | Usage/flag errors (unknown option, missing flag argument, `-m` + `--check`/`--build-bc`, `--check` + `--build-bc`, `--emit`/`-o`/`--strip` without `--build-bc`, unknown `--emit` container, `--emit wya` refusal, no input file) |

## Open questions and known gaps

- `to_wya` (the `.wy_a` listing writer) faults on the C VM — its annotation
  path (`_document_annotations`/`_static_repr` chain) feeds non-bjson types
  into `encode_document` and `str`s unhandled types. `--emit wya` refuses
  cleanly; `check_disasm` still consumes pypoc-built `.wy_a` fixtures.
  Fixing `to_wya` is a self-contained follow-up.
- Committed pypoc-era `wy/wyrm/ast.wyc` and `wy/wyrm/image.wyc` shadow their
  builtin-table rows (precompiled may shadow the table by design). Harmless
  today (shape-independent modules), but they are deletion candidates.
- Per-root `.wy`-before-precompiled order is a documented choice; a stale
  `.wy` beside a fresh `.wyd` resolves the `.wy` (its mtime still governs
  the cache).
- Remaining places that assume `pypoc/` (flagged per the epic's report
  requirements, all skip-or-regenerate roles): `run_corpus_sweep.py` +
  `run_selfcompile.py` (skip without pypoc/.venv), `regen_builtins.py`
  (still seeds via the gen0 amalgam; could now self-seed from the built
  binary's own compiler — noted as follow-up), `test_headers_sync.cpp`
  (skips), `build_corpus.py`/golden-fixture regeneration (documented, never
  on the default build/test path), `scripts/check_disasm.sh` (uses
  committed `.wy_a`, no pypoc at runtime).
- Deferred per the epic: `--dump-wys`, `-c`, REPL, `global_cache`, embedded
  big-endian targets (`wy_module_load_image` still refuses non-little-endian
  hosts).
- MVP scope reductions to revisit: `--check` single-file; `.wy_a` emission.

## Proposed edits to epic_12.md

- None required to start. Notes: the dispatch/class code epic 12 optimizes
  now runs constantly in the CLI's compile path (every `-m`/import/build-bc
  compiles go through the same VM), so its baseline benchmarks can use the
  `wyrm` binary alone — no pypoc trees needed. The builtin-table lookup is
  a linear scan over 28 rows behind one hook call; if epic 12's benchmarks
  care, a sorted table + bsearch is a two-line change.

## Orientation for the next session

- Exit criterion (also meson `wy-entry`): `env -i PATH=<empty>
  ./buildDir/src/wyrm/wyrm -Iwy test/bytecode/hello.wy` → `Hello World`.
  The binary rpath-links `buildDir/src/libcwyrm.so`; no env needed.
- The builtin table lives in `src/wyrm/embedded/` (26 image `.c` + generated
  `wyrm_builtins_table.c` + unity `embedded_images.c` + hand-written
  `builtins.h`). Manifest (`EMBED`/`ALIAS_SYMBOLS`) is in
  `wy/wyrm/tools/embed_build.wy`; regen with
  `python3 scripts/regen_builtins.py` (reuse a tree with `--keep`; `--check`
  verifies). The selfcompile meson test fails on staleness.
- Resolution order: per `-I` root, `.wy` (cache/scratch-compile) → `.wyd` →
  `.wyc`; then the builtin table. A table row bars `.wy` source lookup for
  that path — never remove that check (it is the compiler-compiling-itself
  trap).
- `wy/wyrm/tools/compile_source.wy` is the C CLI's compiler entry
  (`compile_source` → container bytes; `build_bc` → `--build-bc`). It runs
  in a scratch machine with builtins + std::io + std::expand only.
- Test by name: `./buildDir/src/test/test_cwyrm --test-case='*import*'`
  (or `*builtin*`); CLI suites are meson tests `wy-entry`, `wy-cache`,
  `wy-flags` (all shell scripts under `scripts/check_wy_*.sh`).
- wy gotchas learned here: no `\"` escape (use `_char_from_cp(34)`);
  `str()` faults (BAD_TYPE) on lists/dicts — don't interpolate aggregates;
  `std::io::open` failures are error values and unchecked handles fault the
  next `write`.
