#ifndef WYRM_OBJECT_TYPE_H_
#define WYRM_OBJECT_TYPE_H_

#include <wyrm/object.h>
#include <wyrm/work_area.h>

WY_BEGIN_DECLS

/**
 * Type descriptor for a garbage collected object
 *
 * Supplies the collector with the operations it needs to finalize an object
 * and to walk the objects it references.
 */
struct wy_object_type
{
    wy_object object;
    wy_type_tag gc_type;

    void (*finalize)(wy_context* context, wy_object* self);

    wy_error (*children_iter_start)(wy_state* state, wy_object* self, wy_work_area* wa);
    wy_error (*children_iter_next)(wy_state* state, wy_object* self, wy_work_area* wa, const wy_object** child);
};

extern const wy_object_type wy_type_type;
extern const wy_object_type wy_type_object;

#define WY_OBJECT_STATIC_INITIALIZER(DTYPE)   { .dtype = DTYPE, .next = WY_NULL, .flags = (WY_GC_STATIC | WY_GC_FLAG_RO)  }
#define WY_OBJECT_TYPE_OBJECT_INIT WY_OBJECT_STATIC_INITIALIZER(&wy_type_type)

/**
 * Run an object's finalizer and mark it finalized
 */
WY_INLINE void wy_object_finalize_f(wy_context* context, wy_object* self)
{
    WY_ASSERT(context != WY_NULL && self != WY_NULL);
    self->flags |= WY_GC_FLAG_FINALIZED;
    if (self->dtype->finalize != WY_NULL) {
        self->dtype->finalize(context, self);
    }
}

/**
 * Begin iterating the objects referenced by `self`
 *
 * @return WY_ERR_INVAL on bad arguments, WY_ERR_NOSUPPORT if the type
 *         does not implement iteration
 */
WY_INLINE wy_error wy_object_children_iter_start(wy_state* state, wy_object* self, wy_work_area* wa)
{
    if (self == WY_NULL || self->dtype == WY_NULL || wa == WY_NULL) { return WY_ERR_INVAL; }
    if (self->dtype->children_iter_start == WY_NULL ||
        self->dtype->children_iter_next == WY_NULL) { return WY_ERR_NOSUPPORT; }
    return self->dtype->children_iter_start(state, self, wa);
}

/**
 * Advance to the next referenced object
 *
 * @return WY_ERR_STOP_ITERATION once the last child has been returned
 */
WY_INLINE wy_error wy_object_children_iter_next_f(wy_state* state, wy_object* self, wy_work_area* wa, const wy_object** object_ptr)
{
    return self->dtype->children_iter_next(state, self, wa, object_ptr);
}

WY_END_DECLS

#endif
