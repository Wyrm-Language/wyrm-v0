#ifndef WYRM_FIBER_H_
#define WYRM_FIBER_H_

#include <wyrm/fwd.h>
#include <wyrm/mem_info.h>
#include <wyrm/stack.h>

/* ------------------------------------------------------------------------- */
/* Fiber API                                                                 */
/* ------------------------------------------------------------------------- */

WY_BEGIN_DECLS

extern const wy_object_type wy_type_fiber;

/**
 * A call frame stack
 *
 * The call frame stack lives parallel to the value stack and tracks
 * the continuations of the current fiber.
 */
typedef struct wy_fiber_frame
{
    wy_value* restore_base;
    wy_exec_fn fn;
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

    //! The total number of entries to preserve on
    wy_uword tail_preserve_count;
};


wy_fiber* wy_fiber_create(wy_context* context, wy_uword stack_len, wy_uword frame_count);
WY_INLINE wy_context* wy_fiber_get_context(wy_fiber* self);
void wy_fiber_finalize_f(wy_fiber* self);
wy_error wy_fiber_exec_f(wy_fiber* self, wy_state* state);

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

WY_INLINE wy_error wy_fiber_push_return_f(wy_fiber* self, wy_value value)
{
    wy_error last_error = wy_fiber_push_value_f(self, value);
    if (last_error == WY_ERR_NONE) {
        self->tail_preserve_count++;
    }
    return last_error;
}


wy_error wy_fiber_push_frame_f(wy_fiber* self, wy_exec_fn fn);
wy_error wy_fiber_pop_continuation_f(wy_fiber* self, wy_exec_fn* out_continuation, wy_uword preserve_count);

/**
 * Get the current call depth excluding root frame
 */
WY_INLINE wy_uword wy_fiber_frame_depth_f(wy_fiber* self)
{
    WY_ASSERT(self != WY_NULL);
    return (wy_uword) (self->current_frame - WY_MEM_INFO_BEGIN_PTR(wy_fiber_frame, &self->frame_memory));
}

WY_INLINE wy_error wy_fiber_push_continuation(wy_fiber* self, wy_exec_fn fn)
{
    if (self == WY_NULL || fn == WY_NULL) { return WY_ERR_INVAL; }
    if (self->pending != WY_NULL) { return WY_ERR_BUSY; }
    if (self->tail_preserve_count > 0) { return WY_ERR_BUSY; }

    return wy_fiber_push_frame_f(self, fn);
}

/**
 * Push continuation call with argument preservation
 */
WY_INLINE wy_error wy_fiber_exec_continue_f(wy_fiber* self, wy_exec_fn continuation, wy_exec_fn fn, const wy_value* args, wy_uword arg_count)
{
    WY_ASSERT(self != WY_NULL && continuation != WY_NULL && fn != WY_NULL);
    WY_ASSERT(args != WY_NULL || arg_count == 0);

    if (self->pending != WY_NULL) { return WY_ERR_BUSY; }
    if (self->tail_preserve_count > 0) { return WY_ERR_BUSY; }

    wy_error last_error = wy_fiber_push_frame_f(self, continuation);
    if (last_error != WY_ERR_NONE) { return last_error; }

    last_error = wy_stack_push_array_f(&self->value_stack, args, arg_count);
    if (last_error != WY_ERR_NONE) {
        wy_exec_fn trash;
        (void) wy_fiber_pop_continuation_f(self, &trash, 0);
        return last_error;
    }

    self->pending = fn;
    return last_error;
}


WY_END_DECLS

#endif
