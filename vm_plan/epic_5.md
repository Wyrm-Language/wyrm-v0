# Epic 5 — Modules, imports, coroutines, CLI

Implements design_c_vm.md M6 + M7. Prerequisite: epic 4 (M5) landed and its report exists.

## Goal

Make a multi-module program work end to end: `import`/`import_star` with real dependency
loading, the three-layer name-fill rule, cycle detection, `getscope`/`setscope`, a hosted
`-I` search path, `__ARGS`/`__name__`, native `std::io`, and coroutines (their own fiber,
`yield`/`yield_from`, `next`/`send`, `StopIteration`). Ends with the epic's own full
conformance sweep: every fixture and sample in `test/bytecode/manifest.txt` in exactly one
of matches/REFUSED/DIVERGES.

**Exit criterion:**
`meson test -C buildDir --test-case="two_module*"` and `--test-case="wildcard*"` pass;
`./buildDir/src/wyrm/wyrm -I test/bytecode/two_module test/bytecode/two_module/report.wyc`
matches `report.out`; the manifest sweep test (`test/bytecode/manifest.txt` driven) reports
zero unexplained category changes.

## Inputs

- `vm_plan/epic_4_report.md` — read first, especially its notes on the qualified-name
  first-component resolver built for message paths (this epic's `getscope` and `import`
  both reuse or parallel it) and the dispatch-cache-table deferral.
- State-scan checklist:
  1. Confirm `wy_module` has `fill_layer`/`fill_source`, `wildcards[]`, `exports`,
     `free_names` fields as in design §5 — read `include/wyrm/module.h`.
  2. Confirm the qualified-name-resolution helper epic 4 built for message identities
     (`getscope`-shaped walk) — read the file epic 4's report names.
  3. Confirm `getattr`/`setattr` vs. `getscope`/`setscope` stay two separate namespaces in
     the opcode dispatch (never share a lookup, wyc-format §6.3) — grep `OP_GETSCOPE`/
     `OP_SETSCOPE` in `src/vm.c`.
  4. Confirm epic 1's toolchain produced `test/bytecode/two_module/` and
     `test/bytecode/wildcard/` fixtures — `ls test/bytecode/`.
  5. Confirm the `wy_fiber`/`wy_context_exec` fiber-switch loop (design §3) is a stub from
     earlier epics or must be built fresh here — grep `WY_EXEC_SWITCH` in `src/fiber.c`,
     `src/context.c`.
  6. Run `meson test -C buildDir` and record the count for "before".
  7. Confirm the hosted platform layer (`src/platform/hosted/`) exists and what it already
     provides before adding `import_fs.c`.
  8. Confirm `pypoc/wypoc/corelib/std/io.wy` is the reference `std::io` surface
     (`open/read/write/lseek/dup2/close/flush` per `wyrm_io.py:40-105`).
  9. Confirm epic 4's message-promotion pypoc fix is committed and the manifest reflects
     `eval_messages.wy` as matches, not DIVERGES, before this epic adds its own entries.
  10. Confirm whether `range` exists anywhere yet — grep `range` in `include/wyrm/*.h`,
      `src/builtin/*.c`; this epic's M4 supplies it if not.

## Context to load

1. `vm_plan/design_c_vm.md` §3 (coroutines), §5 (module and linking) — read in full, ~4k
   tokens; this epic's primary spec.
2. `vm_plan/epic_4_report.md` — read, ~1-2k tokens.
3. `pypoc/doc/wyc-format.md` §7.1-§7.3 (load sequence, three-layer fill, resolving one
   entry) — read in full, ~2.5k tokens.
4. `pypoc/wypoc/vm/module.py:196-263` (`fill`, `global_fault`, `bound`) and
   `pypoc/wypoc/vm/link.py` in full (263 lines: `resolve`, `_message`, `_first_component`,
   `_from_wildcards`, `fill_from_builtins`, `fill_from_import`, `fill_from_wildcard`,
   `scope_member`) — read, ~3k tokens.
5. `pypoc/wypoc/vm/imports.py` in full (`import_path`, `register_wildcard`, cycle check via
   `ev.check_import_cycle`) — read, ~1.5k tokens.
6. `pypoc/wypoc/vm/interp.py` — grep only: `OP_IMPORT`, `OP_IMPORT_STAR`, `OP_GETSCOPE`,
   `OP_SETSCOPE`, `OP_YIELD`, `OP_YIELD_FROM` cases (~470-600).
7. `pypoc/wypoc/vm/values.py:99-145` (`BytecodeCoroutine`, `_CompiledBody`) and
   `pypoc/wypoc/wyrm_eval_parse_tree.py` grep-only: `class CoroutineInstance` (~479-560),
   `_advance_raw` (~553-604), `next_`/`send_` (~625-645), `_yield_value`, `_yield_from`,
   `StopIteration` construction (~3238-3245) — ~2.5k tokens.
8. `pypoc/wypoc/wyrm_io.py` in full (`wyrm_open/read/write/lseek/dup2/close/flush`,
   `install`) and `pypoc/wypoc/corelib/std/io.wy` — read, ~1.5k tokens.
9. `pypoc/test/test_vm_samples.py:38-58` and `pypoc/test/test_vm_run.py:107-141` — read,
   ~1k tokens; the exact REFUSED/DIVERGES/RUNNABLE lists M6's sweep reconciles against.
10. Current C: `include/wyrm/module.h`, `src/module.c`/`src/link.c` (if epic 1/2 stubbed
    them), `include/wyrm/coroutine.h` (new) — read what the scan flags, ~2k tokens.

## Assumptions

- Epic 1's toolchain produced `test/bytecode/two_module/{geometry,report}.wyc` and
  `test/bytecode/wildcard/{palette,paint}.wyc` fixtures per design §9's layout
  *(verify in scan)*.
- The hosted import hook resolves `a::b` to `<dir>/a/b.wyc` under each `-I` root
  (image-first, like the reference `import_path`), but unlike the reference has no
  fallback to compiling `.wy` source — no compiler exists in this repo yet (Phase C). A
  missing `.wyc` is a hard load failure *(verify in scan against design §5's hook)*.
- Message-identity qualified-path resolution (epic 4) and `getscope`'s `::`-path
  resolution (this epic) share the same "walk the first component like `getscope`" logic,
  but are two distinct call sites *(verify in scan against epic 4's actual code)*.
- Coroutines run on **fibers**, not OS threads (design §3) — a deliberate divergence from
  the Python reference (each `CoroutineInstance` on its own OS thread with two
  `threading.Event`s), since AGENTS.md's no-recursion/no-unguarded-allocation rules leave
  no such primitive available here *(verify in scan: no partial thread-based prototype
  exists from an earlier epic)*.
- `__ARGS`/`__name__` are ordinary global slots filled by the loader/CLI before init runs,
  not opcodes of their own *(verify in scan: grep `src/module.c`, `src/wyrm/main.c`)*.
- `range` ships as a compiled-`.wy` prelude (embedded as `.c` arrays, like `hello_1.c`)
  rather than a native builtin, since it's easiest expressed as a first-class iterable
  class with `__iter__` once classes work (epic 4) *(verify in scan: if design_c_vm.md or
  the plan implies a native leaf type instead, correct here)*.

## Milestones

### M1 — Import, import_star, three-layer fill

**Scope**
- `wy_link_fill(module, slot, value, layer, source)`: stronger layer wins; equal layer,
  different source, non-identical value → ambiguity marker (port of `module.py:fill`,
  lines 196-219, exactly, including the "already correct → no-op" short-circuit).
- `wy_link_fill_from_builtins` (layer 3, at load, bare names, `link.py:154-167`);
  `wy_link_fill_from_import(module, path, dep)` (layer 1: exact spelling + every free name
  with prefix `path::`, walking the remainder like `getscope`, `link.py:168-192`);
  `wy_link_fill_from_wildcard` (layer 2, only `dep->exports` minus excepts,
  `link.py:193-211`).
- `import` (`0x47`/`0xC7`): path is a `::`-joined static string; split, resolve via the
  import hook (below), publish-before-init, push init frame on the **current fiber** with
  `WY_RET_IMPORT`, `aux` = target, `ret_dst = reg(dst)` → `goto reload`; on return,
  `state = READY`, layer-1 fill, write the module value.
- `import_star` (`0xB5`): register the wildcard (target + except-symbol window), push with
  `WY_RET_IMPORT_STAR` if the target isn't READY yet, else layer-2 fill directly.
- Cycle detection: an import reaching a module already `INITIALISING` faults `WY_ERR_CYCLE`
  naming both modules (wyc-format §7.1 step 6, `imports.py`'s `check_import_cycle`).
- `getscope`/`setscope` (`0xB3`/`0xB4`): MODULE → exports; CLASS → statics; FUNCTION →
  module exports; else fault. Distinct lookup path from `getattr`/`setattr` (wyc-format
  §6.3's explicit warning).

**Files**
- New: `include/wyrm/link.h`, `src/link.c`.
- Changed: `include/wyrm/module.h` (fill_layer/fill_source/wildcards fields if not already
  present), `src/vm.c` (`import`, `import_star`, `getscope`, `setscope` cases),
  `include/wyrm/frame.h` (`WY_RET_IMPORT`/`WY_RET_IMPORT_STAR` if not present).

**Acceptance**
Hand-packed test: two synthetic modules, `a` imports `b`, `b`'s export fills `a`'s free
slot; a second hand-packed test where `a` imports `b` and `b` imports `a` faults
`WY_ERR_CYCLE` naming both. `test/bytecode/two_module/report.wyc` matches `.out`.

**Model:** Opus — design-heavy (fill-layer state machine plus fiber-inline init-frame push
is intricate around re-entrancy; one of two Opus milestones in this epic).

**Fan-out:** none — fill layers, import, and cycle detection are one coherent state
machine; splitting risks inconsistent ambiguity-marker handling.

### M2 — Hosted import hook, `-I`, CLI, `__ARGS`/`__name__`

**Scope**
- `wy_import_hook` typedef on the context (design §5): `(ctx, path, len, out_bytes,
  out_len, ud) -> wy_error`.
- `src/platform/hosted/import_fs.c`: resolves `a::b` → `<dir>/a/b.wyc` for each `-I` root
  in order, first match wins; no source-compile fallback (per Assumptions).
- `src/wyrm/main.c`: real CLI accepts repeated `-I dir`, loads and runs the given `.wyc`,
  sets `__ARGS` (remaining argv after the script path, as a list of strings) and
  `__name__` ("__main__" for the root module) as global slots before init runs, per however
  the loader exposes them (Assumptions notes these are plain slots, not opcodes).

**Files**
- New: `src/platform/hosted/import_fs.c`.
- Changed: `src/wyrm/main.c`, `include/wyrm/context.h` (`import_hook` field if absent).

**Acceptance**
`./buildDir/src/wyrm/wyrm -I test/bytecode/two_module test/bytecode/two_module/report.wyc`
succeeds without a copy of `geometry.wyc` in `report.wyc`'s own directory (confirms `-I`
resolves cross-directory). A script reading `__ARGS` prints them back for
`wyrm script.wyc a b c`.

**Model:** Sonnet (mechanical file-path construction and CLI argv parsing).

**Fan-out:** none (small, depends on M1's hook signature).

### M3 — Coroutines

**Scope**
- `wy_coroutine` object (design §3): `{fiber, resumer, delegate, outer, delegate_dst,
  yield_base, state, result}`; states CREATED/SUSPENDED/RUNNING/DONE.
- `call` on a FUNCTION with the coroutine flag bit (bit 0 of `functions[i].f`, wyc-format
  §8.5) constructs a coroutine + its own `wy_fiber` (`co_stack_len`/`co_frame_count`
  defaults 512/32) instead of running the body; writes the COROUTINE value; nothing
  executes yet.
- `next(co)`/`send(co, v)` as exec natives: DONE → StopIteration error value; `send` on
  CREATED → error; else write `v` into the suspended frame's yield slot if SUSPENDED,
  switch `ctx->current_fiber` to `co->fiber`, return `WY_EXEC_SWITCH`.
- `yield base, count` (`0xA7`): pack the window (0 → nil, 1 → value, n → tuple), save `ip`,
  record `yield_base`, go SUSPENDED, hand the value to `co->resumer`, switch back,
  `WY_EXEC_SWITCH`.
- Body return with `WY_RET_COROUTINE`: `result = result[0]`, DONE; if delegated (`outer`
  set), write to `*outer->delegate_dst` and switch to `outer->fiber`; else StopIteration to
  resumer.
- `yield_from dst, sub` (`0xB0`): link `delegate`/`outer`/`delegate_dst`, resume `sub`.
  `next`/`send` follow `while (co->delegate) co = co->delegate`.
- `wy_context_exec`: driver loop re-reading `ctx->current_fiber` after every
  `WY_EXEC_SWITCH` so a switch doesn't require the C call stack to grow.
- GC: coroutine children = fiber, resumer, delegate, outer, result; fiber children walk
  every stack value and each frame's module/defers/dispatch_body/aux; roots include
  `ctx->current_fiber` and the context's fiber list, so an abandoned suspended coroutine is
  collected along with its stack.

**Files**
- New: `include/wyrm/coroutine.h`, `src/coroutine.c`.
- Changed: `src/context.c` (`wy_context_exec` loop), `src/gc.c` (fiber/coroutine
  `children_iter`), `src/builtin/builtins.c` (`next`/`send` exec natives).

**Acceptance**
`test/bytecode/coroutines.wyc` matches `.out`. Hand-packed test: create a coroutine, `next`
it twice observing two yields, then twice more observing `StopIteration` both times
(idempotent-after-done). Second hand-packed test: create a suspended coroutine, drop all
references, force a GC cycle, assert its fiber's memory is reclaimed — the "abandon
suspended coroutine" case design §3 calls out explicitly.

**Model:** Opus — no C precedent for fiber-based coroutines here; the reference uses OS
threads, which this VM cannot, so the switch-loop and GC-of-suspended-fibers design is
first-of-kind.

**Fan-out:** none — switch loop, yield/yield_from, next/send are one coupled state machine.

### M4 — `std::io`, `range` prelude

**Scope**
- Native `std::io`: `open(path, mode)`, `read(handle, size)`, `write(handle, data)`,
  `lseek(handle, offset, whence)`, `dup2(old, new)`, `close(handle)`, `flush(handle)`, as
  exec natives (file I/O may block/error, matching design §1.3's leaf-vs-exec split) backing
  `pypoc/wypoc/corelib/std/io.wy`'s wyrm-level wrapper, importable as `std::io`.
- `range` prelude: a small wyrm-level class with `__iter__`/`__bool__` as needed, compiled
  and embedded the way `hello_1.c` is, loaded as a builtin-adjacent module so
  `for i in range(n):` works without a user-level import.

**Files**
- New: `src/platform/hosted/io_native.c`, embedded prelude source under `test/bytecode/` or
  a new `src/prelude/` per whatever epic 1/2 chose for `hello_1.c`'s embedding convention.
- Changed: `src/builtin/builtins.c` (register `std::io` module, `range` prelude at
  context init).

**Acceptance**
`eval_io` sample runs and matches the tree-walker's output for a script that opens, writes,
reads back, and closes a temp file. `eval_range` sample matches.

**Model:** Sonnet (spec-driven ports of an exact Python reference: `wyrm_io.py:40-105` for
the natives, ordinary Python-range semantics for `range`).

**Fan-out:** 2 Sonnet subagents, disjoint files, fully independent — (a) `std::io` natives
(`src/platform/hosted/io_native.c`); (b) `range` prelude (prelude directory).

### M5 — Decorators fixture

**Scope**
- Confirm `test/bytecode/decorators/{declib,decorated}.wyc` (self-contained per
  llm-bytecode.md §9, unaffected by the sample's module-level-`var` expansion limitation)
  runs correctly given M1-M4's linking and dispatch. No new opcode work expected; this is
  verification plus a small bug-fix budget if the decorator-expanded code exercises a
  linking or dispatch path the other milestones' unit tests missed.

**Files**
- Likely none, or small fixes surfaced by the fixture in `src/link.c`/`src/dispatch.c`.

**Acceptance**
`test/bytecode/decorators/decorated.wyc` matches its `.out`.

**Model:** Sonnet (verification-shaped; escalate to Opus only if a genuine new design
question surfaces, and note that escalation in the report).

**Fan-out:** none.

### M6 — Full corpus sweep

**Scope**
Run the entire `test/bytecode/manifest.txt` sweep (every fixture and every non-REFUSED
sample from `pypoc/wypoc/samples/*.wy`) against the C VM. Update the manifest so every
entry is in exactly one of matches/REFUSED(reason)/DIVERGES(reason), matching the pypoc
`REFUSED`/`DIVERGES` categorisation where the same sample appears there, and add C-VM-only
entries for anything the Python reference doesn't need to categorise (e.g. a sample that
matches under the tree walker but a real gap remains on the C side — write the reason).

Known categorisation to carry over from `pypoc/test/test_vm_samples.py:38-58`:
- **REFUSED** (compiler refuses, never produces a `.wyc`, cannot appear as a C-VM fixture):
  `basics.wy`, `eval_defer_with_do.wy`, `lexical.wy` (`with` removed from the language);
  `classes.wy`, `control_flow.wy`, `eval_builtins.wy`, `eval_classes.wy`, `eval_io.wy`
  (fragments needing harness-supplied names); `decorators.wy` (compile-time-expanded, not a
  `--build-bc` target); `eval_signals.wy` (`signal` in a class body not lowered).
- **DIVERGES**: none expected beyond epic 4's fix for `eval_messages.wy` — if the sweep
  finds it still diverging, flag as a regression, not a new accepted entry.
- **Multi-value fixtures are `matches`, not DIVERGES**: `arith`, `collections` and `multiret`
  diverge from the *tree walker* (`pypoc/test/test_vm_run.py` `DIVERGENCES`, llm-bytecode.md §9),
  but epic 1's `scripts/build_corpus.py` copies the checked-in `*.vm.out` as their expected
  output, which is what a spec-honest VM produces. The C VM must match those `.out` files
  exactly; a mismatch there is a VM bug. Do not add them to DIVERGES.

**Files**
- Changed: `test/bytecode/manifest.txt`, this epic's own test asserting the sweep (new
  `src/test/test_bytecode_sweep.cpp` or extension of the golden harness) fails loudly on
  any unexplained category change.

**Acceptance**
The sweep test passes; the report includes the final matches/REFUSED/DIVERGES counts.
`test/bytecode/{two_module,wildcard,coroutines,decorators}` all pass;
`eval_modules`, `eval_coroutines`, `eval_io` samples match.

**Model:** Sonnet (mechanical reconciliation once M1-M5 land; escalate only on a genuine
unresolved design gap).

**Fan-out:** none — one coherent pass so category-change bookkeeping doesn't race.

## Out of scope / deferred

- Per-site inline cache for `msg`, per-class slot dict — epic 6.
- `bytes`-mode file I/O in `std::io` (binary reads/writes) — epic 7, once `bytes` exists.
- Remote modules (`wyrm_remote.RemoteModule`) — no C-side design section covers this.
- `import static` (compile-time-only dependency) — has no runtime behaviour distinct from
  `import` yet (llm-bytecode.md §9); treat identically to `import` if a fixture needs it.
- Fixing the pypoc tree walker's multi-value-return gap (it models `return a, b` as one
  tuple); the VM side is already spec-correct and checked against the `.vm.out` goldens.

## Risks

- **Fiber-inline import init frames re-entering the same fiber's stack** could interact
  badly with a coroutine's own fiber if an import happens inside a coroutine body. Mitigate
  with a hand-packed test: a coroutine body that does `import` on first `next()`.
- **Cycle detection false positives**: re-importing an already-READY module must not be
  mistaken for a cycle. Mitigate with a hand-packed test re-importing a module from two
  different importers, asserting no fault and no double-init.
- **GC of suspended coroutines is where fiber lifetime and GC roots truly interact**; get
  the root set wrong and either a live coroutine's stack gets collected (use-after-free) or
  a dead one never does (leak). Mitigate with the GC-stress test in M3, under
  `WY_TEST_GC_THRESHOLD=0`.
- **`range` as a compiled-wyrm prelude depends on classes and iteration being fully
  correct** (epics 3, 4) — a residual bug there makes `range` fail confusingly. Mitigate
  with `range`'s own small unit test independent of the `eval_range` sample.

## Report

`epic_5_report.md` must additionally record:
- The final `test/bytecode/manifest.txt` counts: total fixtures/samples, matches, REFUSED
  (with reasons), DIVERGES (with reasons) — enough for epic 6 to know what "full corpus"
  means going into hardening.
- Whether `range` shipped as a compiled prelude or a native type, and why, if that
  Assumption turned out wrong.
- The import hook's exact signature as implemented, for epic 6's embedding-API doc.
- Confirmation the coroutine GC test passed, and fiber memory-accounting numbers if
  available.
- Any sample that changed category from what this file predicted, with the reason.
- Proposed edits to `epic_6.md`: whether the deferred message-dispatch cache needs any hook
  epic 5 should have added but didn't (e.g. a stable per-`msg`-site code offset key), and
  whether `std::io` is complete enough for epic 6's benchmark suite.
