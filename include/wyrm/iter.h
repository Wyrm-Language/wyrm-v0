#ifndef WYRM_ITER_H_
#define WYRM_ITER_H_

#include <wyrm/object.h>
#include <wyrm/value.h>

WY_BEGIN_DECLS

struct wy_iterator
{
    wy_object object;
    wy_value source;
    wy_value current;
    wy_uword state;
};

extern const wy_object_type wy_iterator_type;

wy_error wy_iterator_new(wy_context* context, wy_value source, wy_iterator** out);
wy_error wy_iterator_next(wy_context* context, wy_iterator* self, wy_value* out);

WY_END_DECLS

#endif
