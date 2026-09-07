#ifndef WYRM_OBJECT_TYPE_H_
#define WYRM_OBJECT_TYPE_H_

#include <wyrm/object.h>
#include <wyrm/work_area.h>

WYRM_BEGIN_DECLS

/**
 * Type descriptor for a garbage collected object
 *
 * Supplies the collector with the operations it needs to finalize an object
 * and to walk the objects it references.
 */
struct wyrm_object_type
{
    wyrm_object object;
    wyrm_type_tag gc_type;

    void (*finalize)(wyrm_context* context, wyrm_object* self);

    wyrm_error (*children_iter_start)(wyrm_state* state, wyrm_object* self, wyrm_work_area* wa);
    wyrm_error (*children_iter_next)(wyrm_state* state, wyrm_object* self, wyrm_work_area* wa, const wyrm_object** child);
};

extern const wyrm_object_type wyrm_type_type;
extern const wyrm_object_type wyrm_type_object;

#define WYRM_OBJECT_STATIC_INITIALIZER(DTYPE)   { .dtype = DTYPE, .next = WYRM_NULL, .flags = (WYRM_GC_STATIC | WYRM_GC_FLAG_RO)  }
#define WYRM_OBJECT_TYPE_OBJECT_INIT WYRM_OBJECT_STATIC_INITIALIZER(&wyrm_type_type)

/**
 * Run an object's finalizer and mark it finalized
 */
WYRM_INLINE void wyrm_object_finalize_f(wyrm_context* context, wyrm_object* self)
{
    WYRM_ASSERT(context != WYRM_NULL && self != WYRM_NULL);
    self->flags |= WYRM_GC_FLAG_FINALIZED;
    if (self->dtype->finalize != WYRM_NULL) {
        self->dtype->finalize(context, self);
    }
}

/**
 * Begin iterating the objects referenced by `self`
 *
 * @return WYRM_ERR_INVAL on bad arguments, WYRM_ERR_NOSUPPORT if the type
 *         does not implement iteration
 */
WYRM_INLINE wyrm_error wyrm_object_children_iter_start(wyrm_state* state, wyrm_object* self, wyrm_work_area* wa)
{
    if (self == WYRM_NULL || self->dtype == WYRM_NULL || wa == WYRM_NULL) { return WYRM_ERR_INVAL; }
    if (self->dtype->children_iter_start == WYRM_NULL ||
        self->dtype->children_iter_next == WYRM_NULL) { return WYRM_ERR_NOSUPPORT; }
    return self->dtype->children_iter_start(state, self, wa);
}

/**
 * Advance to the next referenced object
 *
 * @return WYRM_ERR_STOP_ITERATION once the last child has been returned
 */
WYRM_INLINE wyrm_error wyrm_object_children_iter_next_f(wyrm_state* state, wyrm_object* self, wyrm_work_area* wa, const wyrm_object** object_ptr)
{
    return self->dtype->children_iter_next(state, self, wa, object_ptr);
}

WYRM_END_DECLS

#endif
