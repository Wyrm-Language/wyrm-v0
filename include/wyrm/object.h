#ifndef WYRM_OBJECT_H_
#define WYRM_OBJECT_H_

#include <wyrm/fwd.h>
#include <wyrm/value.h>

WY_BEGIN_DECLS

// ----------------------------------------------------------------------------
// Object flags
// ----------------------------------------------------------------------------

enum
{
    WY_GC_STATIC          = 0x001,
    WY_GC_FLAG_MARKED     = 0x004,
    WY_GC_FLAG_FINALIZED  = 0x008,
    WY_GC_FLAG_RO         = 0x010,
};

#define WY_OBJECT_INITIALIZER(DTYPE) { .dtype = DTYPE, .flags = 0, .next = WY_NULL }
#define WY_OBJECT_INITIALIZER_S(DTYPE) { .dtype = DTYPE, .flags = WY_GC_STATIC, .next = WY_NULL }

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

WY_END_DECLS

#endif
