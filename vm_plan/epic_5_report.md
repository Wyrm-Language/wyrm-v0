# Epic 5 report — M1–M5

M1 and M2 implemented with Codex, 2026-09-16. M3 implemented with Claude
(Sonnet 5) the same day. M4 and M5 also implemented with Claude (Sonnet 5),
2026-09-17. M6 remains open. Changes require human review under
AI_POLICY.md; M1-M4 are committed (`0b6d43e`, `8c2cb0e`, `28c8d01`,
`f75889d`); M5 is not yet committed.

## Baseline and corrections to the plan

- Before: 5/5 Meson tests, 205 doctest cases / 15,500 assertions. The
  working tree was clean. The epic 4 report's final count was 204.
- `module.h` already had fill bookkeeping and wildcard storage; `link.c`
  only implemented builtin fill. Both import return kinds already existed.
- Qualified message resolution is still a `WY_ERR_NOSUPPORT` branch in
  `src/message.c`. There was no first-component resolver to reuse. M1 adds
  `wy_link_scope_member` for explicit fill and scope opcodes; qualified
  message resolution remains an outstanding epic integration task.
- The two-module and wildcard images and goldens exist. `eval_messages.wy`
  is `matches`, and pypoc HEAD is `ba4115a`, the message-promotion fix.
- Hosted allocation/machine support exists, but there is no filesystem
  import implementation. Fiber switch handling exists in the trampoline;
  coroutines and `range` remain later-milestone work.
- The reference `std::io` source is buffered Wyrm code over `__open`,
  `__read`, etc.; it is more than just the primitive list in the epic.

## Implementation

- Three-layer arbitration preserves the reference's same-source or
  identical-value no-op, stronger-layer replacement, and deferred
  ambiguity. Heap identity compares pointers; immediate values compare
  their active fields. The high bit of `fill_layer` marks an ambiguity
  error stored in the global, preserving ordinary error-valued globals.
  `gget` faults on the marker; `gset` clears it when replacing the value.
- Builtins fill bare free names at load. Explicit imports fill exact
  spellings and matching qualified prefixes through scope bindings.
  Wildcards fill only declared exports and retain copied except symbols.
- Compact/wide `import` and `import_star` initialize dependencies on the
  current fiber. The module registry publishes dependencies before init;
  init frames carry a stable module-owned prototype. Return fills the
  importer and marks the dependency READY. Faults and stack exhaustion
  mark affected init modules FAILED and unwind normally. Reimporting READY
  modules never reruns init; FAILED entries are rejected.
- `getscope`/`setscope` access module exports, class statics in the class's
  owning module, or function-owner exports. They never use instance slots
  or attribute accessors.
- Module GC traversal includes wildcard targets and full import-path
  strings; finalization frees copied except arrays. Import cache keys use
  full strings rather than truncated interned symbols. Language binding
  names and fill-source bookkeeping retain the existing symbol semantics.
- `wy_error_obj.code` defaults to NONE for ordinary errors; import faults
  carry the underlying code, including CYCLE. The cycle diagnostic names
  importer and dependency. Host execution returns FAULT with this error
  object on the fiber, consistent with the existing VM convention.

## Necessary scope adjustments

The context hook interface was pulled forward from M2 because M1 cannot
load dependencies without it:

```c
typedef wy_error (*wy_import_hook)(wy_context*, const char* path,
    wy_uword len, wy_u8** out_bytes, wy_uword* out_len, void* ud);
```

Set `context->import_hook` and `context->import_ud`. The hook receives the
`::`-joined path and returns bytes allocated through `wy_context_gc_alloc`.
On success the loader owns those bytes, including cleanup if the image is
malformed. On failure the hook remains responsible for its allocations.
The golden harness supplies a fixture hook; `-I` and hosted search remain M2.

The required `two_module/report.wyc` exposed a previously missing `str`
builtin. CLion debugger inspection at the failing golden assertion showed
`fiber->fault->what = "unbound global 'str'"` after dependency execution.
A scalar native conversion was added using the existing numeric formatter;
strings pass through unchanged. Container/instance conversion and `__str__`
dispatch remain part of broader builtin/dunder parity work.

The import frame's module already identifies the dependency. Its `aux`
holds the import-path string for ordinary imports or an index into the
importer's owned wildcard array for wildcard imports. This keeps excepts
alive without copying them into a second tuple, and avoids retaining a
pointer into a reallocatable array.

## Validation

- After: **5/5 Meson tests; 220/220 doctest cases, 15,799 assertions**.
- `two_module/report.wyc` and `wildcard/paint.wyc` match their `.out` files
  byte-for-byte with ordinary GC and `gc_threshold = 0`.
- Synthetic tests cover compact/wide import, inline dependency init,
  repeated imports from separate importers, cycles naming both modules,
  wildcard-first init, copied except lists, export filtering, nested scope
  fill, prefix boundaries, all layer orderings, identity versus equal
  string contents, persistent ambiguity, explicit override, scope opcode
  round trips, invalid namespaces, malformed/missing imports, failed cache
  entries, and stack exhaustion with lifecycle/stack restoration checks.
- Scalar `str` tests verify results, string identity, and no output-hook
  writes. `git diff --check` passes.
- Build command used `CCACHE_DIR=/tmp/cpoc-ccache meson compile -C buildDir`
  because the default ccache directory is read-only in the agent sandbox.

## M2 — hosted import hook, `-I` search path, CLI, `__ARGS`/`__name__`

### Scope delivered

- New `include/wyrm/platform/hosted/import_fs.h` + `src/platform/hosted/
  import_fs.c`: a `wy_import_fs_search_path` (root list owned by the `wy_allocator`
  passed to `wy_import_fs_add_root`, released by
  `wy_import_fs_search_path_finalize_f`) and the hosted `wy_import_fs_hook`.
  The hook maps a `::`-joined module path to `<root>/<path-with-slashes>.wyc`
  in root order; the first existing file wins, with no `.wy` source-compile
  fallback, and a miss returns `WY_ERR_UNBOUND`, honoring link's contract.
- `wy_import_fs_read_file` (moved out of `src/wyrm/main.c`) reads whole files
  through `wy_context_gc_alloc`, so the entry file and the hook's returned
  images share one ownership discipline (the loader owns hook bytes even when
  it rejects the image).
- `src/wyrm/main.c`: accepts repeated `-I dir` (detached and attached `-Idir`
  forms), a single `.wyc`, and `--sections`/`--disasm` anywhere on the line
  (kept so `scripts/check_disasm.sh`'s `wyrm <path> --disasm` still works).
  It installs the hook with the search path as `context->import_ud`, seeds
  `__name__` ("__main__") and `__ARGS` into the module's free-name slots
  before `wy_module_run_init`, and reports load/init/fault failures on
  stderr.

### Decisions

- `__ARGS` is a `wy_list` of string values (the milestone's "list of
  strings"), not pypoc's tuple. Args are indexed directly off `argv`, so
  script arguments are never copied.
- Free-name slot writes are ordinary data: `gget` of a still-unset global
  faults, a module-set value keeps what the module wrote, and the
  fill-layer ambiguity bit is cleared on seed so the marker can never be read
  as a global value.
- Everything after the script path is left untouched as `__ARGS`; only the
  three known tool flags are still consumed there. Unknown options before the
  script exit with usage, as before.
- Internal helpers follow the `_`-suffix fast-path convention from AGENTS.md
  (`wy_import_fs_search_path_init_s`, `wy_import_fs_search_path_finalize_f`).

### Validation

- 5/5 Meson tests; doctest suite grows to **227/227 cases, 15,842
  assertions** (the +7 cases / +43 assertions over M1's 220/15,799 come from
  `test_import_fs`).
- `test_import_fs` covers: flat and nested (`a::b::c`) path translation,
  first-root-wins and reversed root order, `WY_ERR_UNBOUND` for a missing
  module under every root and for an empty search path, root-string survival
  after the caller's buffer is gone (duplication on add), and
  `wy_import_fs_read_file` hit/miss.
- CLI acceptance: `wyrm -I test/bytecode/two_module
  test/bytecode/two_module/report.wyc` matches `report.out` byte-for-byte
  (cross-directory resolution with no `geometry.wyc` next to `report.wyc`);
  a compiled `println(__ARGS)` script prints `[a, b, c]` for
  `wyrm script.wyc a b c` and `[]` with no args; `println(__name__)` prints
  `__main__`. Both `-I dir` and `-Idir` pass; `--sections`/`--disasm` are
  unchanged and still exit before any init. `git diff --check` clean.
- Known limitation, deliberate: a script argument that spells one of the
  three tool flags (`--disasm`, `--sections`, `-I...`) after the script path
  is still consumed as a tool option, in order to keep check_disasm.sh's
  path-then-`--disasm` form working. Full pypoc-style flag parity remains
  with epic 11's CLI milestone.

## M3 — Coroutines

Implemented with Claude Sonnet 5, 2026-09-17, **not** the Opus the epic plan
calls for given this milestone's flagged design risk (fiber-based
coroutines with no C precedent in this repo, GC-of-suspended-fibers being
first-of-kind). The user was asked and explicitly chose Sonnet before work
started. Flagged uncertainties are called out below rather than papered
over; this milestone in particular would benefit from a second opinion
given the plan's own risk assessment. No commit was created (AI_POLICY.md).

### Implementation

- `include/wyrm/coroutine.h`, `src/coroutine.c`: `wy_coroutine` exactly per
  design_c_vm.md §3 (`fiber`, `resumer`, `delegate`, `outer`,
  `delegate_dst`, `yield_base`, `state`, `result`), with a
  `children_iter`/`finalize` pair following the `wy_instance`/`wy_fiber`
  convention. `wy_coroutine_innermost_f` implements "next/send follow
  `while (co->delegate) co = co->delegate)`".
- `src/vm.c`: `WY_OP_CALL`'s FUNCTION branch checks `WY_FN_COROUTINE`
  before calling `push_bytecode_call_bind_f` and instead calls a new
  `construct_coroutine_f` - a fresh `wy_fiber_create(ctx, ctx->co_stack_len,
  ctx->co_frame_count)`, a `wy_coroutine` wrapping it, and the body frame
  pushed on *that* fiber (by temporarily swapping `ctx->current_fiber`
  around the existing `push_bytecode_call_bind_f`, so the P-frame binding
  logic - defaults, `*args`, `**kwargs` - is not duplicated). `yield`
  (`0xA7`) and `yield_from` (`0xB0`) are new opcode cases; `do_return`
  gained a `WY_RET_COROUTINE` case that always leaves via `WY_EXEC_SWITCH`
  (to the outer coroutine on delegation, else StopIteration to the
  resumer) since a finished coroutine's fiber never has anything left to
  reload into.
- `src/builtin/builtins.c`: `next`/`send` registered as `WY_NATIVE_EXEC`
  (the module gained a small exec-native registration loop alongside the
  existing leaf one). Both walk the delegate chain, special-case DONE
  (StopIteration, no switch) and CREATED (`send` only: a fault), then set
  `co->resumer`, `co->state = RUNNING`, `co->fiber->pending = wy_vm_run`,
  `ctx->current_fiber = co->fiber`, and return `WY_EXEC_SWITCH`.

### The exec-native bridge is genuinely new, not just reused

Epic 5's own file list didn't anticipate this, but M1's `import`/`getscope`
never called an exec native *from bytecode* - every `WY_OP_CALL` NATIVE
branch before this milestone either ran a leaf inline or faulted
("exec natives called from bytecode are not supported until epic 3+"). M3
needs this for real (`next`/`send`), so `WY_OP_CALL`'s NATIVE branch now
bridges through the existing (already-implemented-but-previously-unused-
from-bytecode) `wy_vm_call_exec_push_f`/`wy_vm_native_await_complete_f`
pair from `src/vm_call.c`, plus a small continuation
(`exec_native_call_resume_f`) that copies the result and re-enters
`wy_vm_run`. Getting the destination window and result count from the call
site to that continuation - which may run much later, after arbitrarily
much coroutine execution on other fibers - needed three new scratch fields
on `wy_fiber` (`pending_native_dst`, `pending_native_base_count`,
`pending_native_nres`), since `wy_frame`'s own `ret_dst`/`ret_nres` are
already spoken for by the *calling* frame's own eventual return and can't
be reused for this.

**This bridge does one nested C call** (`exec_native_call_resume_f` calls
`wy_vm_run` directly rather than looping) which is worth a second look
against AGENTS.md's no-C-recursion-in-VM-execution rule. The reasoning
recorded in the code comment: every `wy_vm_run` invocation this bridges
between has already fully returned (via `WY_EXEC_CONTINUE`/`WY_EXEC_SWITCH`
unwinding through `wy_fiber_exec_f`'s pending loop and
`wy_context_exec`'s iterative fiber-switch loop) before the continuation
runs, regardless of how many coroutines or delegation levels are involved -
so the extra nesting is a fixed one-frame bridge depth, not something that
grows with wyrm-level call or coroutine nesting. I believe this holds, and
the coroutine-calls-coroutine and yield-from tests exercise a few levels of
it without incident, but this is exactly the kind of claim I'd want an
Opus (or human) review to independently re-derive rather than take on
trust, given the plan's own risk framing of this milestone.

### A real pre-existing bug this milestone's own tests exposed

`wy_vm_call_sync`'s `WY_EXEC_CONTINUE` handling called
`wy_fiber_exec_f(ctx->current_fiber, ctx)` once - fine for a single
native-call turn, but wrong once a call can switch fibers and switch back
(which every `next`/`send` on a not-yet-DONE coroutine does). The first
draft of the coroutine tests failed with garbage values in call results
(the destination register was never actually written, because
`wy_vm_call_sync` returned "done" after the *first* switch, before the
coroutine body - or the bridge's own continuation - ever ran). Fixed by
having `wy_vm_call_sync` call `wy_context_exec(ctx)` instead, which already
implements exactly the "keep following `current_fiber`" loop this needs
(M1's own comment on that loop anticipated this: "no coroutines exist yet
to actually produce a switch, but the loop shape lands with this
milestone"). This was dead code before M3 - nothing exercised
`wy_vm_call_sync`'s CONTINUE path previously - so the fix carries no risk
to M1/M2 behavior, confirmed by the full suite staying green.

### GC roots (design_c_vm.md §3's explicit risk area)

- `wy_context` gained `fiber_list` (an intrusive `wy_fiber::next_fiber`
  list) as the permanent root set for *root/independent* fibers -
  `wy_context_attach_fiber` links onto it. A coroutine's own private fiber
  (built directly via `wy_fiber_create` in `construct_coroutine_f`) is
  never linked here, so it stays reachable only through its owning
  `wy_coroutine`.
- `wy_context_gc_full_run` visits every fiber in `fiber_list`, plus
  `current_fiber` unconditionally (covering a coroutine fiber that's
  currently running but not on that list, so a safepoint mid-body doesn't
  collect the fiber it's executing on).
- `wy_fiber`'s own `children_iter` (previously: value stack + each frame's
  defer chain + fault) now also walks each live frame's `module`,
  `dispatch_body`, and `aux` per design_c_vm.md §3's explicit list - a
  coroutine body frame's `aux` is the only reference keeping its own
  `wy_coroutine` reachable while the frame that constructed it is off the
  stack (recall `wy_coroutine` isn't otherwise self-referential from its
  fiber).
- Net effect, matching the epic's explicit acceptance test: a coroutine
  `next`ed once and then dropped (no register/global holds it, `next`
  wasn't called again) is reachable from nowhere after its call frame
  returns, so a full collection can reclaim its fiber and stack.

**Honesty note on the GC test** (`test_coroutine.cpp`, "an abandoned
suspended coroutine is collected..."): it calls `wy_context_gc_full_run`
and checks the run completes without faulting, but does **not** positively
assert the fiber's memory was actually freed (no live-object-count hook was
available in this session's time budget to wire up cheaply). The stronger
guarantee - that nothing here is a use-after-free - comes from the existing
`cwyrm-golden-gcstress` suite (`WY_TEST_GC_THRESHOLD=0`, full corpus),
which stayed green through this change. I'd treat the abandonment test as
"exercises the path," not "proves the memory is reclaimed," and flag that
gap explicitly for anyone reviewing this milestone.

### Scope narrower than the design text in a few places

- Coroutine construction is wired into `WY_OP_CALL` only, not `WY_OP_CALL_VA`
  (`f(*a, **k)` splat calls) or message dispatch (`WY_OP_MSG`/`MSG_VA`/
  `SUPER`). The epic's own scope text says "call on a FUNCTION," and no
  fixture needs the other call shapes to construct a coroutine; flagging in
  case epic 6 or a real `.wy` sample does.
- `next`/`send`'s exec-native bridge likewise only fires from `WY_OP_CALL`,
  not `WY_OP_CALL_VA` - calling `next(*args)` faults as "not supported."
- `StopIteration` is delivered as a plain error value with
  `ctx->stop_iteration_class` (a new context field, populated by
  `wy_builtins_new`); no `raise`/user-catch story beyond `is_error`/`is`
  existed before this milestone to hook into.

### Files changed beyond the epic's own M3 list

The epic predicted `include/wyrm/coroutine.h`, `src/coroutine.c`,
`src/context.c`, `src/gc.c`, `src/builtin/builtins.c`. Actually touched:
`include/wyrm/coroutine.h`, `src/coroutine.c`, `src/builtin/builtins.c`,
`src/vm.c` (opcodes, `do_return`, the exec-native bridge, the
`wy_vm_call_sync` fix), `include/wyrm/fiber.h` + `src/fiber.c` (new fields,
extended `children_iter`), `include/wyrm/context.h` + `src/context.c`
(`fiber_list`, `stop_iteration_class`, `co_stack_len`/`co_frame_count`,
updated `wy_context_attach_fiber`/`wy_context_gc_full_run`),
`include/wyrm/fwd.h` (forward declaration). `src/gc.c` itself needed no
changes - the fiber/coroutine `children_iter`s live on their own object
types (`src/fiber.c`, `src/coroutine.c`), matching how every other heap
type in this codebase supplies its own rather than `gc.c` special-casing
types.

### Validation

- Before this milestone: 5/5 Meson tests, 227/227 doctest cases, 15,842
  assertions (matching M2's closing count above).
- After: **5/5 Meson tests (including `cwyrm-golden-gcstress`,
  `WY_TEST_GC_THRESHOLD=0`); 230/230 doctest cases, 15,877 assertions**
  (+3 cases / +35 assertions, all in the new `src/test/test_coroutine.cpp`).
- New hand-packed tests (bytecode built the same way `test_wvm.cpp`/
  `test_link.cpp` do - no compiler exists in this repo): (1) a coroutine
  yielding twice then answering `StopIteration` idempotently on two further
  `next()` calls; (2) a coroutine that runs `import` on its first `next()`
  - the epic's own named risk ("a coroutine body that does `import` on
  first `next()`") - confirming the dependency initializes inline on the
  coroutine's own fiber and the qualified global fills correctly; (3) the
  abandoned-suspended-coroutine GC exercise discussed above.
- No fixture named `coroutines.wyc`/`test/bytecode/coroutines/` exists yet
  in this repo (epic 1/2's toolchain territory) - the epic's acceptance
  criterion `test/bytecode/coroutines.wyc matches .out` is not yet
  checkable and stays open for whichever milestone adds that fixture.
- `git diff --check` clean (no trailing whitespace).

### For epic 6 / whoever reviews this next

- The `wy_vm_call_sync` fix (switch to `wy_context_exec`) is a correctness
  fix to M1-era code, not new M3 behavior, but changes what
  `wy_vm_call_sync` does on *any* future `WY_EXEC_CONTINUE` - worth a
  second look if epic 6 adds another exec-native-from-bytecode caller.
- Fan-out advice for a future coroutine-adjacent milestone: don't split
  the switch-loop/yield/yield_from/next/send state machine - it's as
  coupled as the epic predicted, confirmed by how much the GC-root and
  exec-native-bridge work ended up touching each other.

## M4 — `std::io`, `range` prelude

Implemented by Claude (Sonnet 5), 2026-09-17, continuing directly on top of
the M3 commit's working tree (`8c2cb0e` + the uncommitted M3 changes above).
The epic tags M4 Sonnet, so no model-escalation question was needed here
(unlike M3).

### `range`

- Shipped as a **compiled prelude**, exactly as the epic's Assumption
  predicted, and closer to the reference than expected: `range(begin, end)`
  is not a class with `__iter__` (the epic's own guess) but a bare `co
  range(begin, end)` **coroutine function**, matching
  `pypoc/wypoc/corelib/prelude.wy` and the semantics
  `pypoc/wypoc/samples/eval_range.wy` actually exercises (`next(range(2,
  6))` calls `next` directly on the constructed coroutine - there is no
  `__iter__` indirection at all). Writing a class wrapper would have been
  extra, unrequired work; M3's coroutine machinery is the entire
  implementation this needed.
- `test/bytecode/embedded/range.wy` (source) and `range_1.c` (embedded
  image, generated the same way `hello_1.c` was - `wypoc.compiler_bc.
  compile_module` + `ModuleImage.to_c()`, not re-run through
  `scripts/build_corpus.py` since this one-off prelude isn't one of that
  script's fixtures) are new. `src/builtin/meson.build` compiles
  `range_1.c` straight into `libcwyrm` (not just the test binary, unlike
  `hello_1.c`), since `range` must exist at every context's builtins init,
  not only in tests.
- `wy_builtins_new` (`src/builtin/builtins.c`) loads `range_1_image` via
  `wy_module_load_image`, runs its one-time init via `wy_module_run_init`,
  then copies the resulting coroutine-function value into a builtins slot
  under the bare name `range` - the same layer-3 `fill_from_builtins` path
  `println`/`str` already use, so `for i in range(n):` needs no `import`.
- **A real bug this surfaced and fixed**: `wy_module_run_init` executes
  real bytecode, which can hit a GC safepoint. The builtins module under
  construction (and the freshly loaded `range` module) were both
  unreachable from any GC root at that point - `context->builtins` is only
  assigned by the *caller* after `wy_builtins_new` returns, and an
  unregistered loaded module is invisible to the root scan. A GC during
  `range`'s init corrupted both modules' `exports` (observed directly:
  `module->exports.capacity` read back as `0` after `run_init`, and
  `wy_slot_dict_add_entry` failed with `WY_ERR_NOMEM` from a full/corrupt
  probe). Fixed by rooting `context->builtins = module` as soon as
  `module`'s globals/fill_layer/fill_source/exports are internally
  consistent (before the leaf-native loop), and by registering the range
  submodule via `wy_context_module_register` before running its init. This
  is a general hazard for *any* future builtins-init code that runs
  bytecode, not just `range` - worth a note for whoever touches
  `wy_builtins_new` next.
- `test/bytecode/samples/eval_range.wyc`/`.out` already existed in the
  repo (byte-identical to what recompiling produces) but had no golden
  `TEST_CASE` exercising them; added `"samples/eval_range"` to both
  `TEST_SUITE("golden")` and `TEST_SUITE("golden-gcstress")` in
  `src/test/test_bytecode_golden.cpp`. It matches the tree walker's output
  (empty stdout - the sample only computes and discards values) byte for
  byte, gcstress included. Also added a hand-packed unit test
  (`src/test/test_builtins.cpp`) constructing `range(0, 3)` and driving
  `next()` four times (0, 1, 2, then `StopIteration`) via `WY_OP_CALL`, the
  same convention `test_coroutine.cpp` uses.

### `std::io`

- New `include/wyrm/platform/hosted/io_native.h` + `src/platform/hosted/
  io_native.c`: the seven natives (`open`/`read`/`write`/`lseek`/`dup2`/
  `close`/`flush`) as `WY_NATIVE_EXEC` callables (per the epic's own
  "exec natives" call, matching design §1.3), plus `STDIN`/`STDOUT`/
  `STDERR` constants, assembled into a `WY_MODULE_BUILTIN` module the same
  way `wy_builtins_new` assembles its own (own `globals`/`exports`, no
  `run_init`). `wy_io_module_install` registers it under import path
  `"std::io"`; `wy_link_import` already checks already-registered modules
  by name/`import_path` before ever consulting `context->import_hook`, so
  `import std::io` resolves it directly with no filesystem access.
- **Handles are real POSIX file descriptors**, not a synthetic handle
  table the way the Python reference (`wyrm_io.py`) keeps one: a real fd
  already satisfies "0/1/2 are stdin/stdout/stderr" and every other
  behavior (`dup2`, `lseek`, `close`) maps onto the matching syscall with
  no bookkeeping layer needed. `open` maps `"r"/"w"/"a"/"r+"/"w+"/"a+"`
  (optional trailing `"b"` ignored - no text/binary distinction at the
  POSIX layer) to `O_*` flags; `read(handle, -1)` loops to EOF into a
  growable buffer; `write` loops until every byte is written; `flush`
  calls `fsync`, swallowing `EINVAL`/`ENOTSUP` (pipes/ttys/sockets have no
  durability concept to flush).
- **A syscall failure is an OSError *value*, never a VM fault** - matching
  `pypoc/wypoc/corelib/std/io.wy`'s explicit contract ("the handle is an
  error value when the open fails... propagates it" via `try`). Added
  `context->os_error_class` (`include/wyrm/context.h`), populated by
  `wy_builtins_new` alongside the existing `stop_iteration_class` wiring,
  so `io_native.c` can build a proper `OSError` instance from `errno`/
  `strerror`. Genuine engine-level faults (wrong argument types/counts)
  still go through `context->current_fiber->fault` as `WY_EXEC_FAULT`,
  matching every other native in this codebase.
- **Necessary scope narrowing, and a divergence worth a second look**: the
  epic says these natives back "`pypoc/wypoc/corelib/std/io.wy`'s
  wyrm-level wrapper" - that reference file is a buffered `File` class plus
  `println`/`eprintln` built from double-underscore-prefixed primitives
  (`__open`/`__read`/...). This C VM already has its own native
  `println`/`print` (`src/builtin/builtins.c`, a different design than the
  reference's), so porting `println`/`eprintln` again would duplicate
  behavior this VM already has under different names. Rather than embed a
  compiled `io.wy` wrapper (the `range`-style path), the seven natives are
  exported **directly** under `std::io` (`std::io::open`, not `__open`),
  with no `File` class wrapper. This satisfies the epic's literal
  acceptance text ("native `std::io`... importable as `std::io`") but is
  narrower than "backing `io.wy`'s wyrm-level wrapper" read strictly - a
  `File` class (`read`/`write`/`seek`/`tell`/`flush`/`close` methods per
  the reference source) is not yet available at `std::io::File`. Flagging
  this as a scope call rather than an oversight: building the wrapper is
  straightforward follow-on work (compile `io.wy` almost verbatim,
  substituting `__open` etc. for `open` etc., embed it like `range`) if a
  later milestone or sample needs `std::io::File` specifically.
- **`wypoc`'s compiler emits an implicit parent-package import**: `import
  std::io` compiles to *two* `import` opcodes - `import std` then `import
  std::io` (the same way Python's `import a.b` also binds bare `a`),
  confirmed by disassembling a real compiled test script. Without a module
  registered under bare `"std"`, the first import faults `WY_ERR_UNBOUND`
  even though `"std::io"` itself is registered and would resolve fine.
  Fixed by also registering an empty `WY_MODULE_BUILTIN` module under
  import path `"std"` (`install_std_package_`, called from
  `wy_io_module_install`) - a namespace placeholder with no exports, purely
  so the ancestor import resolves. This generalizes if a second `std::*`
  submodule is ever added: both would share the same `"std"` package
  registration, so `wy_io_module_install` (or a shared "install std
  package" call) must run before/alongside any of them, not be duplicated.
- **A gap found, not fixed (adjacent to M3, out of this milestone's
  scope)**: `wy_vm_call_sync` (the host-call entry point) does not
  special-case a `WY_FN_COROUTINE`-flagged function - it runs the body
  directly via `push_bytecode_call_bind_f` instead of constructing a
  coroutine object, so calling `range(...)` (or any coroutine function)
  *directly* from C via `wy_vm_call_sync` faults (`yield` inside a
  non-coroutine-constructed frame is invalid). This only matters for
  host code calling a coroutine constructor directly; all real wyrm-level
  call sites go through `WY_OP_CALL` (bytecode), which already handles
  coroutine construction correctly (confirmed: `eval_range.wy`'s
  `next(range(2, 6))`, itself compiled bytecode, works). Found while
  writing the `range` unit test (first attempt called `range_fn` via
  `wy_vm_call_sync` directly and got `WY_ERR_FAULT`); worked around by
  hand-packing a small bytecode function instead, matching
  `test_coroutine.cpp`'s own convention. This is the same shape as M3's
  already-noted "coroutine construction only wired into `WY_OP_CALL`, not
  `CALL_VA` or message dispatch" gap, extended to host-direct calls too -
  worth fixing together if a future milestone needs any of these paths.
- `wyrm: main.c` calls `wy_io_module_install(context)` right after
  `wy_builtins_new`, before reading the entry file - `import std::io`
  works from the real CLI (`./buildDir/src/wyrm/wyrm script.wyc`), not
  just in unit tests. Verified end-to-end with a hand-written script:
  `import std::io; open/write/lseek/read/println/close` round-trips
  `"hello from wyrm\n"` through a real temp file via the actual CLI binary
  (16 bytes written, same 16 bytes read back).
- New `src/platform/hosted/test/test_io_native.cpp`: open/write/read-back/
  close round trip on a real file; open on a missing path in read mode
  answers an `OSError` value (not a fault); `dup2` makes a new fd refer to
  the same file, verified by writing through one and reading through the
  other; `flush` on a regular file's handle; `wy_io_module_install`
  resolves via `wy_link_import` under path `"std::io"`.
- `eval_io.wy` stays `REFUSED` at the pypoc-compiler level per the epic's
  own M6 list ("a fragment: needs harness-supplied names") - there is no
  way to compile it standalone for a C-VM fixture, confirmed by reading
  `pypoc/test/test_vm_samples.py`'s `REFUSED` dict. This is not a gap this
  milestone introduced; it stays open exactly as the epic already
  predicted, and the real end-to-end CLI script above is the closest
  available substitute for the epic's "eval_io sample runs..." acceptance
  text.

### Validation

- Before this milestone: 5/5 Meson tests, 233/233 doctest cases, 15,907
  assertions (230/230, 15,877 from M3, plus the `range` prelude work above
  which landed first within this same session).
- After: **5/5 Meson tests (including `cwyrm-golden-gcstress`); 238/238
  doctest cases, 16,027 assertions** (+5 cases / +120 assertions, all in
  the new `test_io_native.cpp` suite).
- `git diff --check` clean (no trailing whitespace); build via
  `CCACHE_DIR=/tmp/cpoc-ccache meson compile -C buildDir`.

### For epic 6 / whoever reviews this next

- The GC-safety hazard found in `range`'s integration (any C code that
  runs bytecode before its own module is reachable from a GC root) is
  worth a general audit if epic 6 adds more builtins-init-time bytecode
  execution.
- The `wy_vm_call_sync` coroutine gap above should probably be fixed
  alongside M3's already-noted `CALL_VA`/message-dispatch coroutine gaps,
  as one pass, rather than three separate patches.
- `std::io::File` (the buffered wrapper class) is not yet built - flagged
  above as a scope call, not an oversight.

## M5 — Decorators fixture

Implemented by Claude (Sonnet 5), 2026-09-17, continuing on top of the M4
commit (`f75889d`). No model escalation needed - the fixture surfaced
exactly the kind of small, well-scoped bug the epic's own text anticipated.

### What was wrong and the fix

`test/bytecode/decorators/decorated.wyc` (source in the gitignored
`pypoc/test/bytecode/decorators/{declib,decorated}.wy` - decorators expand
at compile time, so the runtime image never contains a `Decorated` node;
`decorated.wy` just calls the already-expanded `plain`/`loud` functions)
faulted `unbound global 'TreeBase'` during `declib`'s module init. `declib.wy`
registers two messages typed on `TreeBase` (`fn [TreeBase] unchanged()`/
`traced(label)` - the decorator-authoring convention, wyc-format.md §8's
message type constraint) but nothing in the C VM ever defined that global;
the reference (`pypoc/wypoc/wyrm_builtins.py`'s `TREE_BASE_CLASS`) exposes it
as a builtin bare class with one `__tree` slot.

Fixed by adding `TreeBase` to `wy_builtins_new` (`src/builtin/builtins.c`)
as a fifteenth (`WY_BUILTINS_COUNT + 1`) builtin export: a bare
`wy_class_new` class named `TreeBase`, following the exact same pattern as
the five error classes just above it in that function, but with no
`WY_CLASS_ERROR` flag and no super. Deliberately narrower than the
reference: no `__tree` slot, since no fixture in this epic's scope
constructs a `TreeBase` instance (decorators run at compile time; the
runtime image only needs the class identity to exist so `reg_msg`'s type
constraint resolves). A real decorator-authoring sample that boxes a tree
value (`plain::$ast`, `pypoc/wypoc/samples/decorators.wy`) stays `REFUSED`
per the epic's own M6 list, so that gap doesn't block anything here -
flagging for whoever eventually un-refuses that sample.

### Files changed

- `src/builtin/builtins.c`: `TreeBase` builtin class, `WY_BUILTINS_COUNT`
  bumped by one.
- `src/test/test_bytecode_golden.cpp`: added `"decorators decorated"` to
  both the `golden` and `golden-gcstress` `TEST_SUITE`s (no case existed
  before this milestone; `declib.out` is empty and isn't independently
  wired since nothing but `decorated`'s own `import static declib` exercises
  it).

No `src/link.c`/`src/dispatch.c` changes were needed - the epic's own
"likely none" prediction for this milestone held once the missing global
was supplied.

### Validation

- Before: 5/5 Meson tests, 238/238 doctest cases, 16,027 assertions
  (M4's closing count).
- After: **5/5 Meson tests (including `cwyrm-golden-gcstress`); 240/240
  doctest cases, 16,047 assertions** (+2 cases / +20 assertions: the new
  golden + golden-gcstress cases for `decorators/decorated`).
- `./buildDir/src/wyrm/wyrm -I test/bytecode/decorators
  test/bytecode/decorators/decorated.wyc` matches `decorated.out`
  byte-for-byte (`42`, `calling loud`, `2`).
- `git diff --check` clean.

### For epic 6 / whoever reviews this next

- `TreeBase` currently has no slots. If a later epic un-refuses
  `decorators.wy` (or any sample that boxes `$ast`/constructs a `TreeBase`
  instance) and needs the reference's `__tree` slot plus `sexpr`/`macroexpand`
  unwrapping, that's new scope, not a gap in this milestone.
- No other linking/dispatch bug surfaced; M1-M4's machinery handled the
  fixture's `import static`, message registration, and closures over `this`
  without further changes.

## M6 — Full corpus sweep

Implemented by Claude (Sonnet 5), 2026-09-17, continuing on top of the M5
commit (`abe09ef`). This closes epic 5: every entry in
`test/bytecode/manifest.txt` is now actually exercised against the C VM
(not just categorised by inheriting the pypoc/tree-walker's prediction from
epic 1), and every previously-unwired fixture is either wired into the
golden harness as `matches` or given a written `DIVERGES` reason.

### What the sweep found

Before this milestone, `src/test/test_bytecode_golden.cpp` only ran 16 of
the manifest's 23 `matches`-predicted rows through the C VM - the other 7
(`coroutines`, `decorators/declib`, `two_module/geometry`,
`wildcard/palette`, and five `samples/*`) had never actually been executed
here; their "matches" status was inherited unverified from epic 1's corpus
generation. Wiring every remaining row into `golden`/`golden-gcstress`
surfaced:

- **Three harness-only gaps, fixed** (the harness's environment didn't
  match the real CLI's, not VM bugs):
  - `run_fixture` never called `wy_io_module_install`, so any fixture
    importing `std::io` failed at the loader (`could not open .../std.wyc`).
    Now installed alongside `wy_builtins_new`, matching `src/wyrm/main.c`.
  - `run_fixture` never seeded `__ARGS`/`__name__`, so `samples/eval_args.wy`
    (which reads `__ARGS`) faulted `unbound global '__ARGS'` even though the
    real CLI runs it fine with zero args. Now seeded (`__ARGS = []`,
    `__name__ = "__main__"`) via a small `seed_global` mirroring
    `main.c`'s, before `wy_module_run_init`.
  - `samples/eval_modules.wy`'s `import shapes` resolves against
    `pypoc/wypoc/corelib/shapes.wy` (`wyrm_modules.DEFAULT_COREPATH`), not
    the samples directory the fixture importer searches - epic 1's corpus
    build never anticipated a sample reaching into corelib. Compiled it
    with `pypoc/.venv/bin/wyrm --build-bc` and committed
    `test/bytecode/samples/shapes.wyc` (manifest row added, self-contained,
    no `.out` of its own).
- **One real, narrow VM gap, fixed**: `std::io::println` didn't exist -
  M4 deliberately exported only the seven POSIX-backed natives under
  `std::io`, reasoning the VM already has its own bare `println`. But two
  samples (`eval_closures.wy`, `eval_modules.wy`) call `std::io::println`
  directly (the reference's `corelib/std/io.wy` wrapper exposes it there
  too). Rather than embed a compiled `io.wy` wrapper, exposed the exact
  same rendering bare `println` already uses: `wy_builtin_println_body_f`
  (renamed out of `builtin_println_`, declared in `include/wyrm/builtins.h`)
  is now also registered as a leaf native under `std::io::println`
  (`src/platform/hosted/io_native.c`, a new `io_leaf_natives_` table
  alongside the existing exec-native one). No new formatting logic, no
  `io.wy` wrapper needed.
- **Four real, non-trivial VM gaps, left open and written into the
  manifest as `DIVERGES`** (each is a genuine design gap, not a corpus or
  harness mistake, and each is bigger than this milestone's "mechanical
  reconciliation, escalate only on a genuine unresolved design gap"
  mandate - see the model-staging table):
  1. **`samples/eval_assignments.wy`** - `!`-messages on native containers
     (`grown!resize(5)`, `expand`, `append`, `remove`) compile to `WY_OP_MSG`,
     but `dispatch_body_f`'s result always flows into
     `push_bytecode_call_bind_f`, which only understands bytecode FUNCTION
     bodies (`fn->proto`, `fn->module`) - never a NATIVE-tagged one. The
     existing `resize`/`expand`/`append`/`remove` builtins are plain global
     functions (bare-name calls only), never registered as message
     overloads anywhere, so `msg` resolves an empty message and faults "no
     overload of 'resize' matches 1 receiver(s)". This is epic 4's
     already-flagged "native-message promotion" gap (this report's own
     closing line, previously), now confirmed concretely by the sweep. A
     fix needs `WY_OP_MSG`'s dispatch to branch on the resolved body's type
     (bytecode vs. native) the way `WY_OP_CALL` already does, plus a place
     to register wildcard native overloads (the reference's
     `register_native_method` registers into a message table every module
     can see - this VM's `message_table` is per-module with no fallback
     tier, so builtins would need a fourth resolution path mirroring
     layer-3 global fill).
  2. **`samples/eval_coroutines.wy`** - `cofun.value` (reading a
     coroutine's last-yielded/returned value) is plain `getattr` on a
     COROUTINE receiver. design_c_vm.md §7's non-INSTANCE property table is
     "initially empty -> fault", and nothing populates a COROUTINE entry;
     faults "getattr: unsupported receiver type". Needs a property-table
     entry (or dedicated opcode handling) exposing `wy_coroutine::result`.
  3. **`samples/eval_closures.wy`** - `for i in range(0, end):` compiles to
     `iter`/`itnext` (the general iterator protocol, `src/iter.c`) wrapping
     whatever `range(...)` returns - a coroutine. `wy_iterator_next` has no
     COROUTINE case, so it silently answers `WY_ERR_STOP_ITERATION` on the
     very first call: the loop body never runs, and nothing faults (this
     fixture's other, non-loop `std::io::println` call was the only thing
     that printed, confirmed by manually diffing the captured output). This
     is the same class of gap M3's own report flagged ("next/send only
     wired from WY_OP_CALL, not CALL_VA or message dispatch"), extended to
     the iterator protocol: resuming a coroutine from inside a plain C
     function needs the same fiber-switching machinery `next()`/`send()`
     use, which `wy_iterator_next` has no way to trigger from where it's
     called (`WY_OP_ITNEXT`'s handler doesn't handle `WY_EXEC_SWITCH`).
  4. **`samples/eval_modules.wy`** - `import std::io::println as
     alias_println` compiles a bare `import` of the full leaf path.
     `wy_link_import` (`src/link.c`) only knows how to resolve a path as a
     registered module or a hook-loaded file; it has no fallback for "the
     path names a plain member of an already-resolved parent module,"
     which is exactly what the reference does
     (`pypoc/wypoc/vm/imports.py`'s `import_path`: "`import a::b::c` is
     ambiguous in the source and stays ambiguous here... may name a member
     of the module before it"). Confirmed by disassembly: the compiler
     really does emit one `import` per prefix including the full leaf path,
     for both `import X::Y::Z` and `import X::Y::Z as W` forms. Needs the
     same parent-then-member fallback ported into `wy_link_import`.

  All four are written into `test/bytecode/manifest.txt` with the specific
  mechanism and file/line reasoning above (not just "known issue"), so
  epic 6 can pick any of them up without re-deriving the cause. None of the
  four TEST_CASEs were added to `golden`/`golden-gcstress` (they'd fail
  every run); the manifest is the source of truth for their status, per
  this milestone's own scope text ("add C-VM-only entries for anything...
  a real gap remains on the C side - write the reason").

### Files changed

- `src/test/test_bytecode_golden.cpp`: `wy_io_module_install` +
  `__ARGS`/`__name__` seeding in `run_fixture`; 14 new `TEST_CASE`s across
  `golden`/`golden-gcstress` for previously-unwired-but-passing manifest
  rows (`coroutines`, `decorators/declib`, `two_module/geometry`,
  `wildcard/palette`, `samples/decolib`, `samples/eval_args`,
  `samples/eval_control_flow`, `samples/eval_error_handling`,
  `samples/eval_functions`, `samples/eval_messages`, `samples/eval_strings`,
  plus the three already present `eval_range`/`decorators decorated` kept).
- `include/wyrm/builtins.h`, `src/builtin/builtins.c`: `builtin_println_`'s
  body extracted to public `wy_builtin_println_body_f`.
- `include/wyrm/platform/hosted/io_native.h` unchanged;
  `src/platform/hosted/io_native.c`: new `io_leaf_natives_` table,
  `println` registered under `std::io` via `wy_native_leaf_new`, global
  slot accounting split into `WY_IO_EXEC_COUNT`/`WY_IO_LEAF_COUNT`.
- `test/bytecode/samples/shapes.wyc` (new, compiled from
  `pypoc/wypoc/corelib/shapes.wy`).
- `test/bytecode/manifest.txt`: four rows recategorised `matches` ->
  `DIVERGES` with reasons above; one new row for `samples/shapes.wy`.

### Validation

- Before this milestone: 5/5 Meson tests, 240/240 doctest cases, 16,047
  assertions (M5's closing count).
- After: **5/5 Meson tests (including `cwyrm-golden-gcstress`); 262/262
  doctest cases, 16,517 assertions**.
- `meson test -C buildDir` green; `./buildDir/src/wyrm/wyrm -I
  test/bytecode/two_module test/bytecode/two_module/report.wyc` matches
  `report.out` byte-for-byte.
- Final `test/bytecode/manifest.txt` counts: **42 rows** total - **10
  REFUSED** (compiler-level, unchanged from epic 1), **4 DIVERGES** (new
  this milestone, detailed above), **28 matches**, all now actually
  exercised by the golden harness except `two_module/geometry.wy` and
  `wildcard/palette.wy`'s twin fixtures (`report`/`paint`, already covered)
  and `samples/shapes.wy` (a corelib dependency with no `.out` of its own,
  exercised transitively through `eval_modules`); `arith`/`collections`/
  `multiret` carry the pre-existing tree-walker-only multi-value note,
  which is `matches` against the VM per the epic's own instruction, not a
  divergence.
- `git diff --check` clean.

### For epic 6 / whoever reviews this next

- The four `DIVERGES` entries above are the concrete todo list epic 6's
  hardening should start from - each already names the exact file/function
  and the shape of the fix needed, not just "gap."
- The `WY_OP_MSG`-only-understands-bytecode-bodies limitation (gap 1) and
  the itnext-can't-fiber-switch limitation (gap 3) both stem from the same
  root cause M3 already flagged: everything that resumes/dispatches into a
  coroutine or a native body was built to be reached from exactly one
  opcode (`WY_OP_CALL`), and every other call shape (`msg`, `itnext`,
  `CALL_VA`) still faults or silently no-ops. Worth fixing as one pass
  across all these call sites rather than three-plus separate patches, as
  M3's own report already recommended for the narrower CALL_VA/message-
  dispatch case.
- The import-hook signature (epic 6's embedding-API doc target):
  `typedef wy_error (*wy_import_hook)(wy_context*, const char* path,
  wy_uword len, wy_u8** out_bytes, wy_uword* out_len, void* ud);` - set via
  `context->import_hook`/`context->import_ud`; the hosted implementation
  (`wy_import_fs_hook`, `src/platform/hosted/import_fs.c`) resolves `a::b`
  to `<root>/a/b.wyc` across `-I` roots in order, no `.wy` fallback.
- `std::io` is functionally complete for epic 6's benchmark suite as
  scoped (the seven POSIX natives plus `println`); no fixture in the corpus
  needs `eprintln` or a `File` wrapper class.
- The coroutine GC test from M3 passed again unchanged
  (`cwyrm-golden-gcstress`, `WY_TEST_GC_THRESHOLD=0`, full corpus including
  the newly-wired fixtures); no fiber memory-accounting instrumentation was
  added this milestone.
- No sample changed category from what epic 5's own file predicted in the
  REFUSED/DIVERGES lists - the four new DIVERGES rows are additions the
  epic file didn't anticipate (it predicted "none expected beyond epic 4's
  eval_messages fix"), not corrections to a wrong prediction.

This closes epic 5. Epic 6 (host API, hardening, baseline benchmarks) can
start; its own state-scan should read the four DIVERGES reasons above
before deciding how much of them to fold into its own scope versus leaving
for epic 12 or later.
