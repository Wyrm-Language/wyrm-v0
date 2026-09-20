#include "vm_internal.h"

#include <stdio.h>
#include <string.h>

#include <wyrm.h>
#include <wyrm/error.h>
#include <wyrm/dict.h>
#include <wyrm/function.h>
#include <wyrm/iter.h>
#include <wyrm/list.h>
#include <wyrm/native.h>
#include <wyrm/pair.h>
#include <wyrm/string.h>
#include <wyrm/tuple.h>
#include <wyrm/vm.h>

/**
 * The dispatch loop (design_c_vm.md §2). Entry point and continuation for
 * every bytecode frame; loops across bytecode-to-bytecode calls/returns
 * with no C recursion, yielding back to the fiber trampoline (WY_EXEC_*)
 * only for a native call, a fault, or a GC-abandoned-mid-instruction edge
 * case (none in this milestone).
 */

static wy_value fault_value_f(wy_context* ctx, const char* message)
{
    wy_string* what = WY_NULL;
    if (wy_string_strdup(ctx, message, &what) != WY_ERR_NONE) { return wy_value_word(-1); }
    wy_error_obj* obj = WY_NULL;
    if (wy_error_obj_new(ctx, WY_NULL, what, wy_value_nil(), &obj) != WY_ERR_NONE) { return wy_value_word(-1); }
    return wy_value_object(WY_TYPE_TAG_ERROR, (wy_object*) obj);
}

/**
 * `error("msg")`: the base error type's construction. The reference
 * (wyrm_builtins.py install()) binds `error` to a real Class and
 * special-cases its construct-on-call; general construct-on-call for
 * arbitrary user classes is epic 4 scope (design_c_vm.md's class rework),
 * but this one builtin case - calling the base `error` class with a single
 * string - is cheap to special-case here directly, exactly as the
 * reference special-cases it, without building generic class dispatch.
 * `argc`/`args` are the callee's positional arguments (a bare `call`'s
 * window, or `call_va`'s unpacked positional tuple).
 *
 * @return WY_ERR_NONE with `*out` set to the new error value, WY_ERR_ARITY
 *   or WY_ERR_BAD_TYPE for a bad call (caller faults), or WY_ERR_UNBOUND
 *   when `callee` is not the `error` class at all (not handled here).
 */
static wy_error try_construct_error_f(wy_context* ctx, wy_value callee, wy_value* args, wy_uword argc, wy_value* out)
{
    if (callee.type != WY_TYPE_TAG_CLASS || (wy_class*) callee.data.gc_object != ctx->error_class) {
        return WY_ERR_UNBOUND;
    }
    if (argc != 1) { return WY_ERR_ARITY; }
    if (args[0].type != WY_TYPE_TAG_STR) { return WY_ERR_BAD_TYPE; }

    wy_error_obj* obj = WY_NULL;
    wy_error err = wy_error_obj_new(ctx, ctx->error_class, args[0].data.str, wy_value_nil(), &obj);
    if (err != WY_ERR_NONE) { return err; }
    *out = wy_value_object(WY_TYPE_TAG_ERROR, (wy_object*) obj);
    return WY_ERR_NONE;
}

/** Longest binding-fault message this module formats (a missing-argument
 * trap, an unexpected-keyword list, or a too-many-positional trap). */
enum { WY_VM_CALL_FAULT_MSG = 256 };

/** Bound keyword arguments tracked per call, and sorted leftovers. The
 * compiler caps a parameter list at 128 (§8.5), so this never truncates a
 * well-formed image. */
enum { WY_VM_CALL_MAX_KWARGS = 128 };

/** True when the keyword argument named `key` was already bound to a
 * parameter during this call. */
static bool binding_kwarg_consumed_f(const wy_symbol* consumed, wy_uword n, wy_symbol key)
{
    for (wy_uword i = 0; i < n; i++) {
        if (consumed[i] == key) { return true; }
    }
    return false;
}

/**
 * Push a bytecode call frame for `fn` (design_c_vm.md §1.1.2), binding the
 * parameters the language specifies (frame.py:build_pframe, wyc-format.md
 * §8.5): positionally in order, then by keyword from `kwargs` (never mutated
 * - the caller's dict is shared), then a declared default from the module
 * statics, and a fault naming the parameter if nothing supplied it. A `*args`
 * collect the leftover positional values as a tuple and a `**kwargs` the
 * leftover keyword entries as a dict - ordinary P slots whose role the
 * function's flags record.
 *
 * One path, not a fast bit and a slow bit that can disagree on capture
 * order: the frame is always `[this(k)][params][captures]` (epic_3.md M2).
 * The common shape (exactly its parameters, no varargs/kwargs, no kwargs
 * dict) still copies straight through.
 *
 * `kwargs == WY_NULL` behaves exactly like an empty dict.
 *
 * On a binding failure the frame is not pushed, the value stack is restored,
 * and WY_ERR_ARITY returns with the trap text formatted into `fault_msg`
 * (the caller turns it into a fault value). Other errors (STACK_OVERFLOW,
 * NOMEM) return without touching `fault_msg`.
 */
static wy_error push_bytecode_call_bind_f(wy_context* ctx, wy_function* fn, const wy_value* args, wy_uword argc,
    wy_dict* kwargs, wy_value* ret_dst, wy_uword nres, wy_ret_kind ret_kind,
    char* fault_msg, wy_uword fault_msg_size)
{
    const wy_function_proto* proto = fn->proto;
    const bool has_varargs = (proto->flags & WY_FN_VARARGS) != 0;
    const bool has_kwargs = (proto->flags & WY_FN_KWARGS) != 0;
    const wy_uword plain = (wy_uword) proto->nparams
        - (has_varargs ? 1u : 0u) - (has_kwargs ? 1u : 0u);
    const wy_uword p_count = (wy_uword) proto->nparams + fn->ncaps;
    const wy_uword total = p_count + proto->nlocals;
    const wy_symbol fname = (proto->name != WY_NULL) ? proto->name : "<init>";

    wy_fiber* fiber = ctx->current_fiber;
    WY_ASSERT(proto->nparams == 0 || proto->params != WY_NULL);

    wy_frame* new_frame = fiber->current_frame + 1;
    if (!WY_MEM_INFO_TOP_NOT_AT_END(wy_frame, new_frame, &fiber->frame_memory)) { return WY_ERR_STACK_OVERFLOW; }
    if (wy_stack_capacity_remaining_f(&fiber->value_stack) < total) { return WY_ERR_STACK_OVERFLOW; }

    if (!has_varargs && !has_kwargs && kwargs == WY_NULL && argc == proto->nparams) {
        /* The common shape: the whole P frame is a straight copy. */
        wy_value* p = fiber->value_stack.top;
        wy_value* l = p + p_count;
        fiber->value_stack.top = l + proto->nlocals;

        for (wy_uword i = 0; i < argc; i++) { p[i] = args[i]; }
        for (wy_uword i = 0; i < fn->ncaps; i++) { p[argc + i] = fn->caps[i]; }
        for (wy_uword i = 0; i < proto->nlocals; i++) { l[i] = wy_value_unset(); }

        wy_memset(new_frame, 0, sizeof(wy_frame));
        new_frame->kind = WY_FRAME_BYTECODE;
        new_frame->ret_kind = (wy_u8) ret_kind;
        new_frame->ret_dst = ret_dst;
        new_frame->ret_nres = (wy_u16) nres;
        new_frame->p = p;
        new_frame->l = l;
        new_frame->p_count = (wy_u16) p_count;
        new_frame->ip = fn->module->code + proto->code_offset;
        new_frame->module = fn->module;
        new_frame->proto = proto;

        fiber->current_frame = new_frame;
        return WY_ERR_NONE;
    }

    /* Slow path: build the P frame slot by slot. The region is reserved now
     * and folded back on any failure. GC cannot run mid-instruction, so a
     * partial bind never escapes to a safepoint. */
    wy_value* saved_top = fiber->value_stack.top;
    wy_value* p = saved_top;
    fiber->value_stack.top = saved_top + p_count + proto->nlocals;
    for (wy_uword i = 0; i < proto->nlocals; i++) { p[p_count + i] = wy_value_unset(); }

    wy_symbol consumed[WY_VM_CALL_MAX_KWARGS];
    wy_uword nconsumed = 0;
    wy_uword taken = 0;
    wy_uword pslot = 0;

    for (wy_uword i = 0; i < plain; i++) {
        const wy_param* pm = &proto->params[i];
        wy_value bound;
        if (taken < argc) {
            bound = args[taken++];
        } else if (kwargs != WY_NULL) {
            wy_value* hit = wy_dict_get(ctx, kwargs, WY_TYPE_TAG_SYMBOL, wy_value_symbol(pm->name).data);
            if (hit != WY_NULL) {
                bound = *hit;
                if (nconsumed < WY_VM_CALL_MAX_KWARGS) { consumed[nconsumed++] = pm->name; }
                else {
                    fiber->value_stack.top = saved_top;
                    snprintf(fault_msg, fault_msg_size, "%s: too many keyword parameters", fname);
                    return WY_ERR_ARITY;
                }
            } else if (pm->default_static >= 0) {
                bound = fn->module->statics[pm->default_static];
            } else {
                fiber->value_stack.top = saved_top;
                snprintf(fault_msg, fault_msg_size, "%s() missing required argument: '%s'", fname, pm->name);
                return WY_ERR_ARITY;
            }
        } else if (pm->default_static >= 0) {
            bound = fn->module->statics[pm->default_static];
        } else {
            fiber->value_stack.top = saved_top;
            snprintf(fault_msg, fault_msg_size, "%s() missing required argument: '%s'", fname, pm->name);
            return WY_ERR_ARITY;
        }
        p[pslot++] = bound;
    }

    /* Leftover positional values: a `*args` slot, or a too-many trap. */
    if (has_varargs) {
        wy_tuple* rest = WY_NULL;
        wy_error err = wy_tuple_new(ctx, (argc > taken) ? &args[taken] : WY_NULL, argc - taken, &rest);
        if (err != WY_ERR_NONE) {
            fiber->value_stack.top = saved_top;
            return err;
        }
        p[pslot++] = wy_value_object(WY_TYPE_TAG_TUPLE, (wy_object*) rest);
    } else if (taken < argc) {
        fiber->value_stack.top = saved_top;
        snprintf(fault_msg, fault_msg_size, "%s() takes %u positional argument(s) but %u were given",
            fname, (unsigned) plain, (unsigned) argc);
        return WY_ERR_ARITY;
    }

    /* Leftover keyword entries: a `**kwargs` slot, or an unexpected trap. */
    if (has_kwargs) {
        wy_dict* kd = WY_NULL;
        wy_error err = wy_dict_new(ctx, &kd);
        if (err == WY_ERR_NONE && kwargs != WY_NULL) {
            for (wy_uword i = 0; i < kwargs->count && err == WY_ERR_NONE; i++) {
                const wy_key_hash_value* kv = &kwargs->dense[i];
                if (kv->key.type == WY_TYPE_TAG_SYMBOL
                    && binding_kwarg_consumed_f(consumed, nconsumed, kv->key.data.symtab_entry)) {
                    continue;
                }
                err = wy_dict_set(ctx, kd, kv->key.type, kv->key.data, kv->value.type, kv->value.data);
            }
        }
        if (err != WY_ERR_NONE) {
            fiber->value_stack.top = saved_top;
            return err;
        }
        p[pslot++] = wy_value_object(WY_TYPE_TAG_TABLE, (wy_object*) kd);
    } else if (kwargs != WY_NULL) {
        wy_symbol unexpected[WY_VM_CALL_MAX_KWARGS];
        wy_uword n_unexpected = 0;
        for (wy_uword i = 0; i < kwargs->count; i++) {
            const wy_key_hash_value* kv = &kwargs->dense[i];
            if (kv->key.type == WY_TYPE_TAG_SYMBOL && kv->key.data.symtab_entry != WY_NULL
                && !binding_kwarg_consumed_f(consumed, nconsumed, kv->key.data.symtab_entry)
                && n_unexpected < WY_VM_CALL_MAX_KWARGS) {
                unexpected[n_unexpected++] = kv->key.data.symtab_entry;
            }
        }
        if (n_unexpected > 0) {
            /* Insertion sort, lexicographic by name (the reference sorts the
             * leftover dict's keys the same way). */
            for (wy_uword i = 1; i < n_unexpected; i++) {
                wy_symbol key = unexpected[i];
                wy_uword j = i;
                while (j > 0 && strcmp(unexpected[j - 1], key) > 0) {
                    unexpected[j] = unexpected[j - 1];
                    j--;
                }
                unexpected[j] = key;
            }
            char joined[WY_VM_CALL_FAULT_MSG] = "";
            wy_uword at = 0;
            for (wy_uword i = 0; i < n_unexpected; i++) {
                int n = snprintf(joined + at, sizeof(joined) - at, "%s%s",
                    (i > 0) ? ", " : "", unexpected[i]);
                if (n < 0 || (wy_uword) n >= sizeof(joined) - at) { break; }
                at += (wy_uword) n;
            }
            fiber->value_stack.top = saved_top;
            snprintf(fault_msg, fault_msg_size, "%s() got unexpected keyword argument(s): %s", fname, joined);
            return WY_ERR_ARITY;
        }
    }

    /* Captures come last, in declaration order (§1.1). */
    for (wy_uword i = 0; i < fn->ncaps; i++) { p[pslot++] = fn->caps[i]; }

    wy_value* l = p + p_count;
    wy_memset(new_frame, 0, sizeof(wy_frame));
    new_frame->kind = WY_FRAME_BYTECODE;
    new_frame->ret_kind = (wy_u8) ret_kind;
    new_frame->ret_dst = ret_dst;
    new_frame->ret_nres = (wy_u16) nres;
    new_frame->p = p;
    new_frame->l = l;
    new_frame->p_count = (wy_u16) p_count;
    new_frame->ip = fn->module->code + proto->code_offset;
    new_frame->module = fn->module;
    new_frame->proto = proto;

    fiber->current_frame = new_frame;
    return WY_ERR_NONE;
}

/**
 * The name a free global slot was declared under, or WY_SYMBOL_INVALID when
 * `slot` is the module's own global. A linear scan, taken only when a read
 * finds Unset.
 */
static wy_symbol free_slot_name_f(const wy_module* mod, wy_uword slot)
{
    const wy_slot_dict* names = &mod->free_names;
    for (wy_uword i = 0; i < names->capacity; i++) {
        const wy_slot_dict_entry* entry = &names->entry_table[i];
        if (entry->symbol != WY_SYMBOL_INVALID && entry->slot == slot) { return entry->symbol; }
    }
    return WY_SYMBOL_INVALID;
}

/** The fault text for a failed bytecode frame push. */
static const char* push_fault_text_f(wy_error err, const char* fault_msg)
{
    switch (err) {
    case WY_ERR_ARITY:          return fault_msg;
    case WY_ERR_STACK_OVERFLOW: return "stack overflow";
    case WY_ERR_NOMEM:          return "out of memory";
    default:                    return "call failed";
    }
}

/** `trap`'s message by code (wyc-format.md §6.1, interp.py TRAP_CODES). */
static wy_value trap_fault_value_f(wy_context* ctx, wy_u8 code)
{
    if (code == 0) { return fault_value_f(ctx, "unreachable code reached - or a function body the compiler could not lower"); }
    if (code == 1) { return fault_value_f(ctx, "debugger break"); }
    char msg[16];
    snprintf(msg, sizeof(msg), "trap %u", (unsigned) code);
    return fault_value_f(ctx, msg);
}

/** Defer modes (`defer_reg`'s f, wyc-format.md §6.3). */
enum
{
    WY_DEFER_ALWAYS = 0,
    WY_DEFER_ON_ERROR = 1,
    WY_DEFER_ON_ERROR_OR_NIL = 2,
};

/**
 * Arm a defer on `fr`: cons `(closure . mode)` onto `fr->defers`, most
 * recent first (design_c_vm.md §1.1). Two pairs per defer - the entry and
 * the chain link - reachable from the fiber's frame walk.
 */
static wy_error defer_register_f(wy_context* ctx, wy_frame* fr, wy_value closure, wy_u8 mode)
{
    wy_pair* entry = wy_pair_cons_f(ctx, closure, wy_value_word(mode));
    if (entry == WY_NULL) { return WY_ERR_NOMEM; }
    wy_value tail = (fr->defers != WY_NULL)
        ? wy_value_object(WY_TYPE_TAG_PAIR, (wy_object*) fr->defers)
        : wy_value_nil();
    wy_pair* link = wy_pair_cons_f(ctx, wy_value_object(WY_TYPE_TAG_PAIR, (wy_object*) entry), tail);
    if (link == WY_NULL) { return WY_ERR_NOMEM; }
    fr->defers = link;
    return WY_ERR_NONE;
}

/**
 * Pop the most recent defer off `fr` whose mode says it runs, given how the
 * frame is leaving: `failing` forces the error condition (interp.py
 * run_defers(..., failed=True)), otherwise `result0` - the first returned
 * value, nil for an empty return - decides modes 1 and 2. Defers whose mode
 * says no are discarded on the way.
 *
 * @return true with `*closure` set, or false once the chain is empty
 */
static bool defer_pop_runnable_f(wy_frame* fr, bool failing, wy_value result0, wy_value* closure)
{
    const bool is_error = failing || wy_value_is_error(result0);
    const bool is_nil = !failing && result0.type == WY_TYPE_TAG_NIL;
    while (fr->defers != WY_NULL) {
        wy_pair* link = fr->defers;
        wy_pair* entry = (wy_pair*) link->car.data.gc_object;
        fr->defers = (link->cdr.type == WY_TYPE_TAG_PAIR) ? (wy_pair*) link->cdr.data.gc_object : WY_NULL;

        wy_word mode = entry->cdr.data.word;
        if (mode == WY_DEFER_ON_ERROR && !is_error) { continue; }
        if (mode == WY_DEFER_ON_ERROR_OR_NIL && !(is_error || is_nil)) { continue; }
        *closure = entry->car;
        return true;
    }
    return false;
}

wy_exec_state wy_vm_run(wy_context* ctx, wy_primitive unused)
{
    WY_UNUSED(unused);
    wy_fiber* fb = ctx->current_fiber;
    wy_value fault_v = wy_value_unset();

reload:;
    wy_frame* fr = fb->current_frame;
    const wy_u32* ip = fr->ip;
    wy_value* L = fr->l;
    wy_module* mod = fr->module;
    wy_value* G = mod->globals;

    /* A frame that pushed a defer lands back here when the defer returns
     * (or unwinds past it): resume its drain rather than its code. */
    if (fr->phase == WY_PHASE_RETURNING) { goto do_return; }
    if (fr->phase == WY_PHASE_FAILING) { goto do_unwind; }

    for (;;) {
        if (ctx->gc_pressure > ctx->gc_threshold) {
            fr->ip = ip;
            wy_context_gc_safepoint(ctx);
        }

        wy_u32 w0 = *ip;
        wy_u8 op = (wy_u8) (w0 & 0xffu);
        wy_u8 f = (wy_u8) ((w0 >> 8) & 0xffu);
        wy_u16 a0 = (wy_u16) (w0 >> 16);
        wy_u16 a1 = 0, a2 = 0;
        wy_u32 w1 = 0;
        wy_uword words = (op & WYRM_OP_LONG_START) ? 2u : 1u;
        if (words == 2) {
            w1 = ip[1];
            a1 = (wy_u16) (w1 >> 16);
            a2 = (wy_u16) (w1 & 0xffffu);
        }
        const wy_u32* next_ip = ip + words;

        switch (op) {

        /* -- core one-word ops -- */
        case WY_OP_NOOP:
            ip = next_ip;
            break;

        case WY_OP_TRAP:
            fr->ip = next_ip;
            fault_v = trap_fault_value_f(ctx, f);
            goto do_fault;

        case WY_OP_RETURN:
            /* The window stays in this frame's own L while defers drain. */
            fr->ip = next_ip;
            fr->ret_base = a0;
            fr->ret_count = f;
            fr->phase = WY_PHASE_RETURNING;
            goto do_return;

        case WY_OP_LNIL:
            *wy_vm_reg_f(fr, a0) = wy_value_nil();
            ip = next_ip;
            break;

        case WY_OP_LBOOL:
            *wy_vm_reg_f(fr, a0) = wy_value_bool(f != 0);
            ip = next_ip;
            break;

        case WY_OP_LUNSET:
            *wy_vm_reg_f(fr, a0) = wy_value_unset();
            ip = next_ip;
            break;

        /* -- pairable ops: compact packs the second operand in f, wide in a1/w1 -- */
        case WY_OP_I8:
            *wy_vm_reg_f(fr, a0) = wy_value_word((int8_t) f);
            ip = next_ip;
            break;
        case WY_OP_I32_WIDE:
            *wy_vm_reg_f(fr, a0) = wy_value_word((wy_word) (wy_i32) w1);
            ip = next_ip;
            break;

        case WY_OP_MOVE:
            *wy_vm_reg_f(fr, a0) = *wy_vm_reg8_f(fr, f);
            ip = next_ip;
            break;
        case WY_OP_MOVE_WIDE:
            *wy_vm_reg_f(fr, a0) = *wy_vm_reg_f(fr, a1);
            ip = next_ip;
            break;

        case WY_OP_GGET: case WY_OP_GGET_WIDE: {
            wy_uword dst = (op == WY_OP_GGET) ? f : a1;
            wy_value g = G[a0];
            if (wy_value_is_unset(g)) {
                /* Only a free slot nothing filled is unbound (interp.py
                 * OP_GGET, wyc-format.md §7.3); a module's own global that
                 * is not assigned yet reads as Unset, which `?=`/jnerr test. */
                wy_symbol name = free_slot_name_f(mod, a0);
                if (name != WY_SYMBOL_INVALID) {
                    fr->ip = next_ip;
                    char msg[WY_VM_CALL_FAULT_MSG];
                    snprintf(msg, sizeof(msg), "unbound global '%s'", name);
                    fault_v = fault_value_f(ctx, msg);
                    goto do_fault;
                }
            }
            *wy_vm_reg8_f(fr, (wy_u8) dst) = g;
            ip = next_ip;
            break;
        }
        case WY_OP_GSET: case WY_OP_GSET_WIDE: {
            wy_uword src = (op == WY_OP_GSET) ? f : a1;
            G[a0] = *wy_vm_reg8_f(fr, (wy_u8) src);
            ip = next_ip;
            break;
        }

        case WY_OP_LSYM: case WY_OP_LSYM_WIDE: {
            wy_uword dst = (op == WY_OP_LSYM) ? f : a1;
            *wy_vm_reg8_f(fr, (wy_u8) dst) = wy_value_symbol(mod->symbols[a0]);
            ip = next_ip;
            break;
        }
        case WY_OP_LCONST: case WY_OP_LCONST_WIDE: {
            wy_uword dst = (op == WY_OP_LCONST) ? f : a1;
            *wy_vm_reg8_f(fr, (wy_u8) dst) = mod->statics[a0];
            ip = next_ip;
            break;
        }

        case WY_OP_NEG: case WY_OP_NEG_WIDE:
        case WY_OP_INV: case WY_OP_INV_WIDE:
        case WY_OP_NOT: case WY_OP_NOT_WIDE: {
            bool wide = (op & WYRM_OP_LONG_START) != 0;
            wy_uword src = wide ? a1 : f;
            wy_u8 base_op = wide ? (wy_u8)(op & ~WYRM_OP_LONG_START) : op;
            wy_value out;
            wy_error err = wy_vm_unary_f(ctx, base_op, *wy_vm_reg8_f(fr, (wy_u8) src), &out);
            if (err != WY_ERR_NONE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "unary operator failed");
                goto do_fault;
            }
            *wy_vm_reg_f(fr, a0) = out;
            ip = next_ip;
            break;
        }

        case WY_OP_JF: case WY_OP_JF_WIDE:
        case WY_OP_JT: case WY_OP_JT_WIDE:
        case WY_OP_JERR: case WY_OP_JERR_WIDE:
        case WY_OP_JNERR: case WY_OP_JNERR_WIDE: {
            bool wide = (op & WYRM_OP_LONG_START) != 0;
            wy_value cond = wide ? *wy_vm_reg_f(fr, a0) : *wy_vm_reg8_f(fr, f);
            wy_i32 rel = wide ? (wy_i32) w1 : (int16_t) a0;
            wy_u8 base_op = wide ? (wy_u8) (op & ~WYRM_OP_LONG_START) : op;
            bool take;
            switch (base_op) {
            case WY_OP_JF:    take = !wy_value_truthy(cond); break;
            case WY_OP_JT:    take = wy_value_truthy(cond); break;
            case WY_OP_JERR:  take = wy_value_is_error(cond); break;
            case WY_OP_JNERR: take = !wy_value_is_error(cond); break;
            default:          take = false; break;
            }
            ip = take ? (next_ip + rel) : next_ip;
            break;
        }
        case WY_OP_JMP: {
            wy_i32 rel = (int16_t) a0;
            ip = next_ip + rel;
            break;
        }
        case WY_OP_JMP_WIDE: {
            wy_i32 rel = (wy_i32) w1;
            ip = next_ip + rel;
            break;
        }

        /* -- two-word ops -- */
        case WY_OP_F32: {
            wy_u32 bits = w1;
            float value;
            wy_memcpy(&value, &bits, sizeof(value));
            *wy_vm_reg_f(fr, a0) = wy_value_float((double) value);
            ip = next_ip;
            break;
        }

        case WY_OP_TUPLE: {
            wy_uword base = a1, count = f;
            wy_tuple* tup = WY_NULL;
            wy_error err = wy_tuple_new(ctx, &L[base], count, &tup);
            if (err != WY_ERR_NONE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "tuple construction failed");
                goto do_fault;
            }
            *wy_vm_reg_f(fr, a0) = wy_value_object(WY_TYPE_TAG_TUPLE, (wy_object*) tup);
            ip = next_ip;
            break;
        }
        case WY_OP_LIST: {
            wy_uword base = a1, count = f;
            wy_list* list = WY_NULL;
            wy_error err = wy_list_new(ctx, count, &list);
            if (err == WY_ERR_NONE) {
                for (wy_uword i = 0; i < count; i++) {
                    wy_list_push(ctx, list, L[base + i]);
                }
            }
            if (err != WY_ERR_NONE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "list construction failed");
                goto do_fault;
            }
            *wy_vm_reg_f(fr, a0) = wy_value_object(WY_TYPE_TAG_LIST, (wy_object*) list);
            ip = next_ip;
            break;
        }
        case WY_OP_DICT: {
            wy_uword base = a1, count = f;
            wy_dict* dict = WY_NULL;
            wy_error err = wy_dict_new(ctx, &dict);
            if (err == WY_ERR_NONE) {
                for (wy_uword i = 0; i < count; i++) {
                    err = wy_dict_set(ctx, dict, L[base + 2*i].type, L[base + 2*i].data, L[base + 2*i + 1].type, L[base + 2*i + 1].data);
                    if (err != WY_ERR_NONE) break;
                }
            }
            if (err != WY_ERR_NONE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "dict construction failed");
                goto do_fault;
            }
            *wy_vm_reg_f(fr, a0) = wy_value_object(WY_TYPE_TAG_TABLE, (wy_object*) dict);
            ip = next_ip;
            break;
        }
        case WY_OP_PLIST: {
            wy_uword base = a1, count = f;
            L[a0] = wy_value_nil();
            for (wy_word i = (wy_word)count - 1; i >= 0; i--) {
                wy_pair* p = wy_pair_cons_f(ctx, L[base + i], L[a0]);
                if (!p) {
                    fr->ip = next_ip;
                    fault_v = fault_value_f(ctx, "plist construction failed");
                    goto do_fault;
                }
                L[a0] = wy_value_object(WY_TYPE_TAG_PAIR, (wy_object*) p);
            }
            ip = next_ip;
            break;
        }

        case WY_OP_GETIDX: {
            wy_value out;
            wy_error err = wy_vm_getidx_f(ctx, *wy_vm_reg_f(fr, a1), *wy_vm_reg_f(fr, a2), &out);
            if (err != WY_ERR_NONE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "getidx failed");
                goto do_fault;
            }
            *wy_vm_reg_f(fr, a0) = out;
            ip = next_ip;
            break;
        }
        case WY_OP_SETIDX: {
            wy_error err = wy_vm_setidx_f(ctx, *wy_vm_reg_f(fr, a0), *wy_vm_reg_f(fr, a1), *wy_vm_reg_f(fr, a2));
            if (err != WY_ERR_NONE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "setidx failed");
                goto do_fault;
            }
            ip = next_ip;
            break;
        }

        case WY_OP_NEW_PRIMITIVE: {
            wy_value out = wy_value_nil();
            wy_error err = WY_ERR_NONE;
            switch ((wy_type_tag) f) {
            case WY_TYPE_TAG_LIST: {
                wy_list* list = WY_NULL;
                err = wy_list_new(ctx, 0, &list);
                if (err == WY_ERR_NONE) out = wy_value_object(WY_TYPE_TAG_LIST, (wy_object*) list);
                break;
            }
            case WY_TYPE_TAG_TABLE: {
                wy_dict* dict = WY_NULL;
                err = wy_dict_new(ctx, &dict);
                if (err == WY_ERR_NONE) out = wy_value_object(WY_TYPE_TAG_TABLE, (wy_object*) dict);
                break;
            }
            case WY_TYPE_TAG_TUPLE: {
                wy_tuple* tup = WY_NULL;
                err = wy_tuple_new(ctx, WY_NULL, 0, &tup);
                if (err == WY_ERR_NONE) out = wy_value_object(WY_TYPE_TAG_TUPLE, (wy_object*) tup);
                break;
            }
            case WY_TYPE_TAG_PAIR: {
                wy_pair* p = wy_pair_cons_f(ctx, wy_value_nil(), wy_value_nil());
                if (p) out = wy_value_object(WY_TYPE_TAG_PAIR, (wy_object*) p);
                else err = WY_ERR_NOMEM;
                break;
            }
            default: err = WY_ERR_INVAL; break;
            }
            if (err != WY_ERR_NONE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "new_primitive failed");
                goto do_fault;
            }
            *wy_vm_reg_f(fr, a0) = out;
            ip = next_ip;
            break;
        }

        case WY_OP_ADD: case WY_OP_SUB: case WY_OP_MUL: case WY_OP_DIV: case WY_OP_MOD:
        case WY_OP_POW: case WY_OP_BAND: case WY_OP_BOR: case WY_OP_SHL: case WY_OP_SHR:
        case WY_OP_EQ: case WY_OP_NE: case WY_OP_LT: case WY_OP_LE: case WY_OP_GT: case WY_OP_GE:
        case WY_OP_BXOR: case WY_OP_CMP3: {
            wy_value out;
            wy_error err = wy_vm_binop_f(ctx, op, *wy_vm_reg_f(fr, a1), *wy_vm_reg_f(fr, a2), &out);
            if (err != WY_ERR_NONE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "binary operator failed");
                goto do_fault;
            }
            *wy_vm_reg_f(fr, a0) = out;
            ip = next_ip;
            break;
        }

        case WY_OP_IS: {
            wy_value out;
            wy_error err = wy_vm_is_f(ctx, *wy_vm_reg_f(fr, a1), *wy_vm_reg_f(fr, a2), &out);
            if (err != WY_ERR_NONE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "`is` failed");
                goto do_fault;
            }
            *wy_vm_reg_f(fr, a0) = out;
            ip = next_ip;
            break;
        }

        case WY_OP_IN: {
            wy_value out;
            wy_error err = wy_vm_in_f(ctx, *wy_vm_reg_f(fr, a1), *wy_vm_reg_f(fr, a2), &out);
            if (err != WY_ERR_NONE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "`in` failed");
                goto do_fault;
            }
            *wy_vm_reg_f(fr, a0) = out;
            ip = next_ip;
            break;
        }

        case WY_OP_ITER: {
            wy_iterator* it = WY_NULL;
            wy_error err = wy_iterator_new(ctx, *wy_vm_reg_f(fr, a1), &it);
            if (err != WY_ERR_NONE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "iter failed");
                goto do_fault;
            }
            *wy_vm_reg_f(fr, a0) = wy_value_object(WY_TYPE_TAG_ITER, (wy_object*) it);
            ip = next_ip;
            break;
        }

        case WY_OP_ITNEXT: {
            wy_value it_val = *wy_vm_reg_f(fr, a1);
            if (it_val.type != WY_TYPE_TAG_ITER) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "itnext: object is not an iterator");
                goto do_fault;
            }
            wy_value item;
            wy_error err = wy_iterator_next(ctx, (wy_iterator*) it_val.data.gc_object, &item);
            if (err == WY_ERR_STOP_ITERATION) {
                ip = next_ip + (int16_t) a2;
            } else if (err != WY_ERR_NONE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "itnext failed");
                goto do_fault;
            } else {
                *wy_vm_reg_f(fr, a0) = item;
                ip = next_ip;
            }
            break;
        }

        case WY_OP_UNPACK: {
            wy_uword base = a0, count = f;
            wy_value src = *wy_vm_reg_f(fr, a1);
            bool match = false;

            if (src.type == WY_TYPE_TAG_LIST) {
                wy_list* list = (wy_list*) src.data.gc_object;
                if (list->count == count) {
                    for (wy_uword i = 0; i < count; i++) L[base + i] = list->items[i];
                    match = true;
                }
            } else if (src.type == WY_TYPE_TAG_TUPLE) {
                wy_tuple* tup = (wy_tuple*) src.data.gc_object;
                if (tup->count == count) {
                    for (wy_uword i = 0; i < count; i++) L[base + i] = tup->items[i];
                    match = true;
                }
            } else if (src.type == WY_TYPE_TAG_STR) {
                wy_string* s = src.data.str;
                wy_uword offset = 0, n = 0;
                for (; offset < s->len; n++) {
                    offset += wy_utf8_decode_f(s->str, s->len, offset, &(wy_u32){0});
                }
                if (n == count) {
                    offset = 0;
                    match = true;
                    for (wy_uword i = 0; i < count && match; i++) {
                        wy_u32 cp;
                        wy_uword seq_len = wy_utf8_decode_f(s->str, s->len, offset, &cp);
                        wy_string* ch = WY_NULL;
                        wy_error cherr = wy_string_new(ctx, s->str + offset, seq_len, &ch);
                        if (cherr != WY_ERR_NONE) { match = false; break; }
                        L[base + i] = wy_value_object(WY_TYPE_TAG_STR, (wy_object*) ch);
                        offset += seq_len;
                    }
                }
            } else if (src.type == WY_TYPE_TAG_PAIR) {
                wy_uword actual = 0;
                wy_value curr = src;
                while (curr.type == WY_TYPE_TAG_PAIR) {
                    actual++;
                    curr = ((wy_pair*)curr.data.gc_object)->cdr;
                }
                if (curr.type == WY_TYPE_TAG_NIL && actual == count) {
                    curr = src;
                    for (wy_uword i = 0; i < count; i++) {
                        wy_pair* p = (wy_pair*) curr.data.gc_object;
                        L[base + i] = p->car;
                        curr = p->cdr;
                    }
                    match = true;
                }
            } else if (src.type == WY_TYPE_TAG_TABLE) {
                wy_dict* dict = (wy_dict*) src.data.gc_object;
                if (dict->count == count) {
                    for (wy_uword i = 0; i < count; i++) L[base + i] = dict->dense[i].key;
                    match = true;
                }
            }

            if (!match) {
                wy_value err_val = fault_value_f(ctx, "unpack mismatch");
                for (wy_uword i = 0; i < count; i++) L[base + i] = err_val;
            }

            ip = next_ip;
            break;
        }

        case WY_OP_CLOSURE: {
            /* Captures are copied now, in P-frame capture order (§1.1), by
             * value, from the `f` L slots starting at a2: a capture that
             * must stay shared is a cell the compiler built out of
             * plist/getidx/setidx, and the register holds that cell rather
             * than the variable. */
            wy_function* fn = WY_NULL;
            wy_error c_err = wy_function_new(ctx, mod, &mod->functions[a1],
                (f > 0) ? &L[a2] : WY_NULL, f, &fn);
            if (c_err != WY_ERR_NONE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "out of memory");
                goto do_fault;
            }
            *wy_vm_reg_f(fr, a0) = wy_value_object(WY_TYPE_TAG_FUNCTION, (wy_object*) fn);
            ip = next_ip;
            break;
        }

        case WY_OP_CALL: {
            wy_uword base = a0, argc = f, nres = a1;
            wy_value callee = L[base];

            if (callee.type == WY_TYPE_TAG_CLASS) {
                wy_value result;
                wy_error err = try_construct_error_f(ctx, callee, &L[base + 1], argc, &result);
                if (err != WY_ERR_UNBOUND) {
                    if (err != WY_ERR_NONE) {
                        fr->ip = next_ip;
                        fault_v = fault_value_f(ctx,
                            err == WY_ERR_ARITY ? "wrong argument count" : "error() takes a string");
                        goto do_fault;
                    }
                    *wy_vm_reg_f(fr, base) = result;
                    for (wy_uword i = 1; i < nres; i++) { *wy_vm_reg_f(fr, base + i) = wy_value_nil(); }
                    ip = next_ip;
                    break;
                }
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "value is not callable");
                goto do_fault;
            }

            if (callee.type == WY_TYPE_TAG_FUNCTION) {
                fr->ip = next_ip;
                char fault_msg[WY_VM_CALL_FAULT_MSG];
                wy_error err = push_bytecode_call_bind_f(ctx, (wy_function*) callee.data.gc_object,
                    &L[base + 1], argc, WY_NULL, &L[base], nres, WY_RET_WINDOW,
                    fault_msg, sizeof(fault_msg));
                if (err != WY_ERR_NONE) {
                    fault_v = fault_value_f(ctx,
                        push_fault_text_f(err, fault_msg));
                    goto do_fault;
                }
                goto reload;
            }

            if (callee.type == WY_TYPE_TAG_NATIVE) {
                wy_native* native = (wy_native*) callee.data.gc_object;
                if (native->kind != WY_NATIVE_LEAF) {
                    fr->ip = next_ip;
                    fault_v = fault_value_f(ctx,
                        "exec natives called from bytecode are not supported until epic 3+");
                    goto do_fault;
                }
                wy_error err = wy_vm_call_leaf_f(ctx, native, &L[base + 1], argc, &L[base], nres);
                if (err != WY_ERR_NONE) {
                    fr->ip = next_ip;
                    fault_v = fault_value_f(ctx,
                        err == WY_ERR_ARITY ? "wrong argument count" : "native call failed");
                    goto do_fault;
                }
                /* Backfill nil past whatever the leaf actually wrote, same
                 * as a bytecode return would (leaf natives write directly,
                 * they don't know nres vs. count the way RETURN's operand
                 * does - callers below (println/print) always fill every
                 * reserved slot themselves). */
                ip = next_ip;
                break;
            }

            fr->ip = next_ip;
            fault_v = fault_value_f(ctx, "value is not callable");
            goto do_fault;
        }

        case WY_OP_CALL_VA: {
            /* `f(*a, **k)`: the callee sits at the window base, with the
             * positional tuple and the keyword dict the compiler joined for
             * us above it (§6.3). Results land back at the base. */
            wy_uword base = a0, nres = a1;
            wy_value callee = L[base];
            wy_value posv = L[base + 1];
            wy_value kwv = L[base + 2];

            if (callee.type == WY_TYPE_TAG_CLASS) {
                if (posv.type != WY_TYPE_TAG_TUPLE) {
                    fr->ip = next_ip;
                    fault_v = fault_value_f(ctx, "call_va: expected a positional tuple");
                    goto do_fault;
                }
                wy_tuple* tup = (wy_tuple*) posv.data.gc_object;
                wy_value* args = (tup->count > 0) ? (wy_value*) tup->items : WY_NULL;
                wy_value result;
                wy_error err = try_construct_error_f(ctx, callee, args, tup->count, &result);
                if (err != WY_ERR_UNBOUND) {
                    if (err != WY_ERR_NONE) {
                        fr->ip = next_ip;
                        fault_v = fault_value_f(ctx,
                            err == WY_ERR_ARITY ? "wrong argument count" : "error() takes a string");
                        goto do_fault;
                    }
                    *wy_vm_reg_f(fr, base) = result;
                    for (wy_uword i = 1; i < nres; i++) { *wy_vm_reg_f(fr, base + i) = wy_value_nil(); }
                    ip = next_ip;
                    break;
                }
            }

            if (callee.type != WY_TYPE_TAG_FUNCTION && callee.type != WY_TYPE_TAG_NATIVE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "value is not callable");
                goto do_fault;
            }
            if (posv.type != WY_TYPE_TAG_TUPLE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "call_va: expected a positional tuple");
                goto do_fault;
            }
            if (kwv.type != WY_TYPE_TAG_TABLE && kwv.type != WY_TYPE_TAG_NIL) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "call_va: expected a keyword dict");
                goto do_fault;
            }
            wy_tuple* tup = (wy_tuple*) posv.data.gc_object;
            wy_dict* kwargs = (kwv.type == WY_TYPE_TAG_TABLE) ? (wy_dict*) kwv.data.gc_object : WY_NULL;
            /* A leaf native only reads its arguments; the tuple is not
             * mutated here (wy_vm_call_leaf_f's signature predates const). */
            wy_value* args = (tup->count > 0) ? (wy_value*) tup->items : WY_NULL;

            if (callee.type == WY_TYPE_TAG_NATIVE) {
                wy_native* native = (wy_native*) callee.data.gc_object;
                if (native->kind != WY_NATIVE_LEAF
                    || (kwargs != WY_NULL && kwargs->count > 0)) {
                    fr->ip = next_ip;
                    fault_v = fault_value_f(ctx, "call_va: native keyword calls are not supported");
                    goto do_fault;
                }
                wy_error err = wy_vm_call_leaf_f(ctx, native, args, tup->count, &L[base], nres);
                if (err != WY_ERR_NONE) {
                    fr->ip = next_ip;
                    fault_v = fault_value_f(ctx,
                        err == WY_ERR_ARITY ? "wrong argument count" : "native call failed");
                    goto do_fault;
                }
                ip = next_ip;
                break;
            }

            fr->ip = next_ip;
            char fault_msg[WY_VM_CALL_FAULT_MSG];
            wy_error err = push_bytecode_call_bind_f(ctx, (wy_function*) callee.data.gc_object,
                args, tup->count,
                (kwargs != WY_NULL && kwargs->count > 0) ? kwargs : WY_NULL,
                &L[base], nres, WY_RET_WINDOW, fault_msg, sizeof(fault_msg));
            if (err != WY_ERR_NONE) {
                fault_v = fault_value_f(ctx,
                    push_fault_text_f(err, fault_msg));
                goto do_fault;
            }
            goto reload;
        }

        case WY_OP_DEFER_REG: {
            wy_value closure = *wy_vm_reg_f(fr, a0);
            fr->ip = next_ip;
            if (closure.type != WY_TYPE_TAG_FUNCTION) {
                fault_v = fault_value_f(ctx, "defer_reg: not a function");
                goto do_fault;
            }
            if (f > WY_DEFER_ON_ERROR_OR_NIL) {
                fault_v = fault_value_f(ctx, "defer_reg: invalid mode");
                goto do_fault;
            }
            if (defer_register_f(ctx, fr, closure, f) != WY_ERR_NONE) {
                fault_v = fault_value_f(ctx, "out of memory");
                goto do_fault;
            }
            ip = next_ip;
            break;
        }

        default:
            fr->ip = next_ip;
            fault_v = fault_value_f(ctx, "unknown or unimplemented opcode");
            goto do_fault;
        }
    }

    /*
     * Leaving `fr` normally (design_c_vm.md §2). Each runnable defer is
     * pushed as an ordinary DISCARD call and the loop reloads into it; its
     * return pops back to `fr`, still RETURNING, and lands here again for
     * the next one. The C stack never grows with the defer chain or with
     * the frame depth.
     */
do_return: {
    const wy_value* src = wy_vm_reg_f(fr, fr->ret_base);
    wy_value result0 = (fr->ret_count > 0) ? src[0] : wy_value_nil();
    wy_value closure;
    if (defer_pop_runnable_f(fr, false, result0, &closure)) {
        char fault_msg[WY_VM_CALL_FAULT_MSG];
        wy_error err = push_bytecode_call_bind_f(ctx, (wy_function*) closure.data.gc_object,
            WY_NULL, 0, WY_NULL, WY_NULL, 0, WY_RET_DISCARD, fault_msg, sizeof(fault_msg));
        if (err != WY_ERR_NONE) {
            /* A defer that cannot even start fails the frame; the rest of
             * its defers still run, as on-error ones. */
            fault_v = fault_value_f(ctx, push_fault_text_f(err, fault_msg));
            goto do_fault;
        }
        goto reload;
    }

    switch ((wy_ret_kind) fr->ret_kind) {
    case WY_RET_WINDOW:
        wy_vm_backfill_f(fr->ret_dst, fr->ret_nres, src, fr->ret_count);
        break;
    case WY_RET_DISCARD:
        break;
    default:
        /* RESERVED/CONSTRUCT/IMPORT/COROUTINE are not produced by a
         * BYTECODE frame push yet (epic 4+). */
        break;
    }
    fb->value_stack.top = fr->p;
    fb->current_frame = fr - 1;
    if (fb->current_frame->kind == WY_FRAME_NATIVE) { return WY_EXEC_DONE; }
    goto reload;
}

    /*
     * A fault in `fr` (trap, failed store, uncallable, stack overflow, a
     * binding failure...). The newest fault wins: a defer that faults while
     * its frame is already unwinding replaces the fault it was running for.
     */
do_fault:
    fb->fault = fault_v;
    fr->phase = WY_PHASE_FAILING;
    /* fall through */

    /*
     * Unwind one frame: drain its defers with the error condition forced
     * (every mode runs), each as a DISCARD call the loop reloads into, then
     * pop it and mark the caller FAILING. A native caller (or the fiber
     * root) ends the unwind with WY_EXEC_FAULT and `fiber->fault` set.
     */
do_unwind: {
    wy_value closure;
    while (defer_pop_runnable_f(fr, true, wy_value_nil(), &closure)) {
        char fault_msg[WY_VM_CALL_FAULT_MSG];
        wy_error err = push_bytecode_call_bind_f(ctx, (wy_function*) closure.data.gc_object,
            WY_NULL, 0, WY_NULL, WY_NULL, 0, WY_RET_DISCARD, fault_msg, sizeof(fault_msg));
        if (err == WY_ERR_NONE) { goto reload; }
        /* No room (the fault may itself be a stack overflow): this defer
         * is lost, the original fault stands, and outer frames - which
         * have room again once this one pops - still drain theirs. */
    }
    fb->value_stack.top = fr->p;
    fb->current_frame = fr - 1;
    if (fb->current_frame->kind == WY_FRAME_NATIVE) { return WY_EXEC_FAULT; }
    fb->current_frame->phase = WY_PHASE_FAILING;
    goto reload;
}
}

/**
 * Continuation of every bytecode frame after a native exec call. Not
 * reachable from this milestone's own dispatch (WY_OP_CALL only pushes
 * leaf natives; exec natives from bytecode fault), but declared per
 * design_c_vm.md §1.3 for epic 3+ to wire up.
 */
wy_error wy_vm_call_sync(wy_context* ctx, wy_value callee, const wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    if (ctx == WY_NULL || ctx->current_fiber == WY_NULL) { return WY_ERR_INVAL; }
    if (callee.type != WY_TYPE_TAG_FUNCTION) { return WY_ERR_BAD_TYPE; }

    char fault_msg[WY_VM_CALL_FAULT_MSG];
    wy_error err = push_bytecode_call_bind_f(ctx, (wy_function*) callee.data.gc_object, args, argc, WY_NULL,
        out, nres, WY_RET_WINDOW, fault_msg, sizeof(fault_msg));
    if (err == WY_ERR_ARITY) {
        /* The parameters wouldn't bind (a missing required argument, a too
         * many/twisted call...): report it the way the dispatch loop does,
         * as a fault naming the parameter. */
        ctx->current_fiber->fault = fault_value_f(ctx, fault_msg);
        return WY_ERR_FAULT;
    }
    if (err != WY_ERR_NONE) { return err; }

    wy_exec_state state = wy_vm_run(ctx, wy_primitive_null());
    if (state == WY_EXEC_DONE) { return WY_ERR_NONE; }
    if (state == WY_EXEC_FAULT) { return WY_ERR_FAULT; }

    /* A native call is pending (WY_EXEC_CONTINUE): drive it to completion
     * through the ordinary fiber trampoline, which will re-enter wy_vm_run
     * as the continuation once it resolves (epic 3+ machinery; unreached
     * by this milestone's own callers). */
    return wy_fiber_exec_f(ctx->current_fiber, ctx);
}

wy_error wy_vm_call_continue(wy_context* ctx, wy_exec_fn continuation, wy_value callee,
    const wy_value* args, wy_uword argc, wy_uword nres)
{
    WY_UNUSED(ctx); WY_UNUSED(continuation); WY_UNUSED(callee); WY_UNUSED(args); WY_UNUSED(argc); WY_UNUSED(nres);
    return WY_ERR_NOSUPPORT;  /* epic 3+: natives calling back into the VM */
}

/**
 * Stub pending the epic 2 interpreter loop rewrite against the adopted
 * opcode.h encoding (E1/M2): always fails rather than decoding with the
 * retired enum.
 */
wy_error wy_vm_exec_bytecode(wy_context* ctx, size_t pos, const wy_u32 buffer[], size_t len)
{
    WY_UNUSED(ctx);
    WY_UNUSED(pos);
    WY_UNUSED(buffer);
    WY_UNUSED(len);
    return WY_ERR_INVAL;
}


/**
 * Enter bytecode from a callable payload
 *
 * Unpacks the (module id, address) pair the callable carries and resolves
 * the module the address belongs to. The interpreter loop itself is not
 * wired up yet, so this returns immediately.
 *
 * @param context Context whose current fiber holds the call frame
 * @param c_data Packed module id and code address
 * @return Execution state for the fiber loop
 */
wy_exec_state wy_vm_exec_b_code(wy_context* context, wy_primitive c_data)
{
    WY_ASSERT(context != WY_NULL);

    wy_uword module_id = wy_exec_fn_b_code_module_id(c_data);
    wy_uword address = wy_exec_fn_b_code_address(c_data);

    wy_module* module = wy_context_get_module(context, module_id);
    WY_UNUSED(module);
    WY_UNUSED(address);

    return WY_EXEC_DONE;
}
