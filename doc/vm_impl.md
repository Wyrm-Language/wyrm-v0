# Virtual Machine Implementation

## Code Organization / Standards

Basic Stuff:
  - generally 1 header 1 source per software piece
  - all functions namespaced with wy_ followed by the module (wy_fiber, wy_context, etc...)
  - all preprocessor macros use WY_ namespace in all caps (WY_)

## Agent map (epic 2: foundations and core interpreter)

Read this before touching the interpreter. `vm_plan/README.md` and the
current `vm_plan/epic_N.md` are the plan; this table is where each piece
of it actually lives. Epic 1's own map (loader-only) follows below it.

| File | Responsibility |
|---|---|
| `include/wyrm/primitive.h` | `wy_type_tag`: NIL, BOOL, WORD, UWORD, FLOAT, SYMBOL, PTYPE, then the object-bearing tags (ERROR, PAIR, ..., TUPLE, LIST, BYTES, FUNCTION, NATIVE, INSTANCE, MESSAGE, BOUND_MSG, COROUTINE, ITER). |
| `include/wyrm/value.h` | Constructors/accessors for every tag; `wy_value_is_error` (true for a realised error object **and Unset itself** - this is what `jerr`/`jnerr`/`?=` rely on, don't "fix" it to exclude Unset); `wy_value_truthy` (defined in `src/string.c`, not inline - needs `wy_string`'s complete type). |
| `include/wyrm/symtab.h`, `src/symtab.c` | Real interning hash keyed on the wyc-format.md §8.4 31-codepoint significant prefix, FNV-1a, open addressing. Replaces the old 8KB scaffold in `src/machine.c`; `wy_context_intern` (`src/context.c`) is still the one entry point. |
| `include/wyrm/{tuple,list,bytes,error,function,native}.h` + matching `.c` | The six new GC-tracked heap object kinds (design_c_vm.md §4). `wy_bytes` is a placeholder for epic 7's `bytes` type - no language surface yet. |
| `include/wyrm/gc.h`, `src/gc.c` | Iterative mark (gray worklist, not recursive); abandons the sweep (not just the visit) if the worklist can't grow, so a partial mark never frees a live object. |
| `include/wyrm/context.h`, `src/context.c` | `gc_pressure`/`gc_threshold` + `wy_context_gc_safepoint` (checked once per dispatch-loop instruction); `roots[64]` + `wy_context_root_push_f`/`pop_f` for C code holding a fresh value across allocations; `io.write`/`io.ud` output hook; `builtins` field (a GC root). |
| `include/wyrm/frame.h` | `wy_frame`: tagged native/bytecode call frame, replacing the old `wy_fiber_frame`. `wy_fiber_push_frame_f`/`pop_continuation_f`/`tail_call_f` (`src/fiber.c`) use only its native-frame fields (`native`, `ret_nres`, `restore_base`) with no behavioral change from before this type existed. |
| `include/wyrm/exec_fn.h` | `wy_exec_state` gained `WY_EXEC_SWITCH`/`WY_EXEC_FAULT`; `wy_fiber_exec_f` (`src/fiber.c`) surfaces a fault as `WY_ERR_FAULT` (`fiber->fault` readable) and stops driving a fiber that switched away; `wy_context_exec` (`src/context.c`) loops across switches. |
| `src/vm.c` | `wy_vm_run`: the dispatch loop (design_c_vm.md §2) - a `reload:`-labeled loop over `wy_frame`s, no C recursion. `wy_vm_call_sync` (host/loader entry point) lives here too, moved up from its planned epic-2/M5 slot because M4's own tests need it. |
| `src/vm_ops.c` | Arithmetic/comparison/`is`/unary semantics, pinned against `pypoc/wypoc/wyrm_eval_parse_tree.py`'s `BINOPS` table: `/` is always true division (float even for two ints), div/mod-by-zero and negative shifts produce an **error value**, not a fault. |
| `src/vm_call.c`, `src/vm_internal.h` | Native call bridge: `wy_vm_call_leaf_f` (inline leaf call), `wy_vm_call_exec_push_f`/`wy_vm_native_await_complete_f` (exec-native reservation bridge, built but not yet reachable from bytecode - `call` on an exec NATIVE faults "not supported until epic 3+"). Register accessors (`wy_vm_reg_f`/`reg8_f`) and the window backfill helper. |
| `src/builtin/builtins.c`, `include/wyrm/builtins.h` | `wy_builtins_new`: a synthetic `wy_module` (`state = WY_MODULE_BUILTIN`) exporting `println`/`print` (leaf natives) and `nil`. |
| `src/link.c`, `include/wyrm/link.h` | `wy_link_fill_from_builtins`: layer 3 only (wyc-format.md §7.2) - fills a module's free-name slots from builtins' exports. Layers 1/2 (own definitions, `import`/`import_star`) are epic 2/M6. |
| `src/module.c` | `wy_module_run_init` (new, epic 2/M5): builds a synthetic zero-arg init `wy_function_proto` for a module's word-offset-0 code and runs it via `wy_vm_call_sync`, setting `state` to READY/FAILED. |
| `src/wyrm/main.c` | The `wyrm` CLI's no-flag path now actually runs a module: creates a fiber, installs a real stdout `io.write`, builds+stores `context->builtins`, links, runs init, prints a fault message on error. |
| `src/test/test_bytecode_golden.cpp` | `golden`/`golden-gcstress` meson suites: loads, links and runs each of the seven epic-2 target fixtures' real `.wyc`, diffs captured output against the corpus's `.out`. The strongest available regression check for the interpreter. |

## Agent map (epic 1: toolchain, conformance corpus, image loader)

| File | Responsibility |
|---|---|
| `pypoc/` | Nested checkout (own git history, gitignored) of the Python proof-of-concept compiler and reference VM. Source of truth for the opcode set and the `.wyc` format. |
| `pypoc/doc/wyc-format.md` | **Normative** format spec: container (§2), BSON subset (§4), instruction encoding (§5), opcode set (§6), load sequence (§7), section schemas (§8). |
| `scripts/setup_pypoc.sh` | Creates/updates `pypoc/.venv`, installs `.[dev,lsp]`, runs pypoc's own test suite. Idempotent. |
| `include/wyrm/opcode.h` | The 88-opcode v1 instruction set, encoding macros (`WYRM_OP`, `WYRM_F`, `WYRM_A0/A1/A2`, `WYRM_OP_WORDS`). Originally adopted from pypoc; maintained here now. Keep `opcode_names.h` and `src/embed/wyrm/opcodes.wy` in step with any change. |
| `include/wyrm/opcode_names.h` | `wy_opcode_names[256]`, indexed by raw opcode byte, for the disassembler. Hand-maintained alongside `opcode.h`. |
| `include/wyrm/image.h` | `wy_section_ref`, `WY_SEC_*` ids, `wy_module_image` (a container's sections, zero-copy). Originally adopted from pypoc; maintained here now. |
| `include/wyrm/image_loader.h`, `src/image.c` | `wy_image_from_bytes`: parses the container (magic/version/directory/bounds/alignment) into a `wy_module_image`. Declared separately from `image.h`, which carries only the descriptors a generated `.c` image needs. |
| `include/wyrm/bson.h`, `src/bson.c` | The pinned eight-type BSON reader (§4.2) every structured section is decoded through. `wy_bson_doc_reader` for documents, `wy_bson_array_reader` for arrays (ignores index keys). Any tag outside the eight, a bad bool byte, a binary subtype ≠ 0, or a truncated field is `WY_ERR_IMAGE`. |
| `include/wyrm/module.h`, `src/module.c` | `wy_module_load_image` / `wy_module_load_bytes`: decode every section into a `wy_module` (table below). Every table index is bounds-checked at load (§3). Executes nothing; fills no builtins — that's epic 2. |
| `src/vm.c` | `wy_vm_exec_bytecode`: still a stub (`WY_ERR_INVAL` unconditionally) pending epic 2's interpreter loop against the real encoding. |
| `src/wyrm/main.c` | The `wyrm` CLI: `wyrm [-I dir]... [-v] [--cache-dir DIR] [--sections] [--disasm] [-m mod::sub] [--check] [--build-bc [-o DIR] [--emit LIST] [--strip]] file.wy|.wyc|.wyd [args...]`. A `.wy` entry (and every `.wy` import) compiles in-process through the builtin module table's embedded compiler; `.wyc`/`.wyd` load directly. |
| `src/context.c` (`wy_context_intern`) | The one entry point loader code uses to intern a name. Wraps the scaffold symtab (`src/machine.c`, 8 KB linear, 127-byte cap per symbol) so swapping in a real symtab (epic 2) is a one-function change. |
| `test/corpus/` | Behavioral corpus: `.wy` sources + expected `.out`, `manifest.txt`. No bytecode is committed; the C++ tests build `.wyd` fixtures from it at build time (`scripts/build_fixtures.py`). |
| `src/test/test_image.cpp`, additions to `test_bson.cpp` | Container/BSON rejection cases, mutating a freshly built `hello.wyd` byte-for-byte. |
| `src/test/test_module.cpp` | Exact-value assertions for `hello`/`classes`/`two_module/report`, the embedded `hello_1` image (emitted at build time), and a `loader` suite that loads every built fixture (`meson test -C buildDir --suite loader`). |

## The `wy_module` table (`include/wyrm/module.h`)

One `.wyc` becomes one `wy_module`, populated in this order by
`wy_module_load_image` (wyc-format.md §7.1 steps 1-5; step 6 — publish and
run init — is epic 2):

| Field(s) | Source section | Notes |
|---|---|---|
| `name`, `global_count`, `init_nlocals` | `header` (id 1, required) | `v` must be 1; `n` is interned via `wy_context_intern`. |
| `globals`, `fill_layer`, `fill_source` | header's `g`, then `slot_defaults` (id 3) | All Unset until `slot_defaults` applies constant defaults. `fill_layer`/`fill_source` are epic 2's three-layer-fill bookkeeping (§7.2); always 0/NULL out of epic 1. |
| `symbols` | `symbols` (id 4) | Interned strings; other sections reference them by index. |
| `statics` | `statics` (id 2) | `wy_value` per entry: string → `wy_string`, int32 → `WY_TYPE_TAG_WORD`, double → `WY_TYPE_TAG_FLOAT`, bool → `WY_TYPE_TAG_BOOL`, null → `WY_TYPE_TAG_NIL`, binary → `wy_string` flagged `WY_GC_FLAG_BINARY` (placeholder until the `bytes` type, epic 7). |
| `functions` | `functions` (id 5) | `wy_function_proto` per entry; `code_offset` bound-checked against `code_len`, dispatch types against `global_count`. |
| `class_protos` | `classes` (id 6) | `wy_class_proto` per entry; message map ≤ `WY_CLASS_MAX_MESSAGES` (16). `classes` (realized `wy_class*` objects) stays NULL until epic 4. |
| `messages` | `messages` (id 7) | `wy_message_ref` per entry, path as symbol indices; `bound` always NULL until epic 4. |
| `code`, `code_len` | `code` (id 8, required) | Points straight into the image buffer; never copied. Word count, not bytes. |
| (ignored) | `debug` (id 9) | A VM MUST ignore this section entirely (§8.9); the loader records the section ref but never decodes it. |
| `exports`, `free_names` | `exports` (id 10), `free` (id 11) | `wy_slot_dict`s, name (interned) → global slot index. |
| `wildcards` | — | Always empty; epic 2's `import_star` is the first thing that populates it. |

`image`/`image_len`/`owns_image` track the backing buffer for
`wy_module_load_bytes(..., take_ownership, ...)`; the module's finalizer
frees it (and every array above) if it owns it.

## Conformance corpus

`test/corpus/` holds `.wy` sources with their expected stdout (`.out`) and
`manifest.txt` (status per source: `matches`, `local-only`, `DIVERGES`,
`REFUSED`, with reasons). No bytecode is committed and nothing is compared
with another implementation's bytecode. `scripts/run_behavior.py` (meson test
`behavior`) runs every source from source through the build tree and diffs
against `.out` and, when available, an external wyrm (see AGENTS.md,
Testing). The C++ golden/loader tests run `.wyd` images that
`scripts/build_fixtures.py` compiles from the same corpus on every build.

## Running one fixture

```
meson compile -C buildDir
mkdir -p /tmp/h && ./buildDir/src/wyrm/wyrm --build-bc -o /tmp/h test/corpus/hello.wy
./buildDir/src/wyrm/wyrm /tmp/h/hello.wyd --sections          # section summary
./buildDir/src/wyrm/wyrm /tmp/h/hello.wyd --disasm            # one line per instruction
./buildDir/src/wyrm/wyrm /tmp/h/hello.wyd                     # runs it: prints "Hello World"
```

To iterate on the loader against one doctest case without running the whole suite:

```
meson test -C buildDir --suite loader                          # every built fixture loads
meson test -C buildDir --suite golden                           # the seven epic-2 fixtures run end-to-end
./buildDir/src/test/test_cwyrm --test-suite="module"            # exact-value assertions
./buildDir/src/test/test_cwyrm --test-suite="golden"            # same, doctest-only
./buildDir/src/test/test_cwyrm --test-case="*hello*"
```

## Known gaps left for epic 3+

Epic 1's gaps (big-endian hosts, the 127-byte scaffold symtab) are closed:
the real symtab (`include/wyrm/symtab.h`) has no length cap and enforces
the 31-codepoint significant-prefix rule; big-endian rejection is still in
place (no byte-swap implemented) but that was always the intended fix, not
a gap. `wy_float`-on-32-bit precision and binary statics having no real
type are unchanged (still open, epic 7's `bytes` type is the fix for the
latter).

- **Captures, tuples/lists/dicts, classes/messages, imports, coroutines,
  defers are all epic 3+.** The dispatch loop faults cleanly ("not
  supported until epic N") rather than misbehaving on any bytecode that
  needs them - `closure` with `ncaps > 0`, `call` on an exec NATIVE from
  bytecode, `tuple`/`list`/`dict`/`plist`, `getattr`/`setattr`/`getslot`/
  `setslot`, `msg`/`super`/`class`, `import`/`import_star`, `yield`.
- **`gget` faults on *any* Unset slot**, not just a free slot specifically
  (design_c_vm.md's own text says "fault on Unset *free* slot"; the format
  spec's plain-language rule is "gget on it faults the way reading any
  declared-but-unassigned variable does" for *any* unset global - the
  looser, simpler rule this epic implements). No ambiguity-marker handling
  either (nothing produces one yet - that's epic 2/M6's wildcard imports).
- **No `packed_ops.h`/`pypoc/tools/generate_c_fixtures.py`.** The
  pypoc-generated hand-packed-fixture pipeline design_c_vm.md §9a
  describes was not built; `src/test/test_wvm.cpp` hand-packs instruction
  words directly instead, covering the same ground for this epic's opcode
  set. Worth building properly before epic 3 adds many more opcodes to
  hand-pack by hand.
- **Argument binding is fast-path only**: `argc == nparams`, no defaults,
  no `*args`/`**kwargs`. Anything else faults with `WY_ERR_ARITY`. The
  slow path (`frame.py:build_pframe`'s full binding logic) is epic 3.
- **`wy_vm_call_continue`** (natives calling back into the VM) is declared
  but returns `WY_ERR_NOSUPPORT` unconditionally - needed once epic 3+
  gives natives a reason to re-enter bytecode (e.g. `__iter__` dispatch).

## Embedding `std::io`

`std::io` is a compiled-in module (`src/embed/std/io.wy`, image in the builtin
table) written over pypoc's `__open`/`__read`/`__write`/`__lseek`/`__dup2`/
`__close`/`__flush` builtins and `__STDIN`/`__STDOUT`/`__STDERR`. libcwyrm does
not provide them; the executable does (`src/embed/std/io_native.c`). An
embedder that links the same sources and wants `import std::io` to work must,
per context:

1. `wy_builtins_new(ctx, &b); ctx->builtins = b;`
2. `wy_io_natives_install(ctx)` (`src/embed/std/io_native.h`) - this adds the
   names to the builtins via `wy_builtins_add`, which has 16 spare slots and
   must run before any module is linked against the builtins.
3. Resolve `std::io` through an import hook that falls back to the builtin
   table (`wyrm_builtin_modules`, `src/embed/builtins.h`), as `src/wyrm/main.c`
   and the golden test harness do.

A context that skips step 2 can import `std::io` but not do I/O. Expansion VMs
skip it on purpose and their import hook also refuses `std::io`. `__write` to fd
1 goes through `ctx->io.write` when the host set one, so captured output covers
`File` writes and `println`.
