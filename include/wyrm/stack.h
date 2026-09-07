#ifndef WYRM_STACK_INL_H_
#define WYRM_STACK_INL_H_

#include <wyrm/types.h>
#include <wyrm/sys/string.h>
#include <wyrm/value.h>

WY_BEGIN_DECLS

/**
 * @struct wy_stack
 *
 * Stack primitive for the Wyrm interpreter. Stack space is defined by a
 * pointer range (entries_begin, entries_end); Stack grows upward from
 * begin toward end.
 */
struct wy_stack
{
    wy_value* entries_begin;
    wy_value* entries_end;

    wy_value* base;
    wy_value* top;
};

WY_INLINE wy_error wy_stack_pop_discard_f(wy_stack* self, wy_uword count);

/**
 * Initialize stack
 *
 * @param self Point to the stack area to initialize
 * @param memory Allocated array of wy_value
 * @param capacity Size of allocated array
 */
WY_INLINE void wy_stack_init_f(wy_stack* self, wy_value* memory, wy_uword capacity)
{
    WY_ASSERT(self != WY_NULL && memory != WY_NULL && capacity > 0);
    self->entries_begin = memory;
    self->entries_end   = memory + capacity;
    self->base          = memory;
    self->top           = memory;
}

/**
 * Get current base pointer
 */
WY_INLINE wy_value* wy_stack_base_f(wy_stack* self)
{
    WY_ASSERT(self != WY_NULL);
    return self->base;
}

/**
 * Get current top pointer
 */
WY_INLINE wy_value* wy_stack_top_f(wy_stack* self)
{
    WY_ASSERT(self != WY_NULL);
    return self->top;
}

/**
 * Set the base pointer without moving any values
 *
 * @param self Stack
 * @param base New base, within the stack's entry range
 */
WY_INLINE void wy_stack_base_restore_f(wy_stack* self, wy_value* base)
{
    WY_ASSERT(self != WY_NULL && base != WY_NULL);
    WY_ASSERT(base >= self->entries_begin && base <= self->entries_end);
    self->base = base;
}


/**
 * Get available capacity of stack
 *
 * @param self Pointer to the stack
 */
WY_INLINE wy_uword wy_stack_capacity_remaining_f(wy_stack* self)
{
    WY_ASSERT(self != WY_NULL);
    return self->entries_end - self->top;
}

/**
 * Get total argument on stack
 */
WY_INLINE wy_uword wy_stack_arg_count_f(wy_stack* self)
{
    WY_ASSERT(self != WY_NULL);
    return (wy_uword) (self->top - self->base);
}


/**
 * Retore active frame with argument preservation
 */
WY_INLINE wy_error wy_stack_base_reset_args_f(wy_stack* self, wy_value* base, wy_uword arg_count)
{
    WY_ASSERT(self != WY_NULL && base != WY_NULL);
    WY_ASSERT(base >= self->entries_begin && base <= self->base);

    wy_uword orig_arg_count = wy_stack_arg_count_f(self);
    if (orig_arg_count < arg_count) { return WY_ERR_RANGE; }

    if (orig_arg_count != arg_count) {
        wy_value* dest_base = self->base;
        wy_memmove(dest_base, self->top - arg_count, arg_count * sizeof(wy_value));
        self->top = dest_base + arg_count;
    }

    self->base = base;
    return WY_ERR_NONE;
}


WY_INLINE wy_error wy_stack_pop_to_value_count_f(wy_stack* self, wy_uword count)
{
    WY_ASSERT(self != WY_NULL);

    wy_uword current_count = wy_stack_arg_count_f(self);
    if (current_count < count) { return WY_ERR_RANGE; }

    wy_uword reduce_by = current_count - count;
    return wy_stack_pop_discard_f(self, reduce_by);
}

/**
 * Push value to the stack
 *
 * @param self Stack object
 * @param type_ref Type of primitive being pushed
 * @param value Primitive value
 * @return WY_ERR_NONE on success, WY_ERR_STACK_OVERFLOW if stack is full
 */
WY_INLINE wy_error wy_stack_push_f(wy_stack* self, wy_type_tag type_ref, wy_primitive value)
{
    WY_ASSERT(self != WY_NULL);
    if (self->top >= self->entries_end) { return WY_ERR_STACK_OVERFLOW; }
    self->top->type = type_ref;
    self->top->data = value;
    self->top++;
    return WY_ERR_NONE;
}


/**
 * Pop value from the stack
 *
 * @param self Stack to pop value from
 * @param type_ref Out variable for type
 * @param value Out variable for value
 * @return WY_ERR_NONE on success
 */
WY_INLINE wy_error wy_stack_pop(wy_stack* self, wy_type_tag* type_ref, wy_primitive* value)
{
    if (self == WY_NULL) { return WY_ERR_INVAL; }
    if (self->top <= self->entries_begin) { return WY_ERR_EMPTY; }
    self->top--;
    if (type_ref != WY_NULL) { *type_ref = self->top->type; }
    if (value != WY_NULL)    { *value    = self->top->data; }
    return WY_ERR_NONE;
}

/**
 * Pop values from the stack and discard
 *
 * @param self Stack to pop value from
 * @param count Total elements to discard
 * @return WY_ERR_NONE on success
 */
WY_INLINE wy_error wy_stack_pop_discard_f(wy_stack* self, wy_uword count)
{
    WY_ASSERT(self != WY_NULL);

    ptrdiff_t count_available = self->top - self->entries_begin;
    if (count_available < 0 || (wy_uword) count_available < count) {
        return WY_ERR_RANGE;
    }
    self->top -= count;
    return WY_ERR_NONE;
}

/**
 * Replace the top of the stack.
 * @memberof wy_stack
 */
WY_INLINE wy_error wy_stack_replace_frame_f(wy_stack* self, wy_uword preserve_count)
{
    WY_ASSERT(self != WY_NULL);

    if ((wy_uword)(self->top - self->base) < preserve_count) { return WY_ERR_RANGE; }
    wy_memmove(self->base, self->top - preserve_count, preserve_count * sizeof(wy_value));
    self->top = self->base + preserve_count;
    return WY_ERR_NONE;
}

/**
 * Push array of values to the stack.
 * @memberof wy_stack
 */
WY_INLINE wy_error wy_stack_push_array_f(wy_stack* self, const wy_value* array, wy_uword count)
{
    WY_ASSERT(self != WY_NULL);
    WY_ASSERT(count == 0 || array != WY_NULL);

    if (count == 0) { return WY_ERR_NONE; }
    if ((wy_uword)(self->entries_end - self->top) < count) { return WY_ERR_STACK_OVERFLOW; }
    wy_memmove(self->top, array, count * sizeof(wy_value));
    self->top += count;
    return WY_ERR_NONE;
}

WY_END_DECLS

#endif
