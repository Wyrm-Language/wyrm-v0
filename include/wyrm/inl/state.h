#ifndef WYRM_INL_STATE_INL_H_
#define WYRM_INL_STATE_INL_H_

#include <wyrm.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
 * Initialize state with no active process.
 */
WYRM_INLINE void wyrm_state_init_s(wyrm_state* state)
{
    state->state_flags = 0;
    state->machine = WYRM_NULL;
    state->context = WYRM_NULL;
    state->fiber = WYRM_NULL;
    state->state_alloc_ = WYRM_NULL;
}


/**
 * Initialize temporary state from a context
 *
 * @param state State object
 * @param context Context data
 */
WYRM_INLINE void wyrm_state_init_from_context_f(wyrm_state* state, wyrm_context* context)
{
    wyrm_state_init_s(state);
    state->machine = wyrm_context_get_machine(context);
    state->context = context;
}

/**
 * Initialize state from a context
 */
WYRM_INLINE void wyrm_state_init_context_f(wyrm_state* state, wyrm_context* context)
{
    WYRM_ASSERT(state != WYRM_NULL && context != WYRM_NULL);
    wyrm_state_init_s(state);
    state->machine = wyrm_context_get_machine(context);
    state->context = context;
}

/**
 * Construct New State
 */
WYRM_INLINE wyrm_state* wyrm_state_new(wyrm_allocator* alloc)
{
    wyrm_state* state = (wyrm_state*) wyrm_allocator_alloc(alloc, sizeof(wyrm_state));
    if (state == WYRM_NULL) { return WYRM_NULL; }
    wyrm_state_init_s(state);
    state->state_flags |= (wyrm_uword) WYRM_STATE_FLAG_OWNS_SELF;
    state->state_alloc_ = alloc;
    return state;
}

WYRM_INLINE void wyrm_state_delete(wyrm_state* state)
{
    if (state == WYRM_NULL) { return; }
    wyrm_allocator* alloc = state->state_alloc_;

    if (wyrm_state_check_flag_f(state, WYRM_STATE_FLAG_OWNS_SELF)) {
        WYRM_ASSERT(alloc != WYRM_NULL);
        wyrm_allocator_free(alloc, state);
    }
}

WYRM_INLINE bool wyrm_state_check_flag_f(wyrm_state* state, wyrm_state_flag flag)
{
    WYRM_ASSERT(state != WYRM_NULL);
    return (state->state_flags & ((wyrm_uword) flag)) != 0;
}

WYRM_INLINE wyrm_uword wyrm_state_value_count(wyrm_state* state)
{
    if (state == WYRM_NULL || state->fiber == WYRM_NULL) { return 0; }
    return wyrm_fiber_value_count_f(state->fiber);
}

WYRM_INLINE wyrm_error wyrm_state_pop_to_value_count(wyrm_state* state, wyrm_uword count)
{
    if (state == WYRM_NULL || state->fiber == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return wyrm_fiber_pop_to_value_count_f(state->fiber, count);
}

WYRM_INLINE wyrm_value* wyrm_state_value_n(wyrm_state* state, wyrm_uword index)
{
    if (index >= wyrm_state_value_count(state)) { return WYRM_NULL; }
    return wyrm_fiber_value_n(state->fiber, index);
}

WYRM_INLINE wyrm_error wyrm_state_push(wyrm_state* state, wyrm_value value)
{
    if (state == WYRM_NULL || state->fiber == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return wyrm_fiber_push_value_f(state->fiber, value);
}

WYRM_INLINE wyrm_error wyrm_state_push_return(wyrm_state* state, wyrm_value value)
{
    if (state == WYRM_NULL || state->fiber == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return wyrm_fiber_push_return_f(state->fiber, value);
}

WYRM_INLINE wyrm_error wyrm_state_set_pending(wyrm_state* state, wyrm_exec_fn pending)
{
    if (state == WYRM_NULL) { return WYRM_ERR_INVAL; }
    if (state->fiber == WYRM_NULL) { return WYRM_ERR_INVAL; }
    if (state->fiber->pending != WYRM_NULL) { return WYRM_ERR_BUSY; }
    state->fiber->pending = pending;
    return WYRM_ERR_NONE;
}

WYRM_INLINE wyrm_error wyrm_state_call_continue(wyrm_state* state, wyrm_exec_fn result_cb, wyrm_exec_fn fn, const wyrm_value* args, wyrm_uword arg_count)
{
    if (state == WYRM_NULL || state->fiber == WYRM_NULL) { return WYRM_ERR_INVAL; }
    if (arg_count > 0 && args == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return wyrm_fiber_exec_continue_f(state->fiber, result_cb, fn, args, arg_count);
}



#ifdef __cplusplus
}
#endif

#endif
