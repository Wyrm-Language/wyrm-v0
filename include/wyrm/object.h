#ifndef WYRM_OBJECT_H_
#define WYRM_OBJECT_H_

#include <wyrm/fwd.h>
#include <wyrm/gc_flags.h>
#include <wyrm/primitive.h>

#define WY_OBJECT_INITIALIZER(DTYPE) { .dtype = DTYPE, .flags = 0, .next = WY_NULL }
#define WY_OBJECT_INITIALIZER_S(DTYPE) { .dtype = DTYPE, .flags = WY_GC_STATIC, .next = WY_NULL }
#define WY_OBJECT_STATIC_INITIALIZER(DTYPE)   { .dtype = DTYPE, .next = WY_NULL, .flags = (WY_GC_STATIC | WY_GC_FLAG_RO)  }
#define WY_OBJECT_TYPE_OBJECT_INIT WY_OBJECT_STATIC_INITIALIZER(&wy_type_type)

WY_BEGIN_DECLS

// ----------------------------------------------------------------------------
// Object flags
// ----------------------------------------------------------------------------

WY_INLINE void wy_object_init_header_s(wy_object* self, const wy_object_type* dtype);
WY_INLINE void wy_object_finalize_f(wy_context* context, wy_object* self);
WY_INLINE wy_error wy_object_children_iter_start(wy_context* context, wy_object* self, wy_work_area* wa);
WY_INLINE wy_error wy_object_children_iter_next_f(wy_context* context, wy_object* self, wy_work_area* wa, const wy_object** object_ptr);



/**
 * Generic Garbage Collected Object
 *
 * All objects located on the heap hold this structure as their first member.
 * The wy_object_type* determines the interpretation of the remainder of
 * the structure as well as the fixed offset size.
 */
struct wy_object
{
    const wy_object_type* dtype;
    wy_object* next;
    wy_uword flags;
};

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

    wy_error (*children_iter_start)(wy_context* context, wy_object* self, wy_work_area* wa);
    wy_error (*children_iter_next)(wy_context* context, wy_object* self, wy_work_area* wa, const wy_object** child);
};

extern const wy_object_type wy_type_type;
extern const wy_object_type wy_type_object;

/**
 * Initialize an object header for a heap allocated object
 *
 * @param self Object to initialize
 * @param dtype Type describing the object
 */
WY_INLINE void wy_object_init_header_s(wy_object* self, const wy_object_type* dtype)
{
    self->dtype = dtype;
    self->flags = 0;
    self->next = WY_NULL;
}

/**
 * Initialize an object header for a statically allocated object
 *
 * @param self Object to initialize
 * @param dtype Type describing the object
 */
WY_INLINE void wy_object_init_static_f(wy_object* self, const wy_object_type* dtype)
{
    wy_object_init_header_s(self, dtype);
    self->flags |= WY_GC_STATIC;
}


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
WY_INLINE wy_error wy_object_children_iter_start(wy_context* context, wy_object* self, wy_work_area* wa)
{
    if (self == WY_NULL || self->dtype == WY_NULL || wa == WY_NULL) { return WY_ERR_INVAL; }
    if (self->dtype->children_iter_start == WY_NULL ||
        self->dtype->children_iter_next == WY_NULL) { return WY_ERR_NOSUPPORT; }
    return self->dtype->children_iter_start(context, self, wa);
}

/**
 * Advance to the next referenced object
 *
 * @return WY_ERR_STOP_ITERATION once the last child has been returned
 */
WY_INLINE wy_error wy_object_children_iter_next_f(wy_context* context, wy_object* self, wy_work_area* wa, const wy_object** object_ptr)
{
    return self->dtype->children_iter_next(context, self, wa, object_ptr);
}

WY_END_DECLS

#endif
