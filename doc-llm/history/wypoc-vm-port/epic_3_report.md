# Epic 3 report — Data, closures, errors, defers

Session dates: 2026-09-15 (M1-M3), 2026-09-16 (M4) · Models used: Sonnet
(M1, M2, M3, review fixes), Opus (M4) · Commits: df3289f..ddfafcb, then M4
on top of 371c8de

**Epic 3 is complete.** Exit criterion met: `meson test -C buildDir` green
(5/5), `errors.wyc` matches `errors.out` byte for byte, and `closures`,
`collections` and `errors` are green in the `golden-gcstress` suite
(`gc_threshold = 0`). This report covers M1-M4 plus a review pass over
M1/M2, which had been committed without the epic's own report ever being
written.

## Landed

- M1 — composite data + iteration: tuple/list/dict/plist construction,
  getidx/setidx (including the PAIR-chain capture-box special case at
  index 0 only), iter/itnext, unpack, in/is/cmp3, neg/inv/not,
  new_primitive. `closures.wyc` and `collections.wyc` match their `.out`
  byte-for-byte, including under `WY_TEST_GC_THRESHOLD=0`; both are now
  `TEST_CASE`s in the `golden`/`golden-gcstress` suites
  (`src/test/test_bytecode_golden.cpp`) — they were not before this
  session's review pass, so the epic's own acceptance command
  (`--test-case="collections*"`) had been matching zero tests.
- M2 — closures, captures, call binding: `closure` (0xA8), full parameter
  binding slow path (positional → keyword via `call_va`'s dict → declared
  default → trap naming the missing parameter) ported from
  `frame.py:build_pframe`, `call_va` (0xA1), `*args`/`**kwargs` leftover
  collection. `closures.wyc` and `multiret.wyc` (regression) match;
  hand-packed too-few-args test observes a trap naming the parameter
  (`src/test/test_wvm.cpp`, "a call with too few arguments faults naming
  the parameter").
- M3 — error objects and control ops: `error`/`OutOfMemory`/
  `RuntimeError`/`OSError`/`StopIteration` classes registered as builtins
  globals (`WY_CLASS_ERROR` flag; the four subtypes chain to `error` via
  `super`); `error("msg")` special-cased at `WY_OP_CALL`/`WY_OP_CALL_VA`
  when the callee is specifically the `error` class (not general
  construct-on-call); leaf natives `cons`/`pair`/`car`/`cdr`/`reverse`/
  `nreverse`/`$set_car`/`$set_cdr`/`tuple`/`copy`/`substr`/`append`/
  `resize`/`expand`/`remove`; `wy_dict_remove`. Hand-packed tests: jerr/jnerr
  branch correctly on an error value and a non-error value; `error("msg")`
  constructs a value `is_error` accepts and `is`-checks correctly against
  both the base `error` class and a sibling subtype (`OutOfMemory`, false).
  `jerr`/`jnerr`, `lunset`, `wy_error_obj`, and a minimal `wy_class` stub
  all turned out to already exist from epic 2/M1 — M3's own work was
  registering the builtins on top of that and confirming nothing was a
  stub in name only.

- M4 — defers and fault unwinding (`src/vm.c`, `src/fiber.c`):
  - `defer_reg` (0xAD) conses `(closure . mode)` onto `fr->defers`, most
    recent first — two `wy_pair`s per defer (entry + chain link). A
    non-FUNCTION operand faults `defer_reg: not a function` at arming time.
  - `return` records its window in `fr->ret_base`/`ret_count`, sets
    `WY_PHASE_RETURNING` and jumps to `do_return`, which pops the next
    runnable defer (mode 0 always; 1 iff `is_error(results[0])`; 2 iff
    error or nil; an empty return counts as nil), pushes it with
    `WY_RET_DISCARD` through the ordinary `push_bytecode_call_bind_f`, and
    `goto reload`. `reload` checks `fr->phase` first, so the defer's own
    return lands back in `do_return` for the next one. Only once the chain
    is empty does the window get delivered and the frame popped.
  - Every fault site (all 32 former `unwind_on_fault_f(...); return
    WY_EXEC_FAULT;` pairs, plus `trap`) now sets `fault_v` and
    `goto do_fault`: `fb->fault = fault_v`, `phase = FAILING`, then
    `do_unwind` drains every remaining defer of that frame with the error
    condition forced (all modes run, per `run_defers(failed=True)`), each
    as a DISCARD push + reload; then pops the frame, marks the caller
    `FAILING` and reloads — or, at a native frame / fiber root, returns
    `WY_EXEC_FAULT` (→ `WY_ERR_FAULT` from `wy_vm_call_sync`) with
    `fiber->fault` readable. `unwind_on_fault_f` is deleted.
  - `trap` messages are the reference's `TRAP_CODES` text (code 0:
    "unreachable code reached - or a function body the compiler could not
    lower", 1: "debugger break", otherwise "trap N").
  - A failed frame push now reports "stack overflow" / "out of memory"
    rather than the generic "call failed" (`push_fault_text_f`).
  - GC: the fiber's child walk (`src/fiber.c:next_children_iter`) now also
    visits every live frame's `defers` chain and `fiber->fault`. Before M4
    neither was a root; the fault in particular must survive the defers
    that run while a frame unwinds under GC stress.
- M3 follow-up fix — `gget` faulted on *every* Unset global. Per
  `interp.py` OP_GGET (and wyc-format §7.3) only a **free** slot nothing
  filled (or an ambiguity marker) faults; a module's own not-yet-assigned
  global reads as Unset, which `static count: int` + `count ?= 0`
  (`gget`; `jnerr`) depends on. `gget` now looks the slot up in
  `module->free_names` (linear scan, only when the read finds Unset) and
  faults `unbound global '<name>'`. This was the "unbound global"
  divergence the M1-M3 report left open; `eval_functions` now matches.

## Review fixes applied to M1/M2 (before M3 started)

The two M1/M2 commits (`df3289f`, `3723bae`) had never had an
`epic_3_report.md` written, so this session ran a full review against the
epic file and pypoc reference before touching M3. Findings and fixes:

- **String iteration/indexing/unpack decoded UTF-8 bytes, not codepoints**
  (a real spec deviation, invisible to the ASCII-only fixtures). Per
  `wyrm_eval_parse_tree.py:index_value`, `getidx` on a STR must decode to a
  codepoint integer (`ord(s[i])`); per `_iter_values`/`_unpacked`,
  iteration and unpack must yield one-character *substrings* (Python's own
  str iteration), not byte codes either way. Added `wy_utf8_decode_f`/
  `wy_utf8_codepoint_count_f`/`wy_utf8_offset_at_f` (`string.h`/`string.c`)
  and fixed all four call sites (`src/vm_ops.c` getidx, `src/iter.c`
  iterator next, `src/vm.c` unpack's STR case). Regression tests added in
  `test_wvm.cpp` exercise a string with a 2-byte character ("héllo").
- **`collections` was never wired into the golden test suite**, so the
  epic's own M1 acceptance command matched zero tests despite the fixture
  actually passing when run manually. Added to both `golden` and
  `golden-gcstress` `TEST_SUITE`s in `test_bytecode_golden.cpp`.
- **`AGENTS.md`'s "AI Usage Policy" and "Issue and PR Guidelines" sections
  were deleted** in the M1 commit with no relation to that commit's stated
  scope (composite data + iteration). Restored verbatim. (Not the "no C
  recursion" rule — that survived untouched throughout.)

None of these were severe enough to call M1/M2 falsely claimed done, but
all three are exactly the class of thing the epic's own report step exists
to catch — which is why it hadn't caught them.

## Deviations from the epic file

- epic_3.md's M1 "Files" section said `src/tuple.c`/`src/list.c` were new
  in this epic; they actually landed in epic 2/M1 (six new GC heap kinds).
  Stale plan text, not an execution fault — noted here so nobody goes
  looking for a phantom M1 diff.
- M3's `error("msg")` is **not** bound to a callable class value the way
  the pypoc reference binds `error` (a `Class`, dispatched through generic
  construct-on-call). General construct-on-call for arbitrary user classes
  is explicitly epic 4 scope per epic_3.md's "Out of scope" list, so this
  session special-cased *only* the `error` class at the `WY_OP_CALL`/
  `WY_OP_CALL_VA` sites (`src/vm.c:try_construct_error_f`): if the callee
  is a `WY_TYPE_TAG_CLASS` value whose pointer equals `ctx->error_class`,
  build a `wy_error_obj` directly; any other class value still faults
  "value is not callable" (no generic dispatch exists yet). This satisfies
  the epic's acceptance line ("`error("msg")` builtin call produces a value
  `is_error` accepts") without pulling construct-on-call forward from
  epic 4. `OutOfMemory`/`RuntimeError`/`OSError`/`StopIteration` exist as
  class values (so `is OutOfMemory` etc. work) but are **not** callable —
  nothing in epic 3's target corpus constructs them, and making them
  callable is the same construct-on-call machinery, deliberately deferred.

- The epic's exit criterion names `WY_TEST_GC_THRESHOLD=0`, but nothing
  reads that environment variable (not the CLI, not the test binary).
  Stress mode is the `golden-gcstress` doctest suite, which sets
  `context->gc_threshold = 0` itself; M4's unit tests do the same inline.
  Use `meson test -C buildDir cwyrm-golden-gcstress`.

## Tests

- before (start of session, after epic 2): 5 meson tests (loader, golden,
  golden-gcstress, disasm-check, cwyrm unit suite); cwyrm unit suite itself
  had not been counted per-assertion in prior reports.
- after M1/M2 review fixes: 182/182 doctest assertions passing (added 2
  UTF-8 regression cases; wired `collections` into golden/golden-gcstress).
- after M3: 184/184 test cases, 15271/15271 assertions passing. `meson
  test -C buildDir` (all 5 meson-level tests): green throughout.
- after M4: 191/191 test cases, 15358/15358 assertions. New: `errors` in
  both `golden` and `golden-gcstress`; test suite "vm defers and fault
  unwinding" in `src/test/test_wvm.cpp` (5 cases, see below); the old
  "gget on an unfilled global faults" case was rewritten as "gget on an
  unfilled free slot faults naming it; an unassigned own global reads
  Unset", since it encoded the M3 bug.
- M4 hand-packed tests (suite "vm defers and fault unwinding"):
  - "a trap two frames deep runs the inner then the outer on-error defer,
    then faults to C" — the epic's acceptance test; also checks the fault
    text, that the fiber is back at its native root, and that the value
    stack top is restored. Runs at the default threshold and at
    `gc_threshold = 0`.
  - "defer drains under WY_PHASE_FAILING do not recurse in C: 121 frames
    unwind through their defers" — a 121-deep recursion, each frame
    arming a counter defer, trapping at the bottom; all 121 run. Also run
    at `gc_threshold = 0`. (The non-recursion itself is structural: every
    defer runs as a pushed frame followed by `goto reload`, and neither
    `do_return` nor `do_unwind` calls back into `wy_vm_run`.)
  - "return drains defers most recent first, by mode" — result 5 / nil /
    Unset against modes 0/1/2, plus ordering.
  - "a defer that faults during return fails the frame; the rest run as
    on-error" — and the return window is not delivered.
  - "defer_reg on a non-function faults".
  - Mutation check: removing the forced error condition in
    `defer_pop_runnable_f` fails 2 of the 5 cases.
- Corpus, restricted to epic 3's target samples
  (`test/bytecode/samples/eval_*.wyc`, run manually via
  `./buildDir/src/wyrm/wyrm`, not yet wired into an automated corpus-sweep
  test — that automation is epic 5/6 scope):
  - before M4 → after M4:
  - `eval_strings`: matches → matches.
  - `eval_functions`: DIVERGES (`unbound global`) → **matches** (the gget
    fix above).
  - `eval_args`: DIVERGES (`unbound global`) → DIVERGES, now named:
    `unbound global '__ARGS'` — a host-supplied global (`__ARGS`,
    `__write`, `__STDOUT` in its free section), not epic 3 scope.
  - `eval_range`: DIVERGES → DIVERGES, now `unbound global 'range'`
    (epic 5, expected).
  - `eval_assignments`: listed before as `unbound global`; after M4 it
    faults at `class`/`msg`/`setattr` (epic 4). The earlier line may have
    been mis-recorded; either way it is now blocked only on epic 4.
  - `eval_control_flow`, `eval_closures`, `eval_error_handling`: DIVERGE
    → DIVERGE, unchanged: `unknown or unimplemented opcode` at
    `class`/`reg_msg`/`msg`/`setattr` (epic 4).
  - Summary for the 8 target samples: 1 match / 7 diverge → 2 match / 6
    diverge (0 REFUSED either way; all remaining divergences are epic 4
    class ops, epic 5 `range`, or host globals).
  - `test/bytecode/errors.wyc`: DIVERGED at `defer_reg` → **matches**,
    also under GC stress.

## Required records (epic_3.md "Report")

- **Dict:** already a hash table before M1 (commit 289a3fb), not a linear
  scan — the epic's Assumption was stale. Shape (`src/wdict.c`): an
  insertion-ordered dense array of `{key, key_hash, value}` plus a sparse
  open-addressing index (linear probing, power-of-two capacity, keys
  hashed by `wy_op_hash`, compared by `wy_op_eq`). Growth: when
  `count >= 0.75 * dense_capacity` (or no sparse array yet), dense
  capacity doubles (initial 4) and sparse capacity becomes
  `bit_ceil(2 * dense_capacity)`, full rehash. Removal is swap-remove plus
  full sparse rehash (see Orientation gotcha). Epic 6 tunes this.
- **`wy_class` stub:** `{prototype, super, sym_name, flags}`
  (`include/wyrm/class.h`), `WY_CLASS_ERROR` flag; epic 4 extends, not
  replaces (see Proposed edits).
- **No C recursion in defer drains:** confirmed by construction and by
  "defer drains under WY_PHASE_FAILING do not recurse in C: 121 frames
  unwind through their defers" and "a trap two frames deep runs the inner
  then the outer on-error defer, then faults to C" (`test_wvm.cpp`).
- **Opcodes already present from epic 2** (not re-implemented here):
  `trap`, `return`, `lnil`/`lbool`/`lunset`, `jerr`/`jnerr` decode,
  `closure` with 0 captures, `call`. **Backfilled by this epic:** the
  M1 data ops, `closure` captures, `call_va`, `defer_reg`, and the
  RETURNING/FAILING phases. `gget`'s Unset rule was wrong from epic 2 and
  is fixed. Still absent from the loop and owned by later epics:
  `class`/`reg_msg`/`msg`/`msg_va`/`getattr`/`setattr`/`super`/
  `new_instance` (4), `getscope`/`setscope`/`import`/`import_star`/
  `yield` (5), `return_cps` (reserved in v1).

## Open questions and known gaps

- **A defer that faults, or cannot be pushed, during `return`** fails the
  frame and the *remaining* defers run as on-error ones. The reference
  (`interp.py:run_defers`) clears `frame.defers` before running the batch,
  so there an exception in one defer skips the rest. Deliberate
  deviation: cleanup keeps its guarantee. The newest fault wins in both
  (a defer faulting mid-unwind replaces `fiber->fault`), matching Python's
  exception-in-handler behaviour.
- **A defer that cannot be pushed while already FAILING** (e.g. the fault
  was itself a stack overflow at the innermost frame) is dropped and the
  original fault stands; outer frames, which have room once the inner one
  pops, still drain theirs. No test forces this path yet.
- **Frames do not root their module or callee.** A defer's `wy_function`
  becomes unreachable once popped off the chain (its captures are copied
  into the new P frame, and the module is rooted via the context's module
  table, so this is safe today). Unregistered modules — hand-packed tests
  only — are not rooted by a running frame; `run_fn0` in `test_wvm.cpp`
  registers its module for that reason. Epic 4/5 should root
  `frame->module`, `aux` and `dispatch_body` in the fiber walk if any of
  them can be the only reference.
- `WY_PHASE_AWAIT_NATIVE` is still unused; exec natives called from
  bytecode still fault. Their fault path will need to set the bytecode
  caller `FAILING` rather than returning straight to the trampoline.
- `src/vm_ops.c`'s `is` check for an `INSTANCE` operand
  (`wy_class* cls = ((wy_class**) value.data.gc_object)[0]; // Placeholder`)
  is dead code today (no opcode produces an INSTANCE value yet) but is a
  landmine for epic 4: it assumes a class pointer is the first field of
  the eventual instance struct. Epic 4 must replace this, not build on it.

## Proposed edits to epic_4.md

- Confirm the minimal `wy_class` stub shape is exactly `{prototype, super,
  sym_name, flags}` (`include/wyrm/class.h`) and that epic 4 extends it
  (adds slots/dispatch) rather than replacing it — the five builtin error
  classes epic 3 created (`error` + 4 subtypes) must keep working
  unchanged.
- `is`'s ancestor-distance check (`src/vm_ops.c:is_ancestor`, a plain
  `while (cls) { if (cls==target) return true; cls = cls->super; }` walk)
  is intentionally the simplest possible thing — epic 4's dispatch ranking
  needs a *distance*, not just a boolean, so expect to replace this rather
  than reuse it directly, unless epic 4 wraps it.
- Epic 4's construct-on-call implementation must special-case (or
  subsume) `src/vm.c:try_construct_error_f`'s hand-rolled `error("msg")`
  path — once generic class-call dispatch exists, prefer routing `error`
  through it the way the pypoc reference does (a special case inside
  `call_value`), and delete the epic-3 shortcut, rather than keeping two
  code paths that construct error objects.
- The "unbound global" divergence is resolved (M3 gget bug, fixed in M4);
  drop it from epic 4's scan checklist.
- Construct-on-call's `WY_RET_CONSTRUCT` and the method/super paths must
  fault through `goto do_fault` (never a bare `return WY_EXEC_FAULT`) so
  defers of every frame between the fault and the native root still run;
  a construct frame that fails in `init` unwinds like any other.

## Orientation for the next session

- The dispatch loop is `wy_vm_run` in `src/vm.c`; opcode-specific
  arithmetic/comparison/`is`/getidx/setidx/in helpers live in
  `src/vm_ops.c` (`wy_vm_*_f` functions declared in `src/vm_internal.h`).
- Run one fixture: `./buildDir/src/wyrm/wyrm test/bytecode/<name>.wyc`;
  disassemble with `./buildDir/src/wyrm/wyrm --disasm <file>.wyc` to find
  exactly which opcode a fault stopped on.
- `meson test -C buildDir --test-case="collections*"` and similar
  `--test-case` filters are matched against doctest `TEST_CASE` names
  quoted in the source, not against fixture files — always grep
  `src/test/*.cpp` for the actual registered name before trusting an
  epic's acceptance command.
- `wy_context::error_class` is set once by `wy_builtins_new` and is the
  one non-slot-lookup way C code reaches the base `error` class (used by
  `try_construct_error_f` and the `error`-value builders in
  `src/vm_ops.c`/`src/builtin/builtins.c`).
- UTF-8 string operations (getidx, iter/itnext, unpack, `car`/`cdr`,
  `substr`) all go through `wy_utf8_decode_f`/`wy_utf8_codepoint_count_f`/
  `wy_utf8_offset_at_f` (`include/wyrm/string.h`) now — if you add another
  string-indexing op, use these rather than raw byte indexing.
- Fault anywhere inside `wy_vm_run`: set `fault_v` and `goto do_fault`.
  A bare `return WY_EXEC_FAULT` from the loop now skips defers and leaves
  bytecode frames on the fiber — always a bug.
- Defers are `fr->defers`: a chain of link pairs `(entry . next)` with
  entry `(function . word mode)`. `defer_pop_runnable_f` is the only
  reader; `src/fiber.c:next_children_iter` is the GC walk.
- Gotcha: `wy_dict_remove` rehashes the whole sparse table on every
  removal (swap-remove + full rebuild, not backward-shift deletion). Fine
  for epic 3/4's corpus; epic 6's performance pass should look at it if
  dict-heavy benchmarks show up slow.
