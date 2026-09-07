#ifndef WYRM_WCORE_H_
#define WYRM_WCORE_H_

#include <wyrm/sys/toolchain.h>
#include <wyrm/sys/atomics.h>
#include <wyrm/sys/errors.h>
#include <wyrm/sys/thread_id.h>

#include <wyrm/fwd.h>
#include <wyrm/primitive.h>
#include <wyrm/value.h>
#include <wyrm/object.h>
#include <wyrm/object_type.h>
#include <wyrm/work_area.h>

WY_BEGIN_DECLS

// ----------------------------------------------------------------------------
// Macros
// ----------------------------------------------------------------------------

#define WY_HASH_INVALID WY_UWORD_MAX
#define WY_SLOT_INVALID WY_UWORD_MAX

// ----------------------------------------------------------------------------
// Prototype
// ----------------------------------------------------------------------------
#define WY_BAD_SLOT WY_UWORD_MAX

enum
{
    WY_PROTOTYPE_SLOT_FLAG_BOXED  = 0x0001,  ///< Slot is boxed, may escape
    WY_PROTOTYPE_SLOT_FLAG_STATIC = 0x0002,  ///< Slot is statically allocated
};

#define WY_SLOT_DEFAULTS 0

typedef struct wy_prototype_slot
{
    wy_uword flags;
    wy_symtab_entry symtab_entry;
    wy_value default_value;
} wy_prototype_slot;

struct wy_prototype
{
    wy_object object;
    wy_prototype_slot* slots;
    wy_uword slot_capacity;
    wy_uword slot_count;
};


// ----------------------------------------------------------------------------
// Functions
// ----------------------------------------------------------------------------

/**
 * @brief Create a null primitive value
 */
WY_INLINE wy_primitive wy_primitive_null(void)
{
    wy_primitive v = { .gc_object = WY_NULL };
    return v;
}

/**
 * @brief Create 'nil' primitive
 */
WY_INLINE wy_value wy_value_nil(void)
{
    wy_value v = {
        .type = WY_TYPE_TAG_NIL,
        .data = wy_primitive_null()
    };
    return v;
}

/**
 * @brief Create Unset primitive
 */
WY_INLINE wy_value wy_value_Unset(void)
{
    wy_value v = {
        .type = WY_TYPE_TAG_ERROR,
        .data = wy_primitive_null()
    };
    return v;
}

//! Transitional alias for wy_type_is_object()
WY_INLINE bool wy_type_tag_is_gc(wy_type_tag tag)
{
    return wy_type_is_object(tag);
}

WY_END_DECLS

#endif
