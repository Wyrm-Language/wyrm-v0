# Epic 6 — Host API, hardening, performance

Implements design_c_vm.md M8 + the embedding-API and hardening work the README's phase map
assigns to this epic. Prerequisite: epic 5 (M6 + M7) landed and its report exists, with a
full corpus sweep on record. This is the last epic of Phase A: at its end the toolchain
needs both `pypoc/` (to compile) and this repo (to run), with a real embedding API and
measured baseline performance.

The per-site message inline cache and per-class slot dict originally scoped here as M3 have
been moved to epic 12, run after the self-hosted compiler lands (Phase C). Rationale: the
dispatch and class code this optimisation sits on top of is still expected to move under
epics 7-11 (bytes, front-end cleanup, the compiler port itself), and a correctness-sensitive
cache is exactly the kind of work that shouldn't be built twice. This epic's own benchmark
suite (M4, formerly M5) still measures uncached baseline performance so epic 12 has a
before/after to report against.

**Exit criterion:** `meson test -C buildDir` green under gcc and clang; a benchmark run
(`bench/run.sh` or equivalent) completes and its table is in the report;
`pypoc/.venv/bin/wyrm --build-bc pypoc/wypoc/corelib/wyrm/tokenizer.wy -o
/tmp/tokenizer.wyc` followed by
`./buildDir/src/wyrm/wyrm -I pypoc/wypoc/corelib /tmp/tokenizer.wyc <parser.wy` produces
output identical to the tree walker running the same tokenizer over
`pypoc/wypoc/corelib/wyrm/parser.wy` as input text.

tcc is out of scope for this repo's CI for now (gcc/clang cover the target platforms); if a
future need for tcc/freestanding portability shows up, re-add it as its own milestone rather
than reviving this note's original three-compiler scope wholesale, since AGENTS.md's
"avoid compiler extensions" rule may or may not still hold by then.

## Inputs

- `doc-llm/history/wypoc-vm-port/epic_5_report.md` — read first, especially the final corpus counts and the
  import-hook signature it records (this epic's embedding API wraps that hook).
- State-scan checklist:
  1. Confirm `meson test -C buildDir` passes on the default compiler and record the count.
  2. Confirm gcc and clang are both available in the dev environment (`which gcc clang`);
     note in the report if either is CI-only in this environment.
  3. Confirm `WY_TEST_GC_THRESHOLD=0` (or however epic 3/5 wired GC-stress mode) is an
     environment variable or meson option today, not just an ad hoc test flag — grep
     `gc_threshold` usage across `src/test/`.
  4. Confirm `wy_vm_call_sync` (host/tests only, per design §1.4) exists and where; this
     epic adds `wy_vm_call_continue` (from natives) alongside it, not instead of it.
  5. Confirm `pypoc/wypoc/corelib/wyrm/tokenizer.wy` and `parser.wy` exist and roughly how
     large they are (`wc -l`) — this is the epic's headline benchmark input.
  6. Confirm `pypoc/.venv` exists (epic 1's toolchain setup) and `--build-bc` works today
     against `tokenizer.wy` without error under pypoc alone (sanity check before blaming
     the C VM for any divergence).
  7. Confirm whether `doc-llm/embedding.md` or `include/wyrmxx/` already exist in any form.
  8. Confirm the corpus manifest from epic 5 is unchanged (no drift) before starting —
     `test/bytecode/manifest.txt` diff against epic 5's recorded counts.

## Context to load

1. `doc-llm/history/wypoc-vm-port/design_c_vm.md` §1.4 (C → bytecode), §8 (GC) — read in full, ~2k tokens.
2. `doc-llm/history/wypoc-vm-port/epic_5_report.md` — read, ~1-2k tokens.
3. Current C: `include/wyrm/vm.h`, `src/vm_call.c` (or wherever epic 3's call-binding
   milestone put it), `src/gc.c`, `include/wyrm/context.h` — read only what each
   milestone's scope touches, budget ~3k tokens total, spread across milestones rather than
   all at session start.
4. `pypoc/wypoc/corelib/wyrm/tokenizer.wy`, `pypoc/wypoc/corelib/wyrm/parser.wy` — do
   **not** read in full (large); grep for top-level structure (`fn `, `class `) to gauge
   what language features the benchmark exercises, ~0.5k tokens.
5. AGENTS.md — reread the MISRA C 2023 / no-recursion / `wy_allocator`-only rules before
   the hardening milestone, ~0.5k tokens (already loaded once per session is enough).
6. Existing `meson.build` (root and `src/`) — read to see how compiler selection and test
   running are currently wired, before adding a two-compiler CI matrix, ~1k tokens.

## Assumptions

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

### M2 — Two-compiler CI and GC-stress-as-standing-mode

**Scope**
- `meson.build` changes (or a CI script) to build and test under gcc and clang, both green.
  tcc is explicitly out of scope for this epic (see the note at the top of this file).
- GC-stress (`gc_threshold = 0`, every instruction collects) becomes a standing CI mode: a
  second full `meson test` run with the env var/option set, not just the per-milestone
  hand-packed tests earlier epics added.
- A MISRA-C-2023 pass: run whatever static-analysis tooling AGENTS.md implies (or is
  already configured) over the milestones from epics 3-6 combined, fix flagged violations
  that are cheap, and record the rest as known-accepted deviations with reasons (AGENTS.md
  already allows "use internal abstractions where MISRA violations are unavoidable").

**Files**
- Changed: `meson.build`, `src/meson.build`, `test/meson.build`, plus whatever files the
  MISRA pass touches (list in the report — do not predict here).

**Acceptance**
`CC=gcc meson setup buildDir-gcc && meson test -C buildDir-gcc`,
`CC=clang meson setup buildDir-clang && meson test -C buildDir-clang` both pass.
`WY_TEST_GC_THRESHOLD=0 meson test -C buildDir` (or whatever the actual mechanism is named
by the time this runs) passes as a full-suite run, not per-fixture.

**Model:** Sonnet (mechanical build-matrix and lint work).

**Fan-out:** none — small enough as a two-compiler matrix plus one MISRA pass that splitting
would cost more in coordination than it saves.

### M3 — Public embedding API and C++ wrappers

**Scope**
- `doc-llm/embedding.md`: the public surface a host embeds against — `wy_context_init_s`,
  module loading (`wy_module_load_bytes`), `wy_vm_call_sync`, the import hook typedef, the
  output hook (`ctx->io.write`), GC root push/pop for host-held values across allocations,
  error handling (`wy_error`, `fiber->fault`). Written for a host developer who has not
  read design_c_vm.md.
- `include/wyrmxx/`: C++23 wrappers per AGENTS.md's C++ conventions — RAII context/module
  handles, a `std::expected`-shaped (or equivalent) call result, MISRA C++ 2023 compliant.

**Files**
- New: `doc-llm/embedding.md`, `include/wyrmxx/context.hpp`, `include/wyrmxx/value.hpp` (or
  whatever split the executor's scan finds natural given the C API's actual final shape).

**Acceptance**
A new doctest under the C++ bindings (`src/test/test_embedding_cpp.cpp`) round-trips: embed
a context via the wrapper, load `hello.wyc`, run it, read the captured output, matching
`hello.out`, entirely through `wyrmxx` types with no raw `wy_*` calls in the test body.

**Model:** Sonnet (the C API surface is fixed by the time this milestone starts; this is
documentation and a wrapper layer over an already-decided contract).

**Fan-out:** 2 Sonnet subagents — (a) `doc-llm/embedding.md`, (b) `include/wyrmxx/*` + its
test — independent once the C API is frozen (confirm M1-M2 are done first, or at minimum
that their public signatures won't change further).

### M4 — Benchmark suite and the corelib tokenizer milestone

**Scope**
- `bench/` directory: `fib` (recursive call overhead), `method-call-loop` (dispatch cost —
  this is the uncached baseline epic 12 will compare its inline cache against), `dict-churn`
  (insert/lookup/delete cycling, exercises epic 3's dict rework), and the headline case —
  `pypoc/wypoc/corelib/wyrm/tokenizer.wy` compiled by pypoc and run on the C VM tokenizing
  `pypoc/wypoc/corelib/wyrm/parser.wy` as input text, output compared byte-for-byte against
  the same tokenizer running under the Python tree walker.
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

**Fan-out:** none for the harness itself (small and sequential to depend on M1-M2 being
stable); the four `bench/*.wy` case files could be split across subagents but the total
work is small enough that it's not worth the coordination overhead — note in the report if
the executor disagrees and splits anyway.

### Benchmark table skeleton

The report must fill in a table of this shape (values are placeholders; real numbers come
from `bench/run.sh`'s output). There is no "with cache/dict" column yet — that comparison is
epic 12's job, run against this table's baseline numbers.

| Case | Metric | Baseline (this VM) | Tree-walker (where applicable) |
|---|---|---|---|
| `fib(28)` recursive calls | wall time | — | n/a |
| method-call-loop (1e6 dispatches) | wall time | — | n/a |
| dict-churn (1e5 insert/lookup/delete) | wall time | — | n/a |
| corelib tokenizer over `parser.wy` | wall time, output diff | — | tree-walker wall time |

## Out of scope / deferred

- `bytes` type and anything binary-I/O shaped in `std::io` — epic 7.
- Further GC algorithm work beyond making stress mode a standing CI check (e.g. generational
  collection, concurrent collection) — no design section calls for it in Phase A.
- Self-hosted compiler work of any kind — Phase C (epics 8-11).
- Per-site message inline cache and per-class slot dict — moved to epic 12, after the
  compiler port, so the optimisation is built once against code that's finished moving.
- tcc CI — dropped from this epic's scope; gcc/clang cover current needs. Revisit as its own
  milestone later if a real portability requirement shows up.

## Risks

- **The tokenizer benchmark depends on every prior epic being correct**, so a failure here
  is diagnostically expensive (could be a bug from any of epics 2-5). Mitigate by running
  the *existing* corpus sweep (epic 5's M6) once more at the start of this epic, clean,
  before attributing any tokenizer-run failure to something new.
- **C++ wrapper API locks in the C API's shape.** Mitigate by writing M3 last among the
  three "mechanical" milestones (after M1-M2's signatures are final) rather than in
  parallel with them.

## Report

`epic_6_report.md` must additionally record:
- The completed benchmark table (skeleton above, filled in) with the machine/compiler it
  was measured on, since these numbers are not portable across hardware.
- Which compilers passed cleanly, which needed fixes, and a short list of what MISRA
  findings were fixed vs. accepted-as-deviation with reasons.
- Confirmation the tokenizer benchmark's output matched byte-for-byte, or the exact
  divergence found and which epic's milestone it traces back to.
- This is the last epic of Phase A — the report should end with an explicit "Phase A
  status" paragraph: is the toolchain (both `pypoc/` to compile, this repo to run) actually
  usable end to end for an arbitrary `.wy` file today, and what is the honest list of
  remaining gaps before Phase B (epic 7, `bytes`) can start.
- Proposed edits to `epic_12.md`: whether `src/dispatch.c`/`include/wyrm/class.h` moved in a
  way that invalidates its file list, and the actual baseline numbers from this epic's M4
  benchmark table for epic 12 to compare against.
