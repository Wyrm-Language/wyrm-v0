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

WYRM_BEGIN_DECLS

// ----------------------------------------------------------------------------
// Macros
// ----------------------------------------------------------------------------

#define WYRM_HASH_INVALID WYRM_UWORD_MAX
#define WY_SLOT_INVALID WYRM_UWORD_MAX

// ----------------------------------------------------------------------------
// Prototype
// ----------------------------------------------------------------------------
#define WYRM_BAD_SLOT WYRM_UWORD_MAX

enum
{
    WYRM_PROTOTYPE_SLOT_FLAG_BOXED  = 0x0001,  ///< Slot is boxed, may escape
    WYRM_PROTOTYPE_SLOT_FLAG_STATIC = 0x0002,  ///< Slot is statically allocated
};

#define WYRM_SLOT_DEFAULTS 0

typedef struct wyrm_prototype_slot
{
    wyrm_uword flags;
    wyrm_symtab_entry symtab_entry;
    wyrm_value default_value;
} wyrm_prototype_slot;

struct wyrm_prototype
{
    wyrm_object object;
    wyrm_prototype_slot* slots;
    wyrm_uword slot_capacity;
    wyrm_uword slot_count;
};


// ----------------------------------------------------------------------------
// Functions
// ----------------------------------------------------------------------------

/**
 * @brief Create a null primitive value
 */
WYRM_INLINE wyrm_primitive wyrm_primitive_null(void)
{
    wyrm_primitive v = { .gc_object = WYRM_NULL };
    return v;
}

/**
 * @brief Create 'nil' primitive
 */
WYRM_INLINE wyrm_value wyrm_value_nil(void)
{
    wyrm_value v = {
        .type = WYRM_TYPE_TAG_NIL,
        .data = wyrm_primitive_null()
    };
    return v;
}

/**
 * @brief Create Unset primitive
 */
WYRM_INLINE wyrm_value wyrm_value_Unset(void)
{
    wyrm_value v = {
        .type = WYRM_TYPE_TAG_ERROR,
        .data = wyrm_primitive_null()
    };
    return v;
}

//! Transitional alias for wy_type_is_object()
WYRM_INLINE bool wyrm_type_tag_is_gc(wyrm_type_tag tag)
{
    return wy_type_is_object(tag);
}

WYRM_END_DECLS

#endif
