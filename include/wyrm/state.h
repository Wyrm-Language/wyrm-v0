#ifndef WYRM_STATE_H_
#define WYRM_STATE_H_

#include <wyrm/fwd.h>
#include <wyrm/sys/toolchain.h>
#include <wyrm/fiber.h>
#include <wyrm/allocator.h>

/* ------------------------------------------------------------------------- */
/* State API                                                                 */
/* ------------------------------------------------------------------------- */

WY_BEGIN_DECLS

typedef enum wy_state_flag_tag
{
    WY_STATE_FLAG_OWNS_SELF       = 0x0001,
} wy_state_flag;


/**
 * @brief State Definition for all Interpreter/Object Calls
 *
 * This structure is intended to live on the stack or heap. Always initialize
 * using the wy_state_init_* functions - do not bitwise copy. State objects
 * allow bidirectional communication of complex VM information.
 *
 * Any call that requires memory allocation or interaction with the virtual
 * machine shall utilize a wy_state* as the first parameter to the function.
 * This explicitly defines the current machine, context, and fiber.
 *
 */
struct wy_state
{
    wy_uword state_flags;
    wy_machine* machine;
    wy_context* context;
    wy_fiber* fiber;

    wy_allocator* state_alloc_;
};


WY_INLINE void wy_state_init_s(wy_state* state);
WY_INLINE wy_state* wy_state_new(wy_allocator* mem);
WY_INLINE void wy_state_delete(wy_state* state);
WY_INLINE bool wy_state_check_flag_f(wy_state* state, wy_state_flag flag);

wy_error wy_state_exec(wy_state* state);
WY_INLINE wy_uword wy_state_value_count(wy_state* state);
WY_INLINE wy_value* wy_state_value_n(wy_state* state, wy_uword idx);
WY_INLINE wy_error wy_state_push(wy_state* state, wy_value value);

WY_INLINE wy_error wy_state_set_pending(wy_state* state, wy_exec_fn pending);
WY_INLINE wy_error wy_state_call_continue(wy_state* state, wy_exec_fn result_cb, wy_exec_fn fn, const wy_value* args, wy_uword arg_count);



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



WY_END_DECLS

#endif
