#ifndef WYRM_FIBER_H_
#define WYRM_FIBER_H_

#include <wyrm/fwd.h>
#include <wyrm/mem_info.h>
#include <wyrm/object.h>
#include <wyrm/stack.h>

/* ------------------------------------------------------------------------- */
/* Fiber API                                                                 */
/* ------------------------------------------------------------------------- */

WY_BEGIN_DECLS

extern const wy_object_type wy_type_fiber;

/**
 * A call frame
 *
 * The call frame stack lives parallel to the value stack and tracks
 * the continuations of the current fiber. This also tracks the number
 * of allowed return results.
 */
typedef struct wy_fiber_frame
{
    wy_value* restore_base;     //!< Caller's base, restored on pop
    wy_exec_fn fn;              //!< Continuation run once the call returns
    wy_uword result_count;      //!< Result slots the caller reserved below base
} wy_fiber_frame;

/**
 * Fiber / stack
 */
struct wy_fiber
{
    wy_object object;
    wy_context* parent;
    wy_stack value_stack;
    wy_exec_fn pending;

    wy_mem_info frame_memory;               //!< Reserved memory for frame storage
    wy_fiber_frame* current_frame;          //!< Innermost active frame
};


wy_fiber* wy_fiber_create(wy_context* context, wy_uword stack_len, wy_uword frame_count);
WY_INLINE wy_context* wy_fiber_get_context(wy_fiber* self);
void wy_fiber_finalize_f(wy_fiber* self);
wy_error wy_fiber_exec_f(wy_fiber* self, wy_context* context);

WY_INLINE wy_context* wy_fiber_get_context(wy_fiber* self)
{
    if (!self) { return WY_NULL; }
    return self->parent;
}


WY_INLINE wy_uword wy_fiber_value_count_f(wy_fiber* self)
{
    return wy_stack_arg_count_f(&self->value_stack);
}

WY_INLINE wy_error wy_fiber_pop_to_value_count_f(wy_fiber* self, wy_uword count)
{
    return wy_stack_pop_to_value_count_f(&self->value_stack, count);
}

WY_INLINE wy_value* wy_fiber_value_n(wy_fiber* self, wy_uword index)
{
    return &self->value_stack.base[index];
}

WY_INLINE wy_error wy_fiber_push_value_f(wy_fiber* self, wy_value value)
{
    return wy_stack_push_f(&self->value_stack, value.type, value.data);
}

/**
 * Get the number of result slots the caller reserved for this call
 *
 * @param self Fiber
 * @return Reserved result count for the active frame
 */
WY_INLINE wy_uword wy_fiber_result_count_f(wy_fiber* self)
{
    WY_ASSERT(self != WY_NULL);
    return self->current_frame->result_count;
}

/**
 * Get reserved slot result for the current call
 */
WY_INLINE wy_value* wy_fiber_result_n(wy_fiber* self, wy_uword index)
{
    WY_ASSERT(self != WY_NULL);
    if (index >= self->current_frame->result_count) { return WY_NULL; }
    return &self->value_stack.base[-(wy_word) (index + 1)];
}

/**
 * Store result `index` of the active call, truncating if unreserved
 * @return true when stored, false when truncated
 */
WY_INLINE bool wy_fiber_set_result_f(wy_fiber* self, wy_uword index, wy_value value)
{
    WY_ASSERT(self != WY_NULL);
    wy_value* slot = wy_fiber_result_n(self, index);
    if (slot == WY_NULL) { return false; }

    *slot = value;
    return true;
}

wy_error wy_fiber_push_frame_f(wy_fiber* self, wy_exec_fn fn, wy_uword result_count);
wy_error wy_fiber_pop_continuation_f(wy_fiber* self, wy_exec_fn* out_continuation);
wy_error wy_fiber_tail_call_f(wy_fiber* self, wy_exec_fn fn, wy_uword arg_count);

/**
 * Get the current call depth excluding root frame
 */
WY_INLINE wy_uword wy_fiber_frame_depth_f(wy_fiber* self)
{
    WY_ASSERT(self != WY_NULL);
    return (wy_uword) (self->current_frame - WY_MEM_INFO_BEGIN_PTR(wy_fiber_frame, &self->frame_memory));
}

/**
 * Push a frame whose continuation is `fn`, reserving `result_count` results
 *
 * @param self Fiber
 * @param fn Continuation to run once the frame returns
 * @param result_count Number of result slots to reserve for the call
 * @return WY_ERR_NONE on success, WY_ERR_BUSY if a call is already pending
 */
WY_INLINE wy_error wy_fiber_push_continuation(wy_fiber* self, wy_exec_fn fn, wy_uword result_count)
{
    if (self == WY_NULL || fn == WY_NULL) { return WY_ERR_INVAL; }
    if (self->pending != WY_NULL) { return WY_ERR_BUSY; }

    return wy_fiber_push_frame_f(self, fn, result_count);
}

/**
 * Call `fn` with `args`, resuming at `continuation` with `result_count` results
 *
 * Reserves the result slots, opens the frame, and lays the arguments down
 * above the new base.
 *
 * @param self Fiber
 * @param continuation Continuation to run once `fn` returns
 * @param fn Function to call
 * @param args Argument values
 * @param arg_count Number of arguments
 * @param result_count Number of results to reserve for the call
 * @return WY_ERR_NONE on success, WY_ERR_BUSY if a call is already pending,
 *         WY_ERR_STACK_OVERFLOW if out of stack or frame space
 */
WY_INLINE wy_error wy_fiber_exec_continue_f(wy_fiber* self, wy_exec_fn continuation, wy_exec_fn fn, const wy_value* args, wy_uword arg_count, wy_uword result_count)
{
    WY_ASSERT(self != WY_NULL && continuation != WY_NULL && fn != WY_NULL);
    WY_ASSERT(args != WY_NULL || arg_count == 0);

    if (self->pending != WY_NULL) { return WY_ERR_BUSY; }

    wy_error last_error = wy_fiber_push_frame_f(self, continuation, result_count);
    if (last_error != WY_ERR_NONE) { return last_error; }

    last_error = wy_stack_push_array_f(&self->value_stack, args, arg_count);
    if (last_error != WY_ERR_NONE) {
        wy_exec_fn trash;
        (void) wy_fiber_pop_continuation_f(self, &trash);
        return last_error;
    }

    self->pending = fn;
    return last_error;
}


WY_END_DECLS

#endif
