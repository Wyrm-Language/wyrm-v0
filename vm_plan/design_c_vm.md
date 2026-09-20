# C VM architecture design

Companion to the epics in this directory. Written at plan time (2026-09-15) from a survey of the C tree, `pypoc/doc/wyc-format.md`, and `pypoc/wypoc/vm/`. Epics 2-6 implement it; an epic report may amend a section, and when it does the report must say which section and why.


Scope: the C-side design for `cpoc` to execute images produced by
`pypoc/wypoc/compiler_bc`, conforming to `pypoc/doc/wyc-format.md`. Respects AGENTS.md: no C
recursion in VM execution, all allocation through `wy_allocator`, `snake_case`, `_f`/`_s`
suffixes, inline functions over macros, `WY_ASSERT` for internal invariants only.

## 0 Summary of decisions and where existing convention changes

| Area | Decision | Existing convention kept? |
|---|---|---|
| Stacks | One `wy_stack` per fiber; each bytecode frame is `[P slots][L slots]` contiguous; frame record holds `p`, `l` pointers | Kept (single stack) |
| Results | Bytecode results copied by a single `backfill` into the caller's L window. Reserved-below-base slots remain **only** for native (`wy_exec_fn`) frames and bytecode called *from* C | Changed: reservation no longer the universal return path |
| Frame record | `wy_fiber_frame` replaced by tagged `wy_frame` (native or bytecode) with `ret_kind` | Changed |
| Trampoline | `wy_fiber_exec_f` stays the driver; new states `WY_EXEC_SWITCH` (fiber switched) and `WY_EXEC_FAULT` (unwound); `wy_context_exec` loops across fiber switches | Extended |
| Native calls | **Leaf** natives called inline `(args*, argc, out*, nres)`; **exec** natives (call back into VM or switch fibers) use the existing reservation trampoline | New + kept |
| Callable payload | `(module_id:12, address:20)` packing kept only as host convenience; closures are heap objects | Reduced |
| Opcodes | `include/wyrm/opcode.h` replaced by pypoc's generated header verbatim; `wy_vm_exec_bytecode`, `WY_OP_PASS/LBYTE/SET` removed | Changed |
| Container | `WYC\x02` parser replaced by `WYC\0` + 12-byte directory; `image.h` adopted verbatim; code used in place | Changed |
| Symbols | 8 KB linear scaffold replaced by interning hash keyed on 31-codepoint prefix; `wy_symbol` stays `const char*` | Changed |
| GC | Recursive visit → worklist; allocation-pressure counter; collection only at dispatch-loop safepoints | Changed |
| Type tags | Add `BOOL`, `FLOAT`, `PTYPE`, `FUNCTION`, `NATIVE`, `INSTANCE`, `TUPLE`, `LIST`, `BYTES`, `COROUTINE`, `MESSAGE`, `BOUND_MSG`, `ITER`; `TABLE` is the dict | Extended |
| Bugs | `wy_value_uword` tags WORD; additive `wy_hash_buffer` → `wy_util_fnv1a_buffer` | Fix |

## 1 Frame model

One `wy_stack` per fiber. A bytecode frame is a contiguous slice: `p_count` P slots then
`nlocals` L slots.

```c
/* src/vm_internal.h */
WY_INLINE wy_value* wy_vm_reg_f(wy_frame* fr, wy_u16 r)
{ return (r & WYRM_REG_P_BIT) ? &fr->p[r & 0x7fffu] : &fr->l[r]; }
WY_INLINE wy_value* wy_vm_reg8_f(wy_frame* fr, wy_u8 r)
{ return (r & 0x80u) ? &fr->p[r & 0x7fu] : &fr->l[r]; }
```

### A.1.1 Frame record (`include/wyrm/frame.h`, new)

```c
typedef enum wy_frame_kind  { WY_FRAME_NATIVE = 0, WY_FRAME_BYTECODE } wy_frame_kind;
typedef enum wy_ret_kind {
    WY_RET_WINDOW = 0,   /* copy results to ret_dst[0..nres) with nil backfill */
    WY_RET_RESERVED,     /* results into reserved slots below caller base (C caller) */
    WY_RET_DISCARD,      /* defers */
    WY_RET_CONSTRUCT,    /* result[0] error -> that error, else the instance in aux */
    WY_RET_IMPORT,       /* dependency init finished: fill free slots, ret_dst[0] = module */
    WY_RET_IMPORT_STAR,  /* register wildcard, layer-2 fill */
    WY_RET_COROUTINE,    /* body returned: co->result = result[0], DONE, switch back */
} wy_ret_kind;
typedef enum wy_frame_phase { WY_PHASE_RUN = 0, WY_PHASE_AWAIT_NATIVE,
                              WY_PHASE_RETURNING, WY_PHASE_FAILING } wy_frame_phase;

typedef struct wy_frame {
    wy_u8 kind, ret_kind, phase, flags;     /* flags: WY_FRAME_METHOD, WY_FRAME_INIT */
    wy_u16 ret_nres, p_count;
    wy_value* ret_dst;        /* WINDOW: &caller->l[base]; RESERVED: first reserved slot */
    wy_value* p;              /* P frame = stack base of this frame */
    wy_value* l;              /* L frame = p + p_count */
    wy_value* restore_base;   /* caller's wy_stack.base */
    const wy_u32* ip;         /* saved on every exit from the loop */
    wy_module* module;
    const wy_function_proto* proto;   /* NULL for module init */
    wy_exec_fn native;        /* WY_FRAME_NATIVE continuation */
    wy_pair* defers;          /* (closure . mode) chain, most recent first */
    wy_u16 ret_base, ret_count;       /* RETURNING: own L return window */
    wy_message* dispatch_msg; wy_value dispatch_body;   /* for `super` */
    wy_value aux;             /* per ret_kind: instance / import static / coroutine */
} wy_frame;
```
`wy_fiber` keeps `frame_memory` as a fixed `wy_frame` array and gains `fault` (error value
that unwound it), `coroutine` (owner or NULL), `next_fiber` (context all-fibers list).

### A.1.2 Bytecode → bytecode call
```
caller L: [base]=callee [base+1..base+argc]=args     top = caller.l + caller.nlocals
push:  new.p = top; copy this/args/defaults/captures into P; new.l = new.p + p_count
       reserve nlocals (uninitialised; debug builds fill Unset)
       ret_kind = WINDOW, ret_dst = &caller.l[base], ret_nres = nres
       caller.ip saved; stack.base = new.p; top = new.l + nlocals
```
Arguments are copied (≤128). `return base, count` copies to `ret_dst`, pads nil, pops.
```c
WY_INLINE void wy_vm_backfill_f(wy_value* dst, wy_uword nres, const wy_value* src, wy_uword count)
{ wy_uword n = count < nres ? count : nres;
  for (wy_uword i = 0; i < n; i++) dst[i] = src[i];
  for (wy_uword i = n; i < nres; i++) dst[i] = wy_value_nil(); }
```
Binding (`src/vm_call.c`): fast path plain fn with `argc == nparams` → memcpy + captures;
slow path mirrors `frame.py:build_pframe` (positional, defaults from statics, `*args`
tuple, `**kwargs` dict, missing required → trap, matching the reference).

### A.1.3 Bytecode → native
```c
typedef wy_error (*wy_native_leaf_fn)(wy_context*, wy_value* args, wy_uword argc,
                                      wy_value* out, wy_uword nres);
typedef struct wy_native { wy_object object; wy_symbol name; wy_u8 kind /*LEAF|EXEC*/;
    wy_u8 min_argc, max_argc; union { wy_native_leaf_fn leaf; wy_exec_fn exec; } fn; } wy_native;
```
Leaf: inline, `args = &L[base+1]`, `out = &L[base]`, no frame; may allocate, must not
re-enter the VM. Exec: save `ip`, `phase = AWAIT_NATIVE`, reserve `nres` slots at top,
push args, `pending = native.exec`, return `WY_EXEC_CONTINUE`. When it finishes the
trampoline runs `wy_vm_run`, whose prologue copies reserved slots (reverse order) into the
window and pops them. The reservation system is kept for native frames and bridged.

### A.1.4 C → bytecode (`include/wyrm/vm.h`)
```c
wy_error wy_vm_call_continue(wy_context*, wy_exec_fn continuation, wy_value callee,
                             const wy_value* args, wy_uword argc, wy_uword nres);
wy_error wy_vm_call_sync(wy_context*, wy_value callee, const wy_value* args, wy_uword argc,
                         wy_value* out, wy_uword nres);   /* host/tests only, not from natives */
wy_exec_state wy_vm_run(wy_context*, wy_primitive unused);   /* continuation of every bytecode frame */
```
`wy_vm_call_sync` records frame depth, pushes with `WY_RET_RESERVED`, runs `wy_fiber_exec_f`
until depth returns. Loader uses it for root init; `main.c` for the root module.

## 2 Execution loop (`src/vm.c`)

`wy_vm_run` is entry and continuation for all bytecode frames. `switch` on `op & 0xff`
(no computed goto; TCC). Wide forms widen and `goto` the compact case.

```c
wy_exec_state wy_vm_run(wy_context* ctx, wy_primitive unused)
{
    wy_fiber* fb = ctx->current_fiber; wy_frame* fr = fb->current_frame;
reload:
    const wy_u32* ip = fr->ip; wy_value* L = fr->l; wy_value* P = fr->p;
    wy_module* mod = fr->module; wy_value* G = mod->globals;
    switch (fr->phase) {
    case WY_PHASE_AWAIT_NATIVE: /* copy reserved results into window, pop */ break;
    case WY_PHASE_RETURNING: goto do_return;
    case WY_PHASE_FAILING:   goto do_unwind;
    default: break; }
    for (;;) {
        if (ctx->gc_pressure > ctx->gc_threshold) { fr->ip = ip; wy_context_gc_safepoint(ctx); }
        wy_u32 w0 = *ip; wy_u8 op = w0 & 0xff, f = (w0 >> 8) & 0xff;
        wy_u16 a0 = w0 >> 16, a1 = 0, a2 = 0; wy_u32 w1 = 0;
        if (op & 0x80) { w1 = ip[1]; a1 = w1 >> 16; a2 = w1 & 0xffff; ip += 2; } else ip += 1;
        switch (op) {
        case WY_OP_CALL: { wy_value callee = L[a0];
            switch (callee.type) {
            case WY_TYPE_TAG_FUNCTION: fr->ip = ip;
                if (wy_vm_push_call_f(ctx, fr, callee, &L[a0+1], f, &L[a0], a1, WY_RET_WINDOW)) goto fault;
                fr = fb->current_frame; goto reload;
            case WY_TYPE_TAG_NATIVE: /* leaf inline or exec bridge -> return WY_EXEC_CONTINUE */
            case WY_TYPE_TAG_CLASS:  /* new instance, push init with WY_RET_CONSTRUCT */
            case WY_TYPE_TAG_BOUND_MSG: /* push method frame */
            case WY_TYPE_TAG_PTYPE:  /* cast inline */
            default: goto fault_not_callable; } }
        case WY_OP_RETURN:
            fr->ip = ip; fr->ret_base = a0; fr->ret_count = f; fr->phase = WY_PHASE_RETURNING;
        do_return:
            if (fr->defers) { /* pop (closure . mode); test mode; push WY_RET_DISCARD; goto reload */ }
            wy_vm_complete_return_f(ctx, fr); fr = fb->current_frame;
            if (fr->kind == WY_FRAME_NATIVE || fr == fiber_root) return WY_EXEC_DONE;
            goto reload;
        /* ... */ } } }
```
- `fr->ip` written only on leaving the frame (call, bridge, yield, fault, safepoint). Jumps:
  `ip += rel` from the already-advanced `ip`. `code` is `const wy_u32*` into the image.
- Defers: `defer_reg` conses `(closure . mode)` onto `fr->defers`. `return` → RETURNING with
  window recorded in own L (stays allocated). Each drain pops one, tests mode against
  `results[0]` (mode 1: is_error; mode 2: is_error || is_nil; FAILING forces error), pushes
  the closure with `WY_RET_DISCARD`, `goto reload`; on its return the loop lands back here
  in RETURNING and drains the next. No C recursion.
- Faults (trap, failed store, no overload, stack overflow, uncallable): `fb->fault = err`,
  `phase = FAILING`, drain on-error defers, pop, continue unwinding; at a native frame or
  the fiber root return `WY_EXEC_FAULT` → `WY_ERR_FAULT` with `fiber->fault` readable.
- Three-address ops: `wy_binop_fn wy_binops[]` in `src/vm_ops.c` (int/float/str/list/tuple/
  dict fast paths); INSTANCE operand → look up `__add__`-family and push a method frame with
  `ret_dst = reg(a0)`, `ret_nres = 1`. Same push-and-reload for `not`/`__bool__`,
  `iter`/`__iter__`, virtual-slot getattr, construct-on-call.

## 3 Coroutines (`include/wyrm/coroutine.h`, `src/coroutine.c`)

One `wy_fiber` per coroutine instance; `next`/`send` are exec natives that switch
`ctx->current_fiber`; `wy_context_exec` loops re-reading `current_fiber` after every
`WY_EXEC_SWITCH`.
```c
typedef enum wy_co_state { WY_CO_CREATED, WY_CO_SUSPENDED, WY_CO_RUNNING, WY_CO_DONE } wy_co_state;
struct wy_coroutine { wy_object object; wy_fiber* fiber; wy_fiber* resumer;
    wy_coroutine* delegate; wy_coroutine* outer; wy_value* delegate_dst;
    wy_u16 yield_base; wy_u8 state; wy_value result; };
```
- Creation: `call` on a FUNCTION with flag bit 0 → allocate coroutine + fiber
  (`ctx->co_stack_len`/`co_frame_count`, defaults 512/32), push body frame with P built as
  for a call, `ret_kind = WY_RET_COROUTINE`, `aux = co`, `fiber->pending = wy_vm_run`.
  Write the COROUTINE value; nothing executes. Dispatched `co [Cls] name` goes via `msg`.
- `next(co)`/`send(co, v)`: DONE → StopIteration error value. `send` on CREATED → error.
  Else `co->resumer = current_fiber`; if SUSPENDED write `v` into
  `co->fiber->current_frame->l[co->yield_base]`; RUNNING; `current_fiber = co->fiber`;
  return `WY_EXEC_SWITCH`. Native frame on the resumer stays open with its reserved slot.
- `yield base, count`: pack window (0→nil, 1→value, n→tuple), save ip, `yield_base = base`,
  SUSPENDED, `wy_fiber_set_result_f(co->resumer, 0, v)`, switch back, `WY_EXEC_SWITCH`.
- Body return (`WY_RET_COROUTINE`): `result`, DONE; if `outer`: write to `*outer->delegate_dst`,
  clear delegation, switch to `outer->fiber`; else StopIteration to resumer, switch back.
- `yield_from dst, sub`: link `delegate`/`outer`/`delegate_dst`, resume `sub` with
  `sub->resumer = co->resumer`. In `next/send` follow `while (co->delegate) co = co->delegate`.
- StopIteration is an instance of builtin error class `StopIteration` (`WY_CLASS_ERROR`).
- GC: coroutine children = fiber, resumer, delegate, outer, result. Fiber children walk all
  stack values and each frame's module/defers/dispatch_body/aux. Roots: `current_fiber` and
  the context's fiber list; an abandoned suspended coroutine is collected with its stack.

## 4 Value representation

```c
typedef enum wy_type_tag {
    WY_TYPE_TAG_NIL = 0, WY_TYPE_TAG_BOOL, WY_TYPE_TAG_WORD, WY_TYPE_TAG_UWORD,
    WY_TYPE_TAG_FLOAT /* double; f32 widened */, WY_TYPE_TAG_SYMBOL,
    WY_TYPE_TAG_PTYPE /* primitive-type value, data.uword = tag */,
    WY_TYPE_TAG_GC_PATH_START,
    WY_TYPE_TAG_ERROR /* wy_error_obj*; {ERROR,NULL} = Unset */, WY_TYPE_TAG_PAIR,
    WY_TYPE_TAG_BOX, WY_TYPE_TAG_OBJECT, WY_TYPE_TAG_STR, WY_TYPE_TAG_FIBER,
    WY_TYPE_TAG_DTYPE, WY_TYPE_TAG_CLASS, WY_TYPE_TAG_MODULE, WY_TYPE_TAG_TABLE /* dict */,
    WY_TYPE_TAG_TUPLE, WY_TYPE_TAG_LIST, WY_TYPE_TAG_BYTES, WY_TYPE_TAG_FUNCTION,
    WY_TYPE_TAG_NATIVE, WY_TYPE_TAG_INSTANCE, WY_TYPE_TAG_MESSAGE, WY_TYPE_TAG_BOUND_MSG,
    WY_TYPE_TAG_COROUTINE, WY_TYPE_TAG_ITER,
} wy_type_tag;

struct wy_function  { wy_object object; wy_module* module; const wy_function_proto* proto; wy_uword ncaps; wy_value caps[]; };
struct wy_tuple     { wy_object object; wy_uword count; wy_value items[]; };
struct wy_list      { wy_object object; wy_uword count, capacity; wy_value* items; };
struct wy_bytes     { wy_object object; wy_uword len, capacity; wy_u8* data; };   /* resizable (bytes type) */
struct wy_error_obj { wy_object object; wy_class* cls; wy_string* what; wy_value payload; };
struct wy_instance  { wy_object object; wy_class* cls; wy_value slots[]; };
typedef struct wy_overload { wy_u8 arity; wy_value types[16]; wy_value body; } wy_overload;
struct wy_message   { wy_object object; wy_symbol name; wy_module* owner; wy_uword overload_count, overload_capacity; wy_overload* overloads; };
struct wy_bound_msg { wy_object object; wy_value receiver; wy_message* msg; wy_value body; };
```
Captures are copied by value (compiler cells are pair lists). `is_error(v)` = ERROR tag or
INSTANCE whose class has `WY_CLASS_ERROR` (inherited at realisation). Builtins define
`error` (slot `what`) and `StopIteration`, `RuntimeError`, `OSError`, `OutOfMemory`. Unset
stays `{ERROR, NULL}`; `gget` faults on Unset for *free* slots and on the ambiguity marker
(error obj with payload = tuple of the two source names). Floats print shortest-round-trip
(`%.{1..17}g` until `strtod` round-trips) to match Python repr in goldens.

## 5 Module and linking (`image.h`, `module.h`, `src/image.c`, `src/module.c`, `src/link.c`)

Adopt `image.h` verbatim. `wy_error wy_image_from_bytes(const wy_u8*, wy_uword, wy_module_image*)`
validates magic/version/sorted unique ids/bounds/unknown ids (9 ignored), no copying.
```c
wy_error wy_module_load_image(wy_context*, const wy_module_image*, wy_module** out);   /* steps 1-5 */
wy_error wy_module_load_bytes(wy_context*, const wy_u8*, wy_uword, bool take_ownership, wy_module** out);
wy_error wy_module_run_init(wy_context*, wy_module*);   /* step 6 via wy_vm_call_sync; host only */

typedef struct wy_param { wy_symbol name; wy_i32 default_static; } wy_param;   /* -1 none */
typedef struct wy_function_proto { wy_symbol name; wy_u32 code_offset; wy_u16 nparams, nlocals, ncaptures, ndispatch;
    wy_u8 flags, nresults; wy_param* params; wy_u16* dispatch_slots; } wy_function_proto;
typedef struct wy_slot_proto { wy_symbol name; wy_i32 default_static, getter_fn, setter_fn; } wy_slot_proto;
typedef struct wy_class_proto { wy_symbol name; wy_i32 super_slot, init_fn; wy_u16 nslots, nmsgs, nstatics;
    wy_slot_proto* slots; struct { wy_symbol name; wy_u16 fn; } msgs[16];
    struct { wy_symbol name; wy_u16 global; }* statics; } wy_class_proto;
typedef struct wy_message_ref { wy_u16 path_len; wy_u16* path; wy_message* bound; } wy_message_ref;
typedef struct wy_wildcard { wy_module* target; wy_uword except_count; wy_symbol* excepts; } wy_wildcard;

struct wy_module {
    wy_object head; wy_symbol name; wy_u8 state;   /* LOADED, INITIALISING, READY, FAILED, BUILTIN */
    const wy_u8* image; wy_uword image_len; bool owns_image;
    const wy_u32* code; wy_uword code_len; wy_u16 init_nlocals;
    wy_value* globals; wy_uword global_count; wy_u8* fill_layer; wy_symbol* fill_source;
    wy_value* statics; wy_uword static_count; wy_symbol* symbols; wy_uword symbol_count;
    wy_function_proto* functions; wy_uword function_count;
    wy_class_proto* class_protos; wy_class** classes; wy_uword class_count;
    wy_message_ref* messages; wy_uword message_count;
    wy_slot_dict exports; wy_slot_dict free_names; wy_dict* message_table;
    wy_wildcard* wildcards; wy_uword wildcard_count; };
```
Three-layer fill (`src/link.c`, port of `LoadedModule.fill`): `wy_link_fill(module, slot,
value, layer, source)`; stronger layer wins; equal layer, different source, non-identical
value → ambiguity marker. `wy_link_fill_from_builtins` (layer 3 at load, bare names),
`wy_link_fill_from_import(module, path, dep)` (layer 1: exact spelling + every free name
with prefix `path::`, walking the remainder like `getscope`), `wy_link_fill_from_wildcard`
(layer 2, only `dep->exports` minus excepts).

Builtins are a module (`src/builtin/builtins.c`, `state = BUILTIN`, `exports` from a static
table, `message_table` for per-primitive methods with PTYPE constraints); `ctx->builtins`.

Import hook on the context:
```c
typedef wy_error (*wy_import_hook)(wy_context*, const char* path, wy_uword len,
                                   wy_u8** out_bytes, wy_uword* out_len, void* ud);
```
`src/platform/hosted/import_fs.c` implements `-I` search (`a::b` → `dir/a/b.wyc`). `import`:
module table hit READY → fill + write; INITIALISING → fault `WY_ERR_CYCLE` naming both;
absent → hook → load → publish (INITIALISING) → push init frame on the **current fiber**
with `WY_RET_IMPORT`, `aux = static`, `ret_dst = reg(dst)` → `goto reload`; on return set
READY, layer-1 fill, write MODULE. `import_star` likewise with `WY_RET_IMPORT_STAR` (excepts
copied into `aux` as a tuple). `getscope/setscope`: MODULE → exports; CLASS → statics;
FUNCTION → module exports; else fault.

## 6 Symbols and strings

```c
typedef struct wy_symtab_node { wy_u32 hash; wy_u16 sig_len; wy_u16 len; char text[]; } wy_symtab_node;
typedef struct wy_symtab { wy_allocator* allocator; wy_symtab_node** buckets; wy_uword capacity, count; } wy_symtab;
wy_symbol wy_symtab_intern(wy_symtab*, const char* utf8, wy_uword len);   /* returns node->text */
```
`wy_symbol` remains `const char*` (pointer identity). Identity = first 31 codepoints
(`sig_len` bytes); FNV-1a; open addressing, doubling. Symbols are machine-lifetime.
Strings: `{ wy_object; len; hash; char data[]; }` immutable inline, FNV-1a once. Static-pool
strings created at load, owned by the module. Str literals are **not** interned; STR dict
keys hash by content, SYMBOL by pointer.

## 7 Classes, instances, messages (`class.h` rework, `src/wclass.c`, `src/dispatch.c`)

```c
typedef struct wy_class_slot { wy_symbol name; wy_value default_value; wy_value getter, setter; } wy_class_slot;
struct wy_class { wy_object object; wy_symbol name; wy_class* super; wy_module* module;
    wy_u16 slot_count /* inherited + own, base-first */; wy_u16 depth; wy_u8 flags /* WY_CLASS_ERROR */; wy_u8 msg_count;
    wy_class_slot* slots; struct { wy_message* msg; wy_value body; } msg_map[16];
    wy_slot_dict statics; wy_value init; };
```
The `wy_prototype`-based class (256 reserved slots) is replaced. `new_instance` copies
defaults (Unset if none). `getattr` on INSTANCE: scan `cls->slots` (per-class slot dict is
a later add); virtual slot → push getter with `ret_dst = reg(dst)`; setter with DISCARD.
Non-instance receivers consult a per-tag property table (initially empty → fault).

Message identity binds on first read: single component → `module->message_table` (create on
miss); qualified → resolve first component like `getscope` to the owning module. `class`
realisation registers each map entry as overload `((cls), FUNCTION)` and fills `msg_map`;
`reg_msg` appends `(types tuple, closure)`; nil entry = wildcard; PTYPE for primitives.

Dispatch (port of `resolve_overload`): (1) single INSTANCE receiver: walk `cls`, `super`…
checking `msg_map`; first hit wins unless any overload has arity 1 with wildcard/PTYPE or
receiver is not an INSTANCE → (2) general: per overload with matching arity compute
`dist[k]` (wildcard = 0xFFFF; CLASS: ancestor distance via `depth` or no-match; PTYPE: 0 on
tag match); lexicographically smallest wins; equal vector → ambiguity fault; none → "no
overload" fault. No allocation (best + tie flag). Chosen body is pushed with `this` values
in `P0..P(t-1)`, `WY_FRAME_METHOD`, `dispatch_msg`/`dispatch_body`. `getmsg` builds a
BOUND_MSG. `super`: re-run (2) for the frame's this values selecting the best candidate
strictly after `dispatch_body` in ranking; same this values, args from `L[base..]`, results
to `L[base]`. Construct-on-call: CLASS → new_instance → resolve `init` for `(instance)` →
push with `WY_RET_CONSTRUCT` (`aux = instance`) or write instance directly; on return
`is_error(result[0]) ? result[0] : aux`. `getattr/setattr` (property) and
`getscope/setscope` (`::`) never share a lookup.

## 8 GC (`src/gc.c`, `gc.h`, `context.h`)

1. Iterative mark: gray worklist (`wy_object**`, initial 256, grows via arena allocator);
   drain with existing `children_iter_start/next`. If the worklist cannot grow, abandon
   the collection (skip sweep) rather than recurse.
2. Roots: `ctx->fibers` list, `ctx->module_table` (modules pinned), `ctx->builtins`, fixed
   root stack `wy_value* roots[64]` with `wy_context_root_push/pop` for C code holding fresh
   objects across allocations.
3. Trigger: `wy_context_gc_alloc` adds size to `gc_pressure`; loop tests
   `gc_pressure > gc_threshold` per instruction and calls `wy_context_gc_safepoint`.
   Collection only at instruction boundaries so instruction implementations need no
   handles. Test mode `gc_threshold = 0` collects every instruction (stress for missing
   `children_iter` entries). Fiber `children_iter` walks frames' module/defers/
   dispatch_body/aux/native c_data.

## 9 Testing strategy

(a) Hand-packed words: `src/test/test_wvm.cpp` rewritten + committed
`src/test/fixtures/packed_ops.h` generated by `pypoc/tools/generate_c_fixtures.py` (via
`opcodes.pack`) with expected register values per opcode family; test-only
`wy_module_new_synthetic(ctx, code, len, nlocals, nglobals, statics...)`; driven via
`wy_vm_call_sync`. Cases: encode/decode parity, each op group, backfill (nres </> count),
defers in all modes, fault unwinding through two frames, leaf and exec bridge, coroutine
next/send/StopIteration, GC stress on closures.

(b) Golden images: `test/bytecode/<name>.wyc` + `.out` (+ `two_module/`, `wildcard/`),
regenerated by `scripts/build_corpus.py`. Runner inside doctest:
`src/test/test_bytecode_golden.cpp` sets the context output hook
(`ctx->io.write(ctx, bytes, len, ud)`; `println` writes through it) to a string, loads,
runs init, compares to `.out` from `WY_TEST_BYTECODE_DIR` (meson `-D`). One `TEST_CASE` per
fixture. `src/wyrm/main.c` is a real CLI (`wyrm [-I dir] file.wyc`); optional second meson
test per fixture via `scripts/run_golden.sh`, but doctest is the required path.

```
test/bytecode/{hello,arith,control_flow,closures,collections,classes,messages,errors,coroutines,multiret}.{wyc,out}
test/bytecode/two_module/{geometry,report}.wyc  report.out
test/bytecode/wildcard/{palette,paint}.wyc      paint.out
test/bytecode/embedded/hello_1.c                (compiled into test_cwyrm; image.h path)
src/test/fixtures/packed_ops.h
```

## 10 Milestones

| # | Milestone | Lands | Acceptance | Fixtures |
|---|---|---|---|---|
| M0 | Foundations | tags, `wy_value_uword` fix, symtab interning, inline strings + FNV, tuple/list/bytes/error_obj/function/native, worklist GC + safepoint + root stack, `WY_EXEC_SWITCH/FAULT`, `wy_frame` (native path) | existing suite green; symtab prefix identity; GC stress on 10k-pair chain | — |
| M1 | Loader | `image.h` verbatim, `wy_image_from_bytes`, BSON-8 reader, all tables, bounds checks, exports/free dicts, `hello_1.c` embedded | `hello.wyc` loads: `g==2,l==3`, `functions[0].code_offset==11`, statics[0]=="Hello "; corrupted directory rejected | — |
| M2 | Core loop | `wy_vm_run`, frames, call/return + backfill, loads/jumps/arith/compare, `closure` 0 caps, builtins module, `wy_vm_call_sync`, CLI, golden harness | `wyrm test/bytecode/hello.wyc` prints `Hello World`; goldens pass | hello, arith, control_flow, multiret |
| M3 | Data + closures | captures, tuple/list/dict/plist, getidx/setidx (PAIR), iter/itnext/ITER, unpack, in, is, cmp3, unary, call_va, binding | goldens + GC stress | closures, collections |
| M4 | Errors + defers | error objs, error class family, jerr/jnerr, defer drains, trap, fault unwinding | golden errors; trap in nested call runs mode-1 defers → `WY_ERR_FAULT` | errors |
| M5 | Classes + messages | class rework, instances, slots/attrs, virtual slots, construct-on-call, identities, msg/msg_va/getmsg/reg_msg/super, dunder hooks | goldens; ranking + 3-deep super unit tests | classes, messages |
| M6 | Modules + linking | import/import_star inline init frames, hook + `-I`, three-layer fill, cycle detection, getscope/setscope | two_module (with `-I`), wildcard; a↔b cycle → `WY_ERR_CYCLE` | two_module, wildcard |
| M7 | Coroutines | coroutine object, per-coroutine fiber, switch loop, yield/yield_from, next/send, StopIteration, GC | golden coroutines; abandon suspended coroutine → no leak | coroutines |
| M8 | Host API + hardening | `wy_vm_call_continue`, bridge tests, packed entry helper, GC-stress CI, gcc/clang/tcc, docs | full `meson test` green on 3 compilers | all |

## 11 File list

New: `include/wyrm/{image.h (verbatim), frame.h, symtab.h, function.h, tuple.h, list.h,
bytes.h, error.h, instance.h, message.h, coroutine.h, native.h, link.h}`;
`src/{image.c, symtab.c, vm_internal.h, vm_call.c, vm_ops.c, dispatch.c, link.c, coroutine.c,
tuple.c, list.c, bytes.c, error.c, instance.c, function.c, native.c}`,
`src/builtin/builtins.c`, `src/platform/hosted/import_fs.c`,
`src/test/{test_bytecode_golden.cpp, test_image.cpp, test_dispatch.cpp, test_coroutine.cpp,
fixtures/packed_ops.h}`, `test/bytecode/**`, `scripts/build_corpus.py`,
`pypoc/tools/generate_c_fixtures.py`.

Rewritten: `include/wyrm/{opcode.h, primitive.h, value.h, fiber.h, module.h, class.h,
string.h, context.h, vm.h, exec_fn.h}`, `src/{vm.c, module.c, fiber.c, context.c, gc.c,
machine.c, string.c, wclass.c, wyrm/main.c, test/test_wvm.cpp, test/test_class.cpp,
meson.build, test/meson.build}`.

Removed: `wy_prototype` use in classes, `wy_module_code_push/reserve_code_f`,
`wy_vm_exec_bytecode`.
