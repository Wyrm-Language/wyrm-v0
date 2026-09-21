# Epic 10 — Compiler port

> **Status and supersessions (2026-09-18; read before the rest of this file).** M1-M6 landed;
> M7 is in progress. Where this file disagrees with the list below, the list wins.
> 1. **Bar is functional, not byte-identical** (user decision): "compiles and runs correctly",
>    matching pypoc's tree-walking interpreter semantics where the pypoc bytecode VM differs.
>    Every "byte-identical to pypoc" in this file is downgraded to "shape-identical where cheap,
>    functionally equal always"; deltas are documented in the report.
> 2. **Decorators are epic 10a's job.** The ported compiler does not expand them. Do not
>    extend `_dsl.wy` or hack expansion into this epic (see `epic_10a.md`).
> 3. **No CLI yet.** The exit-criterion commands below (`wyrm ... --build-bc --emit wyc`) are
>    epic 11's CLI. Until then the port is driven by `wy/wyrm/tools/compiler_main.wy` (see its
>    header) via `scripts/run_corpus_sweep.py` and `scripts/run_selfcompile.py`.
> 4. **Output extension:** the wyrm-hosted compiler's images are `.wyd`, not `.wyc` (epic 11's
>    contract section). This epic's tools still write `.wyc`; epic 11 M1 renames them. Do not
>    rename now.
> 5. **M7 is re-cut** (see M7's note): 20-module fixed point with pypoc-built `parser.wyc` as an
>    interim; the true 21-module fixed point is epic 10a M5.

## Goal

Port `pypoc/wypoc/compiler_bc/{context,analysis,expressions,statements,functions,classes,
handlers,module,verify}.py` (9 files, ~4.7k lines of Python total) to wyrm itself, under a
new `wy/wyrm/compiler/` directory, replacing the 30-line stub at `wy/wyrm/compiler.wy`.
The Python compiler walks `wypoc.ast_nodes` dataclasses; the wyrm port walks the sexpr
pair-list shape `$['kind, field, ...]` that `wypoc/wypoc/sexpr.py`'s `ROWS` table defines
and that `wy/wyrm/parser.wy` (once epic 8 has cleaned up `ast.wy`) already produces. Each
Python module's dispatch-by-dataclass-type becomes dispatch-by-car-symbol in wyrm. Output
serialization (BSON, section layout, `.wyc`/`.wy_a`/`.c`) is already done: this epic calls
`wy/wyrm/bjson.wy` and `wy/wyrm/image.wy` from epic 9 rather than re-deriving them.

The running engine for every fixture in this epic is: pypoc compiles the wyrm compiler's
own `.wy` sources to bytecode, and this repo's `wyrm` binary (the C VM, epic 6) executes
that bytecode to compile a *target* `.wy` fixture, whose resulting `.wyc` is then
compared byte-for-byte against pypoc's own compilation of the same fixture. Pypoc's
tree-walking interpreter is available as a second engine strictly for debugging a
mismatch (stepping through the same source pypoc-interpreted vs. wyrm-VM-executed), never
as the thing under test.

**Exit criterion:**
```sh
./buildDir/src/wyrm/wyrm -Iwy /tmp/e10/wyrm_compiler.wyc \
  --build-bc --emit wyc -o /tmp/e10/corpus test/bytecode/*.wy pypoc/test/samples/**/*.wy
# the fixed point: gen1 compiles the compiler's own sources, gen2 (built from gen1's
# output) compiles them again
./buildDir/src/wyrm/wyrm -Iwy /tmp/e10/wyrm_compiler.wyc \
  --build-bc --emit wyc -o /tmp/e10/gen1 wy/wyrm/compiler/*.wy wy/wyrm/*.wy
./buildDir/src/wyrm/wyrm -Iwy /tmp/e10/gen1/module.wyc \
  --build-bc --emit wyc -o /tmp/e10/gen2 wy/wyrm/compiler/*.wy wy/wyrm/*.wy
diff -r /tmp/e10/gen1 /tmp/e10/gen2
```
expected: every corpus `.wy` compiles to a `.wyc` passing its `.out` check (epics 1-6's
conformance harness), and `diff -r` on the two generations is empty (the fixed point).

## Inputs

- `doc-llm/history/wypoc-vm-port/epic_9_report.md`: confirms `bjson.wy`/`image.wy`/`opcodes.wy` exist, their
  exact API (function names and argument order for section builders, `pack`, `to_wyc`),
  the section-id enum actually shipped, and whether `assemble_wya()` landed (needed here
  if the milestone policy's "fall back to a `.wy_a` semantic diff" is exercised).
- `doc-llm/history/wypoc-vm-port/epic_8_report.md` if it exists: the state of `wy/wyrm/ast.wy` (node kind table
  aligned with `sexpr.py`'s `ROWS`), `decode.wy` escape fixes, parser parity status. If
  epic 8 has not run, treat the front end as the state described in the plan's Epic 8
  section and re-derive the node-kind table straight from `sexpr.py` instead.

State-scan checklist:
1. Does `wy/wyrm/parser.wy` actually emit sexpr pair lists matching `sexpr.py`'s `ROWS`
   table today, or only a subset? Run the parser on `test/bytecode/hello.wy` and inspect
   the tree (via whatever the parser's own test harness prints) before assuming coverage.
2. Re-run `pypoc/.venv/bin/python tools/generate_opcode_header.py` (or its `_wy`
   counterpart from epic 9) to confirm both generated tables are current; a stale table
   here would make every fixture in this epic fail for a reason unrelated to the port.
3. Confirm `pypoc/wypoc/compiler_bc/*.py` module sizes are close to: `context.py` 591,
   `analysis.py` 226, `expressions.py` 1005, `statements.py` 566, `functions.py` 389,
   `classes.py` 227, `handlers.py` 124, `module.py` 452, `verify.py` 281 (`wc -l`). A
   large delta means the milestone split below may need re-cutting before work starts.
4. List `pypoc/test/bytecode/*.wy` and `pypoc/test/samples/**/*.wy` to confirm the
   fixture progression below (hello, arith, control_flow, closures, collections, errors,
   classes, messages, two_module, wildcard, coroutines, decorators) names real files.
5. Confirm pypoc's register allocation is deterministic run-to-run (compile the same
   fixture twice with `--build-bc` and `cmp` the two `.wyc` outputs) before relying on
   byte-identity as the acceptance bar throughout.
6. Confirm `wy/wyrm/_dsl.wy`'s decorator/template machinery (header comment, ~1-90) is
   sufficient for decorator expansion, or whether epic 10 needs to extend it; grep
   `doc/decorators.md` for what decorator expansion should produce at the sexpr level.
7. Check whether `include/wyrm/opcode.h` in this repo (adopted from pypoc per epic 1) is
   still byte-identical to pypoc's generated header; a divergence silently breaks every
   fixture.
8. Run `meson test -C buildDir` and record the pass count for the report's "before" line.

## Context to load

Read:
1. Module docstrings and top-level function/class lists only (not full bodies) for
   `context.py`, `analysis.py`, `expressions.py`, `statements.py`, `functions.py`,
   `classes.py`, `handlers.py`, `module.py`, `verify.py`, `errors.py`: each file's first
   ~20-30 lines plus `grep -n "^def \|^class "`. ~3k tokens total; this is the map, not
   the territory, milestone-specific deep reads happen per-milestone, see below.
2. `pypoc/doc/llm-bytecode.md` §7 "Lowering recipes" (~665-777) and §8.3 "Register
   allocation" (~819-845: named locals get fixed L slots in declaration order, temps are
   a stack above them, `nlocals` is the high-water mark) and §9 "POC vs spec" (~877-1005).
   Note: this doc's §4 (module image sections) is stale relative to `wyc-format.md` (it
   still lists a `relocations` section and lacks `messages`/`free`); read §7-9 for
   lowering prose only, trust `wyc-format.md` for format facts. ~4k tokens.
3. `pypoc/wypoc/sexpr.py` in full (1007 lines, mostly the `ROWS` table and a handful of
   irregular-kind encoders/decoders; skim the table, read the docstring and "Irregular
   kinds" closely). The exact shape every compiler-module port dispatches on. ~4k tokens.
4. `wy/wyrm/_dsl.wy` header comment (~1-90) if a milestone needs decorator expansion.
5. `wy/wyrm/compiler.wy` in full (30 lines), the stub being replaced; confirm nothing in
   it is worth preserving (scope-stack scaffolding, superseded by `context.wy`).

Grep-only, per milestone, never paste full listings: the function being ported in its
`.py` source, then just that function plus direct callees; `.wy_a` fixtures (`grep -n`
specific sections when diagnosing a mismatch, never dump a whole listing into context).

## Assumptions

- `wy/wyrm/parser.wy` produces sexpr trees matching `sexpr.py`'s `ROWS` for at least the
  constructs the fixture progression below exercises (arithmetic, control flow, closures,
  collections, errors, classes, messages, modules, coroutines). *(verify in scan)*
- Epic 9's `bjson.wy`/`image.wy`/`opcodes.wy` expose enough API surface (section builders
  for statics/symbols/functions/classes/messages/globals, `pack`, `to_wyc`) that this
  epic's port never needs to touch epic 9's files, only call them. *(verify in scan)*
- pypoc's register allocation (named locals in declaration order, temps as a stack above
  them, per `llm-bytecode.md` §8.3) and static-pool dedup are deterministic across runs,
  which is what makes byte-identical `.wyc` comparison a valid acceptance bar rather than
  a source of permanent flakiness. *(verify in scan)*
- Decorator expansion (needed for `classes.py`/`functions.py`'s ports, since wypoc
  decorators rewrite one sexpr into another before lowering) can reuse `_dsl.wy`'s
  existing template machinery rather than needing new infrastructure. *(verify in scan)*
  **Refuted by M7 (2026-09-18):** `_dsl.wy` supplies decorator *bodies*, but running them needs
  compile-time evaluation infrastructure; that is `epic_10a.md`.
- The C VM (epic 6) is complete enough to run arbitrarily deep bytecode-compiled wyrm
  programs, including whatever the compiler itself becomes once ported: pypoc-compiled
  compiler on the C VM compiling wyrm source is not meaningfully different from any other
  bytecode-compiled program the C VM already runs. *(verify in scan)*
- `getidx`/`setidx` index PAIR chains for captured-and-assigned variables (the boxing
  convention in the plan's "Key findings": `compiler_bc/functions.py:_prologue`,
  `analysis.py:cell_names`) rather than `wy_box`; the wyrm port must reproduce this exact
  one-element-pair-list boxing, or captured variables will diverge from pypoc's lowering.
  *(verify in scan)*

## Milestones

Ported in dependency order: `context`+`analysis` have no dependents among the compiler
modules and everything else depends on them; `expressions` depends on `context`;
`statements` depends on `expressions`; `functions` depends on `statements`;
`classes`/`handlers` depend on `functions`; `module`+`verify` depend on everything.
Fixture acceptance rides alongside: each milestone's **Acceptance** names the fixtures
that milestone unlocks, in the progression hello → arith/control_flow →
closures/collections/errors → classes/messages → two_module/wildcard → coroutines.

### M1 — `wy/wyrm/compiler/context.wy` + `wy/wyrm/compiler/analysis.wy`

**Scope.** Port `context.py` (`ModuleContext`: image ownership, global slots by name,
builtin namespace, pending function bodies; `FnContext`: named locals, temp stack, emit
buffer, labels, message-identity set) and `analysis.py` (`own_declared_names`,
`free_names`, `cell_names`, the `SCOPE_BOUNDARIES` walk that stops at `fn`/`co`/lambda/
class/`defer`). No lowering happens yet; this is state and the two static-analysis passes
lowering depends on.

**Files.** New: `wy/wyrm/compiler/context.wy`, `wy/wyrm/compiler/analysis.wy`.

**Acceptance.** Unit-level: a driver script runs `free_names`/`cell_names` against a
handful of hand-built sexpr trees (a closure over an assigned variable, one over a
read-only variable) and checks the same answer `analysis.py` gives today (spot-check via
pypoc; nothing downstream exists yet to compile a whole fixture).

**Model.** Opus, the "compiler architecture" milestone the plan's model-staging table
calls out by name: first-of-kind register-allocation and frame-state design, no
reference implementation in wyrm to lean on.

**Fan-out.** None; `context.wy` and `analysis.wy` are too interdependent to split, and
getting the frame-state shape right here is what every later milestone depends on.

### M2 — `wy/wyrm/compiler/expressions.wy`

**Scope.** Port `expressions.py`'s `compile_expr(node, fn, dst=None) -> register`
contract and its handlers: literals, names, binops (temp discipline per Appendix A:
operands pushed, result pushed above them), calls, message sends, `getidx`/`setidx`
(including the PAIR-chain boxing convention for captured-and-assigned variables).

**Files.** New: `wy/wyrm/compiler/expressions.wy`.

**Acceptance.** `hello` and `arith`/`control_flow` fixtures compile (via a minimal driver
that stubs enough of `module.wy`'s init-code shape inline to emit one function body and
diff code words against `.wy_a`, rather than a full `.wyc`, if `module.wy` isn't ported
yet). Byte-identical code words against pypoc's `--emit wya` output for the same fixture.

**Model.** Sonnet (the contract is fully written down: `compile_expr`'s signature and the
temp-discipline rule from Appendix A are unambiguous).

**Fan-out.** Possible once `context`/`analysis` exist: one Sonnet subagent per expression
family (literals+names, arithmetic+comparison, calls+message-sends, `getidx`/`setidx`),
against disjoint handlers in the same file. Only fan out if the families are genuinely
independent; keep them in one file regardless of how many agents write them.

### M3 — `wy/wyrm/compiler/statements.wy` + `wy/wyrm/compiler/handlers.wy`

**Scope.** Port `handlers.py`'s three dispatch registries (`EXPR_HANDLERS`,
`STATEMENT_HANDLERS`, `TOPLEVEL_HANDLERS`, keyed here by car-symbol instead of Python
class) and `statements.py`'s two-pass shape (`declare_names` walks a body first, then
handlers emit code with temps starting above named locals). Wire `expressions.wy`'s
`compile_expr` in as the statement handlers' expression-lowering call.

**Files.** New: `wy/wyrm/compiler/handlers.wy`, `wy/wyrm/compiler/statements.wy`.

**Acceptance.** `closures`, `collections`, `errors` fixtures compile and match pypoc
byte-for-byte (again via the minimal driver from M2, extended to handle full statement
bodies: `if`/`while`/`for`/`try`/`defer`).

**Model.** Sonnet.

**Fan-out.** One agent for `handlers.wy` (mechanical registry port) plus one for
`statements.wy`'s two-pass walk, run sequentially not in parallel (statements.wy needs
handlers.wy's registries to exist and be named correctly first).

### M4 — `wy/wyrm/compiler/functions.wy`

**Scope.** Port function compilation: parameter/capture layout, nested `fn`/lambda/
`defer` closures (compile the body as an ordinary function whose P frame carries captures
after params, emit a `closure` in the enclosing frame), table-index reservation before
body compilation (so self-reference and the publishing `closure` both have an index).

**Files.** New: `wy/wyrm/compiler/functions.wy`.

**Acceptance.** `closures` fixture (already partially covered by M3 for simple cases) now
covers nested/self-referential closures and `defer` blocks fully; byte-identical to pypoc.

**Model.** Sonnet.

**Fan-out.** None; capture layout and table-index reservation are tightly coupled.

### M5 — `wy/wyrm/compiler/classes.wy`

**Scope.** Port class compilation: slots with constant defaults, virtual-slot accessor
functions, `init`, message map (`class` opcode registers it), statics allotted to module
globals. Uses `wy/wyrm/_dsl.wy` if decorator expansion (e.g. a `@getter`/`@setter`-style
construct, or whatever epic 8 decided for the wyrm-side decorator story) needs it before
a class body reaches `classes.wy`.

**Files.** New: `wy/wyrm/compiler/classes.wy`.

**Acceptance.** `classes` and `messages` fixtures compile, byte-identical to pypoc.

**Model.** Sonnet.

**Fan-out.** None.

### M6 — `wy/wyrm/compiler/module.wy` + `wy/wyrm/compiler/verify.wy`

**Scope.** Port `module.py`'s top-level walk (init code: hoisted imports, then top-level
statements in source order, then a zero-value return; function bodies laid out after
init once sizes are known) and `verify.py`'s structural checks (jump targets, register
operands in range, call windows fit the frame, table indices in range). This is where
the compiler becomes end-to-end runnable for a whole module rather than the ad-hoc
per-milestone drivers used in M2-M5.

**Files.** New: `wy/wyrm/compiler/module.wy`, `wy/wyrm/compiler/verify.wy`. Edit:
`wy/wyrm/compiler.wy` replaced with a re-export or deleted in favor of
`module.wy`'s public entry point (decide which, note it in the report).

**Acceptance.** `two_module`, `wildcard`, `coroutines` fixtures compile end-to-end,
byte-identical to pypoc (or, per the milestone policy, a recorded `.wy_a` semantic diff if
justified). `verify.wy` rejects a deliberately corrupted hand-built tree the same way
`verify.py` does (one negative test per structural check category).

**Model.** Opus for `module.wy` (init-code ordering and function-layout sequencing is
load-bearing, similar in kind to M1); Sonnet suffices for `verify.wy` (a checklist of
structural predicates, fully specified by `verify.py`).

**Fan-out.** `module.wy` and `verify.wy` can proceed in parallel once M1-M5 exist.

### M7 — Corpus sweep and self-compile fixed point

**Scope.** Compile the entire `test/bytecode/*.wy` corpus and `pypoc/test/samples/**/
*.wy` with the wyrm compiler running on the C VM; every resulting `.wyc` must pass the
existing `.out` conformance checks (epics 1-6's harness). Then the fixed point: use the
wyrm compiler (on the C VM, itself compiled by pypoc) to compile its own sources
(`wy/wyrm/compiler/*.wy` plus `wy/wyrm/{bjson,image,opcodes,parser,tokenizer,ast,decode,
_dsl}.wy`); the images must be byte-identical to the first generation's compilation.

**Files.** No new compiler files; test/CI wiring only: a corpus-runner script (extend
whatever epic 1-6 built for `.out` checks), a self-compile driver, a `meson test` entry.

**Acceptance.** Full corpus green; fixed point holds (`diff -r` on the two generations'
image outputs is empty).

> **Re-cut 2026-09-18 (see `epic_10a.md`).** The ported compiler cannot expand decorators,
> and `parser.wy` has 86 `@accept` sites, so the full 21-module fixed point is not reachable
> in this epic. M7 closes with: corpus sweep 19/20 (the `decorators/decorated` refusal stays),
> and a fixed point over the **20 decorator-free modules only**, with `parser.wyc` built by
> pypoc as an explicitly interim, recorded provenance delta (`run_selfcompile.py` takes
> `parser.wyc` from pypoc and drops `parser.wy` from `SELF_SOURCES`). The true 21-module fixed
> point and the removal of that arrangement move to epic 10a M5. Real decorator expansion is
> an eval primitive in the C API plus a Lisp-style expansion pass (`epic_10a.md`), not an
> extension of `_dsl.wy`. Landed while investigating (commits `44508a3`, `bd6039f`): failing
> natives are named in faults, `str(error)` works, imported modules get `__name__`.
> **M7 rescoped 2026-09-18 (final; supersedes the "remaining items" list that stood here).**
> M7 closes this epic; it does NOT chase decorators or templates. Remaining work, in order:
> 1. **Stub reporting.** Make gen 1 print every stubbed function with its recorded reason
>    (`compiler_main.wy`, with a small hook in `compile_module`). Classify each stub:
>    *expected-deferred* = a `_dsl.wy` template (`$`-prefixed `fn`s plus `_tmpl_bool`/
>    `_tmpl_opt`; they use `this` and are quoted, never called) or a `'decorated` node in
>    `parser.wy` awaiting expansion; *unexpected* = anything else, which is a real bug to fix
>    here. Keep the list of expected names in the script so a new stub fails the run.
> 2. **20-module fixed point.** Drop `wyrm/parser.wy` from `SELF_SOURCES` and put a pypoc-built
>    `parser.wyc` (built in a scratch copy, never in `wy/`) into each generation's tree.
>    Interim provenance delta, recorded loudly in the script header and report; removed by
>    10a M5. Goal: gen2 == gen3 byte-identical (gen1 == gen2 only recorded if free).
> 3. **Do NOT add `@template` markers** to `_dsl.wy` or anything else. pypoc accepts
>    `@template` now (pass-through, pypoc `eae6cc8`), but the port cannot yet handle it; the
>    markers, the `'template` node and strict lowering are **10a M0**.
> 4. **Do not change stub semantics.** `compiler_main` keeps `stub_unlowered=true`; making
>    lowering strict is 10a M0.
> 5. Cheap regression tests from the report (the 9-line repro of the old `str(error)` fault in
>    `test/wy/`; a doctest for `str(error)`), then wire `run_corpus_sweep.py` and
>    `run_selfcompile.py` into `meson test` (skip without `pypoc/.venv`) or explicitly defer
>    the wiring to 10a M5, recorded in the report.
> 6. Final `epic_10_report.md`, commit, `meson test` green.

**Model.** Sonnet for the runner/driver scripts; escalate to Opus only if the fixed point
fails to converge and the cause is a genuine architectural gap, not a fixable bug.

**Fan-out.** One Sonnet subagent per failing fixture once a first full run identifies
failures, each fixing its own gap in the relevant `compiler/*.wy` file, provided two
agents never touch the same file at once.

## Out of scope / deferred

- Anything `llm-bytecode.md` §9 already documents as a POC-vs-spec gap; this epic ports
  what pypoc does, not what the spec eventually wants.
- New language features not already lowered by pypoc's compiler.
- Performance of the ported compiler; correctness and byte-identity are the bar.
- Emitting a `debug` section faithfully (epic 9 deferred this too). Adding it is welcome
  if `module.wy` makes it cheap (source positions are available here, unlike epic 9's
  hand-assembly), but not required; compare with `--strip` if not attempted.

## Risks

- **Byte-identity is unreachable for a legitimate reason** (e.g. pypoc's dedup turns out
  less deterministic than assumed). Mitigation: the milestone policy already provides the
  fallback (`.wy_a` semantic diff, recorded); use it sparingly and document exactly which
  fixture and why, so epic 11 is not surprised by a silently-lowered bar.
- **`sexpr.py`'s `ROWS` table and the actual parser output have drifted** (epic 8 may be
  incomplete or skipped). Mitigation: state-scan item 1 catches this before milestones
  start; a lagging parser shrinks the affected milestone's scope, noted for a follow-up,
  not worked around with hand-built trees that bypass the parser.
- **The `wy_box`-vs-PAIR-chain boxing convention is easy to get backwards**, the one place
  this port's runtime semantics diverge from what a native wyrm programmer would expect.
  Mitigation: M1's acceptance spot-checks `cell_names`/capture lowering against pypoc
  before any later milestone builds on it.
- **Nine large files is a lot of surface for silent scope creep per milestone.**
  Mitigation: each milestone's Files list is closed; needing a file outside it is a
  signal to re-cut the milestone boundary, not to expand it in place.
- **Self-compile fixed point never converges** because of an accidental non-determinism
  (e.g. a wyrm-side iteration order that differs from Python's). Mitigation: M7 isolates
  this as its own acceptance step, separate from the corpus sweep, so a convergence
  failure does not block landing a working, if not yet self-hosting, compiler; escalate
  to Opus per M7's model note.

## Report

Write `doc-llm/history/wypoc-vm-port/epic_10_report.md` per `doc-llm/history/wypoc-vm-port/README.md`'s template. Beyond the standard
sections, record explicitly:
- Per fixture: byte-identical, or `.wy_a`-semantic-diff-with-reason, or REFUSED/DIVERGES
  (reuse the pypoc conformance vocabulary from `test_vm_samples.py`).
- Whether the self-compile fixed point converged on the first attempt, and if not, what
  the non-determinism turned out to be.
- The final disposition of `wy/wyrm/compiler.wy` (deleted, or kept as a re-export shim).
- Any decorator-expansion gaps found in `_dsl.wy` that `classes.wy`/`functions.wy` had to
  route around.
