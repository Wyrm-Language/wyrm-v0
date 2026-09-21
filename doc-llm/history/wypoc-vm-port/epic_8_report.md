# Epic 8 report — Front-end cleanup and verification in wy/wyrm/

Session dates: 2026-09-17 · Models used: Opus (scan, all bug investigation/fixes, M4/M6,
report), Sonnet subagents (M1, M2, M3's three parallel corpus slices) · Commits:
`e5aa3c9`..`cb1e746`

## Landed

- **M1** (`e5aa3c9`): deleted the vestigial `TreeItem`/`UnaryOp`/`BinOp`/`Program`/`Number`
  classes from `wy/wyrm/ast.wy` (confirmed zero live references anywhere in `wy/`) and
  replaced the "generally _ALL_ faulty" comment block with a documented
  `kind -> (field, ...)` table for every node kind `wy/wyrm/parser.wy` actually produces.
- **M2** (`74d1bd3`, plus the M3-adjacent fixes below): the epic's four originally-scoped
  fixes (decode.wy `\x`/`\u` escapes, the `getter` keyword bug, the union-type FIXME, the
  `emit`/`signal`/`task`/`thread` keyword decision) plus four more real bugs M1's scan
  incidentally found (`stmt_list()` leaking a raw token and never discarding separators;
  `$_binary_expr` never wrapping a multi-operand chain in a `'binop` head; `$_mk_postfix`'s
  `attr_op`/`index_op` using `cdr` where `cadr` was needed; `$_mk_is`/`$_mk_in` using bare
  `[...]` array-literal syntax instead of `$[...]` pair lists). Re-cut into the epic file
  (`3088814`) with the user's approval before proceeding, since nearly every real `.wy`
  file has a block body or arithmetic expression and M3's corpus would otherwise have
  captured wrong-but-stable truth for almost everything.
- **M3** (`1620386`, plus two more fixes found mid-authoring, `e3e18fa`/`4e179bd`):
  `test/samples/parser/` grown from 2 pairs to **46**, one sample per grammar rule family,
  fanned out across three parallel sessions (literals/expressions; statements/control
  flow; classes/modules/decorators). Found and fixed two more real bugs along the way:
  keyword/spread call arguments (`f(x=1)`, `f(*a, **k)`) were never implemented at all
  (`argument()` was `@accept expression` only, silently degrading any keyword-arg call to
  zero arguments rather than faulting); and `program()` never required reaching
  end-of-input, so any unparseable trailing content — not just the one construct that
  first surfaced it — was silently dropped from the AST with no error signal.
- **M4** (`c4e8782`): fixed `gen_grammar_doc.py`'s dead `PARSER_WY` path
  (`wy/wyrm/parser/parser.wy`, a directory that doesn't exist, → `wy/wyrm/parser.wy`) and
  regenerated `doc/grammar.md` (idempotent: a second run is byte-identical). New
  `scripts/check_parser_truth.py` diffs fresh parser output against all 46 truth pairs,
  exits non-zero with a unified diff on mismatch; smoke-tested against a deliberately
  corrupted `.ast` file. Also fixed `update_sample_parser_truth.py`'s stale bare-`wyrm`
  invocation to match.
- **M5** (`9d33a9d`): confirmed `exit(code)` genuinely propagates as the real process exit
  status, then added `exit(1)` at every FAIL site across all four `test/wy/*.wy` files
  (fail-fast, since a process exit code can only report one outcome). `test_wy_tokenizer.wy`
  needed no API refresh — the epic's own scan checklist speculated drift that scan found
  didn't exist. New `scripts/run_wy_tests.py` (exit-code-or-FAIL-text failure detection,
  skips cleanly when `pypoc/.venv/bin/wyrm` is absent) wired into root `meson.build` as
  `wy-tests`; confirmed 6/6 meson tests pass with it included, and separately confirmed a
  clean SKIP when the binary is hidden.
- **M6**: investigated, found genuinely blocked — see below, not on epic 6.

## Deviations from the epic file

- **The epic's own scan-time assumption about the `getter` bug's fix was wrong**, corrected
  before M2 executed: `setter` was never a hard keyword (`tokenizer.wy`'s `_HARD_KEYWORDS`
  never had a `TOK_KW_SETTER` entry either) — it's matched via `value('setter, "setter")`,
  a *soft* keyword matching literal text on an ordinary identifier token. `getter`'s clause
  mistakenly passed the (nonexistent) symbol `'TOK_KW_GETTER` where the `value()`
  combinator expects a sub-parse expression. Fixed by mirroring `setter`'s working pattern
  (`value('getter, "getter")`), not by adding a hard-keyword table entry.
- **M2 was re-cut mid-execution** for four more real bugs M1's scan surfaced (see Landed,
  above) — recorded in `epic_8.md` itself before the fixes were applied, per protocol.
- **The `emit`/`signal`/`task`/`thread` removal is now evidence-backed, not just the
  epic's default guess**: confirmed by direct testing that pypoc's own reference parser
  (which still hard-reserves `emit`/`signal`, though not `task`/`thread`) fails to parse
  `doc/stdlib.md`'s own documented `fn [Signal] emit(obj):` example
  (`SyntaxError: unexpected 'emit'`). This is pypoc's own bug, out of this epic's scope
  (a separate nested checkout) — flagged for whoever revisits it, not fixed.
- **M3 found and fixed two more real bugs beyond the four from M2's re-cut** (kwargs/spread
  args unimplemented; `program()` not requiring EOF) — see Landed. Neither was scoped at
  plan time; both were fixed directly rather than re-cutting the epic file again, since
  each was narrow and well-understood once diagnosed (matching the "small, independently
  testable" character of the epic's own M2 fixes, not a design-level change).
- **M6 is blocked, but not for the reason the epic file anticipated.** The epic frames M6
  as contingent on epic 6 landing (a C VM CLI that can load and run `.wyc`); that
  capability has existed since epic 1, so epic 6's status was never actually the
  precondition. Investigation instead found two different, genuinely out-of-scope
  blockers — see below.

## The node-kind table

Lives in `wy/wyrm/ast.wy` (search "node table" / read the file directly — not duplicated
here per the epic's own instruction). Every DIVERGES entry against `pypoc/wypoc/sexpr.py`'s
`ROWS` is inline as a comment on its kind. Notable ones: `array`/`list` names swap vs.
pypoc's `list` Row; `neg`/`pos`/`invert` are separate kinds where pypoc has one `'unop`
with an op field; `and`/`or` are flat n-ary where pypoc's Row is strictly binary
(chains nest); the whole `import` family is structurally different (wyrm nests narrowing
kinds, pypoc has one flat `'import` Row); `class_def`/`class_expr`/`slot_def`/
`slot_option`/`co_*`/`lambda`/`kwarg`/`spread`/`spread_kw` have no pypoc counterpart at
all. `true`/`false`/`nil`/`break`/`continue`/`pass` are documented as bare symbols (not
wrapped `$[kind]` nodes) after M3 found this was being misread as a bug — see below.

## The `emit`/`signal`/`task`/`thread` decision

Removed from `wy/wyrm/tokenizer.wy`'s `_HARD_KEYWORDS`. Rationale and evidence in
Deviations, above.

## The union-type node shape

`$['type, 'union, type1, type2, ...]` — tag, then N payload items, matching every other
variadic node's convention in this file (`cons('tuple, expr)`, `cons('and, expr)`, etc.).
Flows through to `is` for free (`x is int | str` → `$['is, 'x, $['type, 'union, ...]]`),
since `comparison()`'s `is` rule already reuses `type_expression`. Revisitable by epic 10:
nothing meaningfully consumes `'type` nodes yet, so this shape has never been exercised by
a real downstream consumer.

## Corpus: 46 pairs (was 2)

- `1_0NN-*` (16 pairs): literals (int/float/string/symbol/bool/nil), collections, unary,
  binary arithmetic/bitwise/exponent/comparison, logical `and`/`or`, `is`/`in` with union
  types, postfix chains, qualified names, lambda, call kwargs/spread args.
- `2_0NN-*` (13 pairs): `if`/`elif`/`else`, `while`, `for`/`else`, `return`/`yield`,
  `defer`/`defer on`, `try`/`catch`, `var`/`set`/`set_values`/`if_set`, `static`, nested
  blocks, multi-statement blocks, `break`/`continue`/`pass`.
- `3_0NN-*` (15 pairs): class basics/slots/getter-setter/methods/anonymous classes, every
  import form, decorators on `fn`/`class`/`var`, full `fn` param grammar including union
  types, `co` definitions, lambda and co-lambda.
- `0_0NN-*` (2 pairs): the original `assign`/`set` samples, untouched.

Every pair was hand-verified against `ast.wy`'s node table before being saved as truth —
this process is what found the `stmt_list`/`binop`/`attr`/`index`/`is`/`in`/kwargs/
`program()`-EOF bugs, not a bug-hunting pass separate from corpus authoring.

## A false-positive worth recording

A sibling M3 session initially flagged `break`/`continue`/`pass` as buggy (`while true:
continue` producing body `$['continue]`, which it read as "the continue node's contents
leaked into the body list"). It isn't a bug: `nil`/`true`/`false`/`break`/`continue`/
`pass` are all represented as **bare symbols**, not wrapped `$[kind]` nodes — the parser
DSL's own `value(sym, subseq)` combinator doc comment ("replace subseq with the literal
node `$[sym]`") is misleading; `_tmpl_value` actually substitutes the bare symbol
directly. Confirmed directly: `x := true` parses to `$['define, 'x, ..., 'true]`, not
`$['define, 'x, ..., $['true]]`. `ast.wy`'s table and `_dsl.wy`'s doc comment were both
corrected to state this explicitly (`e3e18fa`), and a `break`/`continue`/`pass` truth
sample (`2_012-break-continue-pass.wy`) was added with the correct expected shape.

## M6: blocked, not on epic 6

The C VM's CLI has loaded and run `.wyc` images since epic 1; that was never the actual
precondition. Investigation (compiling `wy/wyrm/parser.wy` with `pypoc/.venv/bin/wyrm
--build-bc` and running the result on the C VM against an M3 sample) found two different,
genuinely out-of-scope blockers instead:

1. **The C VM's hosted import hook has no notion of a package `__init__.wy`.**
   `wy_import_fs_hook` (`src/platform/hosted/import_fs.c`) maps `a::b` to
   `<root>/a/b.wyc` uniformly. `import wyrm::ast` compiles to an implicit ancestor import
   of bare `wyrm` first (the same "`import std::io` also imports bare `std`" pattern
   epic 5/M6 already found and worked around for `std::io` specifically, by hand-registering
   an empty builtin module named `"std"`). `wyrm` isn't a builtin module, so that fix
   doesn't apply — the loader looks for `<root>/wyrm.wyc`, which nothing produces
   automatically; `wy/wyrm/__init__.wy` (an empty package marker, just a comment) is never
   compiled into one by pypoc's own toolchain. I worked around this manually
   (hand-compiled `__init__.wy`'s content under a renamed output, `wyrm.wyc`) purely to
   keep investigating — this is not a real fix and shouldn't be treated as one.
2. **pypoc's bytecode compiler rejects a negative literal as a slot default.**
   `wy/std/io.wy`'s `slot handle: int = -1` (needed transitively by `parser.wy`'s
   `import std::io::*`) fails `--build-bc` outright: `slot handle: a default must be a
   constant (line 15, column 23)`. The tree-walking interpreter accepts this exact same
   source fine (used throughout epic 7's own testing) — this is a real, narrow
   pypoc-compiler bug (Python code, `wypoc/compiler_bc/*`), confirmed reproducible, not
   investigated further since it's outside `wy/`.

Both are real, reproducible, narrow bugs — neither is `wy/` front-end code, so per the
plan's own "epic 8 does not touch C code" boundary (and, for #2, does not touch pypoc's
Python compiler either), neither was fixed here. Whoever picks this up next has an exact
repro for both.

## `test/wy/*.wy` before/after

Before (start of session) and after (end of session) PASS/FAIL text, all four files: 0
FAIL, all PASS, in both cases — no regressions were ever introduced, but the *meaning* of
"before" changed: every file always exited 0 regardless of PASS/FAIL text before M5;
after M5, a FAIL now exits 1 (confirmed via a deliberate smoke test). The meson `wy-tests`
wrapper's SKIP path was demonstrated directly (temporarily hiding
`pypoc/.venv/bin/wyrm`, confirmed `Skipped: 1`, restored).

## `doc/grammar.md` regeneration

`cmp`: identical. `python3 scripts/gen_grammar_doc.py > /tmp/grammar.md && cmp
/tmp/grammar.md doc/grammar.md` passes as the epic's own exit criterion specifies.
`scripts/check_parser_truth.py` reports `46/46 match`.

## Orientation for the next session

- `wy/wyrm/ast.wy`'s node table is the reference for what any node kind should look like;
  cross-check against it before assuming a shape is a bug (the break/continue/pass
  false-positive above is the cautionary example).
- `program()` (`wy/wyrm/parser.wy`) now requires `~'TOK_END`; any future grammar rule
  addition that can legitimately leave trailing input (there shouldn't be one) needs to
  route through `program()`'s own sequence, not bypass it.
- The `argument()` kwargs/spread fix landed a wyrm-only `kwarg`/`spread`/`spread_kw` node
  shape with no pypoc counterpart — flag this DIVERGES if a future epic needs `'call`
  nodes to round-trip through pypoc's own compiler/decoder.
- M6's two blockers (hosted-import `__init__.wy` gap; pypoc bytecode-compiler negative-
  literal-default rejection) are both precisely reproducible per the repro steps above —
  worth picking up before epic 9/10 need the front end to actually run standalone on the
  C VM, since epic 9's own bjson writer will need `std::io`-equivalent functionality one
  way or another.
- `scripts/check_parser_truth.py` and `scripts/run_wy_tests.py` both resolve
  `pypoc/.venv/bin/wyrm` directly (not `~/tools/bin/wyrm`, a stale unrelated checkout) —
  keep using that convention for any future `wy/` tooling script.
