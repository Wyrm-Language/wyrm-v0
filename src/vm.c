#include "vm_internal.h"

#include <stdio.h>
#include <string.h>

#include <wyrm.h>
#include <wyrm/error.h>
#include <wyrm/bound_msg.h>
#include <wyrm/coroutine.h>
#include <wyrm/dict.h>
#include <wyrm/function.h>
#include <wyrm/instance.h>
#include <wyrm/iter.h>
#include <wyrm/list.h>
#include <wyrm/link.h>
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

/* Fault text for a failed native call: names the native and the wy_error
 * code so a bare "native call failed" is diagnosable. */
static void native_fail_msg_f(char* buf, size_t size, const wy_native* native, wy_error err)
{
    if (err == WY_ERR_ARITY) { snprintf(buf, size, "wrong argument count"); return; }
    snprintf(buf, size, "native call failed: %s (error %d)",
             (native != WY_NULL && native->name != WY_NULL) ? native->name : "?", (int) err);
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
 * A `call_va`/`msg_va` keyword dict has plain STR keys (the compiler builds
 * it from ordinary string statics, matching the reference interpreter's
 * `kwargs[a.name] = ...` - `**kwargs` is an ordinary str-keyed dict once it
 * reaches wyrm code, per wyc-format.md §8.5). `pm->name` is an interned
 * `wy_symbol`, not a `wy_string*`, so it can't be looked up via
 * `wy_dict_get`'s STR case without first boxing it - a linear content
 * comparison avoids that allocation on every parameter of every call that
 * uses keyword arguments.
 */
static wy_value* dict_get_by_symbol_as_str_f(wy_dict* kwargs, wy_symbol name)
{
    wy_uword name_len = wy_strlen_f(name);
    for (wy_uword i = 0; i < kwargs->count; i++) {
        const wy_key_hash_value* kv = &kwargs->dense[i];
        if (kv->key.type == WY_TYPE_TAG_STR) {
            wy_string* s = kv->key.data.str;
            if (s->len == name_len && wy_strncmp_f(s->str, name, name_len) == 0) {
                return (wy_value*) &kv->value;
            }
        }
    }
    return WY_NULL;
}

/** Same STR-vs-interned-symbol content comparison as above, for checking
 * whether a leftover kwargs entry (STR key) was already consumed by a
 * plain parameter (tracked as a `wy_symbol` in `consumed[]`). */
static bool binding_kwarg_str_consumed_f(const wy_symbol* consumed, wy_uword n, wy_string* key)
{
    for (wy_uword i = 0; i < n; i++) {
        wy_uword clen = wy_strlen_f(consumed[i]);
        if (clen == key->len && wy_strncmp_f(consumed[i], key->str, clen) == 0) { return true; }
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
static wy_error push_bytecode_call_bind_f(wy_context* ctx, wy_function* fn,
    wy_uword this_count, const wy_value* this_values,
    const wy_value* args, wy_uword argc,
    wy_dict* kwargs, wy_value* ret_dst, wy_uword nres, wy_ret_kind ret_kind,
    char* fault_msg, wy_uword fault_msg_size)
{
    const wy_function_proto* proto = fn->proto;
    const bool has_varargs = (proto->flags & WY_FN_VARARGS) != 0;
    const bool has_kwargs = (proto->flags & WY_FN_KWARGS) != 0;
    const wy_uword plain = (wy_uword) proto->nparams
        - (has_varargs ? 1u : 0u) - (has_kwargs ? 1u : 0u);
    const wy_uword p_count = this_count + (wy_uword) proto->nparams + fn->ncaps;
    const wy_uword total = p_count + proto->nlocals;
    const wy_symbol fname = (proto->name != WY_NULL) ? proto->name : "<init>";

    wy_fiber* fiber = ctx->current_fiber;
    WY_ASSERT(proto->nparams == 0 || proto->params != WY_NULL);
    WY_ASSERT(this_count == 0 || this_values != WY_NULL);

    wy_frame* new_frame = fiber->current_frame + 1;
    if (!WY_MEM_INFO_TOP_NOT_AT_END(wy_frame, new_frame, &fiber->frame_memory)) { return WY_ERR_STACK_OVERFLOW; }
    if (wy_stack_capacity_remaining_f(&fiber->value_stack) < total) { return WY_ERR_STACK_OVERFLOW; }

    if (!has_varargs && !has_kwargs && kwargs == WY_NULL && argc == proto->nparams) {
        /* The common shape: the whole P frame is a straight copy. `this`
         * values (design_c_vm.md §7's "P0..P(t-1)") always precede the
         * declared parameters, which is why a plain call - this_count == 0 -
         * needs no special case here. */
        wy_value* p = fiber->value_stack.top;
        wy_value* l = p + p_count;
        fiber->value_stack.top = l + proto->nlocals;

        for (wy_uword i = 0; i < this_count; i++) { p[i] = this_values[i]; }
        for (wy_uword i = 0; i < argc; i++) { p[this_count + i] = args[i]; }
        for (wy_uword i = 0; i < fn->ncaps; i++) { p[this_count + argc + i] = fn->caps[i]; }
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
    for (wy_uword i = 0; i < this_count; i++) { p[pslot++] = this_values[i]; }

    for (wy_uword i = 0; i < plain; i++) {
        const wy_param* pm = &proto->params[i];
        wy_value bound;
        if (taken < argc) {
            bound = args[taken++];
        } else if (kwargs != WY_NULL) {
            wy_value* hit = dict_get_by_symbol_as_str_f(kwargs, pm->name);
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
                if (kv->key.type == WY_TYPE_TAG_STR
                    && binding_kwarg_str_consumed_f(consumed, nconsumed, kv->key.data.str)) {
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
            if (kv->key.type == WY_TYPE_TAG_STR
                && !binding_kwarg_str_consumed_f(consumed, nconsumed, kv->key.data.str)
                && n_unexpected < WY_VM_CALL_MAX_KWARGS) {
                unexpected[n_unexpected++] = kv->key.data.str->str;
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

/**
 * Construct-on-call for a CLASS callee (design_c_vm.md §7): `new_instance`,
 * find the applicable `init` (`wy_class_find_init_f`), and either push it
 * with `WY_RET_CONSTRUCT` (`aux` set to the instance once the push
 * succeeds) or, when there's no applicable `init`, hand back the instance
 * directly if the call took no arguments.
 *
 * Only called once `try_construct_error_f` has ruled out the `error` class
 * special case (`WY_ERR_UNBOUND`).
 *
 * @param out_pushed set to true iff a frame was pushed (caller must `goto
 *   reload`); false means `*out_value` is the fully-constructed instance
 *   and the caller should write it and fall through like an ordinary call.
 * @return WY_ERR_NONE, WY_ERR_NOMEM, WY_ERR_ARITY (fault_msg filled either
 *   by this function - no applicable `init` but arguments were given - or
 *   by push_bytecode_call_bind_f - init's own parameter binding failed),
 *   or whatever else push_bytecode_call_bind_f returns.
 */
static wy_error construct_on_call_f(wy_context* ctx, wy_class* cls, const wy_value* args, wy_uword argc,
    wy_dict* kwargs, wy_value* ret_dst, wy_uword nres, bool* out_pushed, wy_value* out_value,
    char* fault_msg, wy_uword fault_msg_size)
{
    wy_instance* inst = WY_NULL;
    wy_error err = wy_instance_new_f(ctx, cls, &inst);
    if (err != WY_ERR_NONE) { return err; }
    wy_value inst_v = wy_value_object(WY_TYPE_TAG_INSTANCE, (wy_object*) inst);

    wy_value init = wy_class_find_init_f(cls);
    if (wy_value_is_unset(init)) {
        if (argc > 0 || (kwargs != WY_NULL && kwargs->count > 0)) {
            snprintf(fault_msg, fault_msg_size, "%s(...) takes no arguments (no applicable 'init')",
                (cls->name != WY_NULL) ? cls->name : "<class>");
            return WY_ERR_ARITY;
        }
        *out_pushed = false;
        *out_value = inst_v;
        return WY_ERR_NONE;
    }

    /* init's declared parameters are bound after the instance, which
     * occupies P0 outside the ordinary parameter list (design_c_vm.md §7's
     * "this values in P0..P(t-1)"; confirmed against the reference compiler,
     * compiler_bc/functions.py's `this_count` - a method's `params` never
     * include an implicit `self`). */
    err = push_bytecode_call_bind_f(ctx, (wy_function*) init.data.gc_object, 1, &inst_v,
        args, argc, kwargs, ret_dst, nres, WY_RET_CONSTRUCT, fault_msg, fault_msg_size);
    if (err != WY_ERR_NONE) { return err; }

    ctx->current_fiber->current_frame->aux = inst_v;
    *out_pushed = true;
    return WY_ERR_NONE;
}

/** `trap`'s message by code (wyc-format.md §6.1, interp.py TRAP_CODES). */
/**
 * The positional arguments of a `call_va`/`msg_va` window: a tuple (what the
 * reference compiler joins for `f(a, *rest)`) or a plain list (what both
 * compilers pass for a bare `f(*xs)`). The items are only read.
 */
static bool va_positional_f(wy_value posv, wy_value** items, wy_uword* count)
{
    if (posv.type == WY_TYPE_TAG_TUPLE) {
        wy_tuple* tup = (wy_tuple*) posv.data.gc_object;
        *items = (tup->count > 0) ? (wy_value*) tup->items : WY_NULL;
        *count = tup->count;
        return true;
    }
    if (posv.type == WY_TYPE_TAG_LIST) {
        wy_list* list = (wy_list*) posv.data.gc_object;
        *items = (list->count > 0) ? list->items : WY_NULL;
        *count = list->count;
        return true;
    }
    return false;
}

/**
 * The receiver(s) for a message send/bind (design_c_vm.md §7): `recv`
 * itself for ordinary single dispatch, or a TUPLE's items for multiple
 * dispatch. `*out_receivers` points into `*recv` (single) or the tuple's
 * own storage (multiple) - never allocates.
 */
static void dispatch_receivers_f(const wy_value* recv, const wy_value** out_receivers, wy_uword* out_n)
{
    if (recv->type == WY_TYPE_TAG_TUPLE) {
        wy_tuple* tup = (wy_tuple*) recv->data.gc_object;
        *out_receivers = (tup->count > 0) ? (const wy_value*) tup->items : WY_NULL;
        *out_n = tup->count;
        return;
    }
    *out_receivers = recv;
    *out_n = 1;
}

/**
 * Resolve `msg` against `receivers[0..n)`: the single-INSTANCE fast path
 * (design_c_vm.md §7) when it can apply, else the general ranking.
 * `wy_dispatch_single_instance_f`'s `msg_map` walk only sees overloads a
 * `class` op registered for that exact class hierarchy - not one a
 * standalone `reg_msg` added constrained to one of those classes - so a
 * miss there falls through to the general resolver rather than faulting,
 * to stay correct in that (currently untested, no corpus fixture exercises
 * it) case.
 */
static wy_error dispatch_body_f(wy_message* msg, const wy_value* receivers, wy_uword n, wy_value* out_body,
    char* fault_msg, wy_uword fault_msg_size)
{
    if (n == 1 && receivers[0].type == WY_TYPE_TAG_INSTANCE && !msg->has_wildcard_or_ptype_arity1) {
        wy_instance* inst = (wy_instance*) receivers[0].data.gc_object;
        if (wy_dispatch_single_instance_f(msg, inst, out_body) == WY_ERR_NONE) {
            return WY_ERR_NONE;
        }
    }
    const wy_overload* ov = WY_NULL;
    wy_error err = wy_dispatch_resolve_f(msg, receivers, n, WY_NULL, &ov, fault_msg, fault_msg_size);
    if (err != WY_ERR_NONE) { return err; }
    *out_body = ov->body;
    return WY_ERR_NONE;
}

/**
 * design_c_vm.md §5's builtins message_table fallback (epic 7): when `msg`
 * (the identity resolved in the *calling* module's own table - always
 * distinct per module, per `wy_module_resolve_message_f`) has no overload
 * matching `receivers`, try the identically-named message in
 * `ctx->builtins`'s own message table instead. This is what lets a native
 * container method (`bytes`/`list`/...) registered once, at builtins-init
 * time, be called via `!name(...)` from any module - the per-module `msg`
 * a fresh module gets for a name it never `reg_msg`s itself is otherwise
 * permanently empty. Never creates an entry in either table.
 */
static wy_error dispatch_builtins_fallback_f(wy_context* ctx, wy_message* msg, const wy_value* receivers,
    wy_uword n, wy_value* out_body, char* fault_msg, wy_uword fault_msg_size)
{
    if (ctx->builtins == WY_NULL) { return WY_ERR_NOSUPPORT; }

    wy_message* builtin_msg = WY_NULL;
    wy_error err = wy_module_message_lookup_f(ctx, ctx->builtins, msg->name, &builtin_msg);
    if (err != WY_ERR_NONE || builtin_msg == WY_NULL) { return WY_ERR_NOSUPPORT; }

    return dispatch_body_f(builtin_msg, receivers, n, out_body, fault_msg, fault_msg_size);
}

/**
 * Invoke a NATIVE-bodied message overload (epic 7: `design_c_vm.md` §5's
 * builtins `message_table` is the first thing to ever put a NATIVE value
 * into `wy_overload::body` - every path before this only ever wrote a
 * bytecode FUNCTION there). Unlike `push_bytecode_call_bind_f`, a native
 * leaf function has no separate "this" concept (`wy_native_leaf_fn`'s
 * signature is a flat `args[0..argc)`), so `receivers[0..n)` and
 * `args[0..argc)` - which are not necessarily adjacent in memory, e.g.
 * `WY_OP_MSG_VA`'s args come from a tuple's own item array - are copied
 * into one contiguous scratch buffer, receivers first, before the call.
 *
 * Scoped to single-receiver (`n == 1`) dispatch only: every native message
 * this epic registers is a PTYPE(primitive)-constrained arity-1 overload,
 * and no fixture needs multi-receiver (tuple-recv) dispatch onto a native
 * body. `n > 1` faults explicitly rather than silently misbehaving.
 */
static wy_error dispatch_native_body_f(wy_context* ctx, wy_native* native, const wy_value* receivers, wy_uword n,
    wy_value* args, wy_uword argc, wy_value* out, wy_uword nres, char* fault_msg, wy_uword fault_msg_size)
{
    if (n != 1) {
        snprintf(fault_msg, fault_msg_size, "msg: native message dispatch supports exactly one receiver");
        return WY_ERR_NOSUPPORT;
    }
    if (native->kind != WY_NATIVE_LEAF) {
        snprintf(fault_msg, fault_msg_size, "msg: native exec bodies are not supported as message overloads yet");
        return WY_ERR_NOSUPPORT;
    }

    enum { WY_NATIVE_MSG_MAX_ARGS = WY_DISPATCH_MAX_RECEIVERS + 256 };
    if (n + argc > WY_NATIVE_MSG_MAX_ARGS) {
        snprintf(fault_msg, fault_msg_size, "msg: too many arguments for a native message body");
        return WY_ERR_RANGE;
    }

    wy_value merged[WY_NATIVE_MSG_MAX_ARGS];
    for (wy_uword i = 0; i < n; i++) { merged[i] = receivers[i]; }
    for (wy_uword i = 0; i < argc; i++) { merged[n + i] = args[i]; }

    wy_error err = wy_vm_call_leaf_f(ctx, native, merged, n + argc, out, nres);
    if (err != WY_ERR_NONE) {
        native_fail_msg_f(fault_msg, fault_msg_size, native, err);
        return err;
    }
    return WY_ERR_NONE;
}

/**
 * How many of `receivers[0..n)` a chosen overload's `body` actually wants
 * in `P0..P(t-1)`: its own `ndispatch` (design_c_vm.md §7's "this values"),
 * capped to `n` for safety against a malformed image.
 *
 * This is *not* always `n`: a promoted plain function (llm-bytecode.md §9's
 * message promotion, epic 4/M4 - a wildcard-arity overload whose body is an
 * ordinary `fn name(...)`, compiled with no reserved receiver slots at all)
 * has `ndispatch == 0` even though the *overload* it fills has arity `n` -
 * the receiver(s) exist for ranking purposes only and are discarded, never
 * bound to a P slot, exactly like the reference's wildcard-overload call
 * (`register_overload`'s promoted copy binds no `this` name at all).
 */
static wy_uword dispatch_this_count_f(wy_value body, wy_uword n)
{
    wy_uword t = ((wy_function*) body.data.gc_object)->proto->ndispatch;
    return (t < n) ? t : n;
}

/** Fault text for a bad `wy_module_resolve_message_f`/`reg_msg` result. */
static const char* message_fault_text_f(wy_error err)
{
    switch (err) {
    case WY_ERR_NOSUPPORT: return "qualified message paths are not supported yet";
    case WY_ERR_RANGE:     return "bad message index";
    case WY_ERR_AMBIGUOUS: return "ambiguous overload";
    default:               return "out of memory";
    }
}

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

/**
 * `StopIteration` as an error *value* (design_c_vm.md §3), delivered to
 * `next`/`send` on a coroutine that's already DONE and to a coroutine's own
 * resumer when its body returns without delegation. `ctx->stop_iteration_class`
 * is only NULL in tests that never install the builtins module; the value is
 * still a real error tag either way (`wy_value_is_error` doesn't need the
 * class), just without a class chain to check `is StopIteration` against.
 */
static wy_value stop_iteration_value_f(wy_context* ctx)
{
    wy_string* what = WY_NULL;
    (void) wy_string_strdup(ctx, "StopIteration", &what);
    wy_error_obj* obj = WY_NULL;
    if (wy_error_obj_new(ctx, ctx->stop_iteration_class, what, wy_value_nil(), &obj) != WY_ERR_NONE) {
        return wy_value_word(-1);
    }
    return wy_value_object(WY_TYPE_TAG_ERROR, (wy_object*) obj);
}

/**
 * `call` on a coroutine-flagged FUNCTION (design_c_vm.md §3): a fresh fiber,
 * a wy_coroutine wrapping it, and the body frame pushed on that fiber -
 * built exactly like an ordinary call (push_bytecode_call_bind_f), just
 * targeting the new fiber instead of the current one. Nothing executes;
 * `*out_value` is the COROUTINE value to write at the call site.
 */
static wy_error construct_coroutine_f(wy_context* ctx, wy_function* fn, const wy_value* args, wy_uword argc,
    wy_dict* kwargs, wy_value* out_value, char* fault_msg, wy_uword fault_msg_size)
{
    wy_fiber* co_fiber = wy_fiber_create(ctx, ctx->co_stack_len, ctx->co_frame_count);
    if (co_fiber == WY_NULL) { return WY_ERR_NOMEM; }

    wy_coroutine* co = WY_NULL;
    wy_error err = wy_coroutine_new_f(ctx, co_fiber, &co);
    if (err != WY_ERR_NONE) { return err; }
    co_fiber->coroutine = co;

    wy_fiber* caller_fiber = ctx->current_fiber;
    ctx->current_fiber = co_fiber;
    err = push_bytecode_call_bind_f(ctx, fn, 0, WY_NULL, args, argc, kwargs, WY_NULL, 0, WY_RET_COROUTINE,
        fault_msg, fault_msg_size);
    if (err == WY_ERR_NONE) {
        co_fiber->current_frame->aux = wy_value_object(WY_TYPE_TAG_COROUTINE, (wy_object*) co);
    }
    ctx->current_fiber = caller_fiber;
    if (err != WY_ERR_NONE) { return err; }

    co_fiber->pending = wy_exec_fn_create(wy_vm_run, wy_primitive_null());
    *out_value = wy_value_object(WY_TYPE_TAG_COROUTINE, (wy_object*) co);
    return WY_ERR_NONE;
}

/**
 * The continuation `wy_vm_call_exec_push_f` runs once an exec native called
 * from bytecode (design_c_vm.md §1.3/§3 - currently only `next`/`send`)
 * completes, however long that takes: immediately (no coroutine switch), or
 * after arbitrarily much coroutine execution on other fibers in between.
 * Either way, by the time this runs `ctx->current_fiber` is back to the
 * caller and its NATIVE bridge frame is already popped (fb->current_frame
 * is the bytecode frame that made the call again) - copy the result into
 * that call's destination window and resume the dispatch loop.
 *
 * This is the one place M3 re-enters wy_vm_run as a nested C call rather
 * than looping. It stays O(1): every wy_vm_run invocation this bridges
 * between already fully returned (via WY_EXEC_CONTINUE/WY_EXEC_SWITCH
 * unwinding through wy_fiber_exec_f's pending loop and wy_context_exec's
 * iterative fiber-switch loop) before this continuation runs, regardless of
 * how many coroutines were involved - so this does not grow with wyrm-level
 * call/coroutine nesting the way AGENTS.md's no-C-recursion rule guards
 * against, only with the fixed bridge depth of one exec-native call.
 */
static wy_exec_state exec_native_call_resume_f(wy_context* ctx, wy_primitive c_data)
{
    WY_UNUSED(c_data);
    wy_fiber* fb = ctx->current_fiber;
    wy_value* dst = fb->pending_native_dst;
    wy_uword nres = fb->pending_native_nres;
    wy_uword base_count = fb->pending_native_base_count;
    fb->pending_native_dst = WY_NULL;
    fb->pending_native_nres = 0;

    wy_error err = wy_vm_native_await_complete_f(ctx, base_count, dst, nres);
    if (err != WY_ERR_NONE) {
        fb->fault = fault_value_f(ctx, "native call bridge failed");
        return WY_EXEC_FAULT;
    }
    return wy_vm_run(ctx, wy_primitive_null());
}

/*
 * GC safepoint. Polled only where an unbounded amount of allocation can
 * accumulate: frame entry/re-entry (`reload`, which covers calls and
 * recursion) and backward jumps (loops). Straight-line code between polls
 * allocates a bounded amount. `fr->ip` must name the resume point.
 */
#define WY_VM_SAFEPOINT() \
    do { \
        if (ctx->gc_pressure > ctx->gc_threshold) { \
            fr->ip = ip; \
            wy_context_gc_safepoint(ctx); \
        } \
    } while (0)

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

    /* Frame entry/re-entry (every call, return, resume lands at `reload`). */
    WY_VM_SAFEPOINT();

    for (;;) {
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
            if (mod->fill_layer != WY_NULL && (mod->fill_layer[a0] & (WY_LINK_AMBIGUOUS | WY_LINK_ALIAS))) {
                if (mod->fill_layer[a0] & WY_LINK_AMBIGUOUS) {
                    fr->ip = next_ip;
                    fault_v = g;
                    goto do_fault;
                }
                /* An imported binding: the defining module's own global. */
                g = *mod->aliases[a0];
            }
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
            if (mod->fill_layer != WY_NULL && (mod->fill_layer[a0] & WY_LINK_ALIAS)) {
                /* Only the defining module assigns its names (M2). */
                fr->ip = next_ip;
                wy_symbol name = free_slot_name_f(mod, a0);
                char msg[WY_VM_CALL_FAULT_MSG];
                snprintf(msg, sizeof(msg), "cannot assign to imported name '%s'",
                    name != WY_SYMBOL_INVALID ? name : "<global>");
                fault_v = fault_value_f(ctx, msg);
                goto do_fault;
            }
            G[a0] = *wy_vm_reg8_f(fr, (wy_u8) src);
            if (mod->fill_layer != WY_NULL) { mod->fill_layer[a0] &= WY_LINK_LAYER_MASK; }
            ip = next_ip;
            break;
        }

        case WY_OP_IMPORT: case WY_OP_IMPORT_WIDE: case WY_OP_IMPORT_STAR: {
            fr->ip = next_ip;
            if (a0 >= mod->static_count || mod->statics[a0].type != WY_TYPE_TAG_STR) {
                fault_v = fault_value_f(ctx, "import: path must be a static string");
                goto do_fault;
            }
            wy_string* path = mod->statics[a0].data.str;
            /* A trailing "::" marks a package loaded on the way to one of its
             * children (`import a::b::c` emits "a::", "a::b::", "a::b::c"):
             * design/modules.md M1's pass-through. */
            bool prefix = op != WY_OP_IMPORT_STAR && path->len > 2 &&
                path->str[path->len - 1] == ':' && path->str[path->len - 2] == ':';
            wy_uword path_len = prefix ? path->len - 2 : path->len;
            wy_module* dep = WY_NULL;
            wy_error err = wy_link_import_ex(ctx, path, path_len, prefix, &dep);
            if (err != WY_ERR_NONE) {
                char msg[WY_VM_CALL_FAULT_MSG];
                snprintf(msg, sizeof(msg), "import %s: '%s' -> '%.*s' (error %d)",
                    err == WY_ERR_CYCLE ? "cycle" : "failed", mod->name ? mod->name : "<module>",
                    (int) path_len, path->str, (int) err);
                fault_v = fault_value_f(ctx, msg);
                if (fault_v.type == WY_TYPE_TAG_ERROR) { ((wy_error_obj*) fault_v.data.gc_object)->code = err; }
                goto do_fault;
            }
            wy_uword wildcard_index = 0;
            bool star = op == WY_OP_IMPORT_STAR;
            wy_value* dst = star ? WY_NULL : (op == WY_OP_IMPORT ? wy_vm_reg8_f(fr, f) : wy_vm_reg_f(fr, a1));
            if (star) {
                wy_uword base = a1 & 0x7fffu;
                wy_uword capacity = (a1 & WYRM_REG_P_BIT) ? fr->p_count : fr->proto->nlocals;
                if (base > capacity || f > capacity - base) {
                    fault_v = fault_value_f(ctx, "import_star: invalid except window");
                    goto do_fault;
                }
                err = wy_link_register_wildcard(ctx, mod, dep, wy_vm_reg_f(fr, a1), f, &wildcard_index);
                if (err != WY_ERR_NONE) {
                    fault_v = fault_value_f(ctx, "import_star: invalid except symbols or out of memory");
                    goto do_fault;
                }
            }
            if (dep->state == WY_MODULE_INITIALISING) {
                /* Passed through (only a prefix gets here): bound so its
                 * children are reachable, but none of its own members are
                 * there to fill yet. */
                *dst = wy_value_object(WY_TYPE_TAG_MODULE, (wy_object*) dep);
                ip = next_ip;
                break;
            }
            if (dep->state == WY_MODULE_READY || dep->state == WY_MODULE_BUILTIN) {
                if (star) { err = wy_link_fill_from_wildcard(ctx, mod, &mod->wildcards[wildcard_index]); }
                else {
                    wy_symbol spelling;
                    err = wy_context_intern(ctx, path->str, path_len, &spelling);
                    if (err == WY_ERR_NONE) { err = wy_link_fill_from_import(ctx, mod, spelling, dep); }
                }
                if (err == WY_ERR_NONE) { err = wy_link_adopt_messages(ctx, mod, dep); }
                if (err != WY_ERR_NONE) { fault_v = fault_value_f(ctx, "import: fill failed"); goto do_fault; }
                if (!star) { *dst = wy_value_object(WY_TYPE_TAG_MODULE, (wy_object*) dep); }
                ip = next_ip;
                break;
            }
            dep->init_proto.nlocals = dep->init_nlocals;
            /* Only push_bytecode_call_bind_f reads this temporary callable.
             * The persistent prototype and module are owned by the frame. */
            wy_function init_fn = {0};
            init_fn.module = dep;
            init_fn.proto = &dep->init_proto;
            char fault_msg[WY_VM_CALL_FAULT_MSG];
            err = push_bytecode_call_bind_f(ctx, &init_fn, 0, WY_NULL, WY_NULL, 0,
                WY_NULL, dst, star ? 0 : 1, star ? WY_RET_IMPORT_STAR : WY_RET_IMPORT,
                fault_msg, sizeof(fault_msg));
            if (err != WY_ERR_NONE) {
                dep->state = WY_MODULE_FAILED;
                fault_v = fault_value_f(ctx, push_fault_text_f(err, fault_msg));
                goto do_fault;
            }
            dep->state = WY_MODULE_INITIALISING;
            fb->current_frame->aux = star ? wy_value_uword(wildcard_index) : mod->statics[a0];
            goto reload;
        }

        case WY_OP_GETSCOPE: case WY_OP_SETSCOPE: {
            bool get = op == WY_OP_GETSCOPE;
            wy_uword symbol = get ? a2 : a1;
            wy_value* binding = WY_NULL;
            wy_error err = symbol >= mod->symbol_count ? WY_ERR_RANGE :
                wy_link_scope_member(*wy_vm_reg_f(fr, get ? a1 : a0), mod->symbols[symbol], &binding);
            if (err != WY_ERR_NONE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "scope: namespace has no such member");
                goto do_fault;
            }
            if (get) { *wy_vm_reg_f(fr, a0) = *binding; }
            else { *binding = *wy_vm_reg_f(fr, a2); }
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
            if (take && rel < 0) { WY_VM_SAFEPOINT(); }
            break;
        }
        case WY_OP_JMP: {
            wy_i32 rel = (int16_t) a0;
            ip = next_ip + rel;
            if (rel < 0) { WY_VM_SAFEPOINT(); }
            break;
        }
        case WY_OP_JMP_WIDE: {
            wy_i32 rel = (wy_i32) w1;
            ip = next_ip + rel;
            if (rel < 0) { WY_VM_SAFEPOINT(); }
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

        case WY_OP_GETATTR: {
            /* Property namespace (wyc-format.md §6.3): a2 is a symbol-table
             * index, not a register. Only INSTANCE receivers are supported
             * this milestone; other receivers consult a per-tag property
             * table that starts empty (design_c_vm.md §7), so they fault. */
            wy_value obj = *wy_vm_reg_f(fr, a1);
            if (a2 >= mod->symbol_count) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "getattr: bad symbol index");
                goto do_fault;
            }
            wy_symbol name = mod->symbols[a2];

            if (obj.type != WY_TYPE_TAG_INSTANCE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "getattr: unsupported receiver type");
                goto do_fault;
            }
            wy_instance* inst = (wy_instance*) obj.data.gc_object;
            wy_uword idx;
            wy_class_slot* slot = wy_class_find_slot_f(inst->cls, name, &idx);
            if (slot == WY_NULL) {
                fr->ip = next_ip;
                char msg[WY_VM_CALL_FAULT_MSG];
                snprintf(msg, sizeof(msg), "getattr: no such property '%s'", name);
                fault_v = fault_value_f(ctx, msg);
                goto do_fault;
            }
            bool virtual_slot = !wy_value_is_unset(slot->getter) || !wy_value_is_unset(slot->setter);
            if (!virtual_slot) {
                *wy_vm_reg_f(fr, a0) = inst->slots[idx];
                ip = next_ip;
                break;
            }
            if (wy_value_is_unset(slot->getter)) {
                fr->ip = next_ip;
                char msg[WY_VM_CALL_FAULT_MSG];
                snprintf(msg, sizeof(msg), "getattr: property '%s' has no getter", name);
                fault_v = fault_value_f(ctx, msg);
                goto do_fault;
            }
            fr->ip = next_ip;
            char fault_msg[WY_VM_CALL_FAULT_MSG];
            wy_error err = push_bytecode_call_bind_f(ctx, (wy_function*) slot->getter.data.gc_object,
                1, &obj, WY_NULL, 0, WY_NULL, wy_vm_reg_f(fr, a0), 1, WY_RET_WINDOW,
                fault_msg, sizeof(fault_msg));
            if (err != WY_ERR_NONE) {
                fault_v = fault_value_f(ctx, push_fault_text_f(err, fault_msg));
                goto do_fault;
            }
            goto reload;
        }
        case WY_OP_SETATTR: {
            wy_value obj = *wy_vm_reg_f(fr, a0);
            if (a1 >= mod->symbol_count) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "setattr: bad symbol index");
                goto do_fault;
            }
            wy_symbol name = mod->symbols[a1];
            wy_value src = *wy_vm_reg_f(fr, a2);

            if (obj.type != WY_TYPE_TAG_INSTANCE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "setattr: unsupported receiver type");
                goto do_fault;
            }
            wy_instance* inst = (wy_instance*) obj.data.gc_object;
            wy_uword idx;
            wy_class_slot* slot = wy_class_find_slot_f(inst->cls, name, &idx);
            if (slot == WY_NULL) {
                fr->ip = next_ip;
                char msg[WY_VM_CALL_FAULT_MSG];
                snprintf(msg, sizeof(msg), "setattr: no such property '%s'", name);
                fault_v = fault_value_f(ctx, msg);
                goto do_fault;
            }
            bool virtual_slot = !wy_value_is_unset(slot->getter) || !wy_value_is_unset(slot->setter);
            if (!virtual_slot) {
                inst->slots[idx] = src;
                ip = next_ip;
                break;
            }
            if (wy_value_is_unset(slot->setter)) {
                fr->ip = next_ip;
                char msg[WY_VM_CALL_FAULT_MSG];
                snprintf(msg, sizeof(msg), "setattr: property '%s' has no setter", name);
                fault_v = fault_value_f(ctx, msg);
                goto do_fault;
            }
            fr->ip = next_ip;
            char fault_msg[WY_VM_CALL_FAULT_MSG];
            wy_error err = push_bytecode_call_bind_f(ctx, (wy_function*) slot->setter.data.gc_object,
                1, &obj, &src, 1, WY_NULL, WY_NULL, 0, WY_RET_DISCARD,
                fault_msg, sizeof(fault_msg));
            if (err != WY_ERR_NONE) {
                fault_v = fault_value_f(ctx, push_fault_text_f(err, fault_msg));
                goto do_fault;
            }
            goto reload;
        }
        case WY_OP_GETSLOT: {
            wy_value obj = *wy_vm_reg_f(fr, a1);
            if (obj.type != WY_TYPE_TAG_INSTANCE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "getslot: not an instance");
                goto do_fault;
            }
            wy_instance* inst = (wy_instance*) obj.data.gc_object;
            if (a2 >= inst->cls->slot_count) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "getslot: slot index out of range");
                goto do_fault;
            }
            *wy_vm_reg_f(fr, a0) = inst->slots[a2];
            ip = next_ip;
            break;
        }
        case WY_OP_SETSLOT: {
            wy_value obj = *wy_vm_reg_f(fr, a0);
            if (obj.type != WY_TYPE_TAG_INSTANCE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "setslot: not an instance");
                goto do_fault;
            }
            wy_instance* inst = (wy_instance*) obj.data.gc_object;
            if (a1 >= inst->cls->slot_count) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "setslot: slot index out of range");
                goto do_fault;
            }
            inst->slots[a1] = *wy_vm_reg_f(fr, a2);
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

        case WY_OP_CLASS: {
            /* a1 is a *class* operand: an index into classes[], not a
             * register (wyc-format.md §5.3). Realisation is idempotent and
             * cached in mod->classes[a1] (wy_class_realise_f). */
            if (a1 >= mod->class_count) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "class: bad class index");
                goto do_fault;
            }
            wy_class* cls = WY_NULL;
            wy_error c_err = wy_class_realise_f(ctx, mod, a1, &cls);
            if (c_err != WY_ERR_NONE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx,
                    c_err == WY_ERR_BAD_TYPE ? "class: superclass slot is not a class" : "class: realisation failed");
                goto do_fault;
            }
            *wy_vm_reg_f(fr, a0) = wy_value_object(WY_TYPE_TAG_CLASS, (wy_object*) cls);
            ip = next_ip;
            break;
        }

        case WY_OP_NEW_INSTANCE: {
            wy_value clsv = *wy_vm_reg_f(fr, a1);
            if (clsv.type != WY_TYPE_TAG_CLASS) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "new_instance: not a class");
                goto do_fault;
            }
            wy_instance* inst = WY_NULL;
            wy_error c_err = wy_instance_new_f(ctx, (wy_class*) clsv.data.gc_object, &inst);
            if (c_err != WY_ERR_NONE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "out of memory");
                goto do_fault;
            }
            *wy_vm_reg_f(fr, a0) = wy_value_object(WY_TYPE_TAG_INSTANCE, (wy_object*) inst);
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
                bool pushed = false;
                wy_value instance_v = wy_value_nil();
                char fault_msg[WY_VM_CALL_FAULT_MSG];
                err = construct_on_call_f(ctx, (wy_class*) callee.data.gc_object, &L[base + 1], argc,
                    WY_NULL, &L[base], nres, &pushed, &instance_v, fault_msg, sizeof(fault_msg));
                if (err != WY_ERR_NONE) {
                    fault_v = fault_value_f(ctx, push_fault_text_f(err, fault_msg));
                    goto do_fault;
                }
                if (!pushed) {
                    *wy_vm_reg_f(fr, base) = instance_v;
                    for (wy_uword i = 1; i < nres; i++) { *wy_vm_reg_f(fr, base + i) = wy_value_nil(); }
                    ip = next_ip;
                    break;
                }
                goto reload;
            }

            if (callee.type == WY_TYPE_TAG_FUNCTION) {
                wy_function* fn = (wy_function*) callee.data.gc_object;
                if (fn->proto->flags & WY_FN_COROUTINE) {
                    fr->ip = next_ip;
                    wy_value co_v;
                    char fault_msg[WY_VM_CALL_FAULT_MSG];
                    wy_error err = construct_coroutine_f(ctx, fn, &L[base + 1], argc, WY_NULL, &co_v,
                        fault_msg, sizeof(fault_msg));
                    if (err != WY_ERR_NONE) {
                        fault_v = fault_value_f(ctx, push_fault_text_f(err, fault_msg));
                        goto do_fault;
                    }
                    *wy_vm_reg_f(fr, base) = co_v;
                    for (wy_uword i = 1; i < nres; i++) { *wy_vm_reg_f(fr, base + i) = wy_value_nil(); }
                    ip = next_ip;
                    break;
                }
                fr->ip = next_ip;
                char fault_msg[WY_VM_CALL_FAULT_MSG];
                wy_error err = push_bytecode_call_bind_f(ctx, fn,
                    0, WY_NULL, &L[base + 1], argc, WY_NULL, &L[base], nres, WY_RET_WINDOW,
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
                if (native->kind == WY_NATIVE_EXEC) {
                    /* Bridge to the reservation trampoline (design_c_vm.md
                     * §1.3/§3): `next`/`send` are the only exec natives this
                     * milestone registers, and may switch fibers to run a
                     * coroutine before eventually resuming here. */
                    if (argc < native->min_argc || argc > native->max_argc) {
                        fr->ip = next_ip;
                        fault_v = fault_value_f(ctx, "wrong argument count");
                        goto do_fault;
                    }
                    fr->ip = next_ip;
                    fb->pending_native_dst = &L[base];
                    fb->pending_native_nres = (wy_u16) nres;
                    fb->pending_native_base_count = wy_fiber_value_count_f(fb);
                    wy_error err = wy_vm_call_exec_push_f(ctx,
                        wy_exec_fn_create(exec_native_call_resume_f, wy_primitive_null()),
                        native, &L[base + 1], argc, nres);
                    if (err != WY_ERR_NONE) {
                        char fail_msg[128];
                        native_fail_msg_f(fail_msg, sizeof fail_msg, native, err);
                        fault_v = fault_value_f(ctx, fail_msg);
                        goto do_fault;
                    }
                    return WY_EXEC_CONTINUE;
                }
                wy_error err = wy_vm_call_leaf_f(ctx, native, &L[base + 1], argc, &L[base], nres);
                if (err != WY_ERR_NONE) {
                    fr->ip = next_ip;
                    char fail_msg[128];
                    native_fail_msg_f(fail_msg, sizeof fail_msg, native, err);
                    fault_v = fault_value_f(ctx, fail_msg);
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

            if (callee.type == WY_TYPE_TAG_BOUND_MSG) {
                /* `recv ! name` stored and called later (wyc-format.md
                 * §6.3): the receiver(s) and overload were already
                 * resolved at `getmsg` time. */
                wy_bound_msg* bm = (wy_bound_msg*) callee.data.gc_object;
                const wy_value* receivers; wy_uword n;
                dispatch_receivers_f(&bm->receiver, &receivers, &n);
                wy_uword tcount = dispatch_this_count_f(bm->body, n);
                fr->ip = next_ip;
                char fault_msg[WY_VM_CALL_FAULT_MSG];
                wy_error err = push_bytecode_call_bind_f(ctx, (wy_function*) bm->body.data.gc_object,
                    tcount, receivers, &L[base + 1], argc, WY_NULL, &L[base], nres, WY_RET_WINDOW,
                    fault_msg, sizeof(fault_msg));
                if (err != WY_ERR_NONE) {
                    fault_v = fault_value_f(ctx, push_fault_text_f(err, fault_msg));
                    goto do_fault;
                }
                fb->current_frame->dispatch_msg = bm->msg;
                fb->current_frame->dispatch_body = bm->body;
                fb->current_frame->flags |= WY_FRAME_FLAG_METHOD;
                goto reload;
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
                wy_value* args = WY_NULL;
                wy_uword argc = 0;
                if (!va_positional_f(posv, &args, &argc)) {
                    fr->ip = next_ip;
                    fault_v = fault_value_f(ctx, "call_va: expected a positional tuple");
                    goto do_fault;
                }
                wy_value result;
                wy_error err = try_construct_error_f(ctx, callee, args, argc, &result);
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

                if (kwv.type != WY_TYPE_TAG_TABLE && kwv.type != WY_TYPE_TAG_NIL) {
                    fr->ip = next_ip;
                    fault_v = fault_value_f(ctx, "call_va: expected a keyword dict");
                    goto do_fault;
                }
                wy_dict* kwargs = (kwv.type == WY_TYPE_TAG_TABLE) ? (wy_dict*) kwv.data.gc_object : WY_NULL;

                fr->ip = next_ip;
                bool pushed = false;
                wy_value instance_v = wy_value_nil();
                char fault_msg[WY_VM_CALL_FAULT_MSG];
                err = construct_on_call_f(ctx, (wy_class*) callee.data.gc_object, args, argc,
                    (kwargs != WY_NULL && kwargs->count > 0) ? kwargs : WY_NULL, &L[base], nres,
                    &pushed, &instance_v, fault_msg, sizeof(fault_msg));
                if (err != WY_ERR_NONE) {
                    fault_v = fault_value_f(ctx, push_fault_text_f(err, fault_msg));
                    goto do_fault;
                }
                if (!pushed) {
                    *wy_vm_reg_f(fr, base) = instance_v;
                    for (wy_uword i = 1; i < nres; i++) { *wy_vm_reg_f(fr, base + i) = wy_value_nil(); }
                    ip = next_ip;
                    break;
                }
                goto reload;
            }

            if (callee.type == WY_TYPE_TAG_BOUND_MSG) {
                /* `bound(*args, **kw)` on a stored `recv ! name`: same push
                 * as the plain CALL case, arguments taken from the joined
                 * tuple/dict (bind_message's answer, epic 10a). */
                wy_value* bargs = WY_NULL;
                wy_uword bargc = 0;
                if (!va_positional_f(posv, &bargs, &bargc)) {
                    fr->ip = next_ip;
                    fault_v = fault_value_f(ctx, "call_va: expected a positional tuple");
                    goto do_fault;
                }
                if (kwv.type != WY_TYPE_TAG_TABLE && kwv.type != WY_TYPE_TAG_NIL) {
                    fr->ip = next_ip;
                    fault_v = fault_value_f(ctx, "call_va: expected a keyword dict");
                    goto do_fault;
                }
                wy_bound_msg* bm = (wy_bound_msg*) callee.data.gc_object;
                wy_dict* bkw = (kwv.type == WY_TYPE_TAG_TABLE) ? (wy_dict*) kwv.data.gc_object : WY_NULL;
                const wy_value* receivers; wy_uword n;
                dispatch_receivers_f(&bm->receiver, &receivers, &n);
                wy_uword tcount = dispatch_this_count_f(bm->body, n);
                fr->ip = next_ip;
                char bfault_msg[WY_VM_CALL_FAULT_MSG];
                wy_error berr = push_bytecode_call_bind_f(ctx, (wy_function*) bm->body.data.gc_object,
                    tcount, receivers, bargs, bargc,
                    (bkw != WY_NULL && bkw->count > 0) ? bkw : WY_NULL,
                    &L[base], nres, WY_RET_WINDOW, bfault_msg, sizeof(bfault_msg));
                if (berr != WY_ERR_NONE) {
                    fault_v = fault_value_f(ctx, push_fault_text_f(berr, bfault_msg));
                    goto do_fault;
                }
                fb->current_frame->dispatch_msg = bm->msg;
                fb->current_frame->dispatch_body = bm->body;
                fb->current_frame->flags |= WY_FRAME_FLAG_METHOD;
                goto reload;
            }

            if (callee.type != WY_TYPE_TAG_FUNCTION && callee.type != WY_TYPE_TAG_NATIVE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "value is not callable");
                goto do_fault;
            }
            wy_value* args = WY_NULL;
            wy_uword argc = 0;
            if (!va_positional_f(posv, &args, &argc)) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "call_va: expected a positional tuple");
                goto do_fault;
            }
            if (kwv.type != WY_TYPE_TAG_TABLE && kwv.type != WY_TYPE_TAG_NIL) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "call_va: expected a keyword dict");
                goto do_fault;
            }
            wy_dict* kwargs = (kwv.type == WY_TYPE_TAG_TABLE) ? (wy_dict*) kwv.data.gc_object : WY_NULL;

            if (callee.type == WY_TYPE_TAG_NATIVE) {
                wy_native* native = (wy_native*) callee.data.gc_object;
                if (native->kind != WY_NATIVE_LEAF
                    || (kwargs != WY_NULL && kwargs->count > 0)) {
                    fr->ip = next_ip;
                    fault_v = fault_value_f(ctx, "call_va: native keyword calls are not supported");
                    goto do_fault;
                }
                wy_error err = wy_vm_call_leaf_f(ctx, native, args, argc, &L[base], nres);
                if (err != WY_ERR_NONE) {
                    fr->ip = next_ip;
                    char fail_msg[128];
                    native_fail_msg_f(fail_msg, sizeof fail_msg, native, err);
                    fault_v = fault_value_f(ctx, fail_msg);
                    goto do_fault;
                }
                ip = next_ip;
                break;
            }

            fr->ip = next_ip;
            char fault_msg[WY_VM_CALL_FAULT_MSG];
            wy_error err = push_bytecode_call_bind_f(ctx, (wy_function*) callee.data.gc_object,
                0, WY_NULL, args, argc,
                (kwargs != WY_NULL && kwargs->count > 0) ? kwargs : WY_NULL,
                &L[base], nres, WY_RET_WINDOW, fault_msg, sizeof(fault_msg));
            if (err != WY_ERR_NONE) {
                fault_v = fault_value_f(ctx,
                    push_fault_text_f(err, fault_msg));
                goto do_fault;
            }
            goto reload;
        }

        case WY_OP_MSG: {
            wy_uword base = a0, argc = f, message_idx = a1, nres = a2;
            wy_value recv = *wy_vm_reg_f(fr, base);

            wy_message* msg = WY_NULL;
            wy_error m_err = wy_module_resolve_message_f(ctx, mod, message_idx, &msg);
            if (m_err != WY_ERR_NONE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, message_fault_text_f(m_err));
                goto do_fault;
            }

            const wy_value* receivers; wy_uword n;
            dispatch_receivers_f(&recv, &receivers, &n);
            if (n == 0 || n > WY_DISPATCH_MAX_RECEIVERS) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "msg: bad receiver count");
                goto do_fault;
            }

            wy_value body;
            char fault_msg[WY_VM_CALL_FAULT_MSG];
            wy_error d_err = dispatch_body_f(msg, receivers, n, &body, fault_msg, sizeof(fault_msg));
            if (d_err != WY_ERR_NONE) {
                d_err = dispatch_builtins_fallback_f(ctx, msg, receivers, n, &body, fault_msg, sizeof(fault_msg));
            }
            if (d_err != WY_ERR_NONE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, fault_msg);
                goto do_fault;
            }

            if (body.type == WY_TYPE_TAG_NATIVE) {
                fr->ip = next_ip;
                char fault_msg2[WY_VM_CALL_FAULT_MSG];
                wy_error err = dispatch_native_body_f(ctx, (wy_native*) body.data.gc_object,
                    receivers, n, &L[base + 1], argc, &L[base], nres, fault_msg2, sizeof(fault_msg2));
                if (err != WY_ERR_NONE) {
                    fault_v = fault_value_f(ctx, fault_msg2);
                    goto do_fault;
                }
                ip = next_ip;
                break;
            }

            fr->ip = next_ip;
            char fault_msg2[WY_VM_CALL_FAULT_MSG];
            wy_error err = push_bytecode_call_bind_f(ctx, (wy_function*) body.data.gc_object,
                dispatch_this_count_f(body, n), receivers, &L[base + 1], argc, WY_NULL, &L[base], nres,
                WY_RET_WINDOW, fault_msg2, sizeof(fault_msg2));
            if (err != WY_ERR_NONE) {
                fault_v = fault_value_f(ctx, push_fault_text_f(err, fault_msg2));
                goto do_fault;
            }
            fb->current_frame->dispatch_msg = msg;
            fb->current_frame->dispatch_body = body;
            fb->current_frame->flags |= WY_FRAME_FLAG_METHOD;
            goto reload;
        }

        case WY_OP_MSG_VA: {
            wy_uword base = a0, message_idx = a1, nres = a2;
            wy_value recv = L[base];
            wy_value posv = L[base + 1];
            wy_value kwv = L[base + 2];

            wy_value* args = WY_NULL;
            wy_uword argc = 0;
            if (!va_positional_f(posv, &args, &argc)) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "msg_va: expected a positional tuple");
                goto do_fault;
            }
            if (kwv.type != WY_TYPE_TAG_TABLE && kwv.type != WY_TYPE_TAG_NIL) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "msg_va: expected a keyword dict");
                goto do_fault;
            }
            wy_dict* kwargs = (kwv.type == WY_TYPE_TAG_TABLE) ? (wy_dict*) kwv.data.gc_object : WY_NULL;

            wy_message* msg = WY_NULL;
            wy_error m_err = wy_module_resolve_message_f(ctx, mod, message_idx, &msg);
            if (m_err != WY_ERR_NONE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, message_fault_text_f(m_err));
                goto do_fault;
            }

            const wy_value* receivers; wy_uword n;
            dispatch_receivers_f(&recv, &receivers, &n);
            if (n == 0 || n > WY_DISPATCH_MAX_RECEIVERS) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "msg_va: bad receiver count");
                goto do_fault;
            }

            wy_value body;
            char fault_msg[WY_VM_CALL_FAULT_MSG];
            wy_error d_err = dispatch_body_f(msg, receivers, n, &body, fault_msg, sizeof(fault_msg));
            if (d_err != WY_ERR_NONE) {
                d_err = dispatch_builtins_fallback_f(ctx, msg, receivers, n, &body, fault_msg, sizeof(fault_msg));
            }
            if (d_err != WY_ERR_NONE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, fault_msg);
                goto do_fault;
            }

            if (body.type == WY_TYPE_TAG_NATIVE) {
                if (kwargs != WY_NULL && kwargs->count > 0) {
                    fr->ip = next_ip;
                    fault_v = fault_value_f(ctx, "msg_va: native keyword calls are not supported");
                    goto do_fault;
                }
                fr->ip = next_ip;
                char fault_msg2[WY_VM_CALL_FAULT_MSG];
                wy_error err = dispatch_native_body_f(ctx, (wy_native*) body.data.gc_object,
                    receivers, n, args, argc, &L[base], nres, fault_msg2, sizeof(fault_msg2));
                if (err != WY_ERR_NONE) {
                    fault_v = fault_value_f(ctx, fault_msg2);
                    goto do_fault;
                }
                ip = next_ip;
                break;
            }

            fr->ip = next_ip;
            char fault_msg2[WY_VM_CALL_FAULT_MSG];
            wy_error err = push_bytecode_call_bind_f(ctx, (wy_function*) body.data.gc_object,
                dispatch_this_count_f(body, n), receivers, args, argc,
                (kwargs != WY_NULL && kwargs->count > 0) ? kwargs : WY_NULL,
                &L[base], nres, WY_RET_WINDOW, fault_msg2, sizeof(fault_msg2));
            if (err != WY_ERR_NONE) {
                fault_v = fault_value_f(ctx, push_fault_text_f(err, fault_msg2));
                goto do_fault;
            }
            fb->current_frame->dispatch_msg = msg;
            fb->current_frame->dispatch_body = body;
            fb->current_frame->flags |= WY_FRAME_FLAG_METHOD;
            goto reload;
        }

        case WY_OP_GETMSG: {
            wy_value recv = *wy_vm_reg_f(fr, a1);

            wy_message* msg = WY_NULL;
            wy_error m_err = wy_module_resolve_message_f(ctx, mod, a2, &msg);
            if (m_err != WY_ERR_NONE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, message_fault_text_f(m_err));
                goto do_fault;
            }

            const wy_value* receivers; wy_uword n;
            dispatch_receivers_f(&recv, &receivers, &n);
            if (n == 0 || n > WY_DISPATCH_MAX_RECEIVERS) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "getmsg: bad receiver count");
                goto do_fault;
            }

            /* getmsg always resolves through the general ranking (interp.py
             * OP_GETMSG calls resolve_overload directly), never the
             * single-INSTANCE fast path - a bound message is rare enough
             * that there is no hot path to protect here. */
            const wy_overload* ov = WY_NULL;
            char fault_msg[WY_VM_CALL_FAULT_MSG];
            wy_error d_err = wy_dispatch_resolve_f(msg, receivers, n, WY_NULL, &ov, fault_msg, sizeof(fault_msg));
            if (d_err != WY_ERR_NONE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, fault_msg);
                goto do_fault;
            }

            wy_bound_msg* bm = WY_NULL;
            if (wy_bound_msg_new_f(ctx, recv, msg, ov->body, &bm) != WY_ERR_NONE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "out of memory");
                goto do_fault;
            }
            *wy_vm_reg_f(fr, a0) = wy_value_object(WY_TYPE_TAG_BOUND_MSG, (wy_object*) bm);
            ip = next_ip;
            break;
        }

        case WY_OP_SUPER: {
            wy_uword base = a0, argc = f, nres = a1;
            wy_uword t = (fr->proto != WY_NULL) ? fr->proto->ndispatch : 0;
            if (fr->dispatch_msg == WY_NULL || t == 0) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "super: not inside a message dispatch");
                goto do_fault;
            }

            wy_u16 exclude[WY_DISPATCH_MAX_RECEIVERS];
            if (wy_dispatch_body_distance_f(fr->dispatch_msg, fr->dispatch_body, fr->p, t, exclude) != WY_ERR_NONE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "super: current overload not found");
                goto do_fault;
            }

            const wy_overload* ov = WY_NULL;
            char fault_msg[WY_VM_CALL_FAULT_MSG];
            wy_error d_err = wy_dispatch_resolve_f(fr->dispatch_msg, fr->p, t, exclude, &ov, fault_msg, sizeof(fault_msg));
            if (d_err != WY_ERR_NONE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, fault_msg);
                goto do_fault;
            }

            wy_message* dispatch_msg = fr->dispatch_msg;
            wy_uword push_t = dispatch_this_count_f(ov->body, t);
            fr->ip = next_ip;
            char fault_msg2[WY_VM_CALL_FAULT_MSG];
            wy_error err = push_bytecode_call_bind_f(ctx, (wy_function*) ov->body.data.gc_object,
                push_t, fr->p, &L[base], argc, WY_NULL, &L[base], nres, WY_RET_WINDOW,
                fault_msg2, sizeof(fault_msg2));
            if (err != WY_ERR_NONE) {
                fault_v = fault_value_f(ctx, push_fault_text_f(err, fault_msg2));
                goto do_fault;
            }
            fb->current_frame->dispatch_msg = dispatch_msg;
            fb->current_frame->dispatch_body = ov->body;
            fb->current_frame->flags |= WY_FRAME_FLAG_METHOD;
            goto reload;
        }

        case WY_OP_REG_MSG: {
            /* a0 is a *message* operand: an index into messages[], not a
             * register (wyc-format.md §5.3), resolved/bound on first use. */
            wy_value closure = *wy_vm_reg_f(fr, a1);
            if (closure.type != WY_TYPE_TAG_FUNCTION) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "reg_msg: not a function");
                goto do_fault;
            }
            wy_value types_v = *wy_vm_reg_f(fr, a2);
            if (types_v.type != WY_TYPE_TAG_TUPLE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "reg_msg: expected a types tuple");
                goto do_fault;
            }
            wy_tuple* types_tup = (wy_tuple*) types_v.data.gc_object;
            if (types_tup->count > WY_OVERLOAD_MAX_ARITY) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "reg_msg: too many parameters");
                goto do_fault;
            }
            wy_message* msg = WY_NULL;
            wy_error m_err = wy_module_resolve_message_f(ctx, mod, a0, &msg);
            if (m_err != WY_ERR_NONE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, (m_err == WY_ERR_NOSUPPORT)
                    ? "reg_msg: qualified message paths are not supported yet"
                    : "reg_msg: bad message index");
                goto do_fault;
            }
            m_err = wy_message_add_overload_f(ctx, msg, (wy_u16) types_tup->count,
                (types_tup->count > 0) ? (wy_value*) types_tup->items : WY_NULL, closure);
            if (m_err != WY_ERR_NONE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "out of memory");
                goto do_fault;
            }
            ip = next_ip;
            break;
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

        case WY_OP_YIELD: {
            /* a0 = base, f = count (wyc-format.md §6). Suspend this
             * coroutine, handing count values from L[base..) to whoever is
             * waiting - always co->resumer directly, even mid-delegation
             * (design_c_vm.md §3: yield_from repoints a delegate's own
             * `resumer` at the top-level caller so this needs no chain
             * walk). On resume, next/send writes the sent value into
             * L[base] itself before switching back here. */
            wy_coroutine* co = fb->coroutine;
            if (co == WY_NULL) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "yield: not running inside a coroutine");
                goto do_fault;
            }
            wy_uword base = a0, count = f;
            wy_value v;
            if (count == 0) {
                v = wy_value_nil();
            } else if (count == 1) {
                v = *wy_vm_reg_f(fr, base);
            } else {
                wy_tuple* tup = WY_NULL;
                wy_error err = wy_tuple_new(ctx, &L[base], count, &tup);
                if (err != WY_ERR_NONE) {
                    fr->ip = next_ip;
                    fault_v = fault_value_f(ctx, "yield: tuple construction failed");
                    goto do_fault;
                }
                v = wy_value_object(WY_TYPE_TAG_TUPLE, (wy_object*) tup);
            }
            fr->ip = next_ip;
            co->yield_base = (wy_u16) base;
            co->state = WY_CO_SUSPENDED;
            wy_fiber_set_result_f(co->resumer, 0, v);
            ctx->current_fiber = co->resumer;
            return WY_EXEC_SWITCH;
        }

        case WY_OP_YIELD_FROM: {
            /* a0 = dst, a1 = sub coroutine register (wyc-format.md §6). */
            wy_coroutine* co = fb->coroutine;
            if (co == WY_NULL) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "yield_from: not running inside a coroutine");
                goto do_fault;
            }
            wy_value subv = *wy_vm_reg_f(fr, a1);
            if (subv.type != WY_TYPE_TAG_COROUTINE) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "yield_from: not a coroutine");
                goto do_fault;
            }
            wy_coroutine* sub = (wy_coroutine*) subv.data.gc_object;
            if (sub->state == WY_CO_DONE || sub->state == WY_CO_RUNNING) {
                fr->ip = next_ip;
                fault_v = fault_value_f(ctx, "yield_from: sub-coroutine is not resumable");
                goto do_fault;
            }
            fr->ip = next_ip;
            sub->outer = co;
            sub->delegate_dst = wy_vm_reg_f(fr, a0);
            sub->resumer = co->resumer;
            co->delegate = sub;
            sub->state = WY_CO_RUNNING;
            sub->fiber->pending = wy_exec_fn_create(wy_vm_run, wy_primitive_null());
            ctx->current_fiber = sub->fiber;
            return WY_EXEC_SWITCH;
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
            0, WY_NULL, WY_NULL, 0, WY_NULL, WY_NULL, 0, WY_RET_DISCARD, fault_msg, sizeof(fault_msg));
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
    case WY_RET_IMPORT: case WY_RET_IMPORT_STAR: {
        wy_module* importer = (fr - 1)->module;
        wy_error err;
        if (fr->ret_kind == WY_RET_IMPORT_STAR) {
            err = wy_link_fill_from_wildcard(ctx, importer, &importer->wildcards[fr->aux.data.uword]);
        } else {
            wy_symbol path;
            const wy_string* spelling = fr->aux.data.str;
            wy_uword spelling_len = spelling->len;
            if (spelling_len > 2 && spelling->str[spelling_len - 1] == ':' && spelling->str[spelling_len - 2] == ':') {
                spelling_len -= 2;  /* a prefix load (see WY_OP_IMPORT) */
            }
            err = wy_context_intern(ctx, spelling->str, spelling_len, &path);
            if (err == WY_ERR_NONE) { err = wy_link_fill_from_import(ctx, importer, path, mod); }
        }
        if (err == WY_ERR_NONE) { err = wy_link_adopt_messages(ctx, importer, mod); }
        if (err != WY_ERR_NONE) { fault_v = fault_value_f(ctx, "import: fill failed"); goto do_fault; }
        mod->state = WY_MODULE_READY;
        if (fr->ret_kind == WY_RET_IMPORT) { *fr->ret_dst = wy_value_object(WY_TYPE_TAG_MODULE, (wy_object*) mod); }
        break;
    }
    case WY_RET_CONSTRUCT: {
        /* design_c_vm.md §7: an error result[0] replaces the instance,
         * anything else (including no results at all) keeps it. */
        wy_value out = wy_value_is_error(result0) ? result0 : fr->aux;
        wy_vm_backfill_f(fr->ret_dst, fr->ret_nres, &out, 1);
        break;
    }
    case WY_RET_COROUTINE: {
        /* design_c_vm.md §3: the coroutine's body finished on its own
         * (not via a fault - do_unwind handles that path). Delegation
         * hands the value to the outer coroutine's yield_from and resumes
         * it directly; otherwise the resumer gets StopIteration, matching
         * "raise StopIteration" ending an exhausted generator. Either way
         * this coroutine's own fiber has nothing left to run, so control
         * always leaves via a switch, never a plain reload. */
        wy_coroutine* co = (wy_coroutine*) fr->aux.data.gc_object;
        co->result = result0;
        co->state = WY_CO_DONE;
        fb->value_stack.top = fr->p;
        fb->current_frame = fr - 1;
        if (co->outer != WY_NULL) {
            wy_coroutine* outer = co->outer;
            *outer->delegate_dst = result0;
            outer->delegate = WY_NULL;
            co->outer = WY_NULL;
            ctx->current_fiber = outer->fiber;
            return WY_EXEC_SWITCH;
        }
        wy_value stop_it = stop_iteration_value_f(ctx);
        wy_fiber_set_result_f(co->resumer, 0, stop_it);
        ctx->current_fiber = co->resumer;
        return WY_EXEC_SWITCH;
    }
    default:
        /* RESERVED is bridged through vm_call.c, not produced here. */
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
            0, WY_NULL, WY_NULL, 0, WY_NULL, WY_NULL, 0, WY_RET_DISCARD, fault_msg, sizeof(fault_msg));
        if (err == WY_ERR_NONE) { goto reload; }
        /* No room (the fault may itself be a stack overflow): this defer
         * is lost, the original fault stands, and outer frames - which
         * have room again once this one pops - still drain theirs. */
    }
    if (fr->ret_kind == WY_RET_IMPORT || fr->ret_kind == WY_RET_IMPORT_STAR) {
        mod->state = WY_MODULE_FAILED;
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
    wy_error err = push_bytecode_call_bind_f(ctx, (wy_function*) callee.data.gc_object, 0, WY_NULL, args, argc, WY_NULL,
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

    /* A native call is pending (WY_EXEC_CONTINUE): drive it to completion.
     * Epic 5/M3 is the first milestone where this can be more than one
     * fiber-turn - `next`/`send` may switch to a coroutine's own fiber and
     * back (possibly repeatedly, through further next/send calls the
     * resumed bytecode makes) before the original call actually finishes -
     * so this needs wy_context_exec's "keep following current_fiber" loop,
     * not a single wy_fiber_exec_f turn on whichever fiber happens to be
     * current right now. */
    return wy_context_exec(ctx);
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
