#ifndef WYRM_INL_STATE_INL_H_
#define WYRM_INL_STATE_INL_H_

#include <wyrm.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
 * Initialize state with no active process.
 */
WY_INLINE void wy_state_init_s(wy_state* state)
{
    state->state_flags = 0;
    state->machine = WY_NULL;
    state->context = WY_NULL;
    state->fiber = WY_NULL;
    state->state_alloc_ = WY_NULL;
}


/**
 * Initialize temporary state from a context
 *
 * @param state State object
 * @param context Context data
 */
WY_INLINE void wy_state_init_from_context_f(wy_state* state, wy_context* context)
{
    wy_state_init_s(state);
    state->machine = wy_context_get_machine(context);
    state->context = context;
}

/**
 * Initialize state from a context
 */
WY_INLINE void wy_state_init_context_f(wy_state* state, wy_context* context)
{
    WY_ASSERT(state != WY_NULL && context != WY_NULL);
    wy_state_init_s(state);
    state->machine = wy_context_get_machine(context);
    state->context = context;
}

/**
 * Construct New State
 */
WY_INLINE wy_state* wy_state_new(wy_allocator* alloc)
{
    wy_state* state = (wy_state*) wy_allocator_alloc(alloc, sizeof(wy_state));
    if (state == WY_NULL) { return WY_NULL; }
    wy_state_init_s(state);
    state->state_flags |= (wy_uword) WY_STATE_FLAG_OWNS_SELF;
    state->state_alloc_ = alloc;
    return state;
}

WY_INLINE void wy_state_delete(wy_state* state)
{
    if (state == WY_NULL) { return; }
    wy_allocator* alloc = state->state_alloc_;

    if (wy_state_check_flag_f(state, WY_STATE_FLAG_OWNS_SELF)) {
        WY_ASSERT(alloc != WY_NULL);
        wy_allocator_free(alloc, state);
    }
}

WY_INLINE bool wy_state_check_flag_f(wy_state* state, wy_state_flag flag)
{
    WY_ASSERT(state != WY_NULL);
    return (state->state_flags & ((wy_uword) flag)) != 0;
}

WY_INLINE wy_uword wy_state_value_count(wy_state* state)
{
    if (state == WY_NULL || state->fiber == WY_NULL) { return 0; }
    return wy_fiber_value_count_f(state->fiber);
}

WY_INLINE wy_error wy_state_pop_to_value_count(wy_state* state, wy_uword count)
{
    if (state == WY_NULL || state->fiber == WY_NULL) { return WY_ERR_INVAL; }
    return wy_fiber_pop_to_value_count_f(state->fiber, count);
}

WY_INLINE wy_value* wy_state_value_n(wy_state* state, wy_uword index)
{
    if (index >= wy_state_value_count(state)) { return WY_NULL; }
    return wy_fiber_value_n(state->fiber, index);
}

WY_INLINE wy_error wy_state_push(wy_state* state, wy_value value)
{
    if (state == WY_NULL || state->fiber == WY_NULL) { return WY_ERR_INVAL; }
    return wy_fiber_push_value_f(state->fiber, value);
}

WY_INLINE wy_error wy_state_push_return(wy_state* state, wy_value value)
{
    if (state == WY_NULL || state->fiber == WY_NULL) { return WY_ERR_INVAL; }
    return wy_fiber_push_return_f(state->fiber, value);
}

WY_INLINE wy_error wy_state_set_pending(wy_state* state, wy_exec_fn pending)
{
    if (state == WY_NULL) { return WY_ERR_INVAL; }
    if (state->fiber == WY_NULL) { return WY_ERR_INVAL; }
    if (state->fiber->pending != WY_NULL) { return WY_ERR_BUSY; }
    state->fiber->pending = pending;
    return WY_ERR_NONE;
}

WY_INLINE wy_error wy_state_call_continue(wy_state* state, wy_exec_fn result_cb, wy_exec_fn fn, const wy_value* args, wy_uword arg_count)
{
    if (state == WY_NULL || state->fiber == WY_NULL) { return WY_ERR_INVAL; }
    if (arg_count > 0 && args == WY_NULL) { return WY_ERR_INVAL; }
    return wy_fiber_exec_continue_f(state->fiber, result_cb, fn, args, arg_count);
}



#ifdef __cplusplus
}
#endif

#endif
