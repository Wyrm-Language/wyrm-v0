# REPL plan: an append-only session module

Status: implemented (M-1 through M4); this document is now the design record. User-facing
documentation is `doc-llm/repl.md`. Audience: whoever changes it next. Read AGENTS.md
(build, tests, coding standards) and `doc-llm/agent-notes.md` first; the "Bootstrap trap"
in `doc-llm/agent-notes.md` and the memory note apply to any compiler change here.

## Goal

`wyrm` with no script (or `wyrm -i`) reads input a piece at a time and runs it in one
persistent session.

- **No re-execution.** Input N compiles and runs on its own; nothing earlier is compiled
  or run again. A user must never see a side effect twice.
- Everything defined earlier stays usable: variables, functions, classes, messages,
  imports, live coroutines and fibers.
- A fault or compile error in one input leaves the session intact.

Non-goals (first pass): line editing/history (plain `fgets` is fine), tab completion,
multi-file projects, decorators defined in the same session, bare-metal builds, Ctrl-C
interruption of running code.

## Ground rules

- **Normal compile/run must not change.** The REPL adds a session mode; the ordinary path
  (a script compiled to one module and run) keeps its behavior exactly. Prove it every
  milestone: the whole existing suite stays green (`behavior`, the C++ suites, `wy-tests`,
  `selfcompile`), the behavior corpus still agrees with the external interpreter wherever
  one is available (`scripts/wytest_env.py`: `$WYRM`, pypoc, or `wyrm` on PATH), and for any
  change under `src/embed/` the regenerated stage0 differs **only** in the files you meant
  to change (a compiler change that alters images of unchanged sources means script-mode
  output moved: stop and find out why).
- **REPL tests are local to this repo.** The REPL is a development tool, not part of the
  language contract, so nothing about it is cross-checked against pypoc or any other
  interpreter. Its tests (the whole-file versus piece-by-piece differential, the stdin
  transcripts, the scope tests) run against this repo's own build only and are `local-only`
  in the manifest where they use corpus rows. Cross-checking stays where it already is: the
  ordinary corpus rows.
- Follow AGENTS.md for style and the standing constraints (no recursion in VM execution,
  all allocation through `wy_allocator`, inline functions over macros, tests for new code).

## Prerequisite: a language-level `trap` (done, M-1)

The `trap` opcode already existed (`WY_OP_TRAP`, 0x01, operand `code` in the `f` field) and
the VM handles it (`src/vm.c:788`, `trap_fault_value_f`): code 0 is "unreachable code reached -
or a function body the compiler could not lower", code 1 is "debugger break", any other code
faults as `trap N`. The compiler only emitted it for its own stubs.

Now `trap([code])` is a compiler intrinsic (`_compile_trap`,
`src/embed/wyrm/compiler/expressions.wy`): a call whose name is not bound lowers to
`WY_OP_TRAP`. `code` must be an integer literal 0-255 (8-bit immediate), default 1; 0 is
reserved for compiler stubs, 2-255 are the program's. A local, parameter, capture or module
global named `trap` shadows it; `trap` used as a value is still "undefined name". The fault
ends the call, so code after it is dead; as an expression it never yields a value (the
compiler reserves the result registers and nil-fills one so callers keep their register
discipline). Tests: `scripts/check_wy_trap.sh` (meson test `wy-trap`). Uses in the REPL: an
explicit breakpoint in a dev session (fault "debugger break", session survives), and a clearer
target than an unset slot if forward-declared slots are ever made to hold a trapping stub.

## Chosen design: one module that only grows

The session is a single `wy_module` that is *extended* by each input. Because it is one
module, everything the compiler and VM already do for a module keeps working unchanged:
one global namespace, one class table, one message table (no per-input message adoption),
closures reading live globals.

The one hard problem is that a running VM holds raw pointers into a module's arrays, so
those arrays must never move. The fix is to **reserve the arrays' full capacity up front
and only append**. Overrunning a reservation is a clean error ("session limit reached;
restart"), never a realloc.

### Facts this rests on (verify before you build on them)

- Frames keep a raw `ip` into the code array: `src/vm.c:201` and `:339`,
  `new_frame->ip = fn->module->code + proto->code_offset`. Code must not move.
- `wy_function` holds `const wy_function_proto* proto` (`include/wyrm/function.h:27`),
  created from `&module->functions[i]` (`src/vm.c:1441`, `src/wclass.c:41`). The function
  proto array must not move. Audit `class_protos` the same way (`wy_class` -> proto).
- Statics are read by index through the module (`src/vm.c:239`, `:870`), so they are less
  fragile, but reserve them too rather than prove every reader.
- The loader points `module->code` *into the image* and never copies it
  (`src/module.c:816`); for a session the delta's code must be **copied into the reserve**.
  Audit every other `module->*` pointer that borrows from `image`.
- Loader steps and their order: `src/module.c:805` (`wy_module_load_image`). Free slots
  are filled from builtins by `wy_link_fill_from_builtins` (`src/link.c:94`), at load and
  again in `wy_module_run_init` (`src/module.c:868`).
- `wy_module_run_init` only knows the init at code offset 0 and moves the module to
  READY/FAILED (`src/module.c:868`). A session needs "run function K" and must not mark the
  whole session FAILED when one input faults.
- Module arrays are allocated with `wy_context_gc_alloc` and freed in the finalizer
  (`src/module.c` `finalize`). A 16 MiB reserve must **not** go through `gc_alloc` (it
  would count against GC pressure); allocate it from the machine allocator
  (`wy_context_get_machine(ctx)->allocator`) and free it in the finalizer. All allocation
  stays behind `wy_allocator` (AGENTS.md).
- The compiler state that must persist is `ModuleContext`
  (`src/embed/wyrm/compiler/context.wy:1064`: globals, classes, class statics, slot names,
  definitions, promoted arities, the image builder, ...). Entry point is `compile_module`
  (`src/embed/wyrm/compiler/module.wy`).
- The CLI compiles on an independent machine, created per call
  (`src/wyrm/main.c:269`, `:552`, `compile_source_bytes_` at `:157`). A REPL needs that
  machine to stay alive so the persistent compiler state survives between inputs.

### Reservation sizes

Code is the case the owner sized: **16 MiB = 4,194,304 32-bit words**. Others are
configurable constants (say `WY_SESSION_*` in one header) chosen so total reserved
virtual memory is around 64 MiB. Malloc-backed regions of this size are mapped lazily on
hosted systems, so only touched pages cost memory. Measure `sizeof` for each element
before fixing the numbers.

| Array | Element | Suggested cap | Notes |
|---|---|---|---|
| code | `wy_u32` | 4,194,304 words | 16 MiB |
| functions | `wy_function_proto` | 65,536 | each owns `params`/`dispatch_slots` sub-allocations |
| class_protos | `wy_class_proto` | 4,096 | also `classes` (realised objects) |
| statics | `wy_value` | 65,536 | the compiler caps pools at 65535 (16-bit operands) |
| symbols | `wy_symbol` | 65,536 | same cap |
| globals / fill_layer / fill_source | value / u8 / symbol | 65,536 | same idea as `wy_builtins_add`'s spare slots, but large |
| messages | `wy_message_ref` | 16,384 | |
| exports / free_names | slot dicts | pre-expanded with `wy_slot_dict_expand_f` | as `wy_builtins_new` does |
| wildcards | `wy_wildcard` | plain realloc is fine | link-time only; confirm nothing holds a pointer |

On overrun: return a dedicated error (add `WY_ERR_SESSION_FULL` or reuse `WY_ERR_NOMEM`
with a distinct message) *before* mutating anything.

## Delta images

Reuse the existing container and section schemas so the decoders (`decode_function_`,
`decode_class_`, ...) are reused, with these differences:

- A header flag `d: 1` (or a version bump; owner's call, see Open decisions) marks a delta.
- The header carries **base counts**: code words, functions, statics, symbols, classes,
  messages, globals as the compiler believes the session has *before* this delta.
- Sections contain only the new items. Every cross-reference (function index, static
  index, symbol index, global slot, class index, code offset) is **absolute**. Jumps are
  already ip-relative, so code is relocatable.
- `exports` lists the names newly defined; `free` the newly referenced free names.
- The loader rejects a delta whose base counts differ from the module's real counts
  (`WY_ERR_IMAGE`). This is the desync guard between the compile VM and the host VM.

## Loader: `wy_module_extend`

New API next to `wy_module_load_image` in `include/wyrm/module.h` / `src/module.c`:

- `wy_module_session_new(context, config, &module)`: reserves everything, registers the
  module, state READY, no code.
- `wy_module_extend(context, module, delta_image, &first_function_index)`:
  1. Validate the delta completely (every index against base + new counts, capacities).
  2. Commit by appending (copy code into the reserve; decode into the reserved arrays).
  3. Fill new free slots from builtins.
  Validation and commit are split so a bad delta leaves the module **unchanged**.
- `wy_module_run_function(context, module, function_index)`: creates the `wy_function` and
  calls `wy_vm_call_sync` like `wy_module_run_init`, but never sets FAILED; a fault is just
  returned. Answers the function's return value (see expression results).

## Compiler: a session compile mode

- New persistent object (`SessionContext` wrapping `ModuleContext`) created once, owned by
  the warm compile VM and rooted (`wy_context_root_push_f`).
- `compile_snippet(session, tree) -> delta bytes | error` compiles one parsed input:
  - each input gets its own init function (a new function-table entry, not offset 0);
  - top-level `fn`/`class`/`:=` append to the persistent tables exactly as in a script, so
    slot numbering just continues;
  - counters and tables only grow, and emission produces only what is beyond the previous
    watermark.
- **Transactional.** A compile error halfway through must not leave half-allocated slots,
  statics or classes behind: take watermarks first and roll every table back on error
  (or compile against an undo log). This is the easiest place to introduce a desync bug;
  test it directly.
- **Expression results.** If the last top-level statement is a bare expression, the
  snippet init returns its value, and the compiler also stores it in global `_`. The host
  prints non-nil results. No extra natives.
- `compile_module`'s assumption that init is function 0 at offset 0 must be generalised,
  not forked; the script path must produce byte-identical output to today (the
  `selfcompile` fixed point and stage0 check will tell you).

Any change under `src/embed/` means regenerating stage0 (`scripts/regen_builtins.py`) and
committing the regenerated `.c` files; see AGENTS.md "Testing" and the bootstrap trap in
the memory note.

## Front end: is the input complete?

The driver must know whether to prompt for more lines. Add a distinguishable
end-of-input parse error (for example an error kind `'eof` from `Parser(...)!program()`)
so an unterminated block, bracket or string means "continue", while a real syntax error
is reported. This overlaps the "parse failures surface as the wrong error" item in
`active_issues.md`; fix that item together with this one rather than layering a heuristic
on the wrong error. Blank line ends an indented block.

## Driver

Either in C (`src/wyrm/main.c`) or as an embedded tool (`src/embed/wyrm/tools/repl.wy`)
driven by C; prefer whichever keeps the loop out of hand-written C. Per input:

1. Read a line; parse; if the parse says incomplete, prompt `... ` and append.
2. `compile_snippet` on the warm compile VM -> delta bytes.
3. `wy_module_extend` on the session module (host VM).
4. `wy_module_run_function`; print a non-nil result, or the fault message.
5. On any failure, print it and continue; the session survives.

Commands: `:quit` (and EOF), `:reset` (new session, drops the old one).

The host context is created once with the builtins and `wy_io_natives_install`
(`src/embed/std/io_native.h`) so `import std::io` works; see "Embedding std::io" in
`doc/vm_impl.md`.

## Redefinition: each input is a scope (shadowing, not rebinding)

The language already has the right rule. `doc/language-spec.md:296-298`: declaring a name
already declared in the *same* scope is an error; declaring a name visible from an
*enclosing* scope is legal and shadows it. So the REPL needs no exception to the spec:

> **Each input is its own scope, nested inside the scopes of the inputs before it.**
> Redeclaring a name in a later input shadows the earlier binding; redeclaring it twice
> within one input is the usual same-scope error.

Consequences (this is lexical shadowing, as in ML-family REPLs, not Python's rebinding):

- `fn a`, then `fn b` calling `a()`, then `fn a` again: the new `a` is a **new binding**.
  `b` was compiled against the earlier binding and keeps calling the old `a`. New inputs see
  the new `a`. To make callers see a new value, use **assignment** to the existing binding
  (`a = fn(): ...`), which is legal, writes the same slot, and callers read the slot at call
  time (verified: `fn a` / `fn b(): return a()` / `a = fn(): 2` makes `b()` return 2 in the
  current compiler; confirm against the spec that a `fn` name may be assigned).
- A shadow may change type or kind (`x := 1` then `x := "s"`, a `fn` shadowed by a variable).
- Redefining a class shadows it: earlier instances keep the old class and methods (verified),
  earlier subclasses keep the old base, new inputs get the new class.
- Nothing about scripts changes: a script is a single input, so the same-scope duplicate
  check applies to it unmodified (currently unenforced; see `active_issues.md`).

### How it maps onto the compiler

The session module is still one module with one global slot array; scoping is compile-time
only.

- Keep `ModuleContext.globals` (name -> slot) as the map of *currently visible* bindings, and
  give each input a `declared_here` set.
- Declaring `n`:
  - `n` in `declared_here` -> the spec's same-scope error (identical code path for scripts);
  - otherwise **allocate a fresh global slot**, set `globals[n]` to it, add to `declared_here`.
    (A first-ever declaration of `n` is the same thing, plus the forward-reference rule below.)
- Code emitted earlier already names its slots by number, so it is untouched. New code
  resolves names through the updated map. `exports` (name -> slot) tracks the newest binding.
- Rollback on a failed input is now easy: restore `globals` entries changed by this input
  (keep an undo list of `(name, previous slot or none)`), drop `declared_here`, roll the
  counters back.
- Slots and old function bodies are never reclaimed while the session lives (a live closure
  or coroutine may run them). Bounded by the reservation (65,536 globals, 16 MiB code by
  default); `:reset` recovers. Heavy redefinition costs one slot and one function body each.

### Forward references

A script resolves `b` calling a later `a` because the whole module is declared up front. In a
session `b` may be entered before `a` exists.

- An unknown name in session mode becomes a **forward-declared global**: reserve a slot (Unset)
  on first reference and record it as *pending* for that name. A call before it is bound faults
  as an unbound global at run time, not a compile error.
- The **first** declaration of `n` *fills the pending slot* (so `b`, which read that slot,
  now works). Every later declaration of `n` shadows, per the rule above.
- Decide and test: what a pending forward reference does when a *different scope* declares the
  name later is exactly this fill rule; do not silently fall back to "undefined name".

### Other things to handle

1. **Name-keyed compile bookkeeping** (`definitions` for `foo::$ast`, `slot_names`,
   `superclasses`, `class_statics`, `classes`, `promoted_arities`; see `ModuleContext`,
   `src/embed/wyrm/compiler/context.wy:1064`): with shadowing, the newest entry wins for new
   code. Check each for "already defined" errors or accumulation, and make a shadowed class get
   a new class-table entry with the name moved to it.
2. **Typed/overloaded functions (message promotion).** Shadowing a typed overload of an
   existing message is the delicate case: is it a new message, or does it add an overload to
   the existing one? Adding an identical signature is ambiguous at dispatch. Not verified (the
   syntax I tried did not parse); write the experiment first and decide. Recommended:
   a same-signature typed `fn` in a later input replaces the overload for new dispatch.
3. Changing a function's arity does not break compilation of callers; a mismatch is a run-time
   fault at the call ("missing required argument"). There is no static arity check to
   invalidate. Default parameter values must be constants, so nothing captures a function by
   value at definition time.

## Semantics to document

- One slot array, but each input is a scope: redeclaring shadows (earlier code keeps the old
  binding); assignment rebinds. No deviation from the language spec.
- Decorators come from `-I` roots or the embedded table; a decorator defined in the same
  session cannot be used (expansion VMs are hermetic).
- A fault mid-input keeps whatever that input already stored.
- Live coroutines, fibers and closures created earlier keep running after later inputs
  extend the module (this is the reason for the reservation).

## Milestones

Each milestone should build, pass `meson test` on a release tree and the debug tree, and
be committed on its own. Take timings and asserts from a release tree built with
`-Db_ndebug=false` (AGENTS.md).

**M-1 - `trap` intrinsic** (see "Prerequisite"). Independent; land first.

**M0 - Reservation and session module.** `wy_module_session_new`, config header, finalizer
frees the reserve, overrun returns the error before mutating. C++ unit tests: capacity
limits; addresses of `code` and `functions` never change across many appends.

*Status: done.* `include/wyrm/session.h`, `src/session.c`, `test_session.cpp` (suite `session`);
`wy_module::session` marks a session and `free_table_` in `src/module.c` frees reserved arrays
through the machine allocator; `WY_ERR_SESSION_FULL` appended to the error enum. Measured on
hosted Linux: creating a default session (about 44 MiB reserved with the original table sizes; 27 MiB now that statics and symbols are sized to the compiler's 65535-entry limit) leaves max RSS unchanged
(11.7 MB either way), so the reserve is lazily committed. Unused array memory is never touched
(globals are not pre-filled; GC scans only the counted prefix).

**M1 - Delta format and `wy_module_extend`.** Document the delta in `doc/` next to the
container notes. Tests hand-pack deltas the way `test_coroutine.cpp` and
`test_io_native.cpp` do: append works; a second extend sees the first's globals;
rejected on base-count mismatch; a bad delta leaves the module unchanged; a coroutine
suspended before an extend resumes correctly after it.

*Status: done, with two gaps noted.* Delta format and the loader are in `src/module.c` ("Session
extension" comment block: header keys `d`, `bc`/`bf`/`bs`/`by`/`bk`/`bm`/`bg`, `g`, `i`),
`wy_module_extend` / `wy_module_run_function` in `include/wyrm/session.h`. The ordinary
decoders are reused (their bounds checks see the growing counts), the message-entry decoder was
factored out, and `wy_slot_dict_set` was added so a shadowing input replaces a name's slot.
Tests (`test_session.cpp`, hand-built deltas): append and in-place extension with stable
`code`/`functions`/`statics` pointers, absolute numbering of statics and symbols, desync
rejection (stale, ahead, not-a-delta), full rollback of a malformed delta, overrun before any
change, and a faulting input not poisoning the session. **Gaps:** (1) class and message
sections go through the same append path but are not exercised until M2 produces real deltas
(hand-building a class document is not worth it); (2) the "coroutine suspended before an extend
resumes after it" test is not written (needs a hand-packed yielding coroutine); do it in M2 with
compiled code.

**M2 - Session compile mode.** `SessionContext`, `compile_snippet`, rollback, `_`,
generalised init. Regenerate stage0. **Differential test (the main correctness proof):**
split every runnable `test/corpus` source at top-level statement boundaries, feed the
pieces to one session, and require the same stdout as running the file whole. Also test
that a failed compile changes nothing (compile a bad snippet, then a good one that reuses
the numbering). Scope tests: shadowing (`fn a` / `fn b` calls `a` / `fn a` again: `b` keeps the
old one, new inputs get the new one); assignment rebind (`a = fn(): ...` reaches `b`); a
same-input redeclaration is the spec error; forward reference filled by the first later
declaration and shadowed by the next; a shadowed class leaves old instances alone.

*Status: done (M2 as implemented).* Deviations and additions relative to the plan:

- **One context, not two.** The compiler runs in the *same* context that hosts the session (the
  host is outside the VM when it drives, so `wy_vm_call_sync` is fine). There is no second compile
  machine, so there is no compile-side/host-side counter drift to guard against (the delta base
  counts remain as a cheap check). M3's driver should do the same: keep the session value rooted
  (`wy_context_root_push_f`) and call `session_compile` from C.
- **Implementation:** `SessionContext` / `compile_snippet` / `new_session` in
  `src/embed/wyrm/compiler/module.wy` (snapshot/restore for rollback, per-input scope via
  `ModuleContext._declare_session_global`, forward references via `declare_forward_global` and the
  session branch of `resolve_name`); delta header keys in `image.wy`'s `_header_doc`; entry points
  `session_new`, `session_compile`, `session_compile_tree`, `session_pieces` in
  `src/embed/wyrm/tools/compile_source.wy`. The ordinary path is untouched: after regen only
  context, expressions, module, image and compile_source images changed, `selfcompile` and the
  whole behavior corpus still pass.
- **Loader additions found necessary:** a session remembers its imports and re-runs the
  free-name fills after every extend (`wy_session_note_import_` / `wy_session_refill_`), because an
  import runs in one input while `std::io::println` is first referenced in a later one; and the
  session seeds `__name__` = "__main__" (the REPL is the entry module).
- **Not done:** `_` (open decision 3), a delta-aware `verify` (session inputs skip verify; the
  loader bounds-checks), `__ARGS` seeding (the driver's job), redefinition of `definitions` for
  `foo::$ast`.
- **Tests:** `session-compile` (10 cases in the default `cwyrm` test: value return, functions across
  inputs, shadowing keeps the old binding for old callers, assignment rebinds, forward reference
  filled by the first definition and shadowed by the next, run-time fault for a not-yet-defined
  name, rollback of failed compiles, classes/methods across inputs, imports, and a coroutine
  suspended across extends) and `session-differential` (its own meson test `cwyrm-session-differential`,
  ~3 minutes on a debug build): every runnable corpus source split into one statement per input
  (exact boundaries from the parser) must print exactly its committed `.out`, plus a subset under
  GC stress (a collection at every safepoint; the whole corpus at that setting does not finish in
  reasonable time because the compiler itself runs in the collected VM).
- **Skipped in the differential** (host seams a bare session lacks): `samples/eval_args.wy`
  (`__ARGS`), `io_file.wy` and `bytes_header/cvm_header.wy` (write files), `expand/*`.

*Status: not started. Findings from reading the compiler (use these, they save a day):*

- **Shape of `compile_module`** (`src/embed/wyrm/compiler/module.wy`): a fresh `ModuleContext`,
  then whole-module passes over the tree (`_declare_module_names`, `_collect_definitions`,
  `_hoist_imports`, `_bind_imported_names`, `allocate_block_scopes`), each statement compiled into
  `module.init_ctx`, then `_assemble` (init code first, pending function bodies after, functions'
  `code_offset` set there) and `verify(img)`. Init is *not* a function-table entry; the loader
  runs offset 0. A session input needs its init as a real function entry (`i` in the delta header)
  and `_assemble` must produce sections for only the new items.
- **`ModuleImage`** (`context.wy:186`) already has the persistent pools and dedup dicts
  (`statics`, `symbols`, `messages`, `globals`, `functions`, `classes`, `_static_index`,
  `_symbol_index`, `_message_index`, `_global_index`, `_free_index`) whose indices are just
  `len(pool)`, i.e. already absolute if the image persists. Persisting it also keeps **message
  identities shared across inputs** (a method defined in input 1 must be the same message as the
  call in input 2; a per-input image would duplicate them and break dispatch), and dedups statics
  and symbols.
- **Recommended approach:** keep one `ModuleContext` (and its `ModuleImage`) for the whole
  session; per input (1) record watermarks of every pool and code length, (2) build a fresh
  `init_ctx` and reset `pending`, (3) run the passes over just this input's tree, (4) append the
  input's init as a function entry, (5) `_assemble` the *slices beyond the watermarks* with
  absolute indices and code offsets = session code length + offset within the input, (6) emit the
  delta header (`d`, base counts, `g`, `i`).
- **Scopes/shadowing:** `declare_global` returns the existing slot for a known name; session mode
  needs "new slot if declared in an earlier input, error if declared earlier in *this* input"
  (`declared_here`). `declare_shadow_global` already exists for block-scoped shadows (anonymous,
  never exported) and is a model, but a session shadow must stay exported under the name.
- **Forward references:** `resolve_name` reports "undefined name" for anything not local, module,
  builtin or wildcard (`expressions.wy:305`); session mode must instead reserve a pending global
  (`declare_free_global`-like, but fillable by a later declaration) as described above.
- **Rollback is the risk.** Persisting the context means a failed input has already appended to
  the pools and dicts and may have *mutated older entries* (an `image.globals[i]` dict gets
  `exported`/`free` flags when a forward reference is filled). Snapshot before each input:
  the lengths of the lists, plus shallow copies of the name dicts (`globals`, `classes`,
  `class_statics`, `slot_names`, `superclasses`, `definitions`, `promoted_arities`, the five
  `_*_index` dicts) and a small undo list for mutated global entries; restore on any error.
  Test it directly (compile a bad input, then a good one that must reuse the same numbering).
- **`verify(img)`** assumes a whole image (globals, code, function table). For the first pass run
  it with `check: false` for session inputs (the C++ loader still bounds-checks every delta) and
  add a delta-aware verify later.
- **Return value / `_`:** compile a trailing bare expression as `return <reg>` (nres 1) instead
  of the fixed `return a0=0 f=0` at the end of init; `_` (open decision 3) can follow.
- **Cost of iterating:** every change under `src/embed/` needs a stage0 regen (~2 minutes on a
  release binary), and the compiler's own images must not change for scripts (check
  `git status src/embed` after regen: only the files you edited may differ).
- **New API surface** to add in `src/embed/wyrm/compiler/module.wy` (or a new
  `session.wy` embedded module): `SessionContext()`, `compile_snippet(session, tree) -> bytes
  | error`, and the delta serialiser next to `to_wyc` in `image.wy`.

**M3 - Driver and completeness.** `wyrm -i` (or no-argument), warm compile VM, the eof parse
error, printing, `:quit`/`:reset`, fault survival. Tests are piped-stdin transcripts:
`test/corpus/repl/*.in` with the expected `*.out`, run by a small script wired into meson.

*Status: done (M3 as implemented).* `wyrm -i` (`src/wyrm/repl.c`, `repl.h`; `main.c` calls it after the
usual context setup). Notes and deviations:

- **Completeness** is decided from the token stream (`session_incomplete` in `compile_source.wy`:
  open bracket, a raw string or character literal that ran off the end, or a block in progress),
  not from the parser: the parser answers a non-module tree for input it cannot parse instead of a
  distinguishable end-of-input error. A blank line always submits, so a class or function body
  cannot contain a blank line (the same rule as Python's REPL).
- Prompts (`>>> `, `... `) are printed only when stdin is a terminal, so piped transcripts are just
  output. At end of input a half-typed input (an unclosed bracket) is submitted, so the user sees
  the error instead of silence.
- Non-nil results are shown: strings in single quotes, everything else rendered exactly as
  `println` renders it. Compile errors print `error: ...` to stderr; run-time faults print
  `fault: ...`; a bad line never ends the session.
- Commands: `:quit`/`:q`, `:reset` (fresh session; the old module stays registered until exit),
  `:help`. `__ARGS` is an empty list and `__name__` is `"__main__"` in a session.
- Tests: `scripts/check_wy_repl.sh` (meson test `wy-repl`) pipes `test/corpus/repl/*.in` and compares
  combined output with `*.out`. Local only, per the ground rules.
- **Not done:** `_`, line editing/history, Ctrl-C to interrupt a running input, and a proper
  parse-error message with a location (the message is "syntax error: the input could not be
  parsed"; see the parse-failure item in `active_issues.md`).

**M4 - Limits and polish.** Overrun message, `:reset` frees the old session, docs
(`doc/vm_impl.md`, AGENTS.md), update `active_issues.md` (remove what this fixes, add what
it leaves).

*Status: done.* Reservation overrun prints "session limit reached; :reset to start over" and refuses
only the input that did not fit. Two correctness fixes surfaced here: (1) when the loader refuses a
delta the compiler has already counted, the two sides drift and every later input fails, so the
compile side keeps what it needs to undo the last compile (`session_undo_last`) and `repl.c` calls
it (test: "a refused extend ... is undone on the compile side", which fails without the undo);
(2) `:reset` now unregisters the old module (`wy_context_module_unregister`; ids are kept, the slot
becomes NULL and the registry loops skip it) so the next collection frees its reservation (test:
"unregistering a session lets the next collection free it"). `WYRM_SESSION_CODE_WORDS` overrides the
code reservation; `scripts/check_wy_repl.sh` uses it to check the limit path end to end. Docs:
`doc-llm/repl.md` (user guide), `doc/vm_impl.md` ("Session modules"), AGENTS.md, this plan.

## Risks and traps

- **Pointer audit is the whole game.** grep every use of `module->functions`,
  `->class_protos`, `->code`, `->statics`, `->symbols`, and `->messages`. Anything that
  keeps a pointer across a possible extend must go through the reserve or be re-read.
- **Borrowed image data.** After `extend`, the delta buffer may be freed; make sure no
  module field still points into it (code is the known case).
- **Compile/host desync.** The compile VM and the host VM each count functions, statics,
  and so on. The base-count check is the only guard; keep it strict and test the failure.
- **Rollback completeness** in the compiler (see above).
- **GC.** Module scanning uses the counts; make sure it scans only the used prefix of each
  reserved array and that objects reachable only from the session module are rooted.
- **Init-state machine.** Do not reuse `WY_MODULE_FAILED` for a session; one bad input
  must not poison the rest.
- **Memory.** Confirm on a real run that untouched reserve is not committed (RSS check).
  If a target's allocator commits eagerly, shrink the defaults for it.
- **Stage0 and the bootstrap trap** whenever the compiler or the tools' APIs change.

## Open decisions (owner)

1. **Redefinition** is decided by the spec: each input is a scope and shadowing is legal (see
   "Redefinition"). Remaining decisions: (a) typed overloads redeclared with the same
   signature (replace, recommended, or reject); (b) whether the REPL should offer a
   convenience that redefines a function *and* rebinds it for existing callers (e.g. a
   `:patch a` command that assigns rather than shadows). Pure shadowing first; add the
   convenience only if users ask.
2. **Delta marker:** header flag `d: 1` versus a container version bump.
3. **`_`**: keep as the last-value variable, or drop it.
4. **Non-hosted builds:** disable the feature (assumed) or allow a smaller reservation.
5. Should `:reset` and `:quit` be commands or expressions (`quit()`)?
