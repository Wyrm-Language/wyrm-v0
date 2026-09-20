# Epic 8 - Front-end cleanup and verification in wy/wyrm/

## Goal

Clean up the self-hosted wyrm front end (`wy/wyrm/{ast,parser,tokenizer,decode,_dsl,
compiler}.wy`) so its AST shape is documented and aligned with pypoc's canonical sexpr
wire format, fix the known parser/decoder bugs found at plan time, and build a
verification harness (a comparing golden runner plus truth samples covering every
grammar form) so "the parser works" is checked, not assumed. This front end is what
stands between epic 6's C VM (running pypoc-compiled bytecode) and epics 9-10's goal of
compiling `.wy` source without Python, so this epic's exit is a precondition for epic 9.

**Exit criterion:**
```
python3 scripts/check_parser_truth.py
python3 scripts/gen_grammar_doc.py > /tmp/grammar.md && cmp /tmp/grammar.md doc/grammar.md
```
Both succeed. Additionally, wherever epic 6's C VM can run compiled `.wy` (see M6), its
parser output matches pypoc's for the same source.

## Inputs

- Previous report: `vm_plan/epic_6_report.md` (or latest available) for where the C VM's
  hosted import path and CLI live, since M6 needs to run compiled front-end code on the C
  VM. Epic 8 can run in parallel with epic 6 per the README's phase map; if epic 6 isn't
  finished, M6 is descoped and the report says so.
- State-scan checklist:
  1. Is `wy/wyrm/ast.wy` still ~179 lines with vestigial `TreeItem`/`UnaryOp`/`BinOp`/
     `Program`/`Number` classes (lines ~4-36) and the "generally _ALL_ faulty" comment
     near line 121, alongside the live `n_*`/`s_*`/`mk_*` sexpr helpers?
  2. Does `decode.wy` still lack `\x`/`\u` escapes (TODO near line 18, in `decode_str`)?
  3. Does `parser.wy:871`'s `slot_option` still reference `value('getter,
     'TOK_KW_GETTER)` where `'TOK_KW_GETTER` is absent from `tokenizer.wy`'s
     `_HARD_KEYWORDS` (lines ~105-134)? Confirm with a one-line `.wy` sample using
     `getter =` in a slot option.
  4. Does `parser.wy:581`'s `_mk_type_expression` still discard a union type to
     `$['type, 'auto]` with the algebraic-types FIXME comment?
  5. Are `emit`/`signal`/`task`/`thread` still hard keywords (tokenizer.wy:116, 130,
     133-134) with no parser rule (grep `parser.wy` for `TOK_KW_EMIT`, `TOK_KW_SIGNAL`,
     `TOK_TASK`, `TOK_THREAD`; expect zero hits)?
  6. Is `scripts/gen_grammar_doc.py`'s `PARSER_WY` path still
     `wy/wyrm/parser/parser.wy` (a directory that does not exist; the real file is
     `wy/wyrm/parser.wy`)?
  7. Does `test/wy/test_wy_tokenizer.wy` call a `Scanner` API that has drifted from what
     `tokenizer.wy` implements today (compare against `test_wy_token_stream.wy`)?
  8. Does `test/samples/parser/` still hold only the two pairs (`0_000-assign.wy`,
     `0_001-set.wy`), with nothing actually diffing a fresh parse against the `.ast`
     files (only `update_sample_parser_truth.py`, which regenerates)?
  9. Do `test/wy/*.wy` tests still just print PASS/FAIL and always exit 0? Check for a
     `defer on error` that swallows failures.
  10. Does pypoc's tree walker still not short-circuit `and`/`or` (grep
      `wyrm_eval_parse_tree.py`)? Check whether any `wy/wyrm/*.wy` file relies on
      short-circuiting.
  11. Is `~/tools/bin/wyrm` still pypoc's Python CLI? Confirm this repo's own `wyrm`
      binary (once epic 1's CLI lands) is invoked via its build path, not assumed on
      `PATH`.
  12. Run every `test/wy/*.wy` under `~/tools/bin/wyrm -Iwy`; record the PASS/FAIL text
      verbatim as the "before" baseline for the report.

## Context to load

Read in full:
- `wy/wyrm/ast.wy` (179 lines), the vestigial/live split needs to be seen together.
- `wy/wyrm/decode.wy` (60 lines).
- `scripts/gen_grammar_doc.py` (~40 lines) and `scripts/update_sample_parser_truth.py`
  (~35 lines), both short, both need rewriting.
- `doc/grammar.md`'s first ~20 lines, the preamble the regenerated file must reproduce.
- `AGENTS.md`'s "Wyrm Logic" section, plus any existing example of a `meson.build` `test()`
  wrapping a non-C tool (grep for `find_program`).

Read specific ranges (grep first to confirm line numbers, then read just that span):
- `wy/wyrm/parser.wy`: ~860-880 (`slot_option`, the `getter` bug), ~570-590
  (`type_expression`, the union FIXME), the `#>` doc-comment convention in the first ~30
  lines. Total file is 1369 lines; do not read it all, use Explore or targeted grep for
  specific rule names as needed.
- `wy/wyrm/tokenizer.wy`: ~95-145 (`_HARD_KEYWORDS` and the keyword-list comment).
- `pypoc/wypoc/sexpr.py`: the `ROWS` tuple and the `Row`/`F` helper definitions just
  above it (grep `_ROWS_BY_CLASS`/`_ROWS_BY_KIND` to find the tuple's extent). This is
  the canonical node-kind to fields table `ast.wy`'s replacement must align with.
- `test/wy/test_wy_tokenizer.wy` (full, short) vs. `test/wy/test_wy_token_stream.wy`
  (grep its Scanner-equivalent calls) to find the API drift.
- `src/test/meson.build` (~30 lines), the pattern for registering a test executable, as
  a reference for the new wrapper's `test()` call shape.

Grep-only:
- `wy/wyrm/_dsl.wy` and `wy/wyrm/compiler.wy`: confirm neither calls the vestigial
  `ast.wy` classes by name (`grep -n "TreeItem\|UnaryOp\|BinOp(\|Program(\|Number("`).
- `test/samples/parser/*.wy`/`*.ast`: read only when authoring new pairs in M3.

Rough budget: 8-10k tokens loaded across the epic; keep individual milestones to 2-3k by
delegating full-file searches to Explore subagents.

## Assumptions

- `ast.wy`'s vestigial classes have no live caller anywhere in `wy/` *(verify in scan)*.
- The live sexpr representation (`$['kind, ...]` pair lists, `n_*`/`s_*`/`mk_*` helpers)
  is what `parser.wy` actually emits, keyed the same way `sexpr.py`'s `ROWS` keys its
  ascii `kind` strings *(verify in scan, spot check 3-4 node kinds)*.
- `getter`'s fix is adding a hard-keyword entry, not changing the parser rule shape
  *(verify in scan, check whether `setter` already works as the pattern to mirror)*.
- The union-type FIXME's fix only needs to stop discarding the union into `'auto`; making
  every downstream consumer understand unions is out of scope *(verify in scan, check
  what currently reads `'type` nodes to judge blast radius)*.
- `emit`/`signal`/`task`/`thread` need a decision in M2: add minimal parser rules, or
  remove them from `_HARD_KEYWORDS` so plain identifiers of those names work. Default
  recommendation is to remove them (`doc/stdlib.md`'s `Signal`/`emit` example reads as a
  library pattern, not syntax) unless the scan finds clear evidence otherwise
  *(a plan-time choice the executor may revisit)*.
- pypoc's non-short-circuiting `and`/`or` is out of scope to fix; this epic only confirms
  `wy/wyrm/*.wy` doesn't depend on short-circuit evaluation *(verify in scan)*.
- A meson `test()` can skip cleanly via `find_program(..., required: false)` plus a
  `.found()` check, or the wrapped script exiting with meson's skip code 77
  *(verify in scan against the meson version this repo requires)*.

## Milestones

### M1 - `ast.wy` cleanup and node table

**Scope:** Delete the vestigial `TreeItem`/`UnaryOp`/`BinOp`/`Program`/`Number` classes
once confirmed dead. Replace the "generally _ALL_ faulty" comment block with one
documented table: for every node kind the parser actually emits (cross-reference
`sexpr.py`'s `ROWS`, and grep `parser.wy` for every `$[` construction site so the table
covers what's produced, not just what pypoc's decoder expects), one line of
`kind -> (field, field, ...)`, with an inline `DIVERGES(reason)` note wherever wyrm's
parser and pypoc's `sexpr.py` differ, matching the corpus convention from
`vm_plan/README.md` rather than silently picking a side. Keep the live helper functions;
update any that reference a deleted class.

**Files:** `wy/wyrm/ast.wy`.

**Acceptance:** no vestigial class names remain; the table's kind list has no gaps
against `grep -o "\$\['[a-z_]*" wy/wyrm/parser.wy | sort -u`; `test_wy_parser.wy` still
passes under `~/tools/bin/wyrm -Iwy`.

**Model:** Sonnet, mechanical once the scan confirms dead code.

**Fan-out:** none, single file needs one consistent pass.

### M2 - Parser and decoder bug fixes

**Scope:** Fix the four confirmed bugs: (a) `decode.wy`'s missing `\x`/`\u` escapes in
`decode_str`, matching whatever `doc/language-spec.md` documents for string literals (if
undocumented, match Python's `\xHH`/`\uHHHH`); (b) the `getter` keyword, add it to
`_HARD_KEYWORDS` mirroring `setter`; (c) the union-type FIXME, build a real union type
node (e.g. `$['type, 'union, types...]`, consistent with existing `'type` node shapes)
instead of discarding to `'auto`; (d) the `emit`/`signal`/`task`/`thread` question, act
per the Assumptions default and record the decision and rationale in the report.

**Files:** `wy/wyrm/decode.wy`, `wy/wyrm/tokenizer.wy`, `wy/wyrm/parser.wy`.

**Acceptance:** new truth samples (M3) exercise each fix, a string literal with
`\x41B`, a slot with `getter = ...`, a parameter typed `int | str`, and a sample
proving the keyword decision (either a minimal construct or plain-identifier use).
`check_parser_truth.py` (M4) passes on all of them.

**Model:** Sonnet, each fix is small and independently testable.

**Fan-out:** 2 Sonnet subagents in parallel: one for `decode.wy` (self-contained), one
for the three `parser.wy`/`tokenizer.wy` fixes kept together to avoid file conflicts.

### M3 - Expanded parser truth corpus

**Scope:** Grow `test/samples/parser/` from 2 pairs to one small sample per grammar rule
family in `doc/grammar.md` (literals, `if`/`while`/`for`, `fn` with defaults/varargs/
kwargs, `class` with slots/getters/setters, `import`/`import *`, `try`/`catch`/`defer`,
message send `!`, `is`/type-check including the new union form, decorators, `with`).
Reuse `update_sample_parser_truth.py`'s generation approach, fixing its invocation if
stale, but hand-verify the first few `.ast` outputs by reading them before trusting the
regenerator, since a parser bug could produce a wrong-but-stable truth file.

**Files:** new pairs under `test/samples/parser/*.wy` + `*.ast`; update
`scripts/update_sample_parser_truth.py` if its invocation is stale.

**Acceptance:** roughly 15-25 pairs, one per grammar rule family; running the regenerator
twice is a no-op diff.

**Model:** Sonnet, sample authoring against a known grammar is mechanical.

**Fan-out:** 3 Sonnet subagents in parallel, each owning a disjoint rule-family subset
(literals/expressions; statements/control flow; classes/modules/decorators), each
producing its own pairs with no shared file.

### M4 - Comparing golden runner and grammar-doc regeneration fix

**Scope:** Write `scripts/check_parser_truth.py`: for every `.wy` in
`test/samples/parser/`, parse it (`~/tools/bin/wyrm -Iwy -m wyrm::parser`, matching
`update_sample_parser_truth.py`'s invocation) and diff against the committed `.ast`,
exiting non-zero with a unified diff on mismatch. Fix `gen_grammar_doc.py`'s dead
`PARSER_WY` path; regenerate `doc/grammar.md` and commit the output; confirm the
`#>>include:` mechanism still resolves from the corrected directory.

**Files:** new `scripts/check_parser_truth.py`, `scripts/gen_grammar_doc.py` (path fix),
regenerated `doc/grammar.md`.

**Acceptance:** `check_parser_truth.py` exits 0 against the M3 corpus and non-zero when a
`.ast` file is deliberately corrupted (smoke-test then revert); the grammar-doc cmp in
the exit criterion passes.

**Model:** Sonnet, both scripts are small with an exact spec.

**Fan-out:** none, `check_parser_truth.py` needs M3's corpus first, keep sequential.

### M5 - Tokenizer test refresh and meson wrapper

**Scope:** Refresh `test/wy/test_wy_tokenizer.wy` against the current `Scanner` API.
Fix the always-exit-0 pattern: write a small scraper (mirroring M4's diff-and-exit style)
that runs each `test/wy/*.wy` under `~/tools/bin/wyrm -Iwy`, greps stdout for `FAIL`, and
exits non-zero if found; wire it into `meson test` via `find_program('wyrm', required:
false)`, skipping when absent. Check whether `exit(code)` (referenced in
`wyrm_builtins.py install()` as `exit=exit_`) actually sets process exit status; if so,
have failing tests call it directly instead of relying only on the scraper's grep.

**Files:** `test/wy/test_wy_tokenizer.wy` (rewritten), a new scraper script (e.g.
`scripts/run_wy_tests.py`), a `meson.build` addition (root or new `test/wy/meson.build`).

**Acceptance:** `meson test -C buildDir` includes a wy-tests case that passes when all
`test/wy/*.wy` report only PASS, and reports SKIP (not silently absent, not a hard
failure) when no `wyrm` binary is found.

**Model:** Sonnet for the scraper and meson wiring; escalate to Opus only if
exit-code propagation needs a real language-level decision, verify with a one-line smoke
test before escalating.

**Fan-out:** none, the meson wrapper depends on the scraper existing.

### M6 - Parity check against the C VM (contingent on epic 6)

**Scope:** If epic 6's C VM CLI can load and run `.wyc` images by the time this
milestone starts, compile the front end with pypoc's `--build-bc`, run it on the C VM
against the M3 corpus, and extend `check_parser_truth.py` (or add a sibling) to diff both
engines' outputs against each other as well as the committed truth. Any difference
becomes a fix (only if genuinely in scope, epic 8 does not touch C code per the plan) or
a named `DIVERGES(reason)` entry, per `vm_plan/README.md`'s conformance discipline. If
epic 6 hasn't landed, mark this milestone blocked in the report with what's needed to
unblock it.

**Files:** extension to `scripts/check_parser_truth.py` or a new sibling script; no `.wy`
or C source changes expected.

**Acceptance:** if unblocked, the parity script reports 0 unexplained differences over
the full M3 corpus. If blocked, the report states the blocker precisely.

**Model:** Opus if a genuine cross-engine divergence needs judgment; Sonnet for the
mechanical harness-running part.

**Fan-out:** none, this is the epic's integration point, done last.

## Out of scope / deferred

- Porting the bytecode compiler (`compiler.wy`'s 30-line stub); that is epic 10.
- A bjson writer or image emitter; that is epic 9.
- Fixing pypoc's `and`/`or` short-circuit semantics; confirming `wy/wyrm/*.wy` does not
  rely on it is in scope, changing pypoc's contract is not.
- Full union-type consumption beyond not discarding it at parse time.
- A general wyrm test framework beyond the minimal PASS/FAIL scraper in M5.
- Big-endian or non-x86 target verification; the front end is pure text/tree
  manipulation and not expected to be endian-sensitive.

## Risks

- The vestigial `ast.wy` classes might not be fully dead outside `wy/wyrm/`. Mitigation:
  M1's scan greps across all of `wy/`, not just `wy/wyrm/`, before deleting; keep and note
  in the report anything with a live reference.
- Making `getter` a hard keyword could collide with existing identifier usage.
  Mitigation: `grep -rn '\bgetter\b' wy/ test/` first; if collisions exist, consider a
  soft-keyword approach instead and document the choice.
- The invented union-type node shape might not match what a future compiler expects,
  since nothing consumes `'type` nodes meaningfully yet. Mitigation: keep it consistent
  with existing `'type` conventions in the file; flag it in the report as a choice epic
  10 may revisit.
- The `emit`/`signal`/`task`/`thread` decision is genuinely ambiguous from available
  evidence. Mitigation: default to removal per the Assumptions section unless the scan
  finds clear contrary evidence; document the decision either way.
- `check_parser_truth.py` and the meson wrapper both depend on `~/tools/bin/wyrm`
  existing, which may not be true in CI or a fresh checkout. Mitigation: both are
  designed to skip gracefully (exit 77 / `required: false`), not fail hard.
- M6 may be entirely blocked if epic 6 has not landed; since epic 8 can run in parallel
  with epic 6 per the plan, this is expected. Mitigation: M6 degrades to a documented
  blocker rather than stalling the epic.

## Report

`vm_plan/epic_8_report.md` follows the README template, plus:

- The node-kind table landed in `ast.wy` (pointer to it, not a duplicate), with any
  `DIVERGES` entries against `sexpr.py`'s `ROWS`.
- The decision on `emit`/`signal`/`task`/`thread` and why.
- The union-type node shape chosen in M2, flagged as revisitable by epic 10.
- Final count of `test/samples/parser/` pairs and which grammar rule families they cover.
- Whether M6 ran or was blocked, and exactly what would unblock it.
- Before/after PASS/FAIL output for `test/wy/*.wy`, and confirmation the meson wrapper
  skips correctly when `wyrm` is absent (show the skip output, not just claim it).
- `cmp` result for `doc/grammar.md` regeneration (expect "identical").
