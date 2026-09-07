#ifndef WYRM_CONTEXT_H_
#define WYRM_CONTEXT_H_

#include <wyrm/fwd.h>
#include <wyrm/fiber.h>
#include <wyrm/gc.h>
#include <wyrm/mem_info.h>

/* ------------------------------------------------------------------------- */
/* Context API                                                               */
/* ------------------------------------------------------------------------- */

WY_BEGIN_DECLS

struct wy_context
{
    wy_machine* parent;
    wy_fiber* current_fiber;
    wy_module* root_module;
    wy_main_loop* main_loop;
    wy_primitive wakeable_source;
    wy_gc_arena arena;
};

void wy_context_init_s(wy_context* self);
void wy_context_finalize_f(wy_context* self);

wy_error wy_context_set_root(wy_context* context, wy_module* module);



wy_error wy_context_attach_loop(wy_context* context, wy_main_loop* loop);
void wy_context_detach_loop(wy_context* context);

wy_error wy_context_activate(wy_context* self, wy_fiber* fiber);
wy_error wy_context_attach_fiber(wy_context* self, wy_fiber* fiber);

wy_error wy_context_exec(wy_context* self);

void wy_ctx_object_init_header_static_f(wy_context* context, wy_object* object, const wy_object_type* dtype);

WY_INLINE wy_machine* wy_context_get_machine(wy_context* self);

void wy_context_object_init_header_f(wy_context* context, wy_object* object, const wy_object_type* dtype);

void* wy_context_gc_alloc(wy_context* context, wy_uword dsize);
void* wy_context_gc_realloc(wy_context* context, void* ptr, wy_uword new_size);
void wy_context_gc_free(wy_context* context, void* ptr);
void wy_context_push_gc(wy_context* context, wy_object* gc_info);

void wy_context_gc_full_run(wy_context* context);


#define WY_CONTEXT_MEM_INFO_RESERVE_COUNT(context, mem_info, count, type) (wy_context_mem_reserve_count_f((context), (mem_info), (count), sizeof(type)))

wy_error wy_context_mem_reserve_f(wy_context* context, wy_mem_info* mem_info, wy_uword sz);
void wy_context_mem_release_f(wy_context* context, wy_mem_info* mem_info);

/**
 * Grow `mem_info` to hold at least `count` blocks of `block_sz` bytes
 */
WY_INLINE wy_error wy_context_mem_reserve_count_f(wy_context* context, wy_mem_info* mem_info, wy_uword count, wy_uword block_sz)
{
    if (count > WY_MAX_ARRAY_LEN) { return WY_ERR_INVAL; }
    wy_uword mem_sz = wy_mem_info_block_count_sz_f(block_sz, count);
    return wy_context_mem_reserve_f(context, mem_info, mem_sz);
}


/**
 * Gab machine associated with context
 *
 * @param self Context
 * @return Machine associated with the context
 */
WY_INLINE wy_machine* wy_context_get_machine(wy_context* self)
{
    if (!self) { return WY_NULL; }
    return self->parent;
}


/**
 * Get the fiber the context is currently running
 */
WY_INLINE wy_fiber* wy_context_get_fiber_f(wy_context* self)
{
    if (self == WY_NULL) { return WY_NULL; }
    return self->current_fiber;
}

/**
 * Count the values visible to the current call
 * @return Number of values in the active frame
 */
WY_INLINE wy_uword wy_context_value_count(wy_context* self)
{
    if (self == WY_NULL || self->current_fiber == WY_NULL) { return 0; }
    return wy_fiber_value_count_f(self->current_fiber);
}

/**
 * Discard values until the active frame holds `count` of them
 * @return WY_ERR_NONE on success, WY_ERR_RANGE if the frame holds fewer
 */
WY_INLINE wy_error wy_context_pop_to_value_count(wy_context* self, wy_uword count)
{
    if (self == WY_NULL || self->current_fiber == WY_NULL) { return WY_ERR_INVAL; }
    return wy_fiber_pop_to_value_count_f(self->current_fiber, count);
}

/**
 * Access a value in the active frame
 * @return Pointer to the value, or WY_NULL when out of range
 */
WY_INLINE wy_value* wy_context_value_n(wy_context* self, wy_uword index)
{
    if (index >= wy_context_value_count(self)) { return WY_NULL; }
    return wy_fiber_value_n(self->current_fiber, index);
}

/**
 * Push a value onto the current fiber's stack
 */
WY_INLINE wy_error wy_context_push(wy_context* self, wy_value value)
{
    if (self == WY_NULL || self->current_fiber == WY_NULL) { return WY_ERR_INVAL; }
    return wy_fiber_push_value_f(self->current_fiber, value);
}

/**
 * Set the function the current fiber runs next
 */
WY_INLINE wy_error wy_context_set_pending(wy_context* self, wy_exec_fn pending)
{
    if (self == WY_NULL || self->current_fiber == WY_NULL) { return WY_ERR_INVAL; }
    if (self->current_fiber->pending != WY_NULL) { return WY_ERR_BUSY; }
    self->current_fiber->pending = pending;
    return WY_ERR_NONE;
}

/**
 * Get the number of results the caller reserved for this call
 */
WY_INLINE wy_uword wy_context_result_count(wy_context* self)
{
    if (self == WY_NULL || self->current_fiber == WY_NULL) { return 0; }
    return wy_fiber_result_count_f(self->current_fiber);
}

/**
 * Access a reserved result slot of the current call
 */
WY_INLINE wy_value* wy_context_result_n(wy_context* self, wy_uword index)
{
    if (self == WY_NULL || self->current_fiber == WY_NULL) { return WY_NULL; }
    return wy_fiber_result_n(self->current_fiber, index);
}

/**
 * Store result `result` of the current call, truncating if unreserved
 */
WY_INLINE bool wy_context_set_result(wy_context* self, wy_uword index, wy_value value)
{
    if (self == WY_NULL || self->current_fiber == WY_NULL) { return false; }
    return wy_fiber_set_result_f(self->current_fiber, index, value);
}

/**
 * Reuse the current call's frame to call `fn`
 *
 * The top `arg_count` values become the arguments. `fn` inherits this call's
 * reserved return slots and the written count is reset.
 */
WY_INLINE wy_error wy_context_tail_call(wy_context* self, wy_exec_fn fn, wy_uword arg_count)
{
    if (self == WY_NULL || self->current_fiber == WY_NULL) { return WY_ERR_INVAL; }
    return wy_fiber_tail_call_f(self->current_fiber, fn, arg_count);
}

/**
 * Call `fn` with `args`, resuming at `result_cb` with `result_count` results
 *
 * Reserve `result_count` return slots for the function call and value with
 * the given arguments.
 */
WY_INLINE wy_error wy_context_call_continue(wy_context* self, wy_exec_fn result_cb, wy_exec_fn fn, const wy_value* args, wy_uword arg_count, wy_uword result_count)
{
    if (self == WY_NULL || self->current_fiber == WY_NULL) { return WY_ERR_INVAL; }
    if (arg_count > 0 && args == WY_NULL) { return WY_ERR_INVAL; }
    return wy_fiber_exec_continue_f(self->current_fiber, result_cb, fn, args, arg_count, result_count);
}


WY_END_DECLS

#endif
