#ifndef WYRM_WCORE_H_
#define WYRM_WCORE_H_

#include <wyrm/sys/toolchain.h>
#include <wyrm/sys/atomics.h>
#include <wyrm/sys/errors.h>
#include <wyrm/sys/thread_id.h>

#include <wyrm/primitive.h>
#include <wyrm/value.h>

WYRM_BEGIN_DECLS

// ----------------------------------------------------------------------------
// Macros
// ----------------------------------------------------------------------------

#define WYRM_HASH_INVALID WYRM_UWORD_MAX
#define WYRM_WORK_AREA_LEN 8
#define WY_SLOT_INVALID WYRM_UWORD_MAX

// ----------------------------------------------------------------------------
// Forward Definitions
// ----------------------------------------------------------------------------

struct wyrm_allocator;
struct wyrm_allocator_vt;
struct wyrm_box;
struct wyrm_class;
struct wyrm_context;
struct wyrm_dstruct;
struct wyrm_dict;
struct wyrm_fiber;
struct wyrm_machine;
struct wyrm_main_loop;
struct wy_module;

struct wyrm_object_list;
struct wyrm_object_type;
struct wyrm_prototype;
struct wyrm_state;
struct wyrm_string;
struct wyrm_main_loop_vt;
struct wyrm_work_area;

#ifndef __cplusplus
typedef struct wyrm_allocator wyrm_allocator;
typedef struct wyrm_allocator_vt wyrm_allocator_vt;
typedef struct wyrm_context wyrm_context;
typedef struct wyrm_dstruct wyrm_dstruct;
typedef struct wyrm_dict wyrm_dict;
typedef struct wyrm_fiber wyrm_fiber;
typedef struct wyrm_machine wyrm_machine;
typedef struct wy_module wy_module;
typedef struct wyrm_object_type wyrm_object_type;
typedef struct wyrm_main_loop wyrm_main_loop;
typedef struct wyrm_main_loop_vt wyrm_main_loop_vt;
typedef struct wyrm_prototype wyrm_prototype;
typedef struct wyrm_work_area wyrm_work_area;
#endif

typedef wyrm_object wy_object;
typedef wyrm_allocator wy_allocator;
typedef wyrm_context wy_context;
typedef wyrm_machine wy_machine;
typedef wyrm_fiber wy_fiber;


// ----------------------------------------------------------------------------
// Core Engine Types
// ----------------------------------------------------------------------------




/**
 * Generic 'User Data' Friendly Field
 *
 * A small working space intended for temporary stack parameters and type
 * erased operations. Work areas should be tightly coupled to a single known
 * API usage.
 */
struct wyrm_work_area
{
    wyrm_primitive data[WYRM_WORK_AREA_LEN];
};

/**
 * Generic Garbage Collected Object
 *
 * All objects located on the heap hold this structure as their first member.
 * The wyrm_object_type* determines the interpretation of the remainder of
 * the structure as well as the fixed offset size.
 */
struct wyrm_object
{
    const wyrm_object_type* dtype;
    wyrm_object* next;
    wyrm_uword flags;
};

struct wyrm_object_type
{
    wyrm_object object;
    wyrm_type_tag gc_type;

    void (*finalize)(wyrm_context* context, wyrm_object* self);

    wyrm_error (*children_iter_start)(wyrm_state* state, wyrm_object* self, wyrm_work_area* wa);
    wyrm_error (*children_iter_next)(wyrm_state* state, wyrm_object* self, wyrm_work_area* wa, const wyrm_object** child);
};

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

WYRM_INLINE bool wyrm_type_tag_is_gc(wyrm_type_tag tag)
{
    return tag >= WYRM_TYPE_TAG_GC_PATH_START;
}

WYRM_END_DECLS

#endif
