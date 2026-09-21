# Epic 4 — Classes, instances, messages

Implements design_c_vm.md M5. Prerequisite: epic 3 (M3 + M4) landed and its report exists.
This is the first real implementation of `super` anywhere in the wyrm toolchain — the
Python tree walker does not have one (`pypoc/wypoc/wyrm_eval_parse_tree.py` raises "cannot
evaluate SuperCall"), so there is no reference behaviour to copy for that op; everything
else in this epic has an exact reference in `resolve_overload`.

## Goal

Give the VM classes, instances, slots (stored and virtual), construct-on-call, and message
dispatch with the same multi-dispatch ranking rule as the tree walker, plus `super`. Also
land the one pypoc-side compiler fix this phase needs: message promotion, so
`eval_messages.wy` stops being a named divergence in the corpus.

**Exit criterion:** `meson test -C buildDir` green;
`./buildDir/src/wyrm/wyrm test/bytecode/messages.wyc` matches `test/bytecode/messages.out`;
a corpus rebuild (`scripts/build_corpus.py` re-run against a patched pypoc) shows
`eval_messages.wy` moved from `DIVERGES` to `matches` in `test/bytecode/manifest.txt`.

## Inputs

- `doc-llm/history/wypoc-vm-port/epic_3_report.md` — read first. In particular its notes on the minimal
  `wy_class` stub shape and the ancestor-distance helper it built for `is`.
- State-scan checklist:
  1. Read epic 3's `wy_class` stub (`include/wyrm/class.h`) — confirm fields
     `{name, super, flags}` still hold, or note what epic 3 actually shipped.
  2. Confirm the four builtin error classes register through this stub today — grep
     `WY_CLASS_ERROR` usage in `src/error.c`/`src/builtin/builtins.c`.
  3. Confirm `getattr`/`setattr` (`0x9A`/`0x9B`) and `getslot`/`setslot` (`0x9C`/`0x9D`)
     opcodes decode through the loop already (epic 2's shape dispatch) even if their
     bodies still `goto fault_not_callable` or similar stub.
  4. Confirm `msg`/`msg_va`/`getmsg`/`reg_msg`/`super` (`0xA2-0xA6`, `0xA4`, `0xAC`) opcode
     numbers exist in the synced `include/wyrm/opcode.h` (epic 1) — grep `WY_OP_MSG`.
  5. Confirm whether epic 3's minimal `is` ancestor check used a function epic 4 can call
     directly (`wy_class_distance` or similar) or inlined the walk — read the M1 file epic
     3's report names.
  6. Run `meson test -C buildDir` and record the count for "before".
  7. Confirm `test/bytecode/{classes,messages}.wyc`/`.out` exist — `ls test/bytecode/`.
  8. Read `pypoc/wypoc/samples/eval_messages.wy` to see the exact shape of the promotion
     bug this epic must fix in pypoc.
  9. Confirm `pypoc/wypoc/compiler_bc/` has a place for the promotion fix — grep `reg_msg`
     in `pypoc/wypoc/compiler_bc/*.py` (likely `classes.py` or `module.py`, wherever
     top-level `fn name(...)` outside a class is lowered).
  10. Confirm the dispatch-cache side table mentioned in design §7/wyc-format §10
      ("message dispatch caching") is explicitly out of scope here (it is epic 6's) and no
      half-built version exists to collide with.

## Context to load

1. `doc-llm/history/wypoc-vm-port/design_c_vm.md` §7 (classes, instances, messages) — read in full, ~2.5k
   tokens; this is the epic's primary spec.
2. `doc-llm/history/wypoc-vm-port/epic_3_report.md` — read, ~1-2k tokens.
3. `pypoc/doc/wyc-format.md` §8.6 (`classes` section schema), §6.3 object-access and
   calls/dispatch tables (`getattr/setattr/getslot/setslot`, `msg/msg_va/getmsg/super/
   reg_msg`) — read, ~2k tokens (already excerpted in this repo's scan notes; re-read only
   if the excerpt is stale).
4. `pypoc/wypoc/vm/interp.py` — grep only these cases in `execute` (lines ~283-660):
   `OP_CLASS`, `OP_GETSLOT`/`OP_SETSLOT`, `OP_MSG`/`OP_MSG_VA`, `OP_GETMSG`,
   `OP_REG_MSG`, `OP_NEW_INSTANCE`. Also read `interp.py:164-260` (`call_function`,
   `enter`, `invoke`, `send`, `receivers_of`, `slot_names`, `backfill`) in full, ~1.5k
   tokens — this is the call/dispatch entry-point layer above the opcode cases.
5. `pypoc/wypoc/wyrm_eval_parse_tree.py` — read the two functions in full:
   `_class_distance` (~2838-2852) and `resolve_overload` (~2854-2891); grep only
   `_try_resolve_overload`, `dispatch_message`, `_resolve_message`, `new_instance`,
   `instantiate`, `MethodOverload`, `class Class` definition (fields), `class
   ClassInstance` definition (fields), `_call_dunder` (for `__bool__`/`__iter__`/`__str__`/
   `__add__`-family hooks). Budget ~3k tokens total for this file — it is 4157 lines, do
   not read broadly.
6. `pypoc/wypoc/compiler_bc/` — grep `reg_msg` and the top-level-`fn`-outside-class lowering
   path (likely `classes.py` or `module.py`) to find the promotion fix site named in the
   scan checklist item 9. Read only the function that needs the one-line change plus its
   immediate caller, ~1k tokens.
7. Current C: `include/wyrm/class.h` (epic 3's stub), `include/wyrm/frame.h`
   (`dispatch_msg`, `dispatch_body`, `WY_RET_CONSTRUCT`, `WY_FRAME_METHOD` fields — confirm
   present per design §1.1), `src/vm.c` (call-site opcode dispatch shape) — read only what
   the scan flags as needing extension, budget ~2k tokens.

## Assumptions

- Epic 3 shipped a `wy_class` stub of exactly `{object, name, super, flags}` with no
  `slots`/`msg_map`/`statics` — this epic replaces it wholesale rather than extending it
  *(verify in scan)*.
- `wy_instance` does not exist yet; this epic introduces it from scratch per design §4
  (`{object, cls, slots[]}`) *(verify in scan)*.
- The message-map fixed-size scan (16 entries, wyc-format §8.6) is small enough that a
  linear scan per dispatch is acceptable for this epic's performance budget; inline caching
  is deliberately epic 6's job *(verify in scan: confirm no perf regression test gates this
  epic)*.
- `is` from epic 3 already computes ancestor distance for two class values; this epic's
  `resolve_overload` port reuses that helper rather than duplicating the walk
  *(verify in scan against epic_3_report.md)*.
- The pypoc promotion fix (message promotion: plain `fn name(...)` becomes the wildcard
  overload of message `name`) is a small, local change to the compiler's top-level
  function lowering — one more `reg_msg` with an empty type tuple — not a change to the
  `.wyc` format or `wyc-format.md` *(verify in scan; wyc-format.md §9 in llm-bytecode.md
  already documents this as "compiler work, not a format change")*.
- Dunder hooks (`__add__`-family for the three-address ops, `__bool__` for `not`/`jf`/`jt`,
  `__iter__` for `iter`, `__str__` for string conversion) dispatch through the *same*
  message-send path this epic builds, not a separate fast path — an INSTANCE operand to
  `add` pushes a method frame with `ret_dst = reg(a0)` per design §2's note on
  three-address ops *(verify in scan: confirm epic 3's arithmetic ops already special-case
  INSTANCE as a TODO stub or fault, which this epic fills in)*.
- Construct-on-call writes the instance directly (skipping `WY_RET_CONSTRUCT`'s push) when
  the class has no applicable `init` overload for zero args, mirroring
  `_try_resolve_overload`'s "returns None rather than raising, construction just skips
  dispatch" behaviour *(verify in scan against `instantiate()` in wyrm_eval_parse_tree.py)*.

## Milestones

### M1 — Class realisation, instances, slots

**Scope**
- `wy_class` full struct (design §4/§7): `{object, name, super, module, slot_count, depth,
  flags, msg_count, slots[], msg_map[16], statics, init}`.
- `class` op (`0xA9`): realise from `classes[a1]` — resolve superclass via its global slot
  (wyc-format §8.6: "the superclass is a global slot, never a class index"), compute
  base-first slot layout and `depth = super->depth + 1`, register each `m[]` entry into
  `msg_map` **and** as an overload `((cls), FUNCTION)` on the module's message identity
  (design §7 "class realisation registers each map entry as overload").
- `wy_instance` (`{object, cls, slots[]}`); `new_instance` (`0xAA`): copy slot defaults
  (Unset where none) base-first.
- `getslot`/`setslot` (`0x9C`/`0x9D`): direct index into `slots[]`, bounds-checked against
  `cls->slot_count` at load time (design §8.6), no dispatch.
- `getattr`/`setattr` (`0x9A`/`0x9B`) on INSTANCE: scan `cls->slots` by name (own class
  first, then walk `super`); a slot with `getter`/`setter` set is virtual — push the getter
  as a method frame with `ret_dst = reg(a0)`, or the setter with `WY_RET_DISCARD`; plain
  slots read/write storage directly. Non-instance receivers consult a per-tag property
  table that starts empty (fault until epic 6/7 populate it).

**Files**
- Rewrite: `include/wyrm/class.h` (supersedes epic 3's stub).
- New: `include/wyrm/instance.h`, `src/instance.c`, `src/wclass.c`.
- Changed: `src/vm.c` (`class`, `new_instance`, `getslot`/`setslot`, `getattr`/`setattr`
  cases).

**Acceptance**
Hand-packed test: a 2-level class hierarchy (base + derived, one stored slot each) —
`new_instance` on the derived class yields an instance whose `slots[0]` is the base's
default and `slots[1]` is the derived's; `getslot`/`setslot` round-trip both. Unlocks part
of `test/bytecode/classes.wyc`.

**Model:** Sonnet (wyc-format §8.6 fully specifies slot layout and the class schema).

**Fan-out:** 2 Sonnet subagents — (a) `wy_class` realisation + `class`/`new_instance` ops,
files `include/wyrm/class.h`, `src/wclass.c`; (b) `wy_instance` +
`getslot/setslot/getattr/setattr`, files `include/wyrm/instance.h`, `src/instance.c` —
(b) depends on (a)'s struct layout landing first for slot indices, so sequence rather than
run concurrently unless (a) publishes the struct shape before finishing its op bodies.

### M2 — Construct-on-call and message identities

**Scope**
- Construct-on-call: `WY_TYPE_TAG_CLASS` as a `call` callee — `new_instance`, resolve
  `init` for `(instance)` via M3's dispatch (forward-declare the ranking function or land
  this after M3; see Fan-out), push with `WY_RET_CONSTRUCT` (`aux = instance`); on return,
  `is_error(result[0]) ? result[0] : aux`. No applicable `init` → skip dispatch, write the
  instance directly (per Assumptions).
- Message identities: `wy_message {object, name, owner, overloads[]}`, bound on first read
  (single component → `module->message_table`, create on miss; qualified → resolve first
  component like `getscope`, per design §7 and wyc-format §7.3/§8.7). `wy_overload
  {arity, types[16], body}`.
- `reg_msg` (`0xAC`): append `(types tuple, closure)` to the identity's overload list; nil
  entry in a type slot = wildcard.

**Files**
- New: `include/wyrm/message.h`, `src/message.c` (message identity table, `reg_msg`).
- Changed: `src/vm.c` (`OP_CALL` CLASS case, `reg_msg` case), `include/wyrm/module.h`
  (`message_table` field per design §5, if not already present).

**Acceptance**
Hand-packed test: a class with a zero-arg `init` that sets a slot — calling the class value
produces an instance with that slot set; a class with no `init` — calling it produces an
instance with defaults untouched, no fault.

**Model:** Sonnet (construct-on-call is fully specified by design §7's "Construction and
registration" paragraph and `instantiate()`'s reference behaviour).

**Fan-out:** none (construct-on-call's `init` resolution depends on M3 landing first, or
this milestone stubs a single-candidate resolver and M3 replaces it — pick whichever the
executor's scan finds cheaper and note the choice in the report).

### M3 — Dispatch ranking and `super`

**Scope**
- `msg`/`msg_va` (`0xA2`/`0xA3`): receiver(s) at `L[base]` (a value, or a tuple for
  multiple dispatch), resolve the message's overloads against the receiver(s) by the
  ranking rule (port of `resolve_overload`, `wyrm_eval_parse_tree.py:2854-2891`): per
  overload with matching arity, compute `dist[k]` (wildcard = `WY_WILDCARD_DISTANCE`
  = `0xFFFF`; CLASS constraint → ancestor distance via `depth`, no match → excluded; PTYPE
  constraint → 0 on tag match, else excluded); lexicographically smallest `dist` vector
  wins; a tie among the smallest is an ambiguity fault naming the message and receiver
  count; no candidate matching arity+constraints is a "no overload" fault. No allocation
  in the hot path (fixed `best`/tie-flag, not a sorted list — `resolve_overload`'s Python
  sorts a list, but design §7 explicitly asks for the allocation-free version).
- Single-INSTANCE-receiver fast path (design §7): walk `cls`, `cls->super`, … checking
  `msg_map` directly, skipping the general ranking, **unless** any overload for this
  message has arity 1 with a wildcard/PTYPE constraint or the receiver is not an INSTANCE —
  in which case fall through to the general path above.
- Chosen body pushed with `this` values in `P0..P(t-1)`, `WY_FRAME_METHOD` flag,
  `dispatch_msg`/`dispatch_body` recorded on the frame (for `super`).
- `getmsg` (`0xA4`): build a `WY_TYPE_TAG_BOUND_MSG` `{receiver, msg, body}` without
  calling.
- `super` (`0xA5`) — **no reference implementation exists; this is a first-of-kind
  decision.** Re-run the general ranking for the calling frame's `this` values, restricted
  to candidates strictly *after* `fr->dispatch_body` in the ranking order (i.e. exclude the
  currently-executing overload and anything ranked equal or more specific than it); same
  `this` values, arguments from `L[base..]`, results to `L[base]` (no separate receiver
  slot, per wyc-format §6.3). A `super` call that finds no next candidate is a fault
  ("no more general overload for `<message>`").
- `__add__`-family/`__bool__`/`__iter__`/`__str__` dunder hooks: wire the INSTANCE-operand
  cases epic 3 left stubbed for the three-address ops, `not`, `iter`, and any string
  coercion, to push a method frame via this milestone's dispatch machinery with
  `ret_dst = reg(a0)`, `ret_nres = 1`.

**Files**
- New: `src/dispatch.c` (`wy_dispatch_resolve_f`, `wy_dispatch_super_f`).
- Changed: `src/vm.c` (`msg`, `msg_va`, `getmsg`, `super` cases; three-address/`not`/`iter`
  INSTANCE branches), `include/wyrm/frame.h` if `dispatch_msg`/`dispatch_body` aren't
  present yet.

**Acceptance**
Unit tests (`src/test/test_dispatch.cpp`, new): (a) multi-dispatch ranking — three
overloads of a 2-arity message on unrelated small class hierarchies, assert the correct one
wins for a mixed-specificity receiver pair, and assert a genuine tie faults; (b) 3-deep
`super` chain — grandparent/parent/child each override one message, child's body calls
`super` twice and each hop lands in the next ancestor's body, verified by a side effect per
hop. `test/bytecode/messages.wyc` matches `.out`.

**Model:** Opus — first-of-kind (`super` has no oracle; the allocation-free ranking
rewrite from Python's sort-based reference needs care to stay behaviourally identical on
ties and wildcards).

**Fan-out:** none — ranking and `super` share the same candidate-enumeration code path and
must not drift apart.

### M4 — pypoc message-promotion fix and corpus update

**Scope**
- In `pypoc/wypoc/compiler_bc/` (site found in the state scan, likely `classes.py` or
  `module.py`): when a module defines a plain `fn name(...)` at top level that shares a
  name with any typed `fn [T] name(...)` message overload, emit one additional `reg_msg`
  for the plain closure with an empty type tuple (the wildcard arm), matching the tree
  walker's promotion behaviour (`register_overload` in `wyrm_eval_parse_tree.py`, and
  llm-bytecode.md §9 "Message promotion").
- Re-run `scripts/build_corpus.py` (or the epic 1 equivalent) to regenerate
  `test/bytecode/manifest.txt` and `eval_messages.wy`'s fixture; move it from `DIVERGES` to
  the matches set.
- Add a regression test in pypoc (`pypoc/test/test_compiler_bc*.py`, wherever `reg_msg`
  emission is unit-tested) asserting the wildcard arm is emitted.

**Files**
- Changed (pypoc, nested checkout — confirm epic 1's `.gitignore` treatment before editing;
  this is the one milestone in epics 3-6 that touches `pypoc/`): the compiler lowering
  file found in scan item 9, plus its test file.
- Changed (this repo): `test/bytecode/manifest.txt`, `test/bytecode/messages/` or wherever
  `eval_messages` lands under the corpus layout epic 1 chose.

**Acceptance**
`pytest pypoc/test/test_vm_samples.py -k eval_messages` passes with `eval_messages.wy` no
longer in `DIVERGES`; this repo's `meson test -C buildDir` still green after the corpus
regeneration.

**Model:** Sonnet (the fix is exactly the one-line lowering change llm-bytecode.md §9
already specifies; no design judgement needed).

**Fan-out:** none.

## Out of scope / deferred

- Per-site inline cache for `msg` dispatch, per-class slot dict (vs. array scan) — epic 6.
- Property tables for non-instance receivers' `getattr`/`setattr` (e.g. module or class
  properties) beyond what's needed for slots — stays a fault until something needs it.
- `getscope`/`setscope` (`::` namespace) — epic 5, though message-identity qualified-path
  resolution in M2 reuses the same first-component walk.
- Remote message dispatch (`_dispatch_remote_message` in the reference) — not part of the
  C VM's scope at all; hosted-only wyrm_remote concept, no design section covers it.

## Risks

- **`super`'s ranking-exclusion rule ("strictly after `dispatch_body` in ranking") has no
  reference to check against.** Mitigate with the 3-deep chain unit test in M3, plus a
  diamond-inheritance case (two parents, one child, `super` from a doubly-overridden
  message) exercised as a second unit test even though it's not required by acceptance —
  note in the report whether diamond `super` is well-defined or explicitly deferred.
- **Construct-on-call and dispatch ranking are mutually dependent** (M2's `init` resolution
  needs M3's resolver). Mitigate by explicitly choosing sequencing or a stub-then-replace
  approach in M2 and recording the choice.
- **Message promotion fix in pypoc could interact with other samples that define both a
  plain and typed `fn` of the same name deliberately** (i.e. this may not be unique to
  `eval_messages.wy`). Mitigate by running the *full* sample sweep after the fix, not just
  `eval_messages`, and recording any new REFUSED/DIVERGES movement in the report.
- **Class realisation order**: a class's superclass slot may not be filled yet if declared
  later in the same file's init code runs before that later class statement executes.
  wyc-format notes this is fine ("the slot is read at the moment the class is realised,
  which is what lets a base defined further down the same file work") — mitigate by a
  hand-packed test with the base class declared *after* the derived class in source order.

## Report

`epic_4_report.md` must additionally record:
- The exact ranking algorithm's data layout (fixed-size arrays vs. any allocation) and
  confirmation it performs zero heap allocation on the dispatch hot path, since this is a
  stated design requirement design §7 calls out explicitly.
- Whether diamond-inheritance `super` was tested and what it does.
- The pypoc compiler file and function changed for message promotion, and the full-sweep
  corpus diff (any REFUSED/DIVERGES entries added or removed beyond `eval_messages.wy`).
- Confirmation of the M2 construct-on-call / M3 dispatch sequencing choice actually taken.
- Corpus counts before/after for `classes`, `messages`, `eval_messages`.
- Proposed edits to `epic_5.md`: confirm `getscope`'s first-component-resolution helper
  this epic built for qualified message paths is reusable as-is, or needs generalising for
  import-path resolution.
