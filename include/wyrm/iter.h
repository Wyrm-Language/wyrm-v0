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
    /* Range iterators (`range(begin, end)`): `current` holds the next
     * integer to yield, `limit` the exclusive end. No source object. */
    wy_word limit;
    bool is_range;
};

extern const wy_object_type wy_iterator_type;

/** Iterate `source`. An iterator is its own iterator (the same object). */
wy_error wy_iterator_new(wy_context* context, wy_value source, wy_iterator** out);
/** The integers begin, begin+1, ..., end-1 (empty when begin >= end). */
wy_error wy_iterator_new_range(wy_context* context, wy_word begin, wy_word end, wy_iterator** out);
wy_error wy_iterator_next(wy_context* context, wy_iterator* self, wy_value* out);

WY_END_DECLS

#endif
