# Epic 10 pre-report — state-scan checklist verification

Verified 2026-09-18 against `doc-llm/history/wypoc-vm-port/epic_10.md`'s "State-scan checklist" (items 1-8)
and its *(verify in scan)* Assumptions, before any milestone work starts.

## Checklist results

| # | Check | Result |
|---|-------|--------|
| 1 | Parser output vs `sexpr.py` ROWS | **Partial pass.** `scripts/check_parser_truth.py`: **46/46 match** against the parser's own truth files. `pypoc/test/bytecode/hello.wy` parses to the expected pair-list shape (`$['module, $['fn_def, ...]]`). But `wy/wyrm/ast.wy:185-314` documents explicit **DIVERGES** from pypoc's ROWS: `if` 4 vs 3 fields, `for` 4 vs 3, `fn_def` vs `'fn` 5 vs 7 fields, `param` 3 vs 2, `static` 3 vs 2, `target`/type-union wyrm-only, `break` bare-symbol, and the whole class/lambda/coroutine family has no pypoc kind (`_CANNOT_CROSS`). The port must dispatch on wyrm's documented shapes, not copy ROWS field offsets. |
| 2 | Generated opcode tables current | **Pass.** All three generators report up-to-date, unmodified: `pypoc/tools/generate_opcode_header.py` → `opcode.h`, `generate_opcode_wy.py` → `wy/wyrm/opcodes.wy` (epic 9's `_wy` counterpart), and `generate_opcode_names.py` → `include/wyrm/opcode_names.h` (regenerated to /tmp and byte-diffed). |
| 3 | `compiler_bc` module sizes | **Close, one drift.** `analysis` 226, `expressions` 1005, `statements` 566, `functions` 389, `classes` 227, `handlers` 124, `verify` 281 all match expected exactly; `context.py` 597 (+6); `module.py` **545 vs 452 (+93, +21%)** — growth is `_expand_decorators` + `_flush_statics`. M6's split should account for module.py's added decorator pass. |
| 4 | Fixture progression names real files | **Pass with a path correction.** All progression fixtures exist (hello, arith, control_flow, closures, collections, errors, classes, messages, two_module/, wildcard/, coroutines) — but **only under `pypoc/test/bytecode/`**. Repo-root `test/bytecode/` has `.wyc`/`.out`/`.wy_a` but **no `.wy` sources**, and **`pypoc/test/samples/` does not exist at all** — the exit criterion's `pypoc/test/samples/**/*.wy` is stale. The real corpus is `pypoc/test/bytecode/**` plus root `test/bytecode/manifest.txt` (per-fixture status incl. REFUSED/DIVERGES vocabulary). |
| 5 | pypoc determinism | **Pass.** `hello.wy` and `classes.wy` each compiled twice with `--build-bc --emit wyc`; `cmp` byte-identical both. Byte-identity is a valid acceptance bar. |
| 6 | `_dsl.wy` decorator machinery | **Assumption fails / doc missing.** `doc/decorators.md` **does not exist** (neither root `doc/` nor `pypoc/doc/`). And `wy/wyrm/_dsl.wy` is a parser-combinator DSL (`@accept`/`@expect`, token kinds, packrat memoization) — unrelated to definition decorators. pypoc expands decorators at compile time via `wypoc/wyrm_eval_parse_tree.expand_decorators` (`compiler_bc/module.py:134-163`, seeded by `populate_globals` with corelib prelude), i.e. running real wyrm code over the sexpr tree; `handlers.py` refuses any `Decorated` reaching lowering. M5/M6 need an equivalent compile-time evaluator port, not `_dsl.wy`. Nearest truth: `pypoc/doc/grammar-notes.md:34-39`, spec 7.2. |
| 7 | `opcode.h` byte-identity | **Pass.** Only delta vs `pypoc/wypoc/compiler_bc/include/wyrm/opcode.h` is the 3-line provenance banner on the repo copy (added by `scripts/sync_pypoc_headers.py`); bodies identical. |
| 8 | Baseline test count | **6/6 meson tests OK** (`wyrm:loader`, `golden`, `golden-gcstress`, `disasm`, `cwyrm`, `wy-tests`), 0 fail/skip. |

## Assumptions *(verify in scan)* — outcomes

- **Parser matches ROWS** — only for a subset; see item 1. Epic 8's `ast.wy` table is
  the authoritative shape map for the port.
- **Epic 9 API suffices** — confirmed via `epic_9_report.md`: `sections()`,
  `code_bytes()`, `to_wyc()`, `to_wya()`, `to_c()` in `image.wy`; bjson
  encode/decode in `bjson.wy`; generated decoder/disassembler in `opcodes.wy`.
  Note: **`assemble_wya()` was deferred to epic 10** (epic 9 M4 escape hatch) —
  needed only if the milestone policy's `.wy_a` semantic-diff fallback is exercised.
- **pypoc determinism** — confirmed (item 5).
- **`_dsl.wy` for decorator expansion** — **refuted**, see item 6. Biggest plan
  correction from this scan.
- **C VM completeness** — **3 known gaps still open**, explicitly excluded from the
  golden harness (`src/test/test_bytecode_golden.cpp:184`):
  - `samples/eval_closures` — no COROUTINE case in `wy_iterator_next` (`src/iter.c`),
    so `for i in range(...)` loops never resume the coroutine;
  - `samples/eval_coroutines` — `cofun.value` faults: no property-table entry
    exposing `wy_coroutine::result`;
  - `samples/eval_modules` — no parent-then-member import fallback in
    `wy_link_import` (`src/link.c`).
  All three say "deferred to epic 6"; epic 6 has no report and nothing suggests they
  landed. M7's "full corpus green" bar cannot be met without closing them.
- **PAIR-chain boxing convention** — confirmed: `functions.py:281-342` (`_prologue`,
  "a one-element pair list is the box: `getidx`/`setidx` at index 0") and
  `analysis.py:210` (`cell_names`). The port must reproduce this exact convention.

## Recommended plan edits before M1

1. Fix exit-criterion/M7 paths: corpus sources are `pypoc/test/bytecode/**.wy`, not
   `test/bytecode/*.wy` (repo root has no `.wy`) and not `pypoc/test/samples/**`
   (does not exist).
2. Re-scope M5/M6 for a `wyrm_eval_parse_tree` equivalent (decorator expansion is
   compile-time evaluation of wyrm code over the sexpr tree, not templates), or
   explicitly defer the decorator-dependent fixtures (`samples/decorators.wy` is
   already REFUSED in the manifest, so deferral is precedented).
3. Decide the 3 open C-VM DIVERGES: fix them in this epic, or scope M7's corpus
   sweep to exclude them with a recorded note.
4. Add the +93-line `module.py` growth (decorator pass, statics flush) to M6's
   scope estimate.
