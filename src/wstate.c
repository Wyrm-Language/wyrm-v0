#include <wyrm.h>


wyrm_error wyrm_state_exec(wyrm_state* state)
{
    if (state == WYRM_NULL || state->fiber == WYRM_NULL) { return WYRM_ERR_INVAL; }
    wyrm_fiber* self = state->fiber;
    wyrm_error last_error = WYRM_ERR_NONE;
    wyrm_exec_fn pending = state->pending; state->pending = WYRM_NULL;

    // No pending function, check if there's a continuation
    if (pending == WYRM_NULL) {
        last_error = wyrm_stack_pop_continuation_f(&self->value_stack, &pending, 0);
        if (last_error == WYRM_ERR_EMPTY) {
            last_error = WYRM_ERR_NONE;
        }
    }

    if (last_error != WYRM_ERR_NONE || pending == WYRM_NULL) {
        return last_error;
    }

    // Execute until error
    while (last_error == WYRM_ERR_NONE) {
        wyrm_exec_result result = pending(state);

        if (result.state == WYRM_EXEC_DELEGATE) {
            last_error = wyrm_stack_replace_frame_f(&self->value_stack, result.stack_values);
            if (state->pending != WYRM_NULL) {
                pending = state->pending; state->pending = WYRM_NULL;
            } else if (last_error == WYRM_ERR_NONE) {
                last_error = WYRM_ERR_INVAL;
            }
        } else if (result.state == WYRM_EXEC_CONTINUE) {
            if (state->pending != WYRM_NULL) {
                pending = state->pending; state->pending = WYRM_NULL;
            } else {
                last_error = WYRM_ERR_INVAL;
            }
        } else if (result.state == WYRM_EXEC_DONE) {
            last_error = wyrm_stack_pop_continuation_f(&self->value_stack, &pending, result.stack_values);
            if (last_error == WYRM_ERR_EMPTY) { last_error = WYRM_ERR_NONE; break; }
            if (!pending) { break; }
        } else {
            last_error = WYRM_ERR_INVAL;
        }
    }
    return last_error;
}
