#ifndef WYRM_FIBER_H_
#define WYRM_FIBER_H_

#include <wyrm/core.h>
#include <wyrm/mem_info.h>
#include <wyrm/stack.h>

/* ------------------------------------------------------------------------- */
/* Fiber API                                                                 */
/* ------------------------------------------------------------------------- */

WYRM_BEGIN_DECLS

extern const wyrm_object_type wyrm_type_fiber;

/**
 * A call frame stack
 *
 * The call frame stack lives parallel to the value stack and tracks
 * the continuations of the current fiber.
 */
typedef struct wy_fiber_frame
{
    wyrm_value* restore_base;
    wyrm_exec_fn fn;
} wy_fiber_frame;

/**
 * Fiber / stack
 */
struct wyrm_fiber
{
    wyrm_object object;
    wyrm_context* parent;
    wyrm_stack value_stack;
    wyrm_exec_fn pending;

    wy_mem_info frame_memory;               //!< Reserved memory for frame storage
    wy_fiber_frame* current_frame;          //!< Innermost active frame

    //! The total number of entries to preserve on
    wyrm_uword tail_preserve_count;
};


wyrm_fiber* wyrm_fiber_create(wyrm_context* context, wyrm_uword stack_len, wy_uword frame_count);
WYRM_INLINE wyrm_context* wyrm_fiber_get_context(wyrm_fiber* self);
void wyrm_fiber_finalize_f(wyrm_fiber* self);
wyrm_error wyrm_fiber_exec_f(wyrm_fiber* self, wyrm_state* state);

WYRM_INLINE wyrm_context* wyrm_fiber_get_context(wyrm_fiber* self)
{
    if (!self) { return WYRM_NULL; }
    return self->parent;
}


WYRM_INLINE wyrm_uword wyrm_fiber_value_count_f(wyrm_fiber* self)
{
    return wyrm_stack_arg_count_f(&self->value_stack);
}

WYRM_INLINE wyrm_error wyrm_fiber_pop_to_value_count_f(wyrm_fiber* self, wyrm_uword count)
{
    return wyrm_stack_pop_to_value_count_f(&self->value_stack, count);
}

WYRM_INLINE wyrm_value* wyrm_fiber_value_n(wyrm_fiber* self, wyrm_uword index)
{
    return &self->value_stack.base[index];
}

WYRM_INLINE wyrm_error wyrm_fiber_push_value_f(wyrm_fiber* self, wyrm_value value)
{
    return wyrm_stack_push_f(&self->value_stack, value.type, value.data);
}

WYRM_INLINE wyrm_error wyrm_fiber_push_return_f(wyrm_fiber* self, wyrm_value value)
{
    wyrm_error last_error = wyrm_fiber_push_value_f(self, value);
    if (last_error == WYRM_ERR_NONE) {
        self->tail_preserve_count++;
    }
    return last_error;
}


wy_error wy_fiber_push_frame_f(wy_fiber* self, wyrm_exec_fn fn);
wy_error wy_fiber_pop_continuation_f(wy_fiber* self, wyrm_exec_fn* out_continuation, wyrm_uword preserve_count);

/**
 * Get the current call depth excluding root frame
 */
WYRM_INLINE wyrm_uword wy_fiber_frame_depth_f(wy_fiber* self)
{
    WYRM_ASSERT(self != WYRM_NULL);
    return (wyrm_uword) (self->current_frame - WY_MEM_INFO_BEGIN_PTR(wy_fiber_frame, &self->frame_memory));
}

WYRM_INLINE wyrm_error wyrm_fiber_push_continuation(wyrm_fiber* self, wyrm_exec_fn fn)
{
    if (self == WYRM_NULL || fn == WYRM_NULL) { return WYRM_ERR_INVAL; }
    if (self->pending != WYRM_NULL) { return WYRM_ERR_BUSY; }
    if (self->tail_preserve_count > 0) { return WYRM_ERR_BUSY; }

    return wy_fiber_push_frame_f(self, fn);
}

/**
 * Push continuation call with argument preservation
 */
WYRM_INLINE wyrm_error wyrm_fiber_exec_continue_f(wyrm_fiber* self, wyrm_exec_fn continuation, wyrm_exec_fn fn, const wyrm_value* args, wyrm_uword arg_count)
{
    WYRM_ASSERT(self != WYRM_NULL && continuation != WYRM_NULL && fn != WYRM_NULL);
    WYRM_ASSERT(args != WYRM_NULL || arg_count == 0);

    if (self->pending != WYRM_NULL) { return WYRM_ERR_BUSY; }
    if (self->tail_preserve_count > 0) { return WYRM_ERR_BUSY; }

    wyrm_error last_error = wy_fiber_push_frame_f(self, continuation);
    if (last_error != WYRM_ERR_NONE) { return last_error; }

    last_error = wyrm_stack_push_array_f(&self->value_stack, args, arg_count);
    if (last_error != WYRM_ERR_NONE) {
        wyrm_exec_fn trash;
        (void) wy_fiber_pop_continuation_f(self, &trash, 0);
        return last_error;
    }

    self->pending = fn;
    return last_error;
}


WYRM_END_DECLS

#endif
