#ifndef WYRM_LIST_H_
#define WYRM_LIST_H_

#include <wyrm/object.h>
#include <wyrm/value.h>

WY_BEGIN_DECLS

struct wy_list;
#ifndef __cplusplus
typedef struct wy_list wy_list;
#endif

extern const wy_object_type wy_list_type;

/**
 * A mutable, resizable sequence of values.
 */
struct wy_list
{
    wy_object object;
    wy_uword count;
    wy_uword capacity;
    wy_value* items;
};

/**
 * Allocate an empty list, optionally reserving `initial_capacity` slots.
 *
 * @return WY_ERR_NONE, WY_ERR_INVAL for a null argument, or WY_ERR_NOMEM
 */
wy_error wy_list_new(wy_context* context, wy_uword initial_capacity, wy_list** out);

/**
 * Append `value` to the end of the list, growing storage as needed.
 *
 * @return WY_ERR_NONE, WY_ERR_INVAL for a null argument, or WY_ERR_NOMEM
 */
wy_error wy_list_push(wy_context* context, wy_list* self, wy_value value);

/**
 * Access element `index`.
 *
 * @return Pointer to the stored value, or WY_NULL when `index` is out of range
 */
WY_INLINE wy_value* wy_list_at_f(wy_list* self, wy_uword index)
{
    if (self == WY_NULL || index >= self->count) { return WY_NULL; }
    return &self->items[index];
}

/**
 * Overwrite element `index`.
 *
 * @return WY_ERR_NONE, WY_ERR_INVAL for a null list, or WY_ERR_RANGE when out of range
 */
wy_error wy_list_set(wy_list* self, wy_uword index, wy_value value);

WY_END_DECLS

#endif
