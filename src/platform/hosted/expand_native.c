#include <wyrm/platform/hosted/expand_native.h>

#include <wyrm/builtins.h>
#include <wyrm/bytes.h>
#include <wyrm/error.h>
#include <wyrm/fiber.h>
#include <wyrm/link.h>
#include <wyrm/list.h>
#include <wyrm/machine.h>
#include <wyrm/module.h>
#include <wyrm/native.h>
#include <wyrm/pair.h>
#include <wyrm/platform/hosted/cmachine.h>
#include <wyrm/slot.h>
#include <wyrm/string.h>
#include <wyrm/sys/string.h>
#include <wyrm/tuple.h>
#include <wyrm/value.h>
#include <wyrm/vm.h>

#include <stdio.h>

/* Child fiber sizing: the expander walks a whole module tree recursively in
 * wyrm code, so it gets a much deeper fiber than the CLI's default. */
enum { WY_EXPAND_STACK_LEN = 1u << 16, WY_EXPAND_FRAME_COUNT = 4096u,
    WY_EXPAND_MAX_NODES = 1u << 24, WY_EXPAND_MSG = 256u };

/* -------------------------------------------------------------------------
 * Iterative tree copy between two contexts (no recursion: an explicit stack
 * of (source value, destination slot) tasks). Nothing here can trigger a GC:
 * collection only happens at VM safepoints.
 * ------------------------------------------------------------------------- */

typedef struct copy_task_
{
    wy_value src;
    wy_value* dst;
} copy_task_;

typedef struct copy_stack_
{
    wy_allocator* allocator;
    copy_task_* items;
    wy_uword count;
    wy_uword capacity;
} copy_stack_;

static wy_error copy_push_(copy_stack_* stack, wy_value src, wy_value* dst)
{
    if (stack->count == stack->capacity) {
        wy_uword capacity = stack->capacity == 0 ? 64 : stack->capacity * 2;
        copy_task_* items = wy_allocator_alloc(stack->allocator, sizeof(copy_task_) * capacity);
        if (items == WY_NULL) { return WY_ERR_NOMEM; }
        if (stack->count > 0) { wy_memcpy(items, stack->items, sizeof(copy_task_) * stack->count); }
        if (stack->items != WY_NULL) { wy_allocator_free(stack->allocator, stack->items); }
        stack->items = items;
        stack->capacity = capacity;
    }
    stack->items[stack->count].src = src;
    stack->items[stack->count].dst = dst;
    stack->count++;
    return WY_ERR_NONE;
}

static const char* type_name_(wy_value v)
{
    switch (v.type) {
    case WY_TYPE_TAG_TABLE: return "dict";
    case WY_TYPE_TAG_BYTES: return "bytes";
    case WY_TYPE_TAG_FUNCTION: return "function";
    case WY_TYPE_TAG_NATIVE: return "native function";
    case WY_TYPE_TAG_INSTANCE: return "object";
    case WY_TYPE_TAG_CLASS: return "class";
    case WY_TYPE_TAG_MODULE: return "module";
    case WY_TYPE_TAG_COROUTINE: return "coroutine";
    case WY_TYPE_TAG_BOUND_MSG: return "bound message";
    case WY_TYPE_TAG_ERROR: return "error";
    default: { static char buf[32]; snprintf(buf, sizeof buf, "value of type tag %d", (int) v.type); return buf; }
    }
}

/**
 * Copy the tree `src` (living in `from`) into `to`, storing the result in
 * `*out`. Allowed: nil, bool, word, uword, float, symbol, str, pair, list,
 * tuple. On WY_ERR_INVAL `why` says which value was refused (or that the
 * tree is too large / cyclic).
 */
/**
 * The import hook an expansion VM gets: the parent's (so the compiled-in
 * library modules and -I roots resolve), except `std::io`, which is refused
 * (epic 10a D10: an expansion VM has no host I/O). The embedded std::io is an
 * ordinary module, so without this an expansion scope could import it - it
 * would still lack the `__open`/`__read`/... natives, but the import itself
 * must fail so a scope's use of it is caught at load.
 */
typedef struct expand_hook_chain_
{
    wy_import_hook hook;
    void* ud;
} expand_hook_chain_;

static wy_error expand_child_import_(wy_context* ctx, const char* path, wy_uword len,
    wy_u8** out, wy_uword* out_len, const wy_module_image** out_image, void* ud)
{
    const expand_hook_chain_* chain = (const expand_hook_chain_*) ud;
    if (len == 7 && wy_memcmp(path, "std::io", 7) == 0) { return WY_ERR_UNBOUND; }
    if (chain->hook == WY_NULL) { return WY_ERR_UNBOUND; }
    return chain->hook(ctx, path, len, out, out_len, out_image, chain->ud);
}

static wy_error copy_tree_(wy_context* from, wy_context* to, wy_value src, wy_value* out, char* why, wy_uword why_size)
{
    WY_UNUSED(from);
    copy_stack_ stack = { wy_context_get_machine(to)->allocator, WY_NULL, 0, 0 };
    wy_error err = copy_push_(&stack, src, out);
    wy_uword nodes = 0;
    while (err == WY_ERR_NONE && stack.count > 0) {
        copy_task_ task = stack.items[--stack.count];
        wy_value v = task.src;
        if (++nodes > WY_EXPAND_MAX_NODES) {
            snprintf(why, why_size, "tree is too large or cyclic");
            err = WY_ERR_INVAL;
            break;
        }
        switch (v.type) {
        case WY_TYPE_TAG_NIL:
        case WY_TYPE_TAG_BOOL:
        case WY_TYPE_TAG_WORD:
        case WY_TYPE_TAG_UWORD:
        case WY_TYPE_TAG_FLOAT:
            *task.dst = v;
            break;
        case WY_TYPE_TAG_SYMBOL: {
            wy_symbol sym;
            err = wy_context_intern(to, v.data.symtab_entry, wy_strlen_f(v.data.symtab_entry), &sym);
            if (err == WY_ERR_NONE) { *task.dst = wy_value_symbol(sym); }
            break;
        }
        case WY_TYPE_TAG_STR: {
            wy_string* s = WY_NULL;
            err = wy_string_new(to, v.data.str->str, v.data.str->len, &s);
            if (err == WY_ERR_NONE) { *task.dst = wy_value_object(WY_TYPE_TAG_STR, (wy_object*) s); }
            break;
        }
        case WY_TYPE_TAG_PAIR: {
            wy_pair* src_pair = (wy_pair*) v.data.gc_object;
            wy_pair* pair = wy_pair_cons_f(to, wy_value_nil(), wy_value_nil());
            if (pair == WY_NULL) { err = WY_ERR_NOMEM; break; }
            *task.dst = wy_value_object(WY_TYPE_TAG_PAIR, (wy_object*) pair);
            err = copy_push_(&stack, src_pair->cdr, &pair->cdr);
            if (err == WY_ERR_NONE) { err = copy_push_(&stack, src_pair->car, &pair->car); }
            break;
        }
        case WY_TYPE_TAG_LIST: {
            wy_list* src_list = (wy_list*) v.data.gc_object;
            wy_list* list = WY_NULL;
            err = wy_list_new(to, src_list->count, &list);
            for (wy_uword i = 0; err == WY_ERR_NONE && i < src_list->count; i++) {
                err = wy_list_push(to, list, wy_value_nil());
            }
            if (err != WY_ERR_NONE) { break; }
            *task.dst = wy_value_object(WY_TYPE_TAG_LIST, (wy_object*) list);
            for (wy_uword i = src_list->count; err == WY_ERR_NONE && i > 0; i--) {
                err = copy_push_(&stack, src_list->items[i - 1], &list->items[i - 1]);
            }
            break;
        }
        case WY_TYPE_TAG_TUPLE: {
            wy_tuple* src_tup = (wy_tuple*) v.data.gc_object;
            wy_tuple* tup = WY_NULL;
            if (src_tup->count == 0) {
                err = wy_tuple_new(to, WY_NULL, 0, &tup);
            } else {
                wy_value* blanks = wy_allocator_alloc(stack.allocator, sizeof(wy_value) * src_tup->count);
                if (blanks == WY_NULL) { err = WY_ERR_NOMEM; break; }
                for (wy_uword i = 0; i < src_tup->count; i++) { blanks[i] = wy_value_nil(); }
                err = wy_tuple_new(to, blanks, src_tup->count, &tup);
                wy_allocator_free(stack.allocator, blanks);
            }
            if (err != WY_ERR_NONE) { break; }
            *task.dst = wy_value_object(WY_TYPE_TAG_TUPLE, (wy_object*) tup);
            for (wy_uword i = src_tup->count; err == WY_ERR_NONE && i > 0; i--) {
                err = copy_push_(&stack, src_tup->items[i - 1], &tup->items[i - 1]);
            }
            break;
        }
        default:
            snprintf(why, why_size, "cannot cross the expansion boundary: %s", type_name_(v));
            err = WY_ERR_INVAL;
            break;
        }
    }
    if (stack.items != WY_NULL) { wy_allocator_free(stack.allocator, stack.items); }
    return err;
}

/* -------------------------------------------------------------------------
 * Errors
 * ------------------------------------------------------------------------- */

static wy_error make_error_value_(wy_context* context, const char* message, wy_value* out)
{
    wy_string* what = WY_NULL;
    wy_error err = wy_string_strdup(context, message, &what);
    if (err != WY_ERR_NONE) { return err; }
    wy_error_obj* obj = WY_NULL;
    err = wy_error_obj_new(context, context->error_class, what, wy_value_nil(), &obj);
    if (err != WY_ERR_NONE) { return err; }
    *out = wy_value_object(WY_TYPE_TAG_ERROR, (wy_object*) obj);
    return WY_ERR_NONE;
}

/** The text of an error value (or a fault value), or a placeholder. */
static const char* error_text_(wy_value v)
{
    if (wy_value_is_error(v) && v.type == WY_TYPE_TAG_ERROR && v.data.gc_object != WY_NULL) {
        wy_error_obj* obj = (wy_error_obj*) v.data.gc_object;
        if (obj->what != WY_NULL) { return obj->what->str; }
    }
    return "(no message)";
}

/* -------------------------------------------------------------------------
 * The expansion VM
 * ------------------------------------------------------------------------- */

/** Register the implicit parent package `std` unless already present. */
static wy_error ensure_std_package_(wy_context* context)
{
    for (wy_uword i = 0; i < context->module_count; i++) {
        wy_module* m = wy_context_get_module(context, i);
        if (m != WY_NULL && m->import_path != WY_NULL && m->import_path->len == 3 && wy_memcmp(m->import_path->str, "std", 3) == 0) {
            return WY_ERR_NONE;
        }
    }
    wy_module* module = wy_module_new_f(context);
    if (module == WY_NULL) { return WY_ERR_NOMEM; }
    module->state = WY_MODULE_BUILTIN;
    wy_error err = wy_string_strdup(context, "std", &module->import_path);
    if (err != WY_ERR_NONE) { return err; }
    err = wy_context_intern(context, "std", 3, &module->name);
    if (err != WY_ERR_NONE) { return err; }
    return wy_context_module_register(context, module, WY_NULL);
}

static wy_uword leaked_bytes_ = 0;

wy_uword wy_expand_leaked_bytes(void)
{
    return leaked_bytes_;
}

/**
 * Run the expansion. On WY_ERR_NONE `*out` is the expanded tree in `parent`;
 * on any failure `msg` holds the reason (the caller turns it into an error
 * value) and the child is gone either way.
 */
static wy_error expand_run_(wy_context* parent, wy_value tree, wy_bytes* scope_image, wy_string* entry,
    wy_value* out, char* msg, wy_uword msg_size)
{
    wy_machine* machine = wy_cmachine_new();
    if (machine == WY_NULL) { snprintf(msg, msg_size, "out of memory creating the expansion VM"); return WY_ERR_NOMEM; }
    wy_context* ctx = wy_cmachine_context_new(machine);
    wy_error err = WY_ERR_NOMEM;
    if (ctx == WY_NULL) {
        snprintf(msg, msg_size, "out of memory creating the expansion VM");
        wy_cmachine_destroy(machine);
        return err;
    }

    wy_value tree_in = wy_value_nil();
    wy_value scope_value = wy_value_nil();
    wy_value result = wy_value_nil();
    wy_module* builtins = WY_NULL;
    wy_module* scope = WY_NULL;
    wy_module* expander = WY_NULL;
    wy_string* entry_path = WY_NULL;
    wy_symbol fn_sym;
    wy_value* fn_slot = WY_NULL;
    wy_u8* image_copy = WY_NULL;
    wy_value args[2];
    expand_hook_chain_ chain = { parent->import_hook, parent->import_ud };

    ctx->expansion = true;
    /* Everything the C code holds across VM runs (module init runs GC
     * safepoints) must be rooted for the whole run. */
    if (wy_context_root_push_f(ctx, &tree_in) != WY_ERR_NONE ||
        wy_context_root_push_f(ctx, &scope_value) != WY_ERR_NONE ||
        wy_context_root_push_f(ctx, &result) != WY_ERR_NONE) {
        snprintf(msg, msg_size, "cannot root expansion values");
        err = WY_ERR_NOMEM;
        goto done;
    }
    ctx->import_hook = expand_child_import_;
    ctx->import_ud = &chain;

    wy_fiber* fiber = wy_fiber_create(ctx, WY_EXPAND_STACK_LEN, WY_EXPAND_FRAME_COUNT);
    if (fiber == WY_NULL || wy_context_attach_fiber(ctx, fiber) != WY_ERR_NONE) {
        snprintf(msg, msg_size, "out of memory creating the expansion VM");
        err = WY_ERR_NOMEM;
        goto done;
    }
    err = wy_builtins_new(ctx, &builtins);
    if (err != WY_ERR_NONE) { snprintf(msg, msg_size, "cannot build expansion builtins"); goto done; }
    ctx->builtins = builtins;
    /* No std::io and no host modules: `std::expand` alone, answering the
     * re-entrancy error (and no I/O natives in the builtins). */
    err = wy_expand_module_install(ctx);
    if (err != WY_ERR_NONE) { snprintf(msg, msg_size, "cannot install expansion modules"); goto done; }

    err = copy_tree_(parent, ctx, tree, &tree_in, msg, msg_size);
    if (err != WY_ERR_NONE) {
        if (msg[0] == '\0') { snprintf(msg, msg_size, "cannot copy the tree into the expansion VM"); }
        goto done;
    }

    image_copy = wy_context_gc_alloc(ctx, scope_image->len);
    if (image_copy == WY_NULL) { snprintf(msg, msg_size, "out of memory"); err = WY_ERR_NOMEM; goto done; }
    wy_memcpy(image_copy, scope_image->data, scope_image->len);
    err = wy_module_load_bytes(ctx, image_copy, scope_image->len, true, &scope);
    if (err != WY_ERR_NONE) { snprintf(msg, msg_size, "expansion scope image is not a valid module (error %d)", (int) err); goto done; }
    wy_context_set_root(ctx, scope);
    scope_value = wy_value_object(WY_TYPE_TAG_MODULE, (wy_object*) scope);
    err = wy_module_run_init(ctx, scope);
    if (err != WY_ERR_NONE) {
        snprintf(msg, msg_size, "expansion scope failed to load (expansion VMs have no host modules, e.g. std::io): %s", error_text_(ctx->current_fiber->fault));
        goto done;
    }

    err = wy_string_new(ctx, entry->str, entry->len, &entry_path);
    if (err != WY_ERR_NONE) { snprintf(msg, msg_size, "out of memory"); goto done; }
    err = wy_link_import(ctx, entry_path, &expander);
    if (err != WY_ERR_NONE) {
        snprintf(msg, msg_size, "cannot load expander module '%s' (error %d)", entry->str, (int) err);
        goto done;
    }
    if (expander->state == WY_MODULE_LOADED) {
        err = wy_module_run_init(ctx, expander);
        if (err != WY_ERR_NONE) {
            snprintf(msg, msg_size, "expander module '%s' failed to initialise: %s", entry->str,
                error_text_(ctx->current_fiber->fault));
            goto done;
        }
    }

    err = wy_context_intern(ctx, "expand_tree", 11, &fn_sym);
    if (err == WY_ERR_NONE) {
        err = wy_link_scope_member(wy_value_object(WY_TYPE_TAG_MODULE, (wy_object*) expander), fn_sym, &fn_slot);
    }
    if (err != WY_ERR_NONE || fn_slot == WY_NULL) {
        snprintf(msg, msg_size, "expander module '%s' has no expand_tree", entry->str);
        err = WY_ERR_UNBOUND;
        goto done;
    }

    args[0] = tree_in;
    args[1] = scope_value;
    err = wy_vm_call_sync(ctx, *fn_slot, args, 2, &result, 1);
    if (err != WY_ERR_NONE) {
        /* A decorator faulted instead of answering: name it (D5). The
         * expander records the decorator it is applying in `_STATE[0]`. */
        const char* who = "";
        wy_value* state = WY_NULL;
        wy_symbol state_sym;
        if (wy_context_intern(ctx, "_STATE", 6, &state_sym) == WY_ERR_NONE &&
            wy_link_scope_member(wy_value_object(WY_TYPE_TAG_MODULE, (wy_object*) expander), state_sym, &state) == WY_ERR_NONE &&
            state != WY_NULL && state->type == WY_TYPE_TAG_LIST) {
            wy_list* cell = (wy_list*) state->data.gc_object;
            if (cell->count > 0 && cell->items[0].type == WY_TYPE_TAG_SYMBOL) { who = cell->items[0].data.symtab_entry; }
        }
        if (who[0] != '\0') {
            snprintf(msg, msg_size, "@%s: %s", who, error_text_(ctx->current_fiber->fault));
        } else {
            snprintf(msg, msg_size, "%s", error_text_(ctx->current_fiber->fault));
        }
        goto done;
    }
    if (wy_value_is_error(result)) {
        snprintf(msg, msg_size, "%s", error_text_(result));
        err = WY_ERR_FAULT;
        goto done;
    }

    msg[0] = '\0';
    err = copy_tree_(ctx, parent, result, out, msg, msg_size);
    if (err != WY_ERR_NONE && msg[0] == '\0') { snprintf(msg, msg_size, "cannot copy the expanded tree out"); }

done:
    wy_cmachine_context_destroy(ctx);
    leaked_bytes_ += wy_cmachine_destroy_residual(machine);
    return err;
}

/* -------------------------------------------------------------------------
 * std::expand
 * ------------------------------------------------------------------------- */

static wy_error expand_leaf_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(argc);
    if (nres == 0) { return WY_ERR_NONE; }
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    if (args[1].type != WY_TYPE_TAG_BYTES || args[2].type != WY_TYPE_TAG_STR) { return WY_ERR_BAD_TYPE; }

    char msg[WY_EXPAND_MSG];
    msg[0] = '\0';
    if (context->expansion) {
        return make_error_value_(context, "expansion is not re-entrant", &out[0]);
    }
    wy_value tree = wy_value_nil();
    wy_error err = expand_run_(context, args[0], (wy_bytes*) args[1].data.gc_object,
        args[2].data.str, &tree, msg, sizeof(msg));
    if (err == WY_ERR_NONE) {
        out[0] = tree;
        return WY_ERR_NONE;
    }
    if (err == WY_ERR_NOMEM && msg[0] == '\0') { return WY_ERR_NOMEM; }
    return make_error_value_(context, msg, &out[0]);
}

wy_error wy_expand_module_new(wy_context* context, wy_module** out)
{
    if (context == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }

    wy_module* module = wy_module_new_f(context);
    if (module == WY_NULL) { return WY_ERR_NOMEM; }
    module->state = WY_MODULE_BUILTIN;

    module->global_count = 1;
    module->globals = wy_context_gc_alloc(context, sizeof(wy_value));
    module->fill_layer = wy_context_gc_alloc(context, sizeof(wy_u8));
    module->fill_source = wy_context_gc_alloc(context, sizeof(wy_symbol));
    if (module->globals == WY_NULL || module->fill_layer == WY_NULL || module->fill_source == WY_NULL) {
        return WY_ERR_NOMEM;
    }
    module->globals[0] = wy_value_unset();
    module->fill_layer[0] = 0;
    module->fill_source[0] = WY_NULL;

    wy_allocator* allocator = wy_context_get_machine(context)->allocator;
    wy_error err = wy_slot_dict_expand_f(&module->exports, allocator, 2);
    if (err != WY_ERR_NONE) { return err; }

    wy_symbol sym;
    err = wy_context_intern(context, "expand", 6, &sym);
    if (err != WY_ERR_NONE) { return err; }
    wy_native* native = WY_NULL;
    err = wy_native_leaf_new(context, sym, 3, 3, expand_leaf_, &native);
    if (err != WY_ERR_NONE) { return err; }
    module->globals[0] = wy_value_object(WY_TYPE_TAG_NATIVE, (wy_object*) native);
    err = wy_slot_dict_add_entry(&module->exports, sym, 0);
    if (err != WY_ERR_NONE) { return err; }

    *out = module;
    return WY_ERR_NONE;
}

wy_error wy_expand_module_install(wy_context* context)
{
    if (context == WY_NULL) { return WY_ERR_INVAL; }

    wy_error err = ensure_std_package_(context);
    if (err != WY_ERR_NONE) { return err; }

    wy_module* module = WY_NULL;
    err = wy_expand_module_new(context, &module);
    if (err != WY_ERR_NONE) { return err; }

    err = wy_string_strdup(context, "std::expand", &module->import_path);
    if (err != WY_ERR_NONE) { return err; }
    err = wy_context_intern(context, "std::expand", wy_strlen_f("std::expand"), &module->name);
    if (err != WY_ERR_NONE) { return err; }

    return wy_context_module_register(context, module, WY_NULL);
}
