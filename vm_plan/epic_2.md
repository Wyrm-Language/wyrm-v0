# Epic 2 — Foundations and core interpreter

## Goal

Turn the loaded image into running code. This epic pays the runtime debts identified in
the survey (design milestone M0: type tags, symbol interning, immutable hashed strings,
new heap object kinds, iterative GC with safepoints and roots, the `wy_frame` record and
two new trampoline states) and then implements the dispatch loop (M2): frames laid out as
`[P][L]` on the single fiber stack, the call-window and nil-backfill rule in one place,
loads, moves, globals, statics, symbols, jumps, arithmetic and comparison on scalars and
strings, zero-capture closures, a builtins module supplying layer-3 names, the
C-to-bytecode entry `wy_vm_call_sync`, a real CLI, and the golden-output test harness.
At the end, `hello`, `hello_1/2/3`, `arith`, `control_flow` and `multiret` run identically
to the reference.

**Exit criterion:**
```
./buildDir/src/wyrm/wyrm test/bytecode/hello.wyc          # prints: Hello World
meson test -C buildDir --suite golden                      # hello, hello_1/2/3, arith, control_flow, multiret pass
```

## Inputs

`vm_plan/epic_1_report.md`. State-scan checklist:

1. Read the report's "Proposed edits to epic_2.md" and apply them to this file first.
2. `meson test` green; record the count. `meson test --suite loader` green.
3. `include/wyrm/module.h` matches design §5 (or the report's amended version); note the
   interning entry point the loader calls.
4. `include/wyrm/opcode.h` is the verbatim pypoc copy (`WYRM_OP_LONG_START 0x80`, `f` at
   bits 8-15); `include/wyrm/opcode_names.h` exists.
5. `src/fiber.c:81-129` `wy_fiber_exec_f` and `fiber.c:166-231` frame push/pop/tail-call
   are unchanged since the survey; `include/wyrm/stack.h:10-42` documents the reserved
   result-slot layout.
6. `src/gc.c:82` mark is still recursive; `wy_gc_arena` is an intrusive list; roots are
   built in `src/context.c:231-250`.
7. `src/machine.c` symtab is still the 8 KB scaffold; `include/wyrm/symtab_entry.h`
   defines `wy_symbol` as `const char*`.
8. `include/wyrm/value.h` `wy_value_uword` still tags `WY_TYPE_TAG_WORD` (bug).
9. `include/wyrm/string.h` hash is still the additive byte sum; `wy_util_fnv1a_buffer`
   exists in `include/wyrm/util.h:43`.
10. `test/bytecode/manifest.txt` lists the six fixtures in the exit criterion as `matches`
    and their `.out` files exist (`multiret.out` is the copied `.vm.out`).
11. `src/wyrm/main.c` is the epic-1 CLI (`--sections`, `--disasm`).
12. Confirm which builtins the six fixtures use: `grep -h "free" test/bytecode/{hello*,arith,control_flow,multiret}.wy_a`
    (expected: `println`, maybe `print`, `str`, `len`, `int`).

## Context to load

Read (≈35k tokens):
- `vm_plan/README.md`; `vm_plan/design_c_vm.md` §0–§2, §4, §6, §8, §9, §10 rows M0/M2.
- `AGENTS.md`, `doc/EXPLAINER.md`, `doc/vm_impl.md` (epic-1 version).
- `pypoc/doc/wyc-format.md` §1 machine model, §5 encoding, §6.1–6.2 and the call/return/
  closure rows of §6.3, §7.1 step 4 (builtins fill) and §7.2.
- `include/wyrm/{fiber.h,stack.h,exec_fn.h,context.h,value.h,primitive.h,string.h,
  gc.h,gc_flags.h,work_area.h,object.h,module.h,util.h}`.
- `src/{fiber.c,context.c,gc.c,machine.c,string.c,vm.c}`.
- `src/test/test_fiber.cpp` (the reservation-convention tests you must keep green),
  `src/test/test_wgc.cpp`, `src/test/test_common/*.h`.
- `pypoc/wypoc/vm/interp.py:270-281` (`backfill`), `:372-476` (compact/wide widening,
  jumps), `:134-145` (BINOPS table), `:191-206` (calls, ContextualBuiltin).
- `pypoc/wypoc/vm/frame.py:72-145` (`build_pframe`).
- `pypoc/wypoc/wyrm_builtins.py:788-859` (`install`), and the bodies of `println`,
  `print`, `str`, `int`, `float`, `bool`, `len` only.
- `pypoc/wypoc/wyrm_eval_parse_tree.py`: `BINOPS` and the `_binop_*` helpers for `+ - * /
  % ** & | ^ << >> == != < <= > >=` (grep `BINOPS =` and read the referenced functions),
  and `_to_str`/formatting of ints, floats, bools, nil, strings (what `println` prints).
- `test/bytecode/{hello,arith,control_flow,multiret}.wy_a` — **grep only** for the
  `SECTION code` lines when debugging a fixture; never load whole files.

Grep only:
- `pypoc/wypoc/compiler_bc/opcodes.py` for any operand detail not in the doc.
- `pypoc/test/test_vm_exec.py` for hand-assembled body examples to mirror in
  `packed_ops.h`.

## Assumptions

- Epic 1 left `src/vm.c` a stub and `wy_vm_exec_b_code` unused; nothing else calls into
  the VM *(verify in scan)*.
- The reservation-slot convention in `stack.h`/`fiber.c` is kept for native frames and
  for bytecode called from C (design §1.3–§1.4); `test_fiber.cpp` keeps passing with
  `wy_fiber_frame` renamed/extended to `wy_frame` *(verify in scan)*.
- Fixed-size `frame_memory` array of `wy_frame` (default 1024 in tests) is acceptable;
  stack overflow is a fault, not a realloc *(verify in scan: `wy_fiber_create` args)*.
- `wy_value` stays a two-word `{tag, primitive}` struct; no NaN-boxing *(verify in scan)*.
- Floats are stored as `double` in `wy_primitive.fp`; `f32` immediates widen. Output
  formatting must match Python `repr`-style for the goldens (`3.4`, `1.0`, `2.5e-05`)
  *(verify in scan: grep the `.out` files for floats)*.
- Ints are `wy_word` (register width); `i8`/`i32` sign-extend. Division semantics
  (`/`, `%` on ints, division by zero yields an error value) follow
  `wyrm_eval_parse_tree.BINOPS` *(verify in scan: read those helpers)*.
- The six target fixtures need no captures, no tuples/lists/dicts, no classes, no
  imports *(verify in scan: grep their `.wy_a` for `closure … caps`, `tuple`, `class`,
  `import`; if `multiret` uses `tuple`, pull the `tuple` opcode forward into M4)*.
- `println` joins arguments with a single space and appends `\n`; `print` has no
  newline *(verify in scan against `wyrm_builtins.py`)*.

## Milestones

### M1 — Type tags, values, symbols, strings, new heap kinds
**Scope** (design §4, §6)
- `include/wyrm/primitive.h`: the tag enum from design §4 (`BOOL`, `FLOAT`, `PTYPE` before
  `GC_PATH_START`; `TUPLE`, `LIST`, `BYTES`, `FUNCTION`, `NATIVE`, `INSTANCE`, `MESSAGE`,
  `BOUND_MSG`, `COROUTINE`, `ITER` after). `wy_primitive` gains `fp` (double) and `flag`.
- `include/wyrm/value.h`: constructors/accessors for every tag; fix `wy_value_uword`;
  `wy_value_is_error` (ERROR tag now; INSTANCE-with-error-class added in epic 3);
  `wy_value_truthy` (nil/false/0/0.0/""/Unset → false; everything else true; instances
  come in epic 4).
- `include/wyrm/symtab.h` + `src/symtab.c`: interning hash per design §6 keyed on the
  31-codepoint prefix; replaces `machine.c`'s scaffold; `wy_machine` owns one;
  `wy_context_intern(ctx, utf8, len)` is the entry point the epic-1 loader already calls.
  Tests: identity of two interns with the same 31-codepoint prefix and different tails;
  growth past 1024 symbols.
- `include/wyrm/string.h` + `src/string.c`: immutable inline `{object; len; hash; data[]}`,
  FNV-1a at creation, `wy_string_concat`, `wy_string_eq`, `wy_string_cmp`, UTF-8 length
  helper. Update `test_wstring.cpp`.
- New objects with headers, allocators and GC `children_iter`: `wy_tuple`, `wy_list`,
  `wy_bytes` (resizable, `len/capacity/data`), `wy_error_obj`, `wy_function` (module,
  proto, caps[]), `wy_native` (leaf/exec union), each in its own `include/wyrm/*.h` +
  `src/*.c` per `doc/vm_impl.md`'s one-header-one-source rule. Epic 1's placeholder for
  binary statics is replaced by `wy_bytes`.

**Files** new: `include/wyrm/{symtab.h,tuple.h,list.h,bytes.h,error.h,function.h,
native.h}`, `src/{symtab.c,tuple.c,list.c,bytes.c,error.c,function.c,native.c}`,
`src/test/test_symtab.cpp`; changed: `primitive.h`, `value.h`, `string.h`, `string.c`,
`machine.h`, `machine.c`, `module.c` (statics use FLOAT/BYTES), `test_wstring.cpp`,
`test_machine.cpp`, `src/meson.build`.

**Acceptance** `meson test` green; `test_cwyrm --test-case="*symtab*"` covers prefix
identity and growth; loader suite still green with FLOAT and BYTES statics.
**Model** Sonnet (design gives the structs).
**Fan-out** yes, after `primitive.h`/`value.h` land (Opus or the session does those
first): subagent A symtab + strings; subagent B the six new object kinds. Disjoint files.

### M2 — GC: iterative mark, roots, safepoint trigger
**Scope** (design §8)
- `src/gc.c`: gray worklist replacing the recursion at `gc.c:82`; abandon-collection path
  when the worklist cannot grow.
- `wy_context`: `fibers` list, `builtins`, `module_table` as roots; `roots[64]` with
  `wy_context_root_push/pop`; `gc_pressure`/`gc_threshold`; `wy_context_gc_alloc`
  wrapper used by every object allocator; `wy_context_gc_safepoint`.
- Tests: 10k-element pair chain marks without recursion (stack-depth-insensitive; build
  with `-fstack-usage` optional); `gc_threshold = 0` mode exists and the existing tests
  pass under it; root push/pop protects an object across a forced collection.

**Files** changed: `src/gc.c`, `include/wyrm/gc.h`, `include/wyrm/context.h`,
`src/context.c`, `src/test/test_wgc.cpp`, `src/test/test_context.cpp`.
**Acceptance** `test_cwyrm --test-case="*gc*"` green including the 10k chain.
**Model** Sonnet.
**Fan-out** none (touches shared headers).

### M3 — `wy_frame`, trampoline states, native bridge
**Scope** (design §1.1, §1.3, §1.4)
- `include/wyrm/frame.h`: `wy_frame` per design; `wy_fiber` uses it for `frame_memory`;
  existing native-frame push/pop/tail-call rewritten over the new record with **no change
  in behaviour** (`test_fiber.cpp` unchanged and green, apart from type renames).
- `exec_fn.h`: `WY_EXEC_SWITCH`, `WY_EXEC_FAULT`; `wy_fiber_exec_f` handles FAULT by
  returning `WY_ERR_FAULT` with `fiber->fault` set; `wy_context_exec` loops on SWITCH
  (no coroutines yet, but the loop shape lands now).
- `wy_native` leaf call helper and the exec bridge (`WY_PHASE_AWAIT_NATIVE` prologue)
  written as functions in `src/vm_call.c`, unit-tested with a hand-built bytecode frame
  before the loop exists.
- `include/wyrm/vm.h`: declare `wy_vm_run`, `wy_vm_call_sync`, `wy_vm_call_continue`.

**Files** new: `include/wyrm/frame.h`, `src/vm_call.c`, `src/vm_internal.h`; changed:
`fiber.h`, `fiber.c`, `exec_fn.h`, `context.c`, `vm.h`, `test_fiber.cpp` (renames only).
**Acceptance** `meson test` green; a new test pushes a bytecode frame by hand, calls a
leaf native into its window, and checks backfill for `nres` 0, 1, 3 against `count` 2.
**Model** Opus (this fixes the convention every later opcode relies on).
**Fan-out** none.

### M4 — The dispatch loop and core opcodes
**Scope** (design §2)
- `src/vm.c`: `wy_vm_run` with the `reload:` pattern, phase prologue, GC safepoint check,
  1-or-2-word decode, `switch` on `op`; wide forms widen and `goto` the compact case.
- Opcodes: `noop`, `trap` (fault with code; full defer semantics in epic 3), `return`
  (no defers yet: `fr->defers == NULL` fast path), `lnil`, `lbool`, `lunset`, `i8`/`i32`,
  `f32`, `move`, `gget`/`gset` (fault on Unset *free* slot: use `free_names` membership),
  `lsym`, `lconst`, `neg`/`inv`/`not`, `jf`/`jt`/`jerr`/`jnerr`/`jmp`, `add`…`bxor`,
  `eq`…`ge`, `cmp3`, `call` (FUNCTION, NATIVE leaf, NATIVE exec), `closure` with
  `ncaps == 0` (captures in epic 3), `tuple` only if `multiret` needs it (see Assumptions).
- `src/vm_ops.c`: `wy_binops[]` for WORD/UWORD/FLOAT/STR/BOOL per `BINOPS` semantics;
  comparison across int/float; `add` on STR concatenates; division by zero returns an
  error value; `pow` on ints; shifts; `cmp3`. Unknown combinations return an error value
  (not a fault) — record the exact rule from the reference in the report.
- Faults: `fault:` label sets `fb->fault` to a `wy_error_obj` carrying opcode and word
  offset, unwinds frames (no defers yet), returns `WY_EXEC_FAULT`.
- Parameter binding: fast path only (`argc == nparams`, no flags); anything else faults
  with "binding not supported until epic 3".
- `packed_ops.h` fixture generated by `pypoc/tools/generate_c_fixtures.py` (design §9a)
  covering every opcode in this milestone in compact and wide forms; `test_wvm.cpp`
  drives them through `wy_module_new_synthetic` + `wy_vm_call_sync`.

**Files** changed: `src/vm.c`; new: `src/vm_ops.c`, `src/test/fixtures/packed_ops.h`,
`pypoc/tools/generate_c_fixtures.py`, `src/test/test_wvm.cpp` (rewritten).
**Acceptance** `test_cwyrm --test-case="*vm*"` green: every listed opcode has a compact
and (where paired) a wide case; jump offsets forward and backward; backfill cases;
`trap 0` produces `WY_ERR_FAULT` with the right word offset.
**Model** Opus for the loop skeleton, `call`/`return`, and the fault path; Sonnet for
the opcode bodies once the skeleton is in.
**Fan-out** after the skeleton: subagent A loads/moves/globals/jumps + their fixture
cases; subagent B `vm_ops.c` arithmetic/comparison + cases. Disjoint files; the session
integrates the `switch` arms.

### M5 — Builtins module, `wy_vm_call_sync`, CLI, golden harness
**Scope** (design §5 builtins, §1.4, §9b)
- `src/builtin/builtins.c`: a `wy_module` with `state = BUILTIN` whose `exports` come from
  a static table; leaf natives `println`, `print`, `str`, `int`, `float`, `bool`, `len`
  (strings only for now), `nil` value, `exit`/`end` (set a context stop flag; the loop
  returns DONE). Output goes through a context hook `ctx->io.write(ctx, bytes, len, ud)`
  defaulting to stdout on the hosted port. `println` formatting per the reference
  (`_to_str`): ints, floats (shortest round-trip), bools `true`/`false`, `nil`, strings
  raw. Move `src/builtin/sys/wsys.c` into the module convention (`builtin_sys` dict in
  meson) or delete it if unused.
- Loader step 4 completion: `wy_link_fill_from_builtins` (layer 3) in a new `src/link.c`
  with `fill_layer`/`fill_source` bookkeeping (design §5), called at the end of
  `wy_module_load_image`.
- `wy_vm_call_sync` and `wy_module_run_init` (design §1.4, §5).
- `src/wyrm/main.c`: after loading, run init via `wy_module_run_init`; exit code 0, or 1
  with the fault printed as `file.wyc: fault at word N: <message>` (line numbers from the
  `debug` section are a nice-to-have; skip unless trivial).
- `src/test/test_bytecode_golden.cpp`: for each manifest row with status `matches` **and**
  listed in an in-test `ENABLED` set (this epic: the six fixtures), capture the hook
  output and compare to `.out`; rows not enabled are `SKIP`ped with the name so the
  suite reports progress; suite name `golden`. A second run with `gc_threshold = 0` for
  the enabled set (`golden-gcstress`).

**Files** new: `src/builtin/builtins.c`, `include/wyrm/builtins.h`, `src/link.c`,
`include/wyrm/link.h`, `src/test/test_bytecode_golden.cpp`; changed: `src/module.c`,
`src/context.c`/`.h` (io hook), `src/wyrm/main.c`, `src/builtin/meson.build`,
`src/test/meson.build`.
**Acceptance** the exit criterion; `meson test --suite golden-gcstress` green for the six.
**Model** Sonnet.
**Fan-out** subagent A builtins + formatting (acceptance: a unit test printing each
value kind); subagent B `link.c` fill + `wy_vm_call_sync` + `main.c` (acceptance: hello
runs). Disjoint files; golden harness by the session.

## Out of scope / deferred

- Captures (`ncaps > 0`), tuples/lists/dicts/pairs, iteration, `unpack`, `in`, `is`,
  `call_va`, defaults/varargs/kwargs binding, defers, error class family → epic 3.
- Classes, instances, messages, dunder hooks → epic 4. Imports, coroutines → epic 5.
- Computed-goto dispatch, inline caches → epic 6. TCC/clang matrix → epic 6 (but do not
  use GNU extensions here).
- `return_cps` and `new_primitive`: trap with a distinct code.

## Risks

- **Reservation vs. window bridge subtlety** (results of a native exec call arrive in
  reverse order in reserved slots): mitigate with the M3 unit test before any opcode
  depends on it.
- **Float formatting mismatches** with the goldens: mitigate by implementing
  shortest-round-trip early (M5) and diffing `arith.out` first.
- **Integer semantics** (width, overflow, `/` on ints, `%` sign): pin from the reference
  in M4 tests; record the rule in `doc/vm_impl.md`.
- **Frame array exhaustion** on deep recursion in fixtures: fault cleanly; test it.
- **Scope creep into epic 3** when `multiret` or `control_flow` use an unexpected opcode:
  pull only that opcode forward and note it in the report.

## Report

Beyond the README template: the final tag enum; the integer/float/division rules pinned
and where they are tested; whether any epic-3 opcode was pulled forward; the exact
builtin list and formatting rules implemented; timing of `hello` and `arith` under
`wyrm` (wall-clock, informal) as the first performance baseline; design_c_vm.md sections
amended (especially §1.3 bridge and §2 fault path if they changed); proposed edits to
`epic_3.md` (e.g. whether `wy_dict` was touched, whether `tuple` already exists).
