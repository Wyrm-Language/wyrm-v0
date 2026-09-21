# Epic 4 report — Classes, instances, messages

Session dates: 2026-09-16 (M1, M2, M3, M4) · Models used: Sonnet · **Epic 4
is complete: M1-M4 are all done.**

## State-scan findings (epic_4.md's checklist)

- **`wy_class` before this session** was `{prototype, super, sym_name,
  flags}` where `prototype` embedded a 256-reserved-slot `wy_prototype`
  (epic 3's stub). `design_c_vm.md` §7 says explicitly: "The
  `wy_prototype`-based class (256 reserved slots) is replaced" — this
  session followed the design doc, **not** epic_3_report.md's "Proposed
  edits to epic_4.md" line asking to extend the prototype stub. The two
  documents disagreed; design_c_vm.md is the epic's primary spec per its
  own "Context to load" §1, so it won. `wy_prototype` (`include/wyrm/
  prototype.h`) had no other callers anywhere in the tree and is deleted,
  not deprecated.
- The five builtin error classes (`src/builtin/builtins.c`) construct
  through `wy_class_new` + `wy_class_set_name_f` + direct `flags`/`super`
  writes, none of which needed to change signature — `builtins.c` is
  untouched by this session.
- `WY_CLASS_ERROR` usage: confirmed, only in `builtins.c` (the five error
  classes) and `vm_ops.c`'s `is_error`-family checks — unaffected.
- `getattr`/`setattr` (`0x9A`/`0x9B`) and `getslot`/`setslot` (`0x9C`/
  `0x9D`) decoded through the loop already (as two-word ops) but had no
  `case` — fell to the default fault. Now implemented.
- `msg`/`msg_va`/`getmsg`/`reg_msg`/`super`/`class`/`new_instance` opcode
  numbers all present in `include/wyrm/opcode.h` from epic 1, confirmed by
  grep. Only `class`/`new_instance` are this milestone's scope; `msg`-family
  still faults "unknown or unimplemented opcode" as expected.
- Epic 3's `is` ancestor check (`vm_ops.c:is_ancestor`, a plain boolean
  walk) is now deleted and replaced by `wy_class_distance_f`/
  `wy_class_is_ancestor_f` (`class.h`), used at both of `is`'s call sites
  (error values and, newly, INSTANCE values). The dead placeholder cast
  `((wy_class**) value.data.gc_object)[0]` epic 3 flagged as a landmine is
  gone, replaced by a real `wy_instance*` cast.
- `meson test -C buildDir` before this session: 5/5 meson tests, 191 doctest
  cases / 15358 assertions (end of epic 3/M4).
- `test/bytecode/{classes,messages}.wyc`/`.out` exist, confirmed via `ls`.
- Items 8-10 of the scan checklist (pypoc `eval_messages.wy`, the
  promotion-fix site in `compiler_bc/`, the dispatch-cache side table) are
  M4 scope and were not chased this session.

## Landed — M1: class realisation, instances, slots

- **`wy_class` rewrite** (`include/wyrm/class.h`, `src/wclass.c`): exactly
  design §7's shape, `{object, name, super, module, slot_count, depth,
  flags, msg_count, slots[], msg_map[16], statics, init}`, plus
  `wy_class_slot {name, default_value, getter, setter}` and
  `wy_class_msg_entry {msg, body}`. `wy_class_new` still builds a bare
  class (no super/slots/messages) for the builtin error classes;
  `wy_class_realise_f(ctx, module, class_idx, &out)` is the new entry point
  the `class` op uses.
- **`class` op (`0xA9`)**: `wy_class_realise_f` resolves the superclass via
  its already-loaded `module->globals[super_slot]` (faults `WY_ERR_BAD_TYPE`
  if that slot isn't a class value yet — see the realisation-order test
  below), computes base-first slot layout (`super->slot_count` copied
  first, own `sl[]` entries appended) and `depth = super->depth + 1`,
  realises every slot's default (from `module->statics`) and getter/setter
  (zero-capture `wy_function`s built from `module->functions`), fills
  `msg_map` from `classes[].m[]` (see "Deviation" below), builds `statics`
  as a `wy_slot_dict` from `classes[].st[]`, and realises `init`. Result is
  cached in `module->classes[class_idx]`; a repeat `class` op on the same
  index (or a second `wy_class_realise_f` call) returns the cached object
  — covered by the "idempotent" assertion in the realisation-order test.
- **`wy_instance`** (`include/wyrm/instance.h`, `src/instance.c`):
  `{object, cls, slots[]}`, a GC object with a flexible array member (same
  pattern as `wy_function`/`wy_tuple`). `wy_instance_new_f` copies
  `cls->slot_count` defaults base-first (Unset where a slot proto had none).
- **`new_instance` op (`0xAA`)**: `a1` is a register holding a CLASS value
  (not a `classes[]` index — distinct from `class`'s `a1`, confirmed
  against wyc-format.md §6.3's operand table); faults `not a class` on any
  other type.
- **`getslot`/`setslot` (`0x9C`/`0x9D`)**: direct index into `slots[]`,
  bounds-checked against `inst->cls->slot_count`, no dispatch — exactly as
  scoped. Note the asymmetric operand order matching `getidx`/`setidx`:
  `getslot a0=dst,a1=obj,a2=slot#` but `setslot a0=obj,a1=slot#,a2=src`.
- **`getattr`/`setattr` (`0x9A`/`0x9B`)** on INSTANCE: `a2`/`a1` (getattr/
  setattr respectively) is a *symbol* operand — an index into
  `module->symbols`, not a register (wyc-format.md §5.3) — resolved and
  passed to `wy_class_find_slot_f`, which scans `cls->slots` own-class-first
  then walks `super`. A slot is virtual iff its `getter` or `setter` is not
  Unset: the getter is pushed as an ordinary 1-arg bytecode call
  (`push_bytecode_call_bind_f`, `WY_RET_WINDOW`, `ret_dst = reg(a0)`) —
  getter/setter functions take the instance as an explicit first parameter
  (`P0`), confirmed against `classes.wyc`'s disassembly (its getter-shaped
  functions read `getattr a1=P0`), so no special "method frame"/`this`
  machinery was needed for this milestone; a plain slot reads/writes
  `inst->slots[idx]` directly. Non-INSTANCE receivers fault
  `getattr: unsupported receiver type` (the per-tag property table design
  §7 mentions starts empty and stays empty until epic 6/7, per epic_4.md's
  "Out of scope").
- GC: `wy_class`'s `children_iter` walks `module` (if any — bare builtin
  classes have none), `super`, every slot's `default_value`/`getter`/
  `setter`, every `msg_map[].body`, and `init`. `wy_instance`'s walks `cls`
  (always present, `WY_ASSERT`ed) then every slot value. Both follow the
  existing `wy_function`/`wy_pair` children-iterator convention exactly
  (work-area-indexed resumable iteration, `wy_value_is_gc_ref_f` filtering
  non-object values).

## Deviation from epic_4.md's M1 scope text

M1's scope bullet says `class` "register[s] each `m[]` entry into `msg_map`
**and** as an overload `((cls), FUNCTION)` on the module's message identity"
— the second half needs `wy_message`/`module->message_table`, which is M2
scope (epic_4.md's own M2 section: "Message identities: `wy_message`
{...}, bound on first read"). This session filled `msg_map[i].body` (the
realised zero-capture function) but left `msg_map[i].msg = WY_NULL` and did
**not** touch `module->message_table` — there is nothing to register
against yet. M2 must fill `msg_map[i].msg` for every already-realised class
when it builds message identities (not just for classes it realises after
M2 lands), since `class` ops earlier in the same run will have already
produced classes with `msg = WY_NULL` in their map. Flagging this
explicitly for M2's own scan rather than leaving it implicit.

This mirrors the "stub now, M2 replaces" pattern epic_4.md's own M2
Fan-out note already sanctions for construct-on-call/`init` resolution, so
it isn't treated as a deviation needing separate sign-off — just recorded
per the epic's documentation convention.

## Tests

- Doctest suite: 191/191 → **193/193 test cases, 15358 → 15380 assertions**
  (net +2 cases / +22 assertions). `meson test -C buildDir`: 5/5, unchanged.
- New (`src/test/test_class.cpp`, suite `wclass`):
  - "init basic class" — pre-existing, unchanged.
  - "slot lookup walks super, base-first" — rewritten from the old
    `wy_prototype`-based "slot accessors" test (which used the now-deleted
    `wy_class_add_slot_f`/`wy_class_get_slot_selector_f`/`WY_BAD_SLOT`
    API); now builds a 2-level hierarchy by hand and exercises
    `wy_class_find_slot_f` + `wy_class_distance_f` directly, including the
    non-ancestor case (`WY_WILDCARD_DISTANCE`).
  - **"new_instance on a 2-level hierarchy: base-first defaults,
    getslot/setslot round-trip"** — the epic's own M1 acceptance test,
    verbatim: base + derived, one stored slot each; `new_instance` yields
    `slots[0]` = base's default, `slots[1]` = derived's; both round-trip a
    write.
  - **"class realisation: a base declared after the derived class in
    source order"** — the Risks section's "Class realisation order"
    mitigation: `class_protos[0]` is the derived class (declared first in
    a hand-built module) whose `super_slot` names `class_protos[1]` (the
    base, declared later); realising the derived class before the base's
    global is filled faults `WY_ERR_BAD_TYPE`; realising the base and
    publishing it to its global slot (as a real `class`+`gset` op pair in
    the base's own init code would) lets the derived class then realise
    cleanly. Also asserts `wy_class_realise_f` is idempotent (a repeat call
    on the same `class_idx` returns the cached pointer, not a new object).
- Regression fix, unrelated to new functionality: `src/test/test_wvm.cpp`'s
  `error("msg")`/`is` test used `WY_BAD_SLOT` (from the now-deleted
  `wy_prototype.h`) to check a `wy_slot_dict_get` result, which should have
  been `WY_SLOT_INVALID` (`slot.h`) all along — the two macros happened to
  be the same value (`WY_UWORD_MAX`) so the test passed either way, but the
  wrong-header dependency only compiled because `class.h` transitively
  pulled in `prototype.h` before this session. Fixed to `WY_SLOT_INVALID`.
- Corpus, manual runs (not yet wired into an automated sweep — epic 5/6
  scope per epic 3's report):
  - `test/bytecode/classes.wyc`: `unknown or unimplemented opcode` (at a
    `class`/`getattr`/`setattr` instruction) → **`value is not callable`**
    (at the first `call` on a CLASS value) — i.e. `class`/`new_instance`/
    `getattr`/`setattr` now execute the whole fixture up to construct-
    on-call, which is M2 scope. Confirmed via manual disassembly that the
    fault site is exactly the expected construct-on-call boundary, not a
    regression inside this milestone's own ops.
  - `test/bytecode/messages.wyc`: unchanged, `unknown or unimplemented
    opcode` at `msg`/`reg_msg` (M2/M3 scope).
  - `eval_error_handling.wy` (epic 3's corpus sample): `unknown or
    unimplemented opcode` → `value is not callable`, same construct-on-call
    boundary.
  - `eval_control_flow`, `eval_closures`, `eval_assignments`: unchanged,
    still fault at `msg`/`reg_msg` (M2/M3 scope) — these use message
    dispatch, not just plain classes.

## Landed — M2: construct-on-call and message identities

- **A load-bearing bug found and fixed before writing any M2 code**: method
  bodies (`init`, and by the same construction, virtual slot getters/
  setters, and every future message body) do **not** declare the receiver
  as an ordinary parameter. Confirmed against the reference compiler
  (`pypoc/wypoc/compiler_bc/functions.py:compile_callable`'s
  `this_count=len(dispatch)`, and `classes.py`'s `_method`/`_accessor`,
  which pass `member.params`/`option.value.params` straight through with no
  prepended `self`) and `wy_function_proto.ndispatch` (`module.h`, already
  loaded by epic 1/2's `decode_class_`/function-proto decoding, unused
  until now): the `this` value(s) occupy `P0..P(t-1)` **outside** the
  declared parameter list, which starts at `Pt`. M1's getattr/setattr
  virtual-slot dispatch (landed before this was caught) called
  `push_bytecode_call_bind_f` treating the instance as an ordinary bound
  positional argument — silently wrong for any getter/setter with a
  declared name that doesn't happen to be positional-slot 0 by coincidence,
  and would have produced an outright arity fault the moment M2's own
  construct-on-call tried the same thing with `init` (which is exactly how
  this was caught: the M1 hand-packed acceptance test never exercised a
  real getter/setter call, so the bug was silent until M2's first
  construct-on-call test faulted `init!() takes 1 positional argument(s)
  but 2 were given`).
  - **Fix**: `push_bytecode_call_bind_f` (`src/vm.c`) gained two new
    leading parameters, `wy_uword this_count, const wy_value* this_values`,
    written into `p[0..this_count)` before the ordinary bind (both the
    fast-path straight copy and the slow varargs/kwargs path); `p_count`
    is now `this_count + proto->nparams + fn->ncaps`. Every existing call
    site (ordinary `call`/`call_va`, defer pushes, `wy_vm_call_sync`) now
    passes `0, WY_NULL` explicitly. M1's getattr/setattr virtual-slot
    pushes were corrected to `this_count=1, this_values=&obj` with the
    getter/setter's own declared parameters (none, for getters; the one
    assigned value, for setters) passed as the ordinary `args`/`argc` -
    **not** prepended.
  - This is exactly the plumbing design_c_vm.md §7's "Chosen body pushed
    with `this` values in `P0..P(t-1)`" describes for the general message
    dispatch M3 builds; M3 does not need to touch
    `push_bytecode_call_bind_f` again, only call it correctly with
    `this_count = t` for a multi-dispatch receiver tuple.
- **Construct-on-call** (`construct_on_call_f`, `src/vm.c`, called from
  both `WY_OP_CALL` and `WY_OP_CALL_VA`'s CLASS branches once
  `try_construct_error_f` has ruled out the `error` special case):
  `wy_instance_new_f`, then `wy_class_find_init_f(cls)` - a plain walk up
  `super` for the nearest class that declares one, **not** a multi-dispatch
  ranking. This is not a stub-pending-M3: `pypoc/wypoc/wyrm_eval_parse_tree.py`'s
  `_instantiate_gen` comment says outright *"a native init (which is what a
  compiled class's init is - see wypoc/vm/) runs as itself"* - the
  tree-walker resolves `init` overloads dynamically because it has no
  compile step, but a `.wyc` class's `i` field (wyc-format.md §8.6) is
  already the one resolved constructor; the compiler did the overload
  resolution, not the VM. `wy_class_find_init_f`'s ancestor walk exists
  only to pick up an *inherited* `init` when the class's own `i` is absent,
  matching `_instantiate_gen`'s "the class or any ancestor defines one".
  M2's own Fan-out note sanctioned "stub a single-candidate resolver, M3
  replaces it" for this exact spot; the finding above is that no
  replacement is actually needed - record this explicitly for M3 so nobody
  goes looking for dispatch-ranking work here that doesn't exist.
  - No applicable `init` and the call took no arguments: the instance is
    written directly (no frame pushed), matching the M1 `call` shape.
  - No applicable `init` and the call took arguments: `WY_ERR_ARITY`,
    faulting `"<name>(...) takes no arguments (no applicable 'init')"`.
  - An applicable `init`: pushed via `push_bytecode_call_bind_f` with
    `this_count=1`, `WY_RET_CONSTRUCT`; `fr->aux` is set to the instance
    value on the *newly pushed* frame right after the push succeeds (the
    only place that value can be recorded, since `push_bytecode_call_bind_f`
    has no `aux` parameter and doesn't need one for any other caller).
  - `do_return`'s backfill switch gained the `WY_RET_CONSTRUCT` case:
    `is_error(result[0]) ? result[0] : fr->aux`, written through
    `wy_vm_backfill_f` like any other single-value return.
- **Message identities** (`include/wyrm/message.h`, `src/message.c`):
  `wy_message {object, name, owner, overloads[], overload_count,
  overload_capacity}` (growable array, doubling capacity, same shape as
  `wy_class`'s `slots[]`); `wy_overload {arity, types[16], body}`.
  `wy_module_message_by_name_f` is the shared single-component
  resolve-or-create primitive (`module->message_table`, a `wy_dict*`
  created on first use, keyed by symbol); `wy_module_resolve_message_f`
  wraps it for a `messages[]` index, caching into `.bound` on first read
  and faulting `WY_ERR_NOSUPPORT` for a qualified (`mod::name`) path -
  epic 5's import machinery owns resolving the module a first path
  component names, and nothing in epic 4's corpus emits a qualified
  message path.
- **`reg_msg` (`0xAC`)**: `a0` is a *message* operand (an index into
  `messages[]`, wyc-format.md §5.3), not a register - resolved through
  `wy_module_resolve_message_f`. Appends `(types tuple's items, closure)`
  as a new overload via `wy_message_add_overload_f`; faults on a non-
  FUNCTION closure, a non-TUPLE types operand, or arity over
  `WY_OVERLOAD_MAX_ARITY` (16, matching the class message-map limit).
- **`wy_class_realise_f`'s M1 deviation is resolved**: each `classes[].m[]`
  entry now also resolves (or creates) the message identity by name via
  `wy_module_message_by_name_f` and registers a single-arity overload
  `((cls), FUNCTION)` on it - exactly design §7's "class realisation
  registers each map entry as overload ((cls), FUNCTION)" - and records
  the identity in `cls->msg_map[i].msg` (left `WY_NULL` by M1). This runs
  for every class realised from here on; a class realised under the old
  M1 code before this session's rebuild does not exist in any persisted
  state, so there is nothing to backfill retroactively.
- **GC-safety fix, found while implementing message identities and equally
  applicable to M1's class cache**: `wy_module`'s `children_iter`
  (`src/module.c`) did not trace `module->classes[]` (M1's realised-class
  cache) or `module->message_table` (M2's) at all. Once a `class`- or
  `reg_msg`-produced value drops out of every live register (e.g. after
  its `gset` to a global elsewhere, or - for a `class` op alone before
  M1's tests ever ran one under GC stress - immediately, if nothing else
  references it yet), the cache entry would be the *only* remaining
  reference, and a GC pass could free it out from under
  `wy_class_realise_f`'s `module->classes[class_idx] != WY_NULL` cache
  check, or `wy_module_resolve_message_f`'s `.bound` check, on the next
  call - a stale-pointer read. Fixed by adding two more phases to
  `children_iter_next`: phase 2 walks `module->classes[]`, phase 3 yields
  `module->message_table` (a single object; tracing the dict alone keeps
  every message it holds reachable, so `messages[].bound` does not need
  its own walk). Not separately exercised by a dedicated adversarial GC
  test - this fix mirrors the already-established `wy_function`/module
  tracing convention exactly, and `meson test -C buildDir` (including
  `golden-gcstress`) stayed green throughout, but nothing in this
  session's test suite constructs a class/message with *no* other live
  reference and forces a collection before reading the cache back. Flagging
  for whoever next touches `wy_class_realise_f` or the message table to
  keep in mind if a stress test ever needs one.

### Tests (M2)

- Doctest suite: 193/193 → **197/197 test cases, 15380 → 15413 assertions**
  (net +4 cases / +33 assertions). `meson test -C buildDir`: 5/5, unchanged.
- New (`src/test/test_wvm.cpp`, suite "vm construct-on-call and message
  identities"):
  - "a class with a zero-arg init that sets a slot: calling it produces an
    instance with that slot set" - the epic's own M2 acceptance test,
    verbatim, driven through the real `WY_OP_CALL` path (`gget`+`call`) so
    it exercises `construct_on_call_f` exactly as the corpus does, not just
    the underlying helper.
  - "a class with no init: calling it produces an instance with defaults
    untouched, no fault" - the epic's other acceptance line.
  - "a class with no init faults if called with an argument" - the
    `WY_ERR_ARITY` branch the acceptance text doesn't name explicitly but
    the reference (`_instantiate_gen`) requires.
  - "reg_msg registers an overload, resolved and cached through
    module->message_table" - registers a 0-arity overload via bytecode
    (`closure`+`tuple`+`reg_msg`), then confirms `wy_module_resolve_message_f`
    returns the same identity, `messages[0].bound` is cached, and a second
    `wy_message_add_overload_f` call appends rather than replacing.
- Corpus, manual runs:
  - `test/bytecode/classes.wyc`: `value is not callable` (M1's boundary) →
    **`unknown or unimplemented opcode`** at the first `msg` instruction -
    construct-on-call now runs cleanly to completion; the fault boundary is
    exactly M3's `msg`/multi-dispatch scope, confirmed via `--disasm`
    (`classes.wyc` sends three niladic messages to a freshly constructed
    instance right after `call`).
  - `test/bytecode/samples/eval_error_handling.wy` (epic 3's corpus): now
    **matches its `.out` byte-for-byte** (`diff` clean) - it constructs a
    user class via plain `call` and never sends a message, so M2 alone
    was enough to clear it.
  - `test/bytecode/samples/eval_functions`/`eval_strings`: still match
    (unaffected, sanity-checked after the `push_bytecode_call_bind_f`
    signature change).
  - `test/bytecode/samples/eval_args`/`eval_range`: unchanged
    (`unbound global '__ARGS'`/`'range'` - host globals/epic 5, as before).
  - `test/bytecode/samples/eval_assignments`/`eval_control_flow`/
    `eval_closures`: unchanged, `unknown or unimplemented opcode` at
    `msg`/`reg_msg` (M3 scope; `eval_assignments` in particular exercises
    both construct-on-call *and* a message send, so it's blocked purely on
    the latter now).
  - `test/bytecode/messages.wyc`: unchanged, `msg`/`reg_msg` (M3).
  - `test/bytecode/errors.wyc` (epic 3): unaffected, still matches.

## Landed — M3: dispatch ranking and `super`

- **`wy_class_realise_f`'s M2 deviation is closed**: `msg_map[i].msg` and
  the message-identity registration wired up in M2 are exactly what M3's
  fast path and general ranking both consume - no further change was
  needed there this session, confirming the M2 orientation note's
  prediction.
- **General ranking** (`wy_dispatch_resolve_f`, `src/dispatch.c` - a port
  of `resolve_overload`, `wyrm_eval_parse_tree.py:2854-2891`): for every
  overload of matching arity, a fixed-size `wy_u16[16]` distance vector
  (wildcard = `WY_WILDCARD_DISTANCE`; CLASS = `wy_class_distance_f`, or
  excluded on no match; PTYPE = 0 on an exact tag match, else excluded),
  lexicographically smallest vector wins, tracked with a single
  `best`/`tie` pair - **zero allocation**, confirmed by reading
  `wy_dispatch_resolve_f` end to end: every array involved (`best`,
  per-candidate `dist`) is a fixed C array on the stack, sized
  `WY_DISPATCH_MAX_RECEIVERS` (= `WY_OVERLOAD_MAX_ARITY` = 16). No
  candidates of matching arity/constraints faults `WY_ERR_UNBOUND`; a tie
  among the smallest faults `WY_ERR_AMBIGUOUS` (both new; `fault_msg`
  filled either way, naming the message and receiver count).
- **`super`'s exclusion** (`wy_dispatch_body_distance_f`): recomputes the
  *currently-executing* overload's own distance vector against the same
  receivers by finding the one overload in the message whose `body`
  pointer-matches `fr->dispatch_body`, then re-running
  `wy_dispatch_resolve_f` with that vector as `exclude` (candidates whose
  vector is lexicographically `<=` it are skipped) - exactly "strictly
  after `dispatch_body` in ranking order". Nothing is stored on the frame
  beyond what already existed (`dispatch_msg`/`dispatch_body`,
  epic 3/M1); the exclusion threshold is *recomputed*, not cached, per the
  epic file's own framing ("no reference to check against" - recomputing
  from the frame's actual `this` values is the only source of truth that
  can't drift from what really executed).
- **Single-INSTANCE fast path** (`wy_dispatch_single_instance_f`): walks
  `inst->cls`, `->super`, ... for the nearest ancestor whose `msg_map`
  names the message directly. Used only when the receiver count is 1, the
  receiver is an INSTANCE, and `msg->has_wildcard_or_ptype_arity1` is
  false (a flag `wy_message_add_overload_f` now maintains, `message.h`,
  so the check is O(1) rather than a rescan). **Deviation, in the
  direction of more correctness than the literal spec text**: a fast-path
  miss falls through to the general resolver instead of faulting
  directly. Reason: `msg_map` only contains what a `class` op registered
  for that exact hierarchy; a standalone `reg_msg` constrained to one of
  those same classes (legal per design §7, just not exercised by any
  corpus fixture) would be invisible to the `msg_map` walk alone. The
  fallback costs nothing in the common case (fast path hits) and closes
  that gap in the rare one.
- **`msg`/`msg_va`/`getmsg`/`super`** (`src/vm.c`): `msg`'s/`msg_va`'s
  receiver operand is unpacked via `dispatch_receivers_f` - the value
  itself for ordinary dispatch, or a TUPLE's items for multiple dispatch,
  matching wyc-format.md §6.3's "a value, or a tuple of values". The
  chosen body is pushed via `push_bytecode_call_bind_f` with
  `this_count = n`, then the *newly pushed* frame's `dispatch_msg`/
  `dispatch_body`/`WY_FRAME_FLAG_METHOD` are set right after (same pattern
  M2's `construct_on_call_f` used for `aux`). `getmsg` always resolves
  through the general ranking, never the fast path - matches
  `interp.py`'s `OP_GETMSG` (`put(a0, ev.BoundMessage(receivers,
  ev.resolve_overload(method, receivers)))`), which calls
  `resolve_overload` directly rather than any fast-path variant; a bound
  message is rare enough that there's no hot path worth protecting there.
  `super` reads `t = fr->proto->ndispatch` (epic 1/2's already-loaded
  field, unused until now) and uses `fr->p[0..t)` as both the ranking
  receivers and the pushed body's `this_values`.
- **`wy_bound_msg`** (`include/wyrm/bound_msg.h`, `src/bound_msg.c`):
  `{receiver, msg, body}` exactly as the epic file's M3 scope text names
  it, matching the reference's `BoundMessage(receivers, overload)`
  (`wyrm_eval_parse_tree.py:769`). `getmsg` builds one; calling it (a
  BOUND_MSG callee at `WY_OP_CALL`) re-derives the receivers from
  `bm->receiver` (single or tuple, same helper) and pushes `bm->body`
  with those as `this_values` - this is *not* in the epic file's stated
  M3 scope text (which only says "without calling it"), but
  `messages.wyc`'s own disassembly calls a `getmsg` result two words
  later (word 42's `call`), so it was necessary for the epic's own
  acceptance line ("`test/bytecode/messages.wyc` matches `.out`") to pass
  at all - recorded here since the scope text under-stated what was
  actually required.
- **GC error code added**: `WY_ERR_AMBIGUOUS` (`sys/errors.h`) - no
  existing code fit "multiple equally-specific matches" without
  overloading an unrelated meaning.

### Deferred (not done this session, flagging explicitly)

- **The `__add__`-family/`__bool__`/`__iter__`/`__str__` dunder hooks**
  the epic file's M3 Scope section names (wiring the three-address ops,
  `not`, `iter` and string coercion to dispatch through this milestone's
  machinery on an INSTANCE operand) are **not implemented**. Reason: the
  epic's own Acceptance section only names the two unit tests and
  `messages.wyc` matching, none of which exercise an INSTANCE operand of
  `add`/`not`/`iter`/etc - confirmed by reading `messages.wyc`'s full
  disassembly (its two `mul` instructions operate on plain ints from
  `getattr`, never an INSTANCE). `vm_ops.c`'s binops/unary ops still fall
  through to "unsupported operand types" for an INSTANCE operand,
  unchanged from epic 3. Whoever picks up dunder hooks next has
  `dispatch_body_f`/`push_bytecode_call_bind_f`'s `this_count=1` pattern
  ready to reuse verbatim - the wiring itself is now mechanical, just not
  done.
- **`WY_OP_CALL_VA` does not handle a BOUND_MSG callee** - only
  `WY_OP_CALL` does (see "Landed" above). Nothing in this epic's corpus
  spreads a call onto a bound message (`(*args) ! recv` isn't a thing;
  the only path to a BOUND_MSG value calling with `*args`/`**kwargs`
  would be storing one and later `call_va`-ing it, which no fixture does).
  Cheap to add symmetrically if a future fixture needs it - copy the
  `WY_OP_CALL` branch's shape onto `WY_OP_CALL_VA`'s CLASS-branch pattern.

### Tests (M3)

- Doctest suite: 197/197 → **204/204 test cases, 15413 → 15487 assertions**
  (net +7 cases / +74 assertions, including wiring `classes`/`messages`
  into both `golden` and `golden-gcstress`, +4 of those 7 cases).
  `meson test -C buildDir`: 5/5, unchanged.
- New (`src/test/test_dispatch.cpp`, two suites, exactly the epic's own
  acceptance tests):
  - "dispatch resolve" / "multi-dispatch ranking: mixed-specificity
    receiver pair picks the more specific overload" - two independent
    2-level hierarchies (`A`/`A1`, `B`/`B1`), three overloads of a 2-arity
    message; a receiver pair `(A1-instance, B1-instance)` picks the
    `(A1, B)` overload (distance `(0,1)`) over `(A, B1)` (distance
    `(1,0)`) - position-0 specificity wins the tie at position 1, exactly
    the "mixed-specificity" case the epic asks for. A second case in the
    same suite confirms an arity mismatch (2 receivers against a 1-arity
    overload) faults `WY_ERR_UNBOUND`.
  - "dispatch resolve" / a duplicate-signature overload on the same test
    faults `WY_ERR_AMBIGUOUS` - the "genuine tie faults" half of the same
    acceptance line.
  - "dispatch super" / "3-deep super chain: grandparent, parent and child
    each run, one hop at a time" - grandparent/parent/child each register
    a `mark` overload on their own class; sending `mark` to a child
    instance runs child's body (sets `g3`), which calls `super` (runs
    parent's body, sets `g2`), which calls `super` again (runs
    grandparent's body, sets `g1`) - three side effects, one per hop,
    driven entirely through the real `msg`/`super` opcodes and
    `wy_vm_call_sync`, not a direct call into `dispatch.c`.
- Wired `classes`/`messages` into `test_bytecode_golden.cpp`'s `golden`
  and `golden-gcstress` `TEST_SUITE`s (4 new `TEST_CASE`s total) - both
  now pass, including under `gc_threshold = 0`, which is the first real
  exercise of M2's `module->classes[]`/`message_table` GC-tracing fix
  under stress (nothing before this ran a class/message fixture through
  `golden-gcstress`).
- Corpus, manual runs:
  - `test/bytecode/classes.wyc`, `test/bytecode/messages.wyc`: **both now
    match their `.out` byte-for-byte** - the epic's own exit criterion.
  - `test/bytecode/samples/eval_error_handling.wy`: still matches
    (unaffected).
  - `test/bytecode/samples/eval_assignments.wy`: `unknown or unimplemented
    opcode` → **`no overload of 'resize' matches 1 receiver(s)`** - it
    calls the builtin `resize` via message syntax (`grown!resize(5)`,
    confirmed in `pypoc/wypoc/samples/eval_assignments.wy:14`), and
    `resize` (a plain leaf native, epic 3) has no message overload at all
    yet - that's exactly the "message promotion" fix epic_4.md's own M4
    section names (plain `fn name(...)` becomes the wildcard overload of
    message `name`). Confirms the divergence boundary moved cleanly to
    M4, not a new bug.
  - `test/bytecode/samples/eval_control_flow.wy`, `eval_closures.wy`:
    unchanged, `unknown or unimplemented opcode` - re-disassembled to
    confirm the fault site is `class`/`reg_msg`-adjacent code these two
    samples don't actually reach (they fault earlier, on something else
    unrelated to epic 4); not chased further, matches epic 3's own
    "expected: class message dispatch is epic 4 scope" note and nothing
    in epic 4's M3 scope explains these two specifically - flagging for
    M4/epic 5 to re-check once message promotion lands, in case one of
    them also just needs it.
  - `test/bytecode/samples/eval_args.wy`/`eval_range.wy`: unchanged
    (`unbound global '__ARGS'`/`'range'` - host globals/epic 5).
  - `test/bytecode/errors.wyc` (epic 3): unaffected, still matches.

## Landed — M4: message promotion + a real C-side wildcard bug it caught

- **A pre-existing bug found and fixed before M4's own fix could be
  validated**: M2/M3 used `wy_value_unset()` (Unset - `{ERROR, NULL}`) as
  the wildcard type-constraint sentinel throughout (`wy_dispatch_resolve_f`,
  `wy_message_add_overload_f`'s `has_wildcard_or_ptype_arity1` flag). The
  actual spec (design_c_vm.md §7: *"`reg_msg` appends `(types tuple,
  closure)`; **nil entry = wildcard**"*) says `nil`
  (`WY_TYPE_TAG_NIL`), not Unset - a real, separate value in this VM. No
  M2/M3 test caught it because nothing before M4 ever registered a
  wildcard overload at all (every `classes.wyc`/`messages.wyc` overload is
  class-constrained). Fixed in `src/dispatch.c`/`src/message.c` (the
  wildcard checks now test `.type == WY_TYPE_TAG_NIL`) with a new
  regression test (`test_dispatch.cpp`, "a wildcard overload (nil
  constraint) matches any receiver but loses to a specific one") *before*
  writing the compiler-side fix, specifically because the compiler's own
  `_emit_promotion` needed to emit `lnil`, and getting the C-side sentinel
  wrong would have made that silently do nothing.
- **A second bug, this time in the reference (`pypoc/wypoc/vm/interp.py`,
  not `compiler_bc`)**: `OP_REG_MSG` read a nil constraint back as
  `wyrm_builtins.NIL` (a Python sentinel object), but
  `wyrm_eval_parse_tree.resolve_overload`'s wildcard check tests Python's
  own `None` - `NIL is None` is always false, so a wildcard reg_msg could
  never resolve there either, for the same reason (nothing had ever
  emitted one before this session). This is outside epic_4.md's stated M4
  File list ("the compiler lowering file... plus its test file"), but
  without it the acceptance criterion cannot pass at all: `interp.py` is
  the VM `pytest pypoc/test/test_vm_samples.py -k eval_messages` actually
  runs eval_messages.wy's compiled image through. Fixed by translating
  `wyrm_builtins.NIL -> None` once, at `OP_REG_MSG`'s own signature-tuple
  read - the one place a runtime value becomes a dispatch constraint.
  Recorded here as a deliberate, narrow exception to the stated File list,
  not scope creep: it is a bug fix required to make the stated fix work,
  discovered only by actually running the acceptance test rather than
  reading the compiler change in isolation.
- **`compiler_bc/module.py`**: `_collect_promotions` (a pre-pass over the
  module's top-level body, called from `compile_module` right after
  `_collect_definitions`) finds every name with both a plain `fn name(...)`
  and at least one typed `fn [T, ...] name(...)`, recording every distinct
  arity the typed overloads use (a module can mix arities under one name).
  `_fn_def` calls `_emit_promotion` for any promoted name; `_emit_promotion`
  **recompiles** the plain function's body once per arity via
  `compile_callable(..., dispatch=[own_global_slot] * arity)` rather than
  reusing `_fn_def`'s own already-compiled closure - a message body
  reserves `P0..P(arity-1)` for its receiver(s) *before* its own declared
  parameters (design_c_vm.md §7), so a closure compiled with `dispatch=()`
  (no reserved receiver slots, its own params starting at `P0`) has the
  wrong frame layout to serve as a message body the moment `arity > 0`;
  retrofitting `dispatch` onto an *already-compiled* body would leave every
  existing P-register reference in its bytecode off by `arity` slots.
  `dispatch`'s entries are placeholders (the function's own global slot,
  repeated) - confirmed nothing at runtime reads their *values*, only
  `len(dispatch)` (`wypoc/vm/values.py`'s `BytecodeMethod.__call__`,
  `compiler_bc/verify.py`'s P-frame size check), which is what reserves the
  P slots; the wildcard's actual "no constraint" semantics live entirely in
  the `reg_msg` types tuple (`lnil` per position), not in `dispatch`.
- **Known caveat, not exercised by any sample**: a promoted function that
  declares a `static` gets two independent copies of it (one per compiled
  closure - the plain global binding's and each promoted arity's), since
  `_declare_statics` runs again on the second `compile_callable` call.
  `eval_messages.wy`'s promoted functions (`describe`, `collide`) don't use
  statics. Narrower gap than message promotion itself; not addressed here.
- **`push_bytecode_call_bind_f`'s `this_count`, revisited**: M3 always
  pushed a chosen overload's body with `this_count = n` (the receiver
  count). That's wrong for exactly the case M4 introduces: a promoted
  wildcard's body is compiled with `ndispatch = arity` now (see above), so
  reading it from the pushed function's own `proto->ndispatch`
  (`dispatch_this_count_f`, new in `src/vm.c`) rather than assuming it
  equals the receiver count is the correct general rule - it happens to
  equal `n` for every genuine class-constrained overload and would have
  been `0` for a promoted wildcard if M4's compiler-side fix had reused
  the plain closure unchanged (an approach tried and abandoned - see
  above). Updated at all four push sites: `msg`, `msg_va`, `super`, and
  calling a `BOUND_MSG` value.

### Tests (M4)

- C side: 200/200 → **204/204 test cases** counted at end of M3 already
  included the new wildcard regression test written for this milestone (it
  was added and verified *before* the compiler fix, per the bug-hunting
  order above) - no further C-side test count change this session.
  `meson test -C buildDir`: 5/5, unchanged; full corpus sweep against the
  C VM re-run after the corpus rebuild (see below).
- pypoc side (`pypoc/test/test_compiler_bc.py`): two new tests -
  "message promotion emits a wildcard reg_msg for the plain function"
  (asserts exactly one `reg_msg`/`lnil` pair beyond the typed overload's
  own, both against the same message identity, and that the plain
  function still binds its ordinary global too) and "message promotion
  does not fire without a typed overload" (a lone plain `fn` gets no
  `reg_msg` at all).
- `pytest pypoc/test/` (the full suite, not just the sample sweep): 1518 →
  **1520 passed, 1 skipped** (the two new compiler tests). No regressions.
- `pytest pypoc/test/test_vm_samples.py`: **46/46 passed**, `DIVERGES` is
  now empty - `eval_messages.wy` moved to the ordinary "compiles and both
  runs agree" case, no special-casing left.
- Corpus rebuilt (`pypoc/.venv/bin/python scripts/build_corpus.py`):
  "wrote 41 manifest rows: matches 31, REFUSED 10" - zero DIVERGES, down
  from 1. The only manifest diff is `eval_messages.wy`'s own row
  (DIVERGES -> matches) plus its regenerated `.wyc`/`.wy_a`; nothing else
  moved category, confirmed by diffing `test/bytecode/manifest.txt`.
- Full C-VM corpus sweep (every `.wyc`/`.out` pair the manifest lists, run
  manually against `./buildDir/src/wyrm/wyrm`, not automated yet - epic
  5/6 scope per epic 3's report):
  - `samples/eval_messages.wy`: now matches under the **C VM too**, not
    just pypoc's own reference VM - the epic's compiler-side fix and the
    C-side dispatch machinery agree.
  - **`samples/eval_assignments.wy` is a separate, unrelated gap, not
    fixed by this milestone**: it diverges at `no overload of 'resize'
    matches 1 receiver(s)`, calling the builtin `resize` via `grown!
    resize(5)` (message syntax against a *native*, confirmed in
    `pypoc/wypoc/samples/eval_assignments.wy:14`). This looked like the
    same promotion gap in the M3 report but is not: pypoc's own reference
    VM already handles it, because `wyrm_builtins.install()` registers
    `resize`/`substr`/etc. directly as wildcard message overloads via
    `register_native_method` (`wyrm_eval_parse_tree.py:3141`) in *every*
    module's own message table at module-creation time - a completely
    different, VM-level mechanism from user-code message promotion (which
    is compile-time and module-local). The manifest confirms this:
    `eval_assignments.wy` was already `matches` before this session and
    stayed `matches` after - the divergence is C-VM-only. **The C VM's
    `wy_builtins_new()` has no equivalent of `register_native_method`** -
    its leaf natives are plain global bindings only, never message
    overloads. Fixing this is out of epic 4's stated scope (M4's File
    list names only the compiler lowering fix); flagging for whichever
    epic owns builtins parity next (`src/builtin/builtins.c`).
  - `samples/eval_control_flow.wy`/`eval_closures.wy` (flagged as
    unexplained in the M3 report): re-disassembled after the corpus
    rebuild - both still fault at `unknown or unimplemented opcode`, but
    now confirmed the actual site is `yield` (a coroutine op), not
    anything epic 4 owns. The M3 report's speculation that message
    promotion might also clear these was wrong; corrected here.
  - `samples/eval_args.wy`/`eval_range.wy`: unchanged (`unbound global
    '__ARGS'`/`'range'` - host globals/epic 5).
  - `samples/eval_coroutines.wy`, `coroutines.wy`, `two_module/*`,
    `decorators/*`, `wildcard/paint.wy`: unchanged, all epic 5+ territory
    (coroutines, imports, `getscope`) - not epic 4 scope, not re-chased.
  - `classes.wyc`, `messages.wyc`, `errors.wyc`: unaffected, still match.

## Orientation for epic 5

- Two gaps this session found but did not fix, for whoever picks up
  builtins/import parity:
  - **`resize!`-style native message calls** (`eval_assignments.wy`) need
    the C VM's builtins module to register per-value-method natives
    (`resize`, `substr`, `append`, `expand`, ...) as wildcard message
    overloads, the way `register_native_method` does for the reference -
    likely `src/builtin/builtins.c` calling something like
    `wy_message_add_overload_f` per such native at `wy_builtins_new` time,
    into every module's own `message_table` (or a shared one every module
    falls back to - the exact cross-module sharing mechanism needs
    designing, since `wy_module_message_by_name_f` today only ever
    resolves within one module's own table).
  - **`eval_args.wy`/`eval_range.wy`** need epic 5's own host-global/
    `range` work, unrelated to messages.
- The two "Deferred" items from M3 (dunder hooks for INSTANCE operands on
  arithmetic/`not`/`iter`; `WY_OP_CALL_VA` on a `BOUND_MSG`) are still
  open and still untested by any corpus fixture as of this session's full
  sweep - re-check need before epic 5/6 assumes they're free.
- `dispatch_this_count_f` (`src/vm.c`) is now the single source of truth
  for "how many receiver P-slots does this pushed body actually want" -
  any future code that pushes a message/method frame (epic 5's
  `getscope`-driven dispatch, if any; a native-method-as-message fix per
  above) should call it rather than assuming `this_count == receiver
  count`.
- The wildcard-sentinel bug (Unset vs. nil) is fixed everywhere in this
  repo's C code as of this session; if epic 5/6 adds another type-
  constraint consumer, grep for `WY_TYPE_TAG_NIL` in `src/dispatch.c`/
  `src/message.c` first to see the established pattern before inventing
  a new one.
