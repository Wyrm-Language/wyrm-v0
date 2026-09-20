# Epic 6 — Host API, hardening, performance

Implements design_c_vm.md M8 + the performance and embedding-API work the README's phase
map assigns to this epic. Prerequisite: epic 5 (M6 + M7) landed and its report exists, with
a full corpus sweep on record. This is the last epic of Phase A: at its end the toolchain
needs both `pypoc/` (to compile) and this repo (to run), with a real embedding API and
measured performance.

## Goal

Turn a functionally-complete-but-single-compiler-tested VM into something embeddable and
trustworthy: the C-to-bytecode host call path (`wy_vm_call_continue`), three-compiler CI
(gcc/clang/tcc), GC-stress testing as a standing CI mode rather than a one-off, a per-site
message inline cache and per-class slot dict for real performance, a `bench/` suite whose
headline case is running the corelib tokenizer (compiled by pypoc) on this VM, and a public
embedding API with docs and C++ wrappers.

**Exit criterion:** `meson test -C buildDir` green under gcc, clang, and tcc; a benchmark
run (`bench/run.sh` or equivalent) completes and its table is in the report;
`pypoc/.venv/bin/wyrm --build-bc pypoc/wypoc/corelib/wyrm/tokenizer.wy -o
/tmp/tokenizer.wyc` followed by
`./buildDir/src/wyrm/wyrm -I pypoc/wypoc/corelib /tmp/tokenizer.wyc <parser.wy` produces
output identical to the tree walker running the same tokenizer over
`pypoc/wypoc/corelib/wyrm/parser.wy` as input text.

## Inputs

- `vm_plan/epic_5_report.md` — read first, especially the final corpus counts and the
  import-hook signature it records (this epic's embedding API wraps that hook).
- State-scan checklist:
  1. Confirm `meson test -C buildDir` passes on the default compiler and record the count.
  2. Confirm which of gcc/clang/tcc are available in the dev environment
     (`which gcc clang tcc`) — tcc in particular may need installing; note in the report if
     a compiler is unavailable and CI-only.
  3. Confirm `WY_TEST_GC_THRESHOLD=0` (or however epic 3/5 wired GC-stress mode) is an
     environment variable or meson option today, not just an ad hoc test flag — grep
     `gc_threshold` usage across `src/test/`.
  4. Confirm `wy_vm_call_sync` (host/tests only, per design §1.4) exists and where; this
     epic adds `wy_vm_call_continue` (from natives) alongside it, not instead of it.
  5. Confirm the message-dispatch code path from epic 4 (`src/dispatch.c`) has a stable
     "code offset" or call-site identifier available to key an inline cache on — design
     §5/§10 notes `msg` "has no spare operand, but a cache can be a side table keyed by
     code offset" — check epic 5's report for whether it flagged a gap here.
  6. Confirm `wy_class` (epic 4) has room for a per-class slot dict or whether adding one
     changes the struct's memory layout in a way other code depends on — read
     `include/wyrm/class.h`.
  7. Confirm `pypoc/wypoc/corelib/wyrm/tokenizer.wy` and `parser.wy` exist and roughly how
     large they are (`wc -l`) — this is the epic's headline benchmark input.
  8. Confirm `pypoc/.venv` exists (epic 1's toolchain setup) and `--build-bc` works today
     against `tokenizer.wy` without error under pypoc alone (sanity check before blaming
     the C VM for any divergence).
  9. Confirm whether `doc/embedding.md` or `include/wyrmxx/` already exist in any form.
  10. Confirm the corpus manifest from epic 5 is unchanged (no drift) before starting —
      `test/bytecode/manifest.txt` diff against epic 5's recorded counts.

## Context to load

1. `vm_plan/design_c_vm.md` §1.4 (C → bytecode), §8 (GC) — read in full, ~2k tokens.
2. `vm_plan/epic_5_report.md` — read, ~1-2k tokens.
3. `pypoc/doc/wyc-format.md` §10 "Message dispatch caching" (the one paragraph on inline
   caching) — read, already excerpted in prior scans, ~0.3k tokens.
4. Current C: `include/wyrm/vm.h`, `src/vm_call.c` (or wherever epic 3's call-binding
   milestone put it), `src/dispatch.c` (epic 4), `include/wyrm/class.h` (epic 4),
   `src/gc.c`, `include/wyrm/context.h` — read only what each milestone's scope touches,
   budget ~4k tokens total, spread across milestones rather than all at session start.
5. `pypoc/wypoc/corelib/wyrm/tokenizer.wy`, `pypoc/wypoc/corelib/wyrm/parser.wy` — do
   **not** read in full (large); grep for top-level structure (`fn `, `class `) to gauge
   what language features the benchmark exercises, ~0.5k tokens.
6. AGENTS.md — reread the MISRA C 2023 / no-recursion / `wy_allocator`-only rules before
   the hardening milestone, ~0.5k tokens (already loaded once per session is enough).
7. Existing `meson.build` (root and `src/`) — read to see how compiler selection and test
   running are currently wired, before adding a 3-compiler CI matrix, ~1k tokens.

## Assumptions

- The inline cache is a **side table keyed by code offset** (one cache-line-sized entry:
  last-seen receiver class pointer + resolved overload pointer), not a field added to the
  `msg` instruction's encoding, since wyc-format is frozen and `msg` has no spare operand
  (design §10) *(verify in scan: confirm epic 4 didn't already sneak in a different
  mechanism)*.
- The per-class slot dict is additive to `wy_class`'s existing array-scan `getattr`/
  `setattr` path from epic 4 — small classes keep the linear scan (it's already fast for
  ≤16 msg_map entries and a handful of slots), and the dict only kicks in above a slot-count
  threshold this milestone picks and records *(verify in scan: check epic 4's actual slot
  count typical in the corpus before deciding whether a dict is even worth it for anything
  but pathological cases; if the corpus has no class with more than ~10 slots, downgrade
  this milestone's scope and say so in the report)*.
- tcc support means **compiles and passes tests**, not necessarily with the same
  optimisation or warning strictness as gcc/clang — AGENTS.md already says "avoid compiler
  extensions; TCC, Clang, and GCC all supported," so this epic is closing a gap that should
  already mostly hold if earlier epics followed that rule *(verify in scan: try building
  with tcc *before* writing any tcc-specific milestone scope, to see how much is actually
  broken)*.
- The corelib tokenizer benchmark needs `bytes` for nothing (it is a text-only tokenizer
  over `.wy` source, string in, tokens out) — epic 7's `bytes` type is not a dependency for
  this epic's headline benchmark *(verify in scan by grep for `bytes`/binary literals in
  `tokenizer.wy`)*.
- `wy_vm_call_continue`'s contract (design §1.4: `(ctx, continuation, callee, args, argc,
  nres) -> wy_error`, called from natives, unlike `wy_vm_call_sync` which is host/test-only)
  is what an **exec native** uses internally to call back into the VM without itself being
  recursive C — this epic's bridge tests are the first real exercise of that path since
  design §1.3 introduced it as a placeholder concept *(verify in scan whether any epic 3-5
  exec native already needed this and built an ad hoc version — `std::io` and coroutine
  `next`/`send` are the likely candidates)*.

## Milestones

### M1 — `wy_vm_call_continue` and exec-bridge hardening

**Scope**
- `wy_vm_call_continue(ctx, continuation, callee, args, argc, nres)`: save `ip`,
  `phase = AWAIT_NATIVE`, reserve `nres` slots at stack top, push args, set
  `pending = continuation`, return `WY_EXEC_CONTINUE` — the trampoline's `wy_vm_run`
  prologue then copies reserved slots into the window and pops them (design §1.3's
  reservation-bridge description, generalised into a public entry point).
- Audit every exec native from epics 3-5 (`std::io`, coroutine `next`/`send`, any others)
  and rewrite ad hoc call-back mechanisms onto this one path, so there's exactly one way a
  native re-enters the VM.
- Bridge tests: a native that calls back into a bytecode closure that itself traps, and
  observe `WY_ERR_FAULT` propagates correctly through the bridge without leaking the
  reserved slots or leaving `ctx->current_fiber` in an inconsistent state.

**Files**
- Changed: `include/wyrm/vm.h`, `src/vm_call.c` (or wherever), every exec native site
  touched by the audit (list them explicitly in the report).

**Acceptance**
New doctest `src/test/test_vm_bridge.cpp`: a synthetic exec native calls back into
bytecode via `wy_vm_call_continue`, asserts results land correctly and asserts a trap
inside the called-back bytecode surfaces as `WY_ERR_FAULT` to the *original* native's
continuation, not silently swallowed.

**Model:** Sonnet (the mechanism is fully specified by design §1.3/§1.4; the work is
auditing and unifying existing call sites, not inventing new control flow).

**Fan-out:** none (touches shared call-path code; parallel edits would conflict).

### M2 — Three-compiler CI and GC-stress-as-standing-mode

**Scope**
- `meson.build` changes (or a CI script) to build and test under gcc, clang, and tcc, all
  three green.
- Fix whatever tcc reveals (likely: a compiler extension slipped in somewhere, or an
  alignment/pragma assumption) — scope this narrowly to what the scan's tcc trial run
  actually surfaces, not a speculative rewrite.
- GC-stress (`gc_threshold = 0`, every instruction collects) becomes a standing CI mode: a
  second full `meson test` run with the env var/option set, not just the per-milestone
  hand-packed tests earlier epics added.
- A MISRA-C-2023 pass: run whatever static-analysis tooling AGENTS.md implies (or is
  already configured) over the milestones from epics 3-6 combined, fix flagged violations
  that are cheap, and record the rest as known-accepted deviations with reasons (AGENTS.md
  already allows "use internal abstractions where MISRA violations are unavoidable").

**Files**
- Changed: `meson.build`, `src/meson.build`, `test/meson.build`, plus whatever files the
  tcc/MISRA passes touch (list in the report — do not predict here).

**Acceptance**
`CC=gcc meson setup buildDir-gcc && meson test -C buildDir-gcc`,
`CC=clang meson setup buildDir-clang && meson test -C buildDir-clang`,
`CC=tcc meson setup buildDir-tcc && meson test -C buildDir-tcc` all pass.
`WY_TEST_GC_THRESHOLD=0 meson test -C buildDir` (or whatever the actual mechanism is named
by the time this runs) passes as a full-suite run, not per-fixture.

**Model:** Sonnet (mechanical build-matrix and lint work; escalate to Opus only if tcc
reveals a genuine semantic bug in the VM rather than a portability nit — note any such
escalation in the report).

**Fan-out:** 2 Sonnet subagents once the tcc trial's failure list is known — (a) tcc
portability fixes, (b) MISRA pass — disjoint if the tcc failures and MISRA findings don't
overlap; if they do (likely, since both flag the same non-portable construct), run
sequentially instead and say so.

### M3 — Per-site message inline cache, per-class slot dict

**Scope**
- Per-site inline cache for `msg`/`msg_va`: a side table keyed by code offset (the
  instruction's own position in `module->code`, stable per call site), storing the
  last-seen receiver class and the overload it resolved to; a cache hit skips the full
  `wy_dispatch_resolve_f` ranking walk. Cache invalidation: none needed for a *correctness*
  guarantee if classes are immutable after realisation (confirm this is true per epic 4's
  report — if `reg_msg` can add overloads to an already-cached message at runtime, the
  cache must check overload-count-at-cache-time too, or invalidate on `reg_msg`).
- Per-class slot dict: only for classes above the slot-count threshold M3 picks (see
  Assumptions) — a small hash map from symbol to slot index, built once at class
  realisation, consulted by `getattr`/`setattr` before falling back to the array scan.

**Files**
- Changed: `src/dispatch.c` (inline cache), `src/wclass.c`/`include/wyrm/class.h`
  (per-class slot dict), `src/vm.c` (`msg`/`msg_va` cases wire the cache lookup).

**Acceptance**
A benchmark case (method-call loop, from M5's `bench/` suite — build this milestone's
micro-benchmark first if `bench/` doesn't exist yet, then let M5 formalise it) shows a
measured speedup with the cache enabled vs. a build-time flag that disables it, numbers in
the report. No corpus regression: full `meson test -C buildDir` still green with the cache
on.

**Model:** Opus — design-heavy per the README's staging table (cache invalidation
correctness under `reg_msg`'s late-binding overloads is the kind of subtle bug that only
shows up as a flaky wrong-dispatch result under specific ordering, and there's no reference
implementation of *this* cache anywhere to check against).

**Fan-out:** none (cache correctness must be reasoned about as one unit with the dispatch
code it accelerates).

### M4 — Public embedding API and C++ wrappers

**Scope**
- `doc/embedding.md`: the public surface a host embeds against — `wy_context_init_s`,
  module loading (`wy_module_load_bytes`), `wy_vm_call_sync`, the import hook typedef, the
  output hook (`ctx->io.write`), GC root push/pop for host-held values across allocations,
  error handling (`wy_error`, `fiber->fault`). Written for a host developer who has not
  read design_c_vm.md.
- `include/wyrmxx/`: C++23 wrappers per AGENTS.md's C++ conventions — RAII context/module
  handles, a `std::expected`-shaped (or equivalent) call result, MISRA C++ 2023 compliant.

**Files**
- New: `doc/embedding.md`, `include/wyrmxx/context.hpp`, `include/wyrmxx/value.hpp` (or
  whatever split the executor's scan finds natural given the C API's actual final shape).

**Acceptance**
A new doctest under the C++ bindings (`src/test/test_embedding_cpp.cpp`) round-trips: embed
a context via the wrapper, load `hello.wyc`, run it, read the captured output, matching
`hello.out`, entirely through `wyrmxx` types with no raw `wy_*` calls in the test body.

**Model:** Sonnet (the C API surface is fixed by the time this milestone starts; this is
documentation and a wrapper layer over an already-decided contract).

**Fan-out:** 2 Sonnet subagents — (a) `doc/embedding.md`, (b) `include/wyrmxx/*` + its
test — independent once the C API is frozen (confirm M1-M3 are done first, or at minimum
that their public signatures won't change further).

### M5 — Benchmark suite and the corelib tokenizer milestone

**Scope**
- `bench/` directory: `fib` (recursive call overhead), `method-call-loop` (dispatch cost,
  reused from M3's cache validation), `dict-churn` (insert/lookup/delete cycling, exercises
  epic 3's dict rework), and the headline case — `pypoc/wypoc/corelib/wyrm/tokenizer.wy`
  compiled by pypoc and run on the C VM tokenizing `pypoc/wypoc/corelib/wyrm/parser.wy` as
  input text, output compared byte-for-byte against the same tokenizer running under the
  Python tree walker.
- A benchmark runner (`bench/run.sh` or `bench/run.py`) producing a table: case name,
  C-VM time, a reference point (tree-walker time for the same case, where meaningful —
  `fib`/`dict-churn` have no tree-walker equivalent worth timing since they're VM-internal
  microbenchmarks, but the tokenizer case does).

**Files**
- New: `bench/fib.wy`, `bench/method_call_loop.wy`, `bench/dict_churn.wy`, `bench/run.sh`
  (or `.py`), `bench/README.md` explaining how to add a case.

**Acceptance**
`bench/run.sh` completes and prints a table; the tokenizer case's C-VM output is
byte-identical to the tree-walker's output for the same input (this is the epic's overall
exit criterion, restated here as this milestone's specific acceptance).

**Model:** Sonnet (the benchmark harness is mechanical; the tokenizer parity check is a
diff, not a design decision — escalate only if a genuine semantic gap surfaces in the
tokenizer run, which would mean an earlier epic's milestone has a real bug, not this one's).

**Fan-out:** none for the harness itself (small and sequential to depend on M1-M3 being
stable); the four `bench/*.wy` case files could be split across subagents but the total
work is small enough that it's not worth the coordination overhead — note in the report if
the executor disagrees and splits anyway.

### Benchmark table skeleton

The report must fill in a table of this shape (values are placeholders; real numbers come
from `bench/run.sh`'s output):

| Case | Metric | Baseline (this VM, cache/dict off) | With M3 optimisations | Tree-walker (where applicable) |
|---|---|---|---|---|
| `fib(28)` recursive calls | wall time | — | — | n/a |
| method-call-loop (1e6 dispatches) | wall time | — | — | n/a |
| dict-churn (1e5 insert/lookup/delete) | wall time | — | — | n/a |
| corelib tokenizer over `parser.wy` | wall time, output diff | — | — | tree-walker wall time |

## Out of scope / deferred

- `bytes` type and anything binary-I/O shaped in `std::io` — epic 7.
- Further GC algorithm work beyond making stress mode a standing CI check (e.g. generational
  collection, concurrent collection) — no design section calls for it in Phase A.
- Self-hosted compiler work of any kind — Phase C (epics 8-11).
- A dispatch cache invalidation scheme more sophisticated than "check overload count" if the
  scan finds `reg_msg` can't actually run after a class's first dispatch in practice —
  narrow the milestone rather than build for a case that can't occur, and say so in the
  report.

## Risks

- **tcc's scope is unknown until tried.** Mitigate by running the trial build *before*
  committing to a milestone scope (state-scan checklist item 2), and keeping M2's tcc fixes
  narrowly targeted to what actually fails rather than a speculative portability rewrite.
- **Inline cache correctness under late `reg_msg` binding** is the epic's sharpest
  correctness risk — a stale cache entry silently calling the wrong overload is worse than
  no cache. Mitigate with a dedicated hand-packed test: cache a dispatch, then `reg_msg` a
  more specific overload for the same message and receiver class, then dispatch again and
  assert the new overload is chosen, not the cached stale one.
- **The tokenizer benchmark depends on every prior epic being correct**, so a failure here
  is diagnostically expensive (could be a bug from any of epics 2-5). Mitigate by running
  the *existing* corpus sweep (epic 5's M6) once more at the start of this epic, clean,
  before attributing any tokenizer-run failure to something new.
- **C++ wrapper API locks in the C API's shape.** Mitigate by writing M4 last among the
  four "mechanical" milestones (after M1-M3's signatures are final) rather than in
  parallel with them.

## Report

`epic_6_report.md` must additionally record:
- The completed benchmark table (skeleton above, filled in) with the machine/compiler it
  was measured on, since these numbers are not portable across hardware.
- Which compilers passed cleanly, which needed fixes, and a short list of what MISRA
  findings were fixed vs. accepted-as-deviation with reasons.
- The final inline-cache invalidation rule actually implemented, and the hand-packed test
  name that proves it's correct under `reg_msg`.
- The slot-count threshold chosen for the per-class dict, and whether the corpus actually
  has any class that crosses it (if not, say so plainly rather than implying the feature is
  exercised when it isn't).
- Confirmation the tokenizer benchmark's output matched byte-for-byte, or the exact
  divergence found and which epic's milestone it traces back to.
- This is the last epic of Phase A — the report should end with an explicit "Phase A
  status" paragraph: is the toolchain (both `pypoc/` to compile, this repo to run) actually
  usable end to end for an arbitrary `.wy` file today, and what is the honest list of
  remaining gaps before Phase B (epic 7, `bytes`) can start.
