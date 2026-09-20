#include "vm_internal.h"

#include <wyrm.h>
#include <wyrm/native.h>

/**
 * Call a leaf native inline: no frame, no re-entry into the fiber
 * trampoline. `args`/`out` are caller-owned; the leaf writes its results
 * directly into `out[0..nres)` (design_c_vm.md §1.3).
 */
wy_error wy_vm_call_leaf_f(wy_context* ctx, wy_native* native, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    if (ctx == WY_NULL || native == WY_NULL) { return WY_ERR_INVAL; }
    if (argc > 0 && args == WY_NULL) { return WY_ERR_INVAL; }
    if (nres > 0 && out == WY_NULL) { return WY_ERR_INVAL; }
    if (native->kind != WY_NATIVE_LEAF) { return WY_ERR_INVAL; }
    if (argc < native->min_argc || argc > native->max_argc) { return WY_ERR_ARITY; }

    return native->fn.leaf(ctx, args, argc, out, nres);
}

/**
 * Push an exec native's call using the existing reservation trampoline:
 * reserves `nres` result slots below a new native frame, pushes `args`
 * above it, and schedules `native`'s exec_fn to run next with `continuation`
 * queued to run once it completes (design_c_vm.md §1.3).
 *
 * This is a thin, argc-checked wrapper over wy_fiber_exec_continue_f - the
 * reservation trampoline is unchanged by this milestone, only reused.
 */
wy_error wy_vm_call_exec_push_f(wy_context* ctx, wy_exec_fn continuation, wy_native* native,
    const wy_value* args, wy_uword argc, wy_uword nres)
{
    if (ctx == WY_NULL || ctx->current_fiber == WY_NULL || native == WY_NULL) { return WY_ERR_INVAL; }
    if (native->kind != WY_NATIVE_EXEC) { return WY_ERR_INVAL; }
    if (argc < native->min_argc || argc > native->max_argc) { return WY_ERR_ARITY; }

    return wy_fiber_exec_continue_f(ctx->current_fiber, continuation, native->fn.exec, args, argc, nres);
}

/**
 * Complete an exec native call: read back the `nres` values it produced,
 * in logical order, and drop them from the value stack.
 *
 * Call this as (or from) the continuation `wy_vm_call_exec_push_f` was
 * given. By the time the continuation runs, wy_fiber_pop_continuation_f has
 * already popped the native's frame, so the results it wrote into the
 * reserved slots now sit as ordinary values directly above the caller's own
 * `base_count` values - in reverse logical order, per wy_stack.h's layout
 * (result 0 ends up at the highest address, closest to where the reserved
 * region began). `base_count` is the caller's own value count *before* the
 * call was pushed (i.e. before wy_vm_call_exec_push_f/
 * wy_fiber_exec_continue_f ran).
 */
wy_error wy_vm_native_await_complete_f(wy_context* ctx, wy_uword base_count, wy_value* dst, wy_uword nres)
{
    if (ctx == WY_NULL || ctx->current_fiber == WY_NULL) { return WY_ERR_INVAL; }
    if (nres > 0 && dst == WY_NULL) { return WY_ERR_INVAL; }

    wy_fiber* fiber = ctx->current_fiber;
    if (wy_fiber_value_count_f(fiber) < base_count + nres) { return WY_ERR_RANGE; }

    for (wy_uword k = 0; k < nres; k++) {
        wy_value* slot = wy_fiber_value_n(fiber, base_count + k);
        dst[nres - 1 - k] = (slot != WY_NULL) ? *slot : wy_value_nil();
    }

    return wy_fiber_pop_to_value_count_f(fiber, base_count);
}
