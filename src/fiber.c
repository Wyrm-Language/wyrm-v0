#include <wyrm.h>
#include <wyrm/internal_api.h>

static wyrm_error fiber_continue_with_return(wyrm_fiber* self)
{
    /* Not completed executing current task */
    if (self->pending != WYRM_NULL) { return WYRM_ERR_INVAL; }

    wyrm_error last_error = wyrm_stack_pop_continuation_f(
        &self->value_stack,
        &self->pending,
        self->tail_preserve_count);

    if (last_error == WYRM_ERR_NONE) {
        self->tail_preserve_count = 0;
    }

    /* No continuation, fine - just return */
    if (last_error == WYRM_ERR_EMPTY) {
        last_error = WYRM_ERR_NONE;
    }

    return last_error;
}


wyrm_fiber* wyrm_fiber_create(wyrm_context* context, wyrm_uword stack_len)
{
    wyrm_fiber* fiber = wyrm_context_gc_alloc(context, sizeof(wyrm_fiber));
    if (fiber == WYRM_NULL) { return WYRM_NULL; }

    wyrm_value* stack = wyrm_context_gc_alloc(context, stack_len * sizeof(wyrm_value));
    if (stack == WYRM_NULL) {
        wyrm_context_gc_free(context, fiber);
        return WYRM_NULL;
    }

    fiber->parent = WYRM_NULL;
    fiber->pending = WYRM_NULL;
    fiber->tail_preserve_count = 0;

    wyrm_stack_init_f(&fiber->value_stack, stack, stack_len);
    wyrm_context_gc_init(context, &fiber->obj, WYRM_TYPE_TAG_FIBER);
    return fiber;
}

void wyrm_fiber_finalize_f(wyrm_fiber* self)
{
    wyrm_context_gc_free(self->parent, self->value_stack.entries_begin);
    self->value_stack.entries_begin = WYRM_NULL;
    self->value_stack.entries_end = WYRM_NULL;
    self->value_stack.base = WYRM_NULL;
    self->value_stack.top = WYRM_NULL;
}



wyrm_error wyrm_fiber_exec_f(wyrm_fiber* self, wyrm_state* state)
{
    WYRM_ASSERT(self != WYRM_NULL && state != WYRM_NULL && state->fiber == self);
    wyrm_error last_error = WYRM_ERR_NONE;

    // Continue execution through continuation stack if no forward stack present
    if (self->pending == WYRM_NULL) {
        last_error = fiber_continue_with_return(self);
    }

    if (last_error != WYRM_ERR_NONE || self->pending == WYRM_NULL) {
        return last_error;
    }

    while (last_error == WYRM_ERR_NONE && self->pending != WYRM_NULL) {
        wyrm_exec_fn pending = self->pending;
        self->pending = WYRM_NULL;

        wyrm_exec_state result = pending(state);

        switch (result) {
        case WYRM_EXEC_DELEGATE:
            last_error = wyrm_stack_replace_frame_f(&self->value_stack, self->tail_preserve_count);
            if (self->pending == WYRM_NULL) {
                last_error = WYRM_ERR_INVAL;
            } else {
                self->tail_preserve_count = 0;
            }
            break;

        case WYRM_EXEC_CONTINUE:
            if (self->pending == WYRM_NULL) {
                last_error = WYRM_ERR_INVAL;
            }
            break;

        case WYRM_EXEC_DONE:
            if (self->pending != WYRM_NULL) {
                last_error = WYRM_ERR_INVAL;
                break;
            }

            last_error = fiber_continue_with_return(self);
            break;

        default:
            last_error = WYRM_ERR_INVAL;
        }
    }
    return last_error;
}
