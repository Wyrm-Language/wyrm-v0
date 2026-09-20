# REPL plan: an append-only session module

Status: plan, not started. Audience: the agent that implements it. Read AGENTS.md
(build, tests, coding standards) and `doc/agent-notes.md` first; the "Bootstrap trap"
in `doc/agent-notes.md` and the memory note apply to any compiler change here.

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

## Prerequisite: a language-level `trap`

The `trap` opcode already exists (`WY_OP_TRAP`, 0x01, operand `code` in the `f` field) and
the VM already handles it (`src/vm.c:788`, `trap_fault_value_f`): code 0 is "unreachable code
reached - or a function body the compiler could not lower", code 1 is "debugger break", any
other code faults as `trap N`. What is missing is a way to write one from a program: the
compiler only emits it internally for stubs (`src/embed/wyrm/compiler/functions.wy:449`), and
`trap()` today is "undefined name 'trap'".

Add a compiler intrinsic `trap([code])`:

- Lowers to `WY_OP_TRAP` with the immediate. `code` must be an integer literal 0-255 (it is an
  8-bit immediate, so it cannot be a runtime value); default 1 (debugger break). Anything
  else is a compile error saying so. Reserve 0 for compiler stubs and document 2-255 as
  user-defined.
- Recognised before name resolution, like the other intrinsics in `expressions.wy`; decide
  whether a user binding named `trap` shadows it (recommended: yes, as `@template` is
  shadowable) and test it.
- Ordinary control flow after it is unreachable (the fault ends the call), so no result is
  produced; `x := trap()` is a compile error.
- Uses in the REPL: an explicit breakpoint in dev sessions (fault message "debugger break",
  session survives), and a clearer target than an unset slot if you decide forward-declared
  slots should hold a trapping stub. It is independent of the REPL and can land first as its
  own commit.
- Tests: a corpus row (local-only; pypoc need not know it) whose fault is caught by the runner
  as a nonzero exit, C++ golden cases for the codes, and a shadowing case. Changing
  `expressions.wy` requires the stage0 regen and the "script output unchanged" check above.

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
| statics | `wy_value` | 1,048,576 | |
| symbols | `wy_symbol` | 262,144 | |
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

**M1 - Delta format and `wy_module_extend`.** Document the delta in `doc/` next to the
container notes. Tests hand-pack deltas the way `test_coroutine.cpp` and
`test_io_native.cpp` do: append works; a second extend sees the first's globals;
rejected on base-count mismatch; a bad delta leaves the module unchanged; a coroutine
suspended before an extend resumes correctly after it.

**M2 - Session compile mode.** `SessionContext`, `compile_snippet`, rollback, `_`,
generalised init. Regenerate stage0. **Differential test (the main correctness proof):**
split every runnable `test/corpus` source at top-level statement boundaries, feed the
pieces to one session, and require the same stdout as running the file whole. Also test
that a failed compile changes nothing (compile a bad snippet, then a good one that reuses
the numbering). Scope tests: shadowing (`fn a` / `fn b` calls `a` / `fn a` again: `b` keeps the
old one, new inputs get the new one); assignment rebind (`a = fn(): ...` reaches `b`); a
same-input redeclaration is the spec error; forward reference filled by the first later
declaration and shadowed by the next; a shadowed class leaves old instances alone.

**M3 - Driver and completeness.** `wyrm -i` (or no-argument), warm compile VM, the eof parse
error, printing, `:quit`/`:reset`, fault survival. Tests are piped-stdin transcripts:
`test/corpus/repl/*.in` with the expected `*.out`, run by a small script wired into meson.

**M4 - Limits and polish.** Overrun message, `:reset` frees the old session, docs
(`doc/vm_impl.md`, AGENTS.md), update `active_issues.md` (remove what this fixes, add what
it leaves).

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
