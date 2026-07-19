#ifndef WYRM_API_IMPL_H_
#define WYRM_API_IMPL_H_

#include <wyrm/types.h>
#include <wyrm/internal_api.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef WYRM_OBJECT_LIST_INITIAL_SZ
#define WYRM_OBJECT_LIST_INITIAL_SZ 4
#endif

/* ------------------------------------------------------------------------- */
/* State Initialization                                                      */
/* ------------------------------------------------------------------------- */

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

WYRM_INLINE wyrm_error wyrm_state_delete(wyrm_state* state)
{
    if (state == WYRM_NULL) { return WYRM_ERR_NONE; }
    wyrm_error last_error = WYRM_ERR_NONE;
    wyrm_allocator* alloc = state->state_alloc_;

    if (wyrm_state_check_flag_f(state, WYRM_STATE_FLAG_OWNS_SELF)) {
        WYRM_ASSERT(alloc != WYRM_NULL);
        wyrm_allocator_free(alloc, state);
    }

    return last_error;
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

/* ------------------------------------------------------------------------- */
/* Operations                                                                */
/* ------------------------------------------------------------------------- */

WYRM_INLINE bool wyrm_op_eq(wyrm_state* state, wyrm_type_tag lhst, wyrm_primitive lhs, wyrm_type_tag rhst, wyrm_primitive rhs)
{
    WYRM_UNUSED(state);
    if (lhst != rhst) { return false; }
    switch (lhst) {
    case WYRM_TYPE_TAG_NIL:
        /* primitive value of nil type ignored at runtime, but should be 0 */
        WYRM_ASSERT(lhs.uword == 0 && rhs.uword == 0);
        return true;
    case WYRM_TYPE_TAG_SYMBOL:
        return lhs.symtab_entry == rhs.symtab_entry;
    case WYRM_TYPE_TAG_UWORD:
        return lhs.uword == rhs.uword;
    case WYRM_TYPE_TAG_WORD:
        return lhs.word == rhs.word;
    case WYRM_TYPE_TAG_STR:
        return wyrm_string_eq_f(lhs.str, rhs.str);
    case WYRM_TYPE_TAG_FUNCTION:
        return lhs.cb == rhs.cb;
    case WYRM_TYPE_TAG_VALUE_PTR:
        return lhs.ptr == rhs.ptr;

    case WYRM_TYPE_TAG_TABLE:
    case WYRM_TYPE_TAG_BOX:
    default:
        // TODO:
        return false;
    }
}

WYRM_INLINE wyrm_uword wyrm_op_hash(wyrm_state* state, wyrm_type_tag vt, wyrm_primitive v)
{
    WYRM_UNUSED(state);
    switch (vt) {
    case WYRM_TYPE_TAG_NIL:
        return 0;

    case WYRM_TYPE_TAG_SYMBOL:
        return (wyrm_uword) v.symtab_entry;

    case WYRM_TYPE_TAG_UWORD:
        return v.uword;

    case WYRM_TYPE_TAG_WORD:
        return (wyrm_uword) v.word;

    case WYRM_TYPE_TAG_STR:
        return wyrm_string_hash_f(v.str);

    default:
        return WYRM_HASH_INVALID;
    }
}


WYRM_INLINE bool wyrm_string_eq_f(wyrm_string* lhs, wyrm_string* rhs)
{
    WYRM_ASSERT(lhs != WYRM_NULL && rhs != WYRM_NULL);
    if (lhs == rhs) { return true; }
    if (lhs->hash != rhs->hash) { return false; }
    if (lhs->len != rhs->len) { return false; }
    if (lhs->len == 0) { return true; }
    return wyrm_strncmp_f(lhs->str, rhs->str, lhs->len) == 0;
}

WYRM_INLINE wyrm_uword wyrm_string_hash_f(wyrm_string* str)
{
    if (!str) { return 0; }
    return str->hash;
}


WYRM_INLINE void wyrm_string_finalize_f(wyrm_context* context, wyrm_string* self)
{
    wyrm_context_gc_free(context, (void*) self->str);
    self->str = WYRM_NULL;
    self->len = 0;
    self->hash = 0;
}

/* ------------------------------------------------------------------------- */
/* GC Info */
/* ------------------------------------------------------------------------- */

WYRM_INLINE void wyrm_gc_info_init_s(wyrm_gc_object* self, wyrm_type_tag gc_type)
{
    self->flags = 0;
    self->gc_type = gc_type;
    self->next = WYRM_NULL;
}

WYRM_INLINE void wyrm_gc_info_finalize_f(wyrm_context* context, wyrm_gc_object* self)
{
    if (!self) { return; }
    self->flags |= WYRM_GC_FLAG_FINALIZED;

    switch (self->gc_type)
    {
    case WYRM_TYPE_TAG_STR:
        wyrm_string_finalize_f(context, (wyrm_string*) self);
        break;

    case WYRM_TYPE_TAG_BOX:
    default:
        break;
    }

}



#ifdef __cplusplus
}
#endif

#endif
