# Wyrm Codebase Explainer (Agent Orientation)

Status snapshot as of 2026-09-15. This doc exists so an agent picking up work
here doesn't have to re-derive it from scratch. If you learn something new
about how far the implementation has progressed, update this file.

## What Wyrm Is

Wyrm is an embeddable C11 object system + scripting engine (think "QObject
equivalent"): stable object identity, a property system, signal/slot
dispatch, an introspectable type hierarchy. It's meant to be embeddable from
C, C++, Python, Rust. The scripting surface (the `.wy` language) is
Scheme-inspired but with Python-like offside-rule syntax. See
`AGENTS.md` for coding standards and `doc/design.md`, `doc/language-spec.md`,
`doc/grammar.md` for the language/object model design intent.

**This is a prototype.** Large parts of the design docs (classes, messages,
coroutines, decorators, native-code blocks) describe the *intended* language;
the C implementation currently covers only the low-level runtime substrate
(context/fiber/stack/gc/module plumbing) and a handful of opcodes. The `.wy`
sources under `wy/` are a self-hosted tokenizer/parser/compiler written *in*
the language itself, used to bootstrap — they are not yet wired to the C
loader (see "Bytecode / VM state" below).

## Build

- Build system: Meson (`meson.build` at root, C11 + C++23, `subdir('src')`).
- `src/meson.build` accumulates a `modules` list; each module subdir sets a
  `<namespace>_<name>` dict with `sources`, `inc`, `deps`, `public_inc`,
  `test_sources`, `test_inc`, `test_deps`. All tests link into one
  `test_cwyrm` executable regardless of which module they came from.
- Standard flow:
  ```sh
  meson setup buildDir
  meson compile -C buildDir
  meson test -C buildDir
  ```
- Options: `-Dhosted=true` (malloc/free-backed allocator vs bare-metal),
  `-Dglib=enabled|auto|disabled` (GLib main-loop integration).
- Run a single doctest case: `./buildDir/src/test/test_cwyrm --test-case="name"`.

**As of this session, the tree now builds and the full test suite passes.**
The only defect found was a single missing constant:
`WY_CONTEXT_MODULE_INITIAL` was referenced in `src/context.c`
(`wy_context_module_register`, the module-list growth path) and in
`src/test/test_context.cpp`, but never defined anywhere. Fixed by adding
`#define WY_CONTEXT_MODULE_INITIAL 4` to `include/wyrm/context.h`. If the
tree fails to compile again for you, check first whether this define (or a
similarly-named constant introduced later) has regressed — it's the kind of
thing that goes missing during header refactors.

Ignore any LSP/clangd diagnostics about missing headers/unknown types in
isolated header files (e.g. `context.h` complaining `wyrm/fwd.h` not found) —
that's the editor's compile_commands / include-path setup, not a real build
failure. Trust `meson compile`, not the editor's live diagnostics.

## Top-Level Design

### Layering

```
wy/*.wy                    <- language written in itself (bootstrap compiler,
                               stdlib), not yet consumed by the C runtime
include/wyrmxx, src/wyrmxx <- C++ bindings/wrapper layer over the C core
include/wyrm, src/*.c      <- the C core ("libcwyrm"): object model, VM,
                               allocators, gc, module/bytecode container
src/port/hosted,           <- porting layer: pick one "port" (currently only
src/platform/*                "hosted" = standard C11 malloc/free); platform/*
                               are optional add-ons (glib main loop, static
                               allocator, null allocator) that can be enabled
                               independently
```

### Runtime object graph

- **`wy_machine`** — top-level structure; owns the list of live objects and
  contexts.
- **`wy_context`** (`include/wyrm/context.h`, `src/context.c`) — one thread of
  engine execution; holds a lock during execution, a GC arena
  (`wy_gc_arena`), the registered module list (dense integer ids, grows by
  doubling from `WY_CONTEXT_MODULE_INITIAL`, capped at
  `WY_EXEC_FN_MODULE_MAX`), and the "current fiber" it's driving. Also hosts
  the generic memory-reservation helpers (`wy_context_mem_reserve_f` and
  friends) used throughout for growable arrays (`wy_mem_info`).
- **`wy_fiber`** (`include/wyrm/fiber.h`, `src/fiber.c`) — an execution stack
  (values + call frames) contained within a context. Fibers are the unit of
  suspension/resumption; only one fiber is "current" on a context right now
  (scheduling among multiple fibers is a TODO called out directly in
  `src/context.c`'s `context_triggered`).
- **`wy_module`** (`include/wyrm/module.h`, `src/module.c`) — owns a bytecode
  buffer (`wy_u32[]`) and a globals array. `wy_module_load` deserializes a
  module from a `"WYC\x02"`-tagged BSON-ish container (see `src/bson.c`,
  `include/wyrm/bson.h`) — **it expects already-compiled bytecode, not
  `.wy` source text.** There is currently no C-side compiler; `main.c`
  passing a raw script file to `wy_module_load` will fail the magic-header
  check. The `.wy`-language self-hosted compiler under `wy/wyrm/` (tokenizer
  → parser → ast → compiler → decode) is the intended producer of that
  container format, but nothing currently drives it from the C side — see
  AGENTS.md's note to prefer an external, already-built `wyrm` install for
  actually running scripts.
- **`wy_value`** (`include/wyrm/value.h`) — a tagged union: `wy_type_tag type`
  + `wy_primitive data`, sized to fit a machine register. `NotSet` is
  `{NULL, NULL}`; `nil` is `{PAIR, NULL}` (a typed pair with a null register,
  distinct from a heap-allocated `(NotSet . NotSet)` pair). Pointer-bearing
  register values may only be NULL for `PAIR` or `ERROR` type tags — this is
  a documented system invariant (`doc/design.md`).
- **`wy_exec_fn`** (`include/wyrm/exec_fn.h`) — the uniform C calling
  convention: `wy_exec_state (*)(wy_context*, wy_primitive c_data)`. A
  callable is `{fn, c_data}`. Bytecode callables pack a `(module_id,
  address)` pair into the `c_data` primitive
  (`wy_exec_fn_b_code_pack`/`_module_id`/`_address`) so a callable can be
  either a native C function or a pointer into a module's bytecode without a
  separate tag. `wy_exec_state` is `WY_EXEC_DONE` / `WY_EXEC_CONTINUE` /
  `WY_EXEC_TAIL_CALL` — this return value is how the fiber's exec loop
  decides whether to keep running, chain to a continuation, or reuse the
  current frame for a tail call. **No recursion is used for VM execution
  by design** (stated security property in AGENTS.md) — control flow between
  calls is trampolined through this `wy_exec_state` result rather than
  nested C call stacks.
- **GC** (`include/wyrm/gc.h`, `src/gc.c`, `include/wyrm/gc_flags.h`) — arena
  attached to the context; objects get GC headers via
  `wy_context_object_init_header_f`. All dynamic allocation is required to go
  through the `wy_allocator` vtable (`include/wyrm/allocator.h`), never raw
  malloc, per AGENTS.md.

### Bytecode / VM state — **the most important thing to know before touching this**

As of epic 1 (`vm_plan/epic_1.md`), this repo loads and inspects real `.wyc`
module images compiled by `pypoc/` (`pypoc/.venv/bin/wyrm --build-bc`), but
still executes nothing. See `doc/vm_impl.md` for the file-by-file map, the
`wy_module` table layout, and how to run one fixture; this section is the
short version.

`include/wyrm/opcode.h` and `include/wyrm/image.h` are synced verbatim from
pypoc's compiler (`scripts/sync_pypoc_headers.py`; `src/test/test_headers_sync.cpp`
fails the suite if they drift) — 88 real opcodes, the actual `wyc-format.md`
instruction encoding (1 or 2 packed `wy_u32` words, bit 7 of the opcode byte
selects the length). `include/wyrm/opcode_names.h` is a generated
`wy_opcode_names[256]` mnemonic table for the disassembler.

`src/image.c` (`wy_image_from_bytes`) parses the container: magic, version,
directory, section bounds/alignment — zero-copy, every `wy_section_ref`
points into the caller's buffer. `src/module.c` (`wy_module_load_image` /
`wy_module_load_bytes`) decodes every section into the `wy_module` in
`include/wyrm/module.h`: globals (all Unset), statics, symbols, function and
class prototypes, message identities, and the exports/free name→slot dicts.
Every table index is bounds-checked at load. **Nothing runs yet**: no
builtin fill, no `import`, and no init call — `src/vm.c`'s
`wy_vm_exec_bytecode` is still a stub that returns `WY_ERR_INVAL`
unconditionally. That interpreter loop, the three-layer name fill, and
running word offset 0 as the module init call are epic 2
(`vm_plan/epic_2.md`).

`./buildDir/src/wyrm/wyrm file.wyc --sections` prints a one-line per-section
summary; `--disasm` prints one line per instruction (mnemonic + raw
operands, no symbol resolution yet); with neither flag it loads the module
and prints `not run: interpreter lands in epic 2`. `scripts/check_disasm.sh`
diffs `--disasm`'s mnemonics against every fixture's compiler-emitted
`.wy_a` listing.

Practical implication: if you're asked to "run a `.wy` script" end-to-end
with no Python, that still needs epic 2's interpreter loop. Today, running
anything happens either through pypoc (`pypoc/.venv/bin/wyrm file.wy`) or,
on the C side, by embedding via the C API and driving fibers with native
`wy_exec_fn` C callables directly (see `src/test/test_wvm.cpp`'s "a bytecode
callable resolves its module and returns" for the shape of that path).

### Call/stack model

`wy_fiber` implements a reserved-result-slot calling convention (see the
doctest names in `src/test/test_fiber.cpp`: "push_frame_f reserves results
below the new base", "results land in the reserved slots, in reverse order",
"a tail call returns through the frame it reused"). Callers reserve N result
slots before the call; the callee writes into them via
`wy_context_set_result`/`wy_context_result_n`; unwritten slots are refused if
read. `wy_context_call_continue`/`wy_context_call_continue_exec_fn` chain a
call followed by a continuation callback — this is how multi-step C-native
sequences (like `main.c`'s mul-then-print) are expressed without recursion.

### Module system (build, not runtime)

Distinct from `wy_module` above: `src/meson.build`'s module-accumulation
convention (see AGENTS.md "Build Module System" section) is how *source
directories* register into the build (`sources`, `inc`, `deps`,
`public_inc`, `test_sources`, ...). Don't confuse this with the runtime
`wy_module` bytecode container.

### Language front-end (`wy/` and `test/wy`, `test/samples`)

`wy/wyrm/{tokenizer,parser,ast,compiler,decode,_dsl}.wy` is a self-hosted
front end written in the Wyrm language itself, per the bootstrap philosophy
in AGENTS.md ("prefer the default 'wyrm' in user `$PATH`... this repo's own
implementation may be incomplete/experimental"). `test/wy/*.wy` exercises the
tokenizer/parser/DSL; `test/samples/parser/*.wy` + matching `.wy.ast` files
are parser golden tests (input → expected AST sexpr). None of this is
exercised by the meson/`test_cwyrm` build — it requires a working `wyrm`
binary (chicken-and-egg with the point above: the binary in this repo can't
yet run `.wy` source end-to-end, so bootstrapping currently depends on an
external, already-built `wyrm` install per AGENTS.md's guidance).

## Test Layout

- All C/C++ tests compile into one executable, `src/test/test_cwyrm`
  (doctest framework), regardless of which module directory they live in.
- Fixtures under `src/test/test_common/*.h` (allocator/context/fiber/machine/
  main-loop fixtures) are shared setup for doctest cases.
- 47 test cases currently, all passing (`meson test -C buildDir`), covering:
  allocators (static/cmem/hosted), atomics, bson, class/slot basics,
  context/module registration, fiber frame/continuation/tail-call mechanics,
  gc tracking, machine init, stack push/pop, string/box/table/pair
  primitives, and the glib main-loop integration. Nothing yet exercises
  `src/vm.c` opcode execution beyond what's implicit in fiber tests — there's
  no dedicated "run this bytecode and check the result" test, consistent
  with the VM loop being unimplemented.

## Conventions Worth Knowing (see AGENTS.md for the full list)

- `snake_case` everywhere; `wy_` prefix for public C names, `WY_` for macros.
- Suffixes: `_` internal/non-ABI, `_f` fast path (preconditions caller-
  verified), `_s` fast path + static allocation.
- No recursion in VM execution (security property) — doesn't apply to
  tests/host app code.
- No unguarded dynamic allocation — must go through `wy_allocator`.
- `WY_ASSERT` for internal invariants only; user-facing/input errors must
  return an error code, not assert.
- Never create GitHub issues/PRs from this repo on your own — AGENTS.md says
  to decline and point back to the developer.
