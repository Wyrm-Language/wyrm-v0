#ifndef WYRM_STACK_INL_H_
#define WYRM_STACK_INL_H_

#include <wyrm/types.h>
#include <wyrm/sys/string.h>
#include <wyrm/value.h>

WYRM_BEGIN_DECLS

/**
 * @struct wy_stack
 *
 * Stack primitive for the Wyrm interpreter. Stack space is defined by a
 * pointer range (entries_begin, entries_end); Stack grows upward from
 * begin toward end.
 */
struct wy_stack
{
    wyrm_value* entries_begin;
    wyrm_value* entries_end;

    wyrm_value* base;
    wyrm_value* top;
};

WYRM_INLINE wyrm_error wyrm_stack_pop_discard_f(wyrm_stack* self, wyrm_uword count);

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
 * Get current base pointer
 */
WYRM_INLINE wyrm_value* wyrm_stack_base_f(wyrm_stack* self)
{
    WYRM_ASSERT(self != WYRM_NULL);
    return self->base;
}

/**
 * Get current top pointer
 */
WYRM_INLINE wyrm_value* wyrm_stack_top_f(wyrm_stack* self)
{
    WYRM_ASSERT(self != WYRM_NULL);
    return self->top;
}

/**
 * Set the base pointer without moving any values
 *
 * @param self Stack
 * @param base New base, within the stack's entry range
 */
WYRM_INLINE void wyrm_stack_base_restore_f(wyrm_stack* self, wyrm_value* base)
{
    WYRM_ASSERT(self != WYRM_NULL && base != WYRM_NULL);
    WYRM_ASSERT(base >= self->entries_begin && base <= self->entries_end);
    self->base = base;
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
 * Retore active frame with argument preservation
 */
WYRM_INLINE wy_error wy_stack_base_reset_args_f(wyrm_stack* self, wyrm_value* base, wy_uword arg_count)
{
    WYRM_ASSERT(self != WYRM_NULL && base != WYRM_NULL);
    WYRM_ASSERT(base >= self->entries_begin && base <= self->base);

    wy_uword orig_arg_count = wyrm_stack_arg_count_f(self);
    if (orig_arg_count < arg_count) { return WYRM_ERR_RANGE; }

    if (orig_arg_count != arg_count) {
        wyrm_value* dest_base = self->base;
        wyrm_memmove(dest_base, self->top - arg_count, arg_count * sizeof(wyrm_value));
        self->top = dest_base + arg_count;
    }

    self->base = base;
    return WYRM_ERR_NONE;
}


WYRM_INLINE wyrm_error wyrm_stack_pop_to_value_count_f(wyrm_stack* self, wyrm_uword count)
{
    WYRM_ASSERT(self != WYRM_NULL);

    wyrm_uword current_count = wyrm_stack_arg_count_f(self);
    if (current_count < count) { return WYRM_ERR_RANGE; }

    wyrm_uword reduce_by = current_count - count;
    return wyrm_stack_pop_discard_f(self, reduce_by);
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
