# Epic 11 handoff — bootstrap integration (M1–M3 landed; M4–M6 remain)

> **SUPERSEDED (same day):** M4, M5, and M6 landed after this handoff was
> written — `d603500` (M4), `d7d6d2f` (M5+M6). The epic is complete; see
> `vm_plan/epic_11_report.md`. The "Remaining milestones" section below is
> historical; the traps and regen notes still hold.

Session date: 2026-09-19 · Written per the user's pause-after-a-couple-of-

## State

- Commits this session: `76804ce` (stray treelib.c removed), `9a278a9` (M1),
  `16a04a1` (M2), `0466c4d` (M3). Working tree clean at `0466c4d`.
- Baseline: `meson test -C buildDir` **10/10** (was 8/8; added `wy-entry`,
  `wy-cache`). Corpus sweep 23 fixtures / 22 matches / 0 unexpected
  (unchanged from 10a's final state). Selfcompile: gen1 == gen2
  byte-for-byte **plus** the new embedded-images staleness check ("embedded
  images: N files up to date with wy/").
- The M1 re-cut lives in `vm_plan/epic_11.md` (scan outcomes that supersede
  the original M1 text). Read it before anything else.

## What landed, compressed

- **M1** — `wy_import_hook` (include/wyrm/context.h) gained a
  `const wy_module_image** out_image` out-param; `wy_link_import`
  (src/link.c) loads table hits via `wy_module_load_image` (static,
  zero-copy). The table rides `wy_import_fs_search_path`
  (`builtins`/`builtin_count`, include/wyrm/platform/hosted/import_fs.h) and
  is consulted at import_fs.c's terminal miss — the std::expand child
  inherits it for free via `import_ud` (expand_native.c copies only
  hook/ud). Generated payload in `src/wyrm/embedded/` (26 image .c + a
  generated table .c + a unity `embedded_images.c` so meson never changes on
  regen) compiled into the `wyrm` exe and `test_cwyrm`, **not** libcwyrm.
  All images `.wyd` provenance (port-built, gen1 tree); golden fixtures stay
  `.wyc`.
- **M2** — a `.wy` entry: main.c links `wyrm::tools::compile_source` **from
  the table** (never the filesystem), calls it via `wy_vm_call_sync` with
  (source, name, path), copies the container out of the rooted `wy_bytes`,
  and loads/runs it like any image. Acceptance proof: strace shows the only
  execve is wyrm itself; `wydecorated.wy` (entry .wy + port-compiled
  decorator module + expander from the table) matches its committed `.out`.
- **M3** — cache (`import_cache.c/.h`), per-root resolution order
  `.wy source -> .wyd -> .wyc` then table, `-v` resolution lines,
  `--cache-dir` prefix mapping. `.wy` imports compile on an **independent
  scratch machine** (main.c `compile_on_scratch_`) — the requesting VM is
  suspended mid-import, so calling into it would break the no-recursion
  rule; depth-capped at 8 for `.wy` import cycles. Scratch installs
  builtins + `std::expand` only (D10 holds: compiling never executes the
  source's imports).

## Traps found the hard way (all fixed, all worth re-reading)

1. **The compiler must never resolve through source lookup** (`-Iwy` +
   `import wyrm::tools::compile_source` recursed the compiler into itself →
   segfault; the backtrace is 33k frames of compile_on_scratch_). Fix: a
   builtin-table row bars the hook's `.wy` source branch
   (import_fs.c `table_find_`); precompiled `.wyd`/`.wyc` may still shadow.
2. The import opcode imports **every path prefix in turn** ("a.b.c" imports
   a, then a.b, ...), so the table needs `wyrm` (the `__init__` package
   marker) and `wyrm::compiler` (the aggregator) rows, not just leaf modules.
3. Compiler latent bugs surfaced by the first real `to_c()` exercise:
   `image.wy` `_join` was O(n²) in output size (pairwise join now);
   `context.wy` stored message `_path` as a joined **string** where pypoc
   and every consumer (image.wy `_describe`/`_message_note`,
   verify.wy:242) expect the **parts list**; `to_c` comments needed `*/`
   escaping (image.wy compiling itself puts `" by wypoc's bytecode
   compiler. */"` in a static string constant).
4. wy strings have **no `\"` escape** — build quotes with
   `_char_from_cp(34)` (image.wy's trick). `"\n"` does work.
5. `std::io::open` failures are error values, and `compiler_main.wy`
   passes the handle to `write` unchecked — a missing out-dir faults with
   "write: handle must be an integer...". Keep scratch/out dirs existing.
6. `st_mtime` is whole seconds; a touch in the same second as the cache
   write would not invalidate. `st_mtim` (+ `_POSIX_C_SOURCE 200809L`
   before includes in import_cache.c) fixes it.
7. `image.wy` has no container *reader* — regen re-compiles from source
   instead of parsing tree images (fine; deterministic).

## Regeneration (maintainer flow)

```sh
python3 scripts/regen_builtins.py [--check] [--keep DIR]
# reuses / rebuilds the gen1 tree (pypoc seed -> gen0 amalgam -> gen1),
# compiles wy/wyrm/tools/embed_build.wy with the tree's compiler_main into
# a scratch dir, runs it, and writes/copies into src/wyrm/embedded/.
# The embed manifest (EMBED/ALIAS_SYMBOLS) lives in embed_build.wy.
```
Staleness is enforced by the selfcompile meson test (calls
`regen_builtins.check_tree` after the fixed point). **Post-M3 simplification
opportunity:** the binary can now compile .wy itself, so regen could drive
the built `wyrm` with a tiny driver script instead of the pypoc/gen0 seed —
not done, would be a clean M4/M5 follow-up.

## Remaining milestones (per vm_plan/epic_11.md, unchanged in scope)

- **M4 — CLI parity** (pypoc/wypoc/cli.py is the reference; scan confirmed
  exit codes **0 success / 1 runtime+compile failure / 2 usage errors**):
  `-I` (done), `-v` (done), `--cache-dir` (done). Still to add:
  `--check` (compile-check without running; pypoc follows imports
  recursively — decide: port the recursion or scope down to single-file and
  note it in the report; a recursive check can reuse the scratch-compile
  path), `--build-bc [-o dir] [--emit wya,wyc,c] [--strip]` (writers exist:
  `image.wy` `to_wyc`/`to_wya`/`to_c`; the port never emits a debug section
  so `--strip` is nearly a no-op — document; a wy-side driver is cleaner
  than doing it in C: extend `compile_source.wy` or a new `tools/build_bc.wy`
  embedded module called from main.c), `-m mod::sub` (resolve via the same
  search path — `wy_link_import` + `run_init`, no entry seeding), and exit
  codes matching pypoc (main.c currently returns 0/1/2 — mostly aligned;
  audit each failure path). Each flag needs a meson test. Suggested split:
  parsing skeleton first, then fan out per flag.
- **M5 — pypoc optional + docs.** Verify `meson test` with `pypoc/` renamed
  away: the compiler suite (corpus-sweep, selfcompile) *skips* itself when
  pypoc/.venv is absent (run_corpus_sweep.py:191, run_selfcompile.py:125),
  everything else must pass. Then update AGENTS.md ("Prefer the default
  'wyrm' in user $PATH" → prefer the in-repo binary) and consider a
  `scripts/regen_goldens.sh` note. The sweep/selfcompile still *need*
  pypoc when present — that is accepted (second independent implementation)
  but must be documented.
- **M6 — EXPLAINER.md + the stripped-PATH end-to-end test.** Update
  doc/EXPLAINER.md:189-191 (".wy still needs pypoc" — now false) and the
  engines table in doc/agent-notes.md (already partially updated). Final
  meson test: `env -i PATH=/usr/bin:/bin ./buildDir/src/wyrm/wyrm -Iwy
  test/bytecode/hello.wy` → `Hello World`; assert the test's PATH provably
  lacks python (`command -v python3` must fail) before asserting the run.
  Then write `vm_plan/epic_11_report.md` per the README template (it must
  record: table format + provenance, tier split, exit-code table,
  --check recursion decision, stripped-PATH command output, and any
  remaining pypoc assumptions — the sweep tests + build_amalgam are the
  known ones).

## Quick orientation

- Embedded table: `src/wyrm/embedded/` (26 images, `wyrm_builtins_table.c`
  28 rows incl. `bjson`/`opcodes` package-relative aliases, unity file,
  hand-written `builtins.h`). Rows are `{"virtual::path", &<stem>_image}`.
- CLI: `src/wyrm/main.c` (compile shim, entry flow, flags); resolver:
  `src/platform/hosted/import_fs.c`; cache: `src/platform/hosted/import_cache.c`;
  wy-side compiler entry: `wy/wyrm/tools/compile_source.wy`; generator:
  `wy/wyrm/tools/embed_build.wy`.
- Run one: `./buildDir/src/wyrm/wyrm -v -Iwy -Itest/bytecode/expand
  test/bytecode/expand/wydecorated.wy` exercises entry compile, .wy import
  compile+cache, table expander, decorator expansion, and run.
- Test by name: `./buildDir/src/test/test_cwyrm --test-case='*import*'`.
- Known gaps to carry into the report: per-root `.wy`-before-precompiled
  order is a documented choice (contract didn't fix in-root order); `wy/`
  still holds committed `wy/wyrm/ast.wyc` + `image.wyc` pypoc-era artifacts
  that shadow their table rows (harmless today — shape-independent — but a
  candidate for deletion); `--dump-wys`/`-c`/REPL stay out of scope;
  cross-compilation (big-endian) still open per the epic.
