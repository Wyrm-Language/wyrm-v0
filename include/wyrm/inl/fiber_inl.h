#ifndef WYRM_INL_FIBER_H_
#define WYRM_INL_FIBER_H_

#include <wyrm/inl/stack_inl.h>
#include <wyrm/types.h>

#ifdef __cplusplus
extern "C" {
#endif

WYRM_INLINE wyrm_uword wyrm_fiber_value_count_f(wyrm_fiber* self)
{
    return wyrm_stack_arg_count_f(&self->value_stack);
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

WYRM_INLINE wyrm_error wyrm_fiber_push_continuation(wyrm_fiber* self, wyrm_exec_fn fn)
{
    if (self == WYRM_NULL || fn == WYRM_NULL) { return WYRM_ERR_INVAL; }
    if (self->pending != WYRM_NULL) { return WYRM_ERR_BUSY; }
    if (self->tail_preserve_count > 0) { return WYRM_ERR_BUSY; }

    return wyrm_stack_push_continuation_f(&self->value_stack, fn);
}

/**
 * Configure fiber to call `fn` and continue with current stack + tail preserved at continuation
 *
 * Verify the fiber is not already configured for a return result, then
 * push the continuation and arguments. If successful, the pending function
 * is properly configured. This function results in the stack frame within
 * fiber `self` being reset to the new exec fn.
 *
 * Potential Errors:
 *    - WYRM_ERR_STACK_OVERFLOW: out of stack space
 *    - WYRM_ERR_BUSY: variable already has continuation
 *
 * @param self Current fiber
 * @param continuation required continuation function
 * @param fn required function to call
 * @param args argument data
 * @param arg_count number of arguments
 * @return WYRM_ERR_NONE if successful
 */
WYRM_INLINE wyrm_error wyrm_fiber_exec_continue_f(wyrm_fiber* self, wyrm_exec_fn continuation, wyrm_exec_fn fn, const wyrm_value* args, wyrm_uword arg_count)
{
    WYRM_ASSERT(self != WYRM_NULL && continuation != WYRM_NULL && fn != WYRM_NULL);
    WYRM_ASSERT(args != WYRM_NULL || arg_count == 0);

    if (self->pending != WYRM_NULL) { return WYRM_ERR_BUSY; }
    if (self->tail_preserve_count > 0) { return WYRM_ERR_BUSY; }

    wyrm_error last_error = wyrm_stack_push_continuation_f(&self->value_stack, continuation);
    if (last_error != WYRM_ERR_NONE) { return last_error; }

    last_error = wyrm_stack_push_array_f(&self->value_stack, args, arg_count);
    if (last_error != WYRM_ERR_NONE) {
        wyrm_exec_fn trash;
        (void) wyrm_stack_pop_continuation_f(&self->value_stack, &trash, 0);
        return last_error;
    }

    self->pending = fn;
    return last_error;
}

#ifdef __cplusplus
}
#endif

#endif
