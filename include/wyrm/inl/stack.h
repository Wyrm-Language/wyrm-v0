#ifndef WYRM_INL_STACK_INL_H_
#define WYRM_INL_STACK_INL_H_

#include <wyrm/types.h>
#include <wyrm/sys/string.h>

WYRM_BEGIN_DECLS

/**
 * Initialize stack
 *
 * @param self Point to the stack area to initialize
 * @param memory Allocated array of wyrm_value
 * @param capacity Size of allocated array
 */
WYRM_INLINE void wyrm_stack_init_f(wyrm_stack* self, wyrm_value* memory, wyrm_uword capacity)
{
    WYRM_ASSERT(self != WYRM_NULL && memory != WYRM_NULL && capacity > 0);
    self->entries_begin = memory;
    self->entries_end   = memory + capacity;
    self->base          = memory;
    self->top           = memory;
}


/**
 * Get available capacity of stack
 *
 * @param self Pointer to the stack
 */
WYRM_INLINE wyrm_uword wyrm_stack_capacity_remaining_f(wyrm_stack* self)
{
    WYRM_ASSERT(self != WYRM_NULL);
    return self->entries_end - self->top;
}

/**
 * Get total argument on stack
 */
WYRM_INLINE wyrm_uword wyrm_stack_arg_count_f(wyrm_stack* self)
{
    WYRM_ASSERT(self != WYRM_NULL);
    return (wyrm_uword) (self->top - self->base);
}


/**
 * Push value to the stack
 *
 * @param self Stack object
 * @param type_ref Type of primitive being pushed
 * @param value Primitive value
 * @return WYRM_ERR_NONE on success, WYRM_ERR_STACK_OVERFLOW if stack is full
 */
WYRM_INLINE wyrm_error wyrm_stack_push_f(wyrm_stack* self, wyrm_type_tag type_ref, wyrm_primitive value)
{
    WYRM_ASSERT(self != WYRM_NULL);
    if (self->top >= self->entries_end) { return WYRM_ERR_STACK_OVERFLOW; }
    self->top->type = type_ref;
    self->top->data = value;
    self->top++;
    return WYRM_ERR_NONE;
}


/**
 * Pop value from the stack
 *
 * @param self Stack to pop value from
 * @param type_ref Out variable for type
 * @param value Out variable for value
 * @return WYRM_ERR_NONE on success
 */
WYRM_INLINE wyrm_error wyrm_stack_pop(wyrm_stack* self, wyrm_type_tag* type_ref, wyrm_primitive* value)
{
    if (self == WYRM_NULL) { return WYRM_ERR_INVAL; }
    if (self->top <= self->entries_begin) { return WYRM_ERR_EMPTY; }
    self->top--;
    if (type_ref != WYRM_NULL) { *type_ref = self->top->type; }
    if (value != WYRM_NULL)    { *value    = self->top->data; }
    return WYRM_ERR_NONE;
}

/**
 * Pop values from the stack and discard
 *
 * @param self Stack to pop value from
 * @param count Total elements to discard
 * @return WYRM_ERR_NONE on success
 */
WYRM_INLINE wyrm_error wyrm_stack_pop_discard_f(wyrm_stack* self, wyrm_uword count)
{
    WYRM_ASSERT(self != WYRM_NULL);

    ptrdiff_t count_available = self->top - self->entries_begin;
    if (count_available < 0 || (wyrm_uword) count_available < count) {
        return WYRM_ERR_RANGE;
    }
    self->top -= count;
    return WYRM_ERR_NONE;
}

/**
 * Push continuation to the stack.
 *
 * Set the next continuation for the next set of results. The base pointer
 * marks the space where arguments to the continuation are stored.
 *
 * @memberof wyrm_stack
 *
 * @param self Stack
 * @param cont_fn Required function to invoke when this frame's result is ready
 * @return WYRM_ERR_NONE on success, WYRM_ERR_STACK_OVERFLOW if full
 */
WYRM_INLINE wyrm_error wyrm_stack_push_continuation_f(wyrm_stack* self, wyrm_exec_fn cont_fn)
{
    WYRM_ASSERT(self != WYRM_NULL && cont_fn != WYRM_NULL);

    wyrm_value* saved_base = self->top;
    if (saved_base >= self->entries_end) { return WYRM_ERR_STACK_OVERFLOW; }

    wyrm_value* cont = saved_base + 1;
    if (cont >= self->entries_end) { return WYRM_ERR_STACK_OVERFLOW; }

    // base[-2]: saved base pointer
    saved_base->type = WYRM_TYPE_TAG_VALUE_PTR;
    saved_base->data.value_ptr = self->base;

    // base[-1]: continuation
    cont->type = WYRM_TYPE_TAG_FRAME;
    cont->data.cb = cont_fn;

    self->top  = cont + 1;
    self->base = self->top;
    return WYRM_ERR_NONE;
}


/**
 * Pop a stack frame, transferring last N values to the new stack top
 *
 * Restores the base pointer saved by wyrm_stack_push_continuation_f, then
 * moves preserve_count values from the top of the current frame to where the
 * saved-base-pointer slot was, collapsing the frame.
 *
 * @memberof wyrm_stack
 *
 * @param self           Stack
 * @param preserve_count Number of top-of-frame values to keep as results
 * @return WYRM_ERR_NONE on success
 *         WYRM_ERR_INVAL if top stack values invalid
 *         WYRM_ERR_RANGE if current frame holds fewer than preserve_count values
 */
WYRM_INLINE wyrm_error wyrm_stack_pop_continuation_f(wyrm_stack* self, wyrm_exec_fn* out_continuation, wyrm_uword preserve_count)
{
    WYRM_ASSERT(self != WYRM_NULL && out_continuation != WYRM_NULL);
    if ((wyrm_uword)(self->top - self->base) < preserve_count) { return WYRM_ERR_RANGE; }
    if (self->base <= self->entries_begin) { return WYRM_ERR_EMPTY; }
    wyrm_value* cont = self->base - 1;
    if (cont <= self->entries_begin) { return WYRM_ERR_EMPTY; }
    wyrm_value* old_base_value = cont - 1;

    // Verify expected type and save (stack corruption otherwise)
    if (old_base_value->type != WYRM_TYPE_TAG_VALUE_PTR) { return WYRM_ERR_INVAL; }
    wyrm_value* old_base = old_base_value->data.value_ptr;

    if (cont->type != WYRM_TYPE_TAG_FRAME) { return WYRM_ERR_INVAL; }
    *out_continuation = cont->data.cb;

    // Grab base to restore and find continuation
    wyrm_memmove(old_base_value, self->top - preserve_count, preserve_count * sizeof(wyrm_value));
    self->top  = old_base_value + preserve_count;
    self->base = old_base;
    return WYRM_ERR_NONE;
}

/**
 * Replace the top of the stack.
 * @memberof wyrm_stack
 */
WYRM_INLINE wyrm_error wyrm_stack_replace_frame_f(wyrm_stack* self, wyrm_uword preserve_count)
{
    WYRM_ASSERT(self != WYRM_NULL);

    if ((wyrm_uword)(self->top - self->base) < preserve_count) { return WYRM_ERR_RANGE; }
    wyrm_memmove(self->base, self->top - preserve_count, preserve_count * sizeof(wyrm_value));
    self->top = self->base + preserve_count;
    return WYRM_ERR_NONE;
}

/**
 * Push array of values to the stack.
 * @memberof wyrm_stack
 */
WYRM_INLINE wyrm_error wyrm_stack_push_array_f(wyrm_stack* self, const wyrm_value* array, wyrm_uword count)
{
    WYRM_ASSERT(self != WYRM_NULL);
    WYRM_ASSERT(count == 0 || array != WYRM_NULL);

    if (count == 0) { return WYRM_ERR_NONE; }
    if ((wyrm_uword)(self->entries_end - self->top) < count) { return WYRM_ERR_STACK_OVERFLOW; }
    wyrm_memmove(self->top, array, count * sizeof(wyrm_value));
    self->top += count;
    return WYRM_ERR_NONE;
}

WYRM_END_DECLS

#endif
