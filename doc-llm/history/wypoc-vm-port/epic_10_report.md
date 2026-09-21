# Epic 10 report — Compiler port (COMPLETE: M1-M7 landed 2026-09-19; stable base for 10a)

Session dates: 2026-09-18 (M1-M4, prior session) · 2026-09-18 (cleanup, M5-M7, this
session) · 2026-09-19 (M7 continued) · Models: Opus (M1, scan, this report) · Sonnet (M2-M6 + cleanup)

## Commits

- `e3d109e` Epic 10: M1-M4 (prior session)
- `fbbf133` Epic 10: gating fixes + M5 (classes.wy)
- `090e767` Epic 10: M6 (module.wy + verify.wy)
- `3a2aba9` Epic 10 M7: stub reporting + error_message (committed 2026-09-19)
- M7 completion (gen2 bring-up fixes, parser fixes, harness wiring): see the final commit.

## What we are happy with (accepted milestones)

- **M1-M4** (prior session, re-verified this session): context/analysis/expressions/
  handlers/statements/functions. Every acceptance check still passes.
- **M5 — classes.wy**: slots with constant defaults, virtual-slot accessors, `init`,
  message map, statics as module globals, toplevel class realization (`class` op +
  `gset` + static initializers), `fn [T]` dispatch registration (`reg_msg`).
  Acceptance: `classes.wy` and `messages.wy` compile through the ported compiler and
  run on the C VM with output identical to their corpus `.out` files.
- **M6 — module.wy + verify.wy**: the whole pipeline (declare pass, hoisted imports
  over wyrm's four import shapes, imported-name binding, top-level walk via
  `TOPLEVEL_HANDLERS` with `compile_statement` fallback, static flush, assemble,
  verify). verify.wy ports every structural check with one negative test per
  category, wired into `meson test` (`test/wy/test_compiler_verify.wy`).
  Acceptance: `two_module/{geometry,report}`, `wildcard/{palette,paint}`, and
  `coroutines` compile via `compile_module` and match their corpus `.out`.
- **compiler.wy disposition** (epic file left this open): kept as a documented empty
  package marker. Deleting it breaks every `wyrm::compiler::<child>` import, and a
  re-export of its own children reads as a self-import cycle.
- **M7 corpus sweep** — green: 19/20 corpus fixtures compile via the ported compiler
  (running on the C VM) and their runs match `.out`; the one refusal is
  `decorators/decorated` (decorator expansion, deferred — see blockers).
  `scripts/run_corpus_sweep.py` builds the compiler-driver amalgam, compiles every
  manifest row with a `.wy` source, runs each image, and diffs. Not yet wired into
  `meson test`.

`meson test` is 6/6 at this checkpoint (re-verified 2026-09-19 with the
uncommitted `analysis.wy`/`context.wy` fixes in tree).

## Pre-M5 cleanup (both gating issues from the prior report, resolved)

- **Bare-expression-statement gap**: parser.wy now wraps statement-position
  expressions in `expr_stmt` (`_mk_stmt_expr`), exempting the keyword-statement
  primaries (`if/while/for/return/yield/break/continue/pass`, including their bare
  -symbol forms). Matches pypoc's reference shapes (parser.py's `simple_stmt`
  ordering).
- **Cross-module instance-method dispatch**: the C VM now adopts an imported
  module's message table at import time (`wy_link_adopt_messages`, src/link.c,
  called from both IMPORT paths), the compiled counterpart of the tree walker's
  `_adopt_messages`. pypoc's own compiled VM faults on the same repro; the
  interpreter was the reference. Golden fixtures `two_module/shapes{,_main}` pin it.

## Regressions and latent bugs discovered this session

Each was latent (exercised for the first time by whole-fixture runs) or a true
regression from this session's work; all fixed unless noted.

1. **Statement-position expressions were never wrapped** (parser) — fixed.
2. **`recv ! m()` vs `recv ! m` were indistinguishable trees** (both nil args), so
   every zero-arg send compiled as a bound-message (`getmsg`) and never dispatched —
   the classes fixture printed `<object>`s. Fixed with a `'send`/`'bound` fifth
   field (`message_op`), since pair lists cannot say "empty" vs "absent" the way
   pypoc's grammar distinguishes `[]` from `None`.
3. **Comparisons parsed as operator-headed nodes** (`$['<, l, r]`) — undocumented
   and undispatchable. Now fold into the documented `'binop (op, lhs, rhs)` shape.
4. **`boundary_params` didn't unwrap `co_def`'s `co_params`** — coroutine params
   were lost (`undefined name 'limit'` on the coroutines fixture). Fixed.
5. **Module-level fn names were not declared up front** — recursive top-level fns
   and forward references compiled their self/name reads as never-filled free
   globals (runtime Unset). pypoc's `_declare_module_names` docstring says "`fn`
   definitions included" but its code does not do it; the port now does (dispatched
   `fn [T]` names correctly excluded).
6. **Empty literals arrive as bare symbols** (`[]`→`'array`, `()`→`'tuple`,
   `$[]`→`'list`, `{}`→`'dict`) and fell into name resolution. Handled as literals
   in `_bare_symbol`.
7. **`push_block`'s stage-2 fallback let the frame's own top-level body push steal
   the first nested scope's entry**, shifting every later push: block-scoped locals
   (`e`, `top`, `name` in statements.wy) resolved to nothing ("cannot assign").
   Fixed by (a) keeping the scope stack as integer indexes into `block_slots` (the
   appended bindings dicts read back empty under the tree-walking interpreter —
   engine anomaly, sidestepped) and (b) a `top_body` guard so the frame-body push
   answers false as its comment always claimed.
8. **`sym`/`int`/`float` builtins were missing from the C VM** (leaf builtins added;
   `WY_BUILTINS_LEAF_COUNT` 20→23). Every front-end module takes these for granted;
   `sym` was this session's `is bytes`-class gap. Note: `int(str)` uses C strtol
   base auto-detection, so a bare leading zero reads octal — a documented delta
   from Python's stricter `int(x, 0)`.
9. **Char literals were unreachable** (parser answered the raw token). `_mk_char`
   now lowers `\A`/`\newline` to `$['char, codepoint]` (CHAR_NAMES table mirrored
   from the interpreter).
10. **Self-inflicted, caught**: an instrumentation edit gutted `storage()`'s body to
    `return nil`, sending the bug hunt in circles ("cannot assign" everywhere) until
    the function was restored by diff. Lesson recorded: debug prints in the compiler
    must be removed by re-diffing, not by hand-reverting.

## M7 final status (2026-09-19)

Exit state, all green in `meson test` (8 tests; suite `compiler` is the slow pair):
- `corpus-sweep`: 19/20 fixtures compile through the port on the C VM and match `.out`;
  the one refusal is `decorators/decorated` (10a).
- `selfcompile`: **gen1 == gen2 byte-for-byte** over the 22 decorator-free self-sources,
  with an interim pypoc-built `parser.wyc` in every tree (recorded in the script header;
  removed by 10a M5). Gen 1 stubs are exactly the expected set: 2 `_dsl` templates
  (`this`) and 2 `parser` `'decorated` expressions, plus the 81 parser `@accept`
  decorators (none in the 22 modules compiled by the port).
- Stub reporting, `error_message` builtin, `same_node`/`_append_nodes` (analysis.wy) landed.

### What the gen2 bring-up found (all fixed, in fixing order)

Gen2 died with `native call failed: len` / `str` / a non-loading image. The causes were
seven independent bugs, none in the VM. Several are **parser bugs** hiding behind the
amalgam driver:

1. **Parser: a line-initial `(` after a block glued on as a call** (blocker 3 in the old
   report, which wrongly said "after `for`" only: it was any `if`/`while`/`for`). Fixed in
   `parser.wy`: `block_primary` (if/while/for) takes no postfix ops. Regression tests in
   `test_wy_parser.wy`. The `(this.x) ! append(..)` line-initial forms in `context.wy`,
   `statements.wy`, `functions.wy` now parse correctly and were left as written.
2. **Parser: `(x,)` lost its trailing comma** (parsed as `x`, not a 1-tuple), so the
   tokenizer's `(\",)` end-sequence args compiled as a bare char and every string literal
   failed in gen1. New `paren_expression`/`$_mk_paren`. Tests added.
3. **Parser: binary operators had NO precedence** (flat left fold: `8 + 12 * n` was
   `(8+12)*n`). Corrupted `image.wy`'s section directory offsets in gen1 output. Fixed with
   precedence climbing in `$_binary_expr` (tiers per wyrm.gram). Tests added. Worth a
   thought: the epic-8 parser tests never covered mixed operators.
4. **Amalgam name collisions** (gen0 only): `context.wy::_char_at` shadowed `decode.wy`'s
   (so gen0's `decode_str` never decoded escapes: every gen1 string literal was raw),
   `module.wy::_join` shadowed `image.wy`'s, `context.wy::_static_kind` shadowed image.wy's
   (`"float"` vs `"f64"`). Renamed the compiler-side ones (`_path_char`, `_join_path`,
   `_pool_kind`). Remaining same-name helpers are byte-identical copies by design.
5. **`compile_message` left receiver temporaries live** (`(frame.module.image) ! m(x)`):
   arguments landed one register high. pypoc dodges it because it parses `(a.b.c)` as a
   1-receiver message tuple; the port parses parens as grouping. Fixed by
   `free_to(window + 1)` after the receiver. **Same latent bug exists in pypoc's
   `compile_message`** for an unparenthesized `a.b.c ! m(x)` or a module-global receiver
   (module-level `m.image ! f("x")` miscompiles under pypoc too): not fixed there.
6. **`parser.wy` imported `cadr`/`n_set` only transitively** through `_dsl::*` (works under
   pypoc, unbound on the C VM). Added explicit `import std::pairs::*` and
   `import wyrm::ast::*`.
7. Self-compile harness: `SELFCOMPILE_KEEP=<dir>` keeps the work tree for debugging;
   `parser.wy` dropped from `SELF_SOURCES` with pypoc `parser.wyc` installed per generation.

### Still open (none block 10a)

- Decorator expansion and `@template` (10a M0-M5). `parser.wy` still needs pypoc.
- **C-VM `==` on aggregates is pointer identity** (old blocker 5; compiler routes around
  it via `same_node`). VM follow-up.
- pypoc `compile_message` temp-leak above (report upstream / fix in pypoc).
- Parser tests still don't exercise `postfix` ambiguity beyond the added cases.

## Suggested test cases to lock in / resolve the current state

Parser (wy-tests, `test/wy/`):
1. Statement wrapping: bare call/name/binop lines → `expr_stmt`; keyword primaries
   (`if a: b`, `return 5`, bare `break`/`continue`/`pass`) stay unwrapped (partly
   pinned in test_wy_parser.wy already).
2. Message forms: `x ! m(a)` → `[..., $['a], 'send]`; `x ! m()` → `[..., nil,
   'send]`; `x ! m` → `[..., nil, 'bound]` — pins the dispatch-vs-binding fix.
3. Comparisons fold to `'binop`: `a < b`, `a <=> b` (pins the shape fix).
4. Char literals: `\A` → `$['char, 65]`, `\newline` → `$['char, 10]`.
5. The `(`-after-block glue quirk (expected-fail until the parser is fixed):
   `for ...: <block>` followed by a line starting with `(` parses as two statements.

C VM suite (doctest):
6. `sym`/`int`/`float` leaf builtins (identity, str parse incl. `0x`, float
   truncation, error on garbage).
7. String `==` content semantics (the compiler and compiler_main rely on it;
   pin `"--mir" + "ror" == "--mirror"`).

Compiler (wy-tests, structural):
8. Block-scope locals regression: compile a fn whose `if`/`for` blocks declare
   locals read after assignment (the `_break`/`_continue`/`_set_if_unset` shapes);
   assert compile succeeds and the disasm contains no free-global `gget` for those
   names (pins the `top_body`/scope-stack fix).
   2026-09-19 addition: the two-sibling-`if` repro
   (`fn f(node): if node: return 1 / if node: out := 1; return out /
   return node` — needs an EARLIER sibling `if`) pins bug #11; assert no
   stubs / no free-global `gget` for the local. Regression driver for
   `same_node` itself: structurally equal trees built separately compare
   true (pins the `==`-is-identity finding without depending on it).
9. `fn $name` / `fn [TreeBase]` both register TreeBase-dispatched methods
   (reg_msg + `this` valid).
10. Recursive + forward-referencing top-level fns compile to global gget/call (pins
    the up-front fn-name declaration).

Harness:
11. Wire `run_corpus_sweep.py` into `meson test` (skip without pypoc/.venv, like
    run_wy_tests.py); assert 0 unexpected failures.
12. Wire `run_selfcompile.py` likewise once blocker 1 is resolved; assert the
    gen2==gen3 fixed point.

## Orientation for the next session (epic 10a)

- Read `doc-llm/history/wypoc-vm-port/epic_10a.md` and the "M7 final status" section above.
- Baseline to protect: `meson test` 8/8, corpus sweep 19/20, self-compile gen1 == gen2.
  `SELFCOMPILE_KEEP=/tmp/x python3 scripts/run_selfcompile.py` keeps the trees.
- Debugging recipe that worked: swap gen0/pypoc-built `.wyc` modules into a copy of the
  gen1 tree one at a time, and diff opcode sequences (`wyrm --disasm`, column 2) between the
  port's and pypoc's output for the same source.
- The "Suggested test cases" list above is partly done (parser cases 1-5 for glue/paren/
  precedence exist; harness items 11-12 are wired). Items 6-10 remain cheap adds.
