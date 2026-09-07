#ifndef WYRM_OBJECT_H_
#define WYRM_OBJECT_H_

#include <wyrm/fwd.h>
#include <wyrm/value.h>

WYRM_BEGIN_DECLS

// ----------------------------------------------------------------------------
// Object flags
// ----------------------------------------------------------------------------

enum
{
    WYRM_GC_STATIC          = 0x001,
    WYRM_GC_FLAG_MARKED     = 0x004,
    WYRM_GC_FLAG_FINALIZED  = 0x008,
    WYRM_GC_FLAG_RO         = 0x010,
};

#define WY_OBJECT_INITIALIZER(DTYPE) { .dtype = DTYPE, .flags = 0, .next = WYRM_NULL }
#define WY_OBJECT_INITIALIZER_S(DTYPE) { .dtype = DTYPE, .flags = WYRM_GC_STATIC, .next = WYRM_NULL }

/**
 * Generic Garbage Collected Object
 *
 * All objects located on the heap hold this structure as their first member.
 * The wyrm_object_type* determines the interpretation of the remainder of
 * the structure as well as the fixed offset size.
 */
struct wy_object
{
    const wyrm_object_type* dtype;
    struct wy_object* next;
    wyrm_uword flags;
};

/**
 * Initialize an object header for a heap allocated object
 *
 * @param self Object to initialize
 * @param dtype Type describing the object
 */
WYRM_INLINE void wyrm_object_init_header_s(wyrm_object* self, const wyrm_object_type* dtype)
{
    self->dtype = dtype;
    self->flags = 0;
    self->next = WYRM_NULL;
}

/**
 * Initialize an object header for a statically allocated object
 *
 * @param self Object to initialize
 * @param dtype Type describing the object
 */
WYRM_INLINE void wy_object_init_static_f(wyrm_object* self, const wyrm_object_type* dtype)
{
    wyrm_object_init_header_s(self, dtype);
    self->flags |= WYRM_GC_STATIC;
}

WYRM_END_DECLS

#endif
