# Epic 3 — Data, closures, errors, defers

Implements design_c_vm.md M3 + M4. Prerequisite: epic 2 (M0 foundations + M2 core loop)
landed and its report exists.

## Goal

Bring the interpreter from "calls and arithmetic" to "the data language": tuples, lists,
dicts, pair chains and captured variables, the full iteration protocol, unpack, the
`in`/`is`/`cmp3`/unary family, varargs/kwargs/default parameter binding, and error values
with defers and fault unwinding through nested frames. This is the epic that makes closures
close over something real and makes a `trap` or a failed store actually run `on error`
handlers instead of crashing the process.

**Exit criterion:** `meson test -C buildDir` is green, and
`./buildDir/src/wyrm/wyrm test/bytecode/errors.wyc` prints output matching
`test/bytecode/errors.out` byte for byte, with `WY_TEST_GC_THRESHOLD=0` (stress mode) also
green for `closures.wyc` and `collections.wyc`.

## Inputs

- `doc-llm/history/wypoc-vm-port/epic_2_report.md` — read first; it says where the loop, `wy_vm_call_sync`, and
  the golden harness actually live after epic 2's own scan corrections.
- State-scan checklist (confirm each before cutting milestones; update this file if any is
  false):
  1. `wy_vm_run`'s `switch (op)` structure matches design_c_vm.md §2 (push/reload, `fr->ip`
     saved only on frame exit) — read `src/vm.c`.
  2. `wy_frame` has `defers` (`wy_pair*`) and `aux` fields as in design §1.1 — read
     `include/wyrm/frame.h`.
  3. Confirm which type tags from design §4 landed in epic 2 (`TUPLE`, `LIST`, `BYTES`,
     `ERROR` object shapes) vs. still stubs — grep `wy_type_tag` in `include/wyrm/value.h`.
  4. Confirm `wy_dict` is still the epic-2 linear scan or already reworked — read
     `include/wyrm/dict.h` and `src/*.c` for `wy_dict_`.
  5. Run `meson test -C buildDir` and record pass count for the report's "before" line.
  6. Confirm `test/bytecode/{closures,collections,errors}.wyc` and `.out` exist (built by
     epic 1's corpus script) — `ls test/bytecode/`.
  7. Confirm `wy_builtins` module from epic 2 exists and how leaf natives are registered —
     read `src/builtin/*.c`.
  8. Confirm `WY_OP_*` names for `getidx/setidx/iter/itnext/unpack/in/is/cmp3/plist/dict`
     are already generated in `include/wyrm/opcode.h` (synced verbatim in epic 1) — grep.
  9. Check whether `pypoc/.venv/bin/wyrm` exists (epic 1's toolchain step) — needed only if
     this epic must regenerate a corpus fixture.
  10. Confirm GC stress mode exists (`gc_threshold = 0` path from design §8) or is still
      epic 2 scope-creep — grep `gc_pressure` / `gc_threshold` in `src/gc.c`, `src/context.c`.

## Context to load

Budget assumes a fresh Opus scan session; each executor milestone loads only its own rows.

1. `doc-llm/history/wypoc-vm-port/design_c_vm.md` §1 (frame model), §4 (values), §6 (symbols/strings) — read,
   ~3k tokens.
2. `doc-llm/history/wypoc-vm-port/epic_2_report.md` — read in full, ~1–2k tokens.
3. `pypoc/doc/wyc-format.md` §6.1–§6.3 (opcode tables, already excerpted below) — read
   once, ~2k tokens; do not re-fetch per milestone.
4. `pypoc/wypoc/vm/interp.py:283-660` (`execute`, the whole dispatch body) — read once for
   M1–M3, ~2.5k tokens. Cite by `interp.py:<line>`, do not re-paste.
5. `pypoc/wypoc/vm/frame.py:71-166` (`build_pframe`, `for_function`) — read for M2 (call
   binding), ~1k tokens.
6. `pypoc/wypoc/wyrm_eval_parse_tree.py` — grep only: `_iter_values` (~3460-3490),
   `_PRIMITIVE_TYPE_CHECKS` (~3246-3270), `is_error`, `Pair`/`cons`/`car`/`cdr` in
   `wyrm_builtins.py`. Grep, do not read surrounding thousands of lines.
7. `pypoc/wypoc/wyrm_builtins.py:788-859` (`install`, builtin name list) — read, ~1k
   tokens, for M4's builtin registrations.
8. Current C: `include/wyrm/value.h`, `include/wyrm/frame.h`, `src/vm.c`,
   `include/wyrm/dict.h` — read only the sections the state scan flags as stale, budget
   ~3k tokens total.
9. `pypoc/doc/wyc-format.md` §10 "Undecided behaviour" (failed stores, defer granularity) —
   read, ~1k tokens; these are the decisions this epic must encode.

## Assumptions

- `wy_dict` from epic 0/2 is a linear-scan placeholder and needs a real hash table before
  `collections.wyc` is fast enough for the GC-stress run to finish in test time *(verify in
  scan)*.
- Captures are copied by value at `closure`, and the compiler already boxes
  captured-and-reassigned variables as one-element `PAIR` chains read/written via
  `getidx 0`/`setidx 0` (pypoc: `compiler_bc/functions.py:_prologue`), so `wy_box` is
  **not** used by the VM path — `getidx`/`setidx` must special-case `WY_TYPE_TAG_PAIR`
  *(verify in scan: confirm epic 2 did not already build a competing box-capture path)*.
- `range` as an iterable is **not** yet available; it lands in epic 5's prelude. Epic 3's
  `iter`/`itnext` must work over list/tuple/dict/str/pair/ITER-from-class without `range`
  *(verify in scan: check whether `collections.wy` sample requires range)*.
- Error objects (`wy_error_obj`) and the four builtin error classes (`error`,
  `OutOfMemory`, `RuntimeError`, `OSError`, `StopIteration`) require `wy_class` to exist as
  at least a stub, since `is_error` on INSTANCE checks `cls->flags & WY_CLASS_ERROR`
  (design §4) — epic 4 does the full class rework, so epic 3 needs a **minimal** class
  struct (name, flags, super) *(verify in scan: is there already a stub `wy_class` from
  epic 2, or must epic 3 introduce one now)*.
- `unpack`'s "every destination register receives the error value" on mismatch (wyc-format
  §6.3) is a value, not a fault — distinct from the "failed store is always a fault" rule
  for `setattr`/`setidx`/`setslot`/`setscope` *(verify in scan against wyc-format.md §10)*.
- Defers run at `return` and at every abnormal exit (trap, fault, uncaught host error) from
  the frame that armed them, matching `interp.py`'s `try/except BaseException: run_defers`
  wrapper *(verify in scan: confirm epic 2's `wy_vm_run` has a `do_unwind`/FAILING path
  stub already, per design §2, or whether epic 3 builds it from scratch)*.

## Milestones

### M1 — Composite data and iteration

**Scope**
- `tuple`, `list`, `dict`, `plist` construction ops (`0x81-0x84`).
- `getidx`/`setidx` (`0x98`/`0x99`) over list/tuple/dict/str, plus the PAIR-chain
  special case (index 0 only, per the capture-box convention).
- `iter`/`itnext` (`0xAE`/`0xAF`) producing a `WY_TYPE_TAG_ITER` object per source type
  (list, tuple, dict — key order, str — codepoints, pair chain); `itnext` jumps by `a2` on
  exhaustion (`interp.py:_iter_values`, `_EXHAUSTED` sentinel at `interp.py:660`).
- `unpack` (`0xB1`): exact-count unpack of list/tuple/dict/pair/str; mismatch writes the
  error value to every destination register (not a fault) — `interp.py:_unpacked`.
- `in`, `is`, `cmp3` (`0x96`, `0x97`, `0xB2`); `is` against a primitive-type name string
  uses `_PRIMITIVE_TYPE_CHECKS` semantics (nil/bool/int/uint/float/str/sym/list/tuple/dict/
  pair/error), against a class value uses ancestor-distance (`_class_distance`, ported
  minimally — full ranking is epic 4, but `is` needs "is an ancestor" today).
- `neg`/`inv`/`not` (`0x48-0x4A`); `not` calls the value's own truthiness (`__bool__` hook
  deferred to epic 4 for INSTANCE; bool/int/float/str/nil/list/tuple/dict truthiness is a
  builtin rule, not a message, for these tags).
- `new_primitive` (`0xAB`): fresh empty list/dict/tuple(0)/pair(nil) by tag.

**Files**
- New: `src/tuple.c`, `src/list.c`, `include/wyrm/tuple.h`, `include/wyrm/list.h`.
- Changed: `src/vm.c` (opcode cases), `src/vm_ops.c` if epic 2 created it, `src/dict.c` /
  `include/wyrm/dict.h` (hash rework, see Assumptions), `src/pair.c` (getidx/setidx PAIR
  case).

**Acceptance**
`meson test -C buildDir --test-case="collections*"` green; unlocks
`test/bytecode/collections.wyc` matching `.out`. GC stress
(`WY_TEST_GC_THRESHOLD=0 meson test -C buildDir --test-case="collections*"`) green.

**Model:** Sonnet (spec-driven, contract in wyc-format.md §6.3 and interp.py is exact).

**Fan-out:** 2 Sonnet subagents — (a) tuple/list/dict/plist construction + getidx/setidx,
files `src/tuple.c`, `src/list.c`, `src/dict.c`; (b) iter/itnext/unpack/in/is/cmp3/unary,
files `src/vm.c` opcode cases only (disjoint case blocks) and `include/wyrm/list.h` types
already landed by (a) — sequence (a) before (b) if `wy_list`/`wy_dict` don't exist yet,
otherwise parallel. Each subagent's acceptance: its own opcode's row in a hand-packed
`test_wvm.cpp` case plus the shared `collections.wyc` golden at the end.

### M2 — Closures, captures, call binding

**Scope**
- `closure` (`0xA8`): copy `ncaps` registers from `a2..a2+f` into the new `wy_function`'s
  `caps[]`, in P-frame capture order (design §4 `wy_function`).
- Full parameter binding on `WY_OP_CALL`/`call_va` slow path: positional, then keyword
  (kwargs only reachable via `call_va`'s dict — `f(x)` plain calls never carry kwargs
  per wyc-format, only `call_va` does), then declared default from `statics[]`, then trap
  naming the missing parameter — port of `frame.py:build_pframe` lines 79-141. Fast path
  (`argc == nparams`, no captures, no dispatch, no kwargs) stays a `memcpy`.
  design §1.2 gives the fast/slow split already; this milestone writes the slow path body.
- `call_va` (`0xA1`): callee at `L[base]`, positional tuple at `L[base+1]`, kwarg dict at
  `L[base+2]`.
- `*args`/`**kwargs` flags (bits 2, 3 of `functions[i].f`, wyc-format §8.5): last-but-one /
  last parameter collects into a tuple/dict per `build_pframe`'s `leftover`/`kwargs.pop`
  logic.

**Files**
- Changed: `src/vm_call.c` (or wherever epic 2 put call push logic), `src/vm.c`
  (`OP_CLOSURE`, `OP_CALL_VA` cases), `include/wyrm/function.h` (new, `wy_function` per
  design §4).

**Acceptance**
`test/bytecode/closures.wyc` matches `.out`; `test/bytecode/multiret.wyc` still matches
(regression); a hand-packed unit test calls a function with too few args and observes a
trap naming the parameter.

**Model:** Sonnet (binding rule is fully specified by `frame.py:build_pframe`).

**Fan-out:** none — binding and `closure` share the P-frame construction path; splitting
risks two half-implementations disagreeing on capture order.

### M3 — Error objects and control ops

**Scope**
- `wy_error_obj` (design §4): `{cls, what, payload}`; `is_error(v)` = ERROR tag or
  INSTANCE whose class chain has `WY_CLASS_ERROR` set.
- Minimal `wy_class` stub sufficient for the four builtin error classes and `WY_CLASS_ERROR`
  flag (full class realisation is epic 4 — this is *only* enough for `error`,
  `OutOfMemory`, `RuntimeError`, `OSError`, `StopIteration` to exist as callable classes
  whose instances satisfy `is_error`).
- `jerr`/`jnerr` (`0x4D`/`0x4E`) already have jump-shape decode from epic 2's core loop;
  wire the condition to `is_error`.
- `lunset` (`0x05`) writes the Unset error value (`{ERROR, NULL}` per design §4); `gget` on
  Unset faults naming the slot (free-slot) or the ambiguity marker, matching
  `interp.py`'s `OP_GGET` case (lines ~ "GGET").
- Builtins: `error`, `OutOfMemory`, `RuntimeError`, `OSError`, `StopIteration` classes;
  leaf natives `cons`/`pair`/`car`/`cdr`/`reverse`/`nreverse`/`$set_car`/`$set_cdr`/
  `tuple`/`copy`/`substr`/`append`/`resize`/`expand`/`remove` (`wyrm_builtins.py:install`,
  lines 788-859).

**Files**
- New: `include/wyrm/error.h`, `src/error.c`, `include/wyrm/class.h` rework stub (shared
  with epic 4, which extends it — coordinate via the report).
- Changed: `src/builtin/builtins.c` (add the natives above), `src/vm.c` (`jerr`/`jnerr`/
  `lunset` if not already stubbed by epic 2).

**Acceptance**
Hand-packed test: `jerr`/`jnerr` branch correctly on an error value and a non-error value;
`error("msg")` builtin call produces a value `is_error` accepts.

**Model:** Sonnet (builtin list and class flag are fully specified).

**Fan-out:** none (small, and the class stub must not race with M3's own error-object work
or M4's registration milestone).

### M4 — Defers and fault unwinding

**Scope**
- `defer_reg` (`0xAD`): cons `(closure . mode)` onto `fr->defers`, most-recent-first
  (design §1.1, §2).
- `return` drains defers before completing: mode 0 always runs, mode 1 runs iff
  `is_error(results[0])`, mode 2 runs iff `is_error(results[0]) || results[0] is nil`; each
  drained defer is pushed as a call with `WY_RET_DISCARD`, `goto reload`, and on its return
  the loop lands back in `WY_PHASE_RETURNING` to pop the next one — no C recursion (design
  §2's `do_return` sketch).
- Fault path (`WY_PHASE_FAILING`): a trap, a failed store (`setattr`/`setidx`/`setslot`/
  `setscope` per wyc-format §10 "failed stores are always a fault, never a value"), "no
  callable", or stack overflow sets `fb->fault`, drains **on-error** defers (mode 1, 2) with
  the frame `FAILING` (forcing the error condition per `interp.py`'s
  `run_defers(..., failed=True)`), pops, continues unwinding to the caller frame, which is
  itself now `FAILING` unless it catches — but this VM has no catch statement, so unwinding
  continues to the fiber root or the nearest native frame, which returns `WY_EXEC_FAULT`
  and `WY_ERR_FAULT` with `fiber->fault` readable.
- `trap` (`0x01`) with `TRAP_CODES` (0 = unreachable/uncompiled body, 1 = debugger break,
  2-255 reserved) — `interp.py:147-152`.

**Files**
- Changed: `src/vm.c` (`do_return`/`do_unwind` labels, `defer_reg` case), `include/wyrm/
  frame.h` if `defers`/`phase` aren't already present.

**Acceptance**
`test/bytecode/errors.wyc` matches `.out`; a hand-packed test: nested call (2 frames deep)
where the inner frame traps and the outer frame's mode-1 defer runs, observed via a side
effect, then `WY_ERR_FAULT` is returned to the C caller with the trap's message readable.

**Model:** Opus — this is the one first-of-kind control-flow milestone in the epic (no
existing C skeleton for `WY_PHASE_FAILING` drains across frames; design §2's sketch is
terse and the non-recursive requirement makes the state machine easy to get subtly wrong).

**Fan-out:** none.

## Out of scope / deferred

- `super`, class message dispatch, virtual slots, construct-on-call — epic 4.
- `range` as a first-class iterable, coroutines, `yield`/`yield_from` — epic 5.
- Per-site inline caching for any of these ops — epic 6.
- `getscope`/`setscope`, `import`/`import_star` — epic 5 (the ops may already decode
  cleanly through the loop's shape dispatch by epic 2, but their bodies are epic 5's).
- `bytes` methods beyond the bare object existing as a tag — epic 7.

## Risks

- **PAIR-chain capture convention is easy to get backwards.** `getidx`/`setidx` on a PAIR
  must index only position 0 (the compiler's box convention), not walk the chain like a
  list — mitigate by a hand-packed test that builds a capture box, mutates it via `setidx 0`
  from one closure, and reads the new value via `getidx 0` from another closure over the
  same capture.
- **Dict rework scope creep.** "Replace linear scan with a real hash table" can balloon;
  mitigate by capping M1's dict work to what `collections.wyc` and the GC-stress run need
  (open addressing keyed by `wy_util_fnv1a_buffer` for STR keys, pointer identity for
  SYMBOL keys per design §6) and deferring resize-tuning to epic 6's performance pass.
- **Defer-drain-as-a-call risks infinite reload loops** if a defer's own return path doesn't
  correctly resume unwinding. Mitigate with the two-frames-deep hand-packed test in M4 and
  a GC-stress run over it (`gc_threshold = 0`) to also catch missing root-push around the
  reload.
- **Minimal class stub in M3 conflicts with epic 4's real class rework.** Mitigate by
  keeping M3's `wy_class` to exactly `{name, super, flags}` and having epic 4's report
  note explicitly what it extended rather than replaced.

## Report

`epic_3_report.md` must additionally record:
- Whether the dict rework landed as open addressing or something else, and its load-factor/
  growth policy (epic 6 tunes it; epic 6 needs the actual policy, not "TBD").
- The exact `wy_class` stub shape from M3, flagged for epic 4 to extend vs. replace.
- Confirmation (with the hand-packed test name) that defer drains under `WY_PHASE_FAILING`
  do not recurse in C — this is a security property per AGENTS.md and epic 4/5's own
  fault paths (super, dispatch, import cycles) build on the same drain code.
- Any opcode from §6.1-§6.3 that this epic's scan found already implemented by epic 2 (so
  epic 4/5 don't re-implement it) or found *missing* from epic 2's core loop that this
  epic had to backfill.
- Corpus counts: matches / REFUSED / DIVERGES before and after, restricted to the samples
  epic 3 targets (`eval_functions`, `eval_range` if reachable without `range`,
  `eval_strings`, `eval_error_handling`, `eval_control_flow`, `eval_closures`,
  `eval_assignments`, `eval_args`).
- Proposed edits to `epic_4.md`: confirm or correct the "minimal `wy_class` stub" shape
  Assumption, and whether `is_error`'s ancestor check (built here as a cut-down
  `_class_distance`) should be reused by epic 4's dispatch ranking or replaced.
