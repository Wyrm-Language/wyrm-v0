#ifndef WYRM_TUPLE_H_
#define WYRM_TUPLE_H_

#include <wyrm/object.h>
#include <wyrm/value.h>

WY_BEGIN_DECLS

struct wy_tuple;
#ifndef __cplusplus
typedef struct wy_tuple wy_tuple;
#endif

extern const wy_object_type wy_tuple_type;

/**
 * An immutable, fixed-size sequence of values.
 */
struct wy_tuple
{
    wy_object object;
    wy_uword count;
    wy_value items[];
};

/**
 * Allocate a tuple holding a copy of `items[0..count)`.
 *
 * @return WY_ERR_NONE, WY_ERR_INVAL for a null argument (when count > 0 and
 *   items is null, or out is null), or WY_ERR_NOMEM
 */
wy_error wy_tuple_new(wy_context* context, const wy_value* items, wy_uword count, wy_tuple** out);

WY_END_DECLS

#endif
