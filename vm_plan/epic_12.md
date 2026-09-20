# Epic 12 — Dispatch performance: message inline cache, per-class slot dict

Implements design_c_vm.md §10's inline-cache note, moved out of epic 6 (see that file's
opening note). Prerequisite: epic 11 (bootstrap integration) landed and its report exists —
this epic runs after the self-hosted compiler is real and the toolchain no longer needs
`pypoc/` at all, so the dispatch/class code this optimisation sits on top of has finished
moving for good.

## Goal

Add a per-call-site message inline cache and a per-class slot dict on top of the now-stable
dispatch and class machinery, measured against epic 6 M4's baseline benchmark numbers, with
no corpus regression and no correctness compromise under `reg_msg`'s late-binding overloads.

**Exit criterion:** `meson test -C buildDir` green with the cache enabled; the
method-call-loop benchmark (epic 6's `bench/method_call_loop.wy`) shows a measured speedup
over epic 6's recorded baseline, numbers in the report; a hand-packed test proves the cache
cannot serve a stale overload after `reg_msg` adds a more specific one at runtime.

## Inputs

- `vm_plan/epic_11_report.md` — read first: confirm the self-hosted compiler's own dispatch
  and class code (if any changed shape from the C VM's during the port) hasn't moved
  `src/dispatch.c`/`include/wyrm/class.h` in a way that invalidates this file's plan.
- `vm_plan/epic_6_report.md` — read for the baseline benchmark table (method-call-loop
  number this epic must beat) and any "Proposed edits to epic_12.md" it recorded.
- State-scan checklist:
  1. Confirm `src/dispatch.c` and `include/wyrm/class.h` still exist at the paths epic 4/6
     left them, and read their current dispatch-resolution and slot-lookup code paths.
  2. Confirm classes are still immutable after realisation, or whether anything from epics
     7-11 (bytes methods, front-end cleanup, the compiler port) made `reg_msg` able to run
     against an already-dispatched message at runtime — this determines whether the cache
     needs any invalidation logic beyond an overload-count check.
  3. Confirm `msg`/`msg_va`'s bytecode encoding is unchanged (still has no spare operand for
     a cache key) — grep `OP_MSG` in `src/vm.c` and confirm against wyc-format.md §10.
  4. Run `bench/run.sh` (epic 6 M4) once, uncached, and confirm the numbers roughly match
     what epic 6's report recorded — if hardware differs, re-baseline here rather than
     comparing across machines.
  5. Grep the full corpus (`test/bytecode/`, `pypoc/wypoc/samples/`, `pypoc/wypoc/corelib/`)
     for the largest class by slot count, to settle the slot-dict threshold question for
     real rather than guessing.
  6. Run `meson test -C buildDir` and record the count for "before".

## Context to load

1. `vm_plan/design_c_vm.md` §7 (classes), §10 (message dispatch caching, one paragraph) —
   read in full, ~1.5k tokens.
2. `vm_plan/epic_6_report.md`, `vm_plan/epic_11_report.md` — read, ~2-3k tokens combined.
3. `pypoc/doc/wyc-format.md` §10 — read, ~0.3k tokens.
4. Current C: `src/dispatch.c`, `include/wyrm/class.h`, the `msg`/`msg_va` cases in
   `src/vm.c` — read in full, ~3k tokens.
5. `bench/method_call_loop.wy`, `bench/run.sh` (epic 6) — read, ~0.5k tokens.

## Assumptions

- The inline cache is a **side table keyed by code offset** (one cache-line-sized entry:
  last-seen receiver class pointer + resolved overload pointer), not a field added to the
  `msg` instruction's encoding, since wyc-format is frozen and `msg` has no spare operand
  (design §10) *(verify in scan: confirm nothing in epics 7-11 sneaked in a different
  mechanism)*.
- The per-class slot dict is additive to the existing array-scan `getattr`/`setattr` path —
  small classes keep the linear scan (already fast for ≤16 msg_map entries and a handful of
  slots), and the dict only kicks in above a slot-count threshold this epic picks from the
  scan's real corpus measurement (state-scan item 5) rather than a guess *(verify: if the
  corpus has no class with more than ~10 slots even after epics 7-11, downgrade this
  milestone's scope and say so in the report — do not build for a case that can't occur)*.
- Classes stay immutable after realisation, so cache invalidation needs only an
  overload-count check at cache-hit time, not a full invalidation protocol *(verify in scan
  item 2 against the current `reg_msg` implementation, not epic 4's original one, in case
  anything moved)*.

## Milestones

### M1 — Per-site message inline cache

**Scope**
- Side table keyed by code offset (the `msg`/`msg_va` instruction's own position in
  `module->code`, stable per call site): last-seen receiver class pointer, resolved overload
  pointer, and the message's overload count at cache-fill time. A cache hit re-checks the
  receiver class and current overload count before trusting the cached overload; either
  mismatch falls through to the full `wy_dispatch_resolve_f` ranking walk and refills the
  entry.
- Wire the cache lookup into `msg`/`msg_va`'s dispatch path in `src/vm.c`.

**Files**
- New: whatever side-table type the scan's dispatch-code read suggests (name it in the
  report rather than predicting here).
- Changed: `src/dispatch.c`, `src/vm.c` (`msg`/`msg_va` cases).

**Acceptance**
Hand-packed test: cache a dispatch, then `reg_msg` a more specific overload for the same
message and receiver class, then dispatch again and assert the new overload is chosen, not
the cached stale one. `bench/method_call_loop.wy` shows a measured speedup vs. epic 6's
recorded baseline with the cache on; a build-time flag disabling the cache reproduces the
baseline number (confirms the comparison is real, not noise). No corpus regression: full
`meson test -C buildDir` still green.

**Model:** Opus — design-heavy: cache invalidation correctness under `reg_msg`'s
late-binding overloads is the kind of subtle bug that only shows up as a flaky
wrong-dispatch result under specific ordering, and there's no reference implementation of
this cache anywhere to check against.

**Fan-out:** none (cache correctness must be reasoned about as one unit with the dispatch
code it accelerates).

### M2 — Per-class slot dict

**Scope**
- A small hash map from symbol to slot index, built once at class realisation, for classes
  above the slot-count threshold the scan settled (state-scan item 5). Consulted by
  `getattr`/`setattr` before falling back to the existing array scan; classes below the
  threshold are untouched.

**Files**
- Changed: `include/wyrm/class.h`, wherever class realisation lives (name it in the report).

**Acceptance**
No corpus regression. If the scan found no class in the corpus above the threshold, say so
plainly in the report and scope this milestone down to "threshold picked and mechanism
built, but unexercised by the current corpus" rather than implying a benchmark proves it.

**Model:** Sonnet once M1's cache-correctness pattern is established (the slot dict has no
invalidation question — classes are immutable after realisation — so this is more
mechanical than M1).

**Fan-out:** none — small enough, and depends on M1 landing first for the shared "is this
class immutable after realisation" scan finding.

## Out of scope / deferred

- Any GC algorithm work — unrelated to this epic.
- A dispatch cache invalidation scheme more sophisticated than "check overload count" if the
  scan finds `reg_msg` can't actually run after a class's first dispatch in practice — narrow
  the milestone rather than build for a case that can't occur, and say so in the report.
- Further caching (e.g. polymorphic inline caches with multiple cached classes per site) —
  no fixture or benchmark in this repo's corpus currently justifies it; note if the M1
  benchmark suggests otherwise.

## Risks

- **Inline cache correctness under late `reg_msg` binding** is this epic's sharpest
  correctness risk — a stale cache entry silently calling the wrong overload is worse than
  no cache. Mitigated by M1's dedicated hand-packed test.
- **Benchmark comparability**: epic 6's baseline numbers were measured on whatever machine
  ran that epic. If this epic runs on different hardware, re-baseline uncached first
  (state-scan item 4) rather than comparing across machines.

## Report

`epic_12_report.md` must additionally record:
- The final inline-cache invalidation rule actually implemented, and the hand-packed test
  name that proves it's correct under `reg_msg`.
- The slot-count threshold chosen for the per-class dict, and whether the corpus actually
  has any class that crosses it (if not, say so plainly rather than implying the feature is
  exercised when it isn't).
- The measured speedup (or lack of one, if the benchmark doesn't show a real improvement —
  report that honestly too) with machine/compiler details, since these numbers are not
  portable across hardware.
