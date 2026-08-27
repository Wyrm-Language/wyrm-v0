#ifndef WYRM_WCORE_H_
#define WYRM_WCORE_H_

#include <wyrm/sys/toolchain.h>
#include <wyrm/sys/atomics.h>
#include <wyrm/sys/errors.h>
#include <wyrm/sys/thread_id.h>

WYRM_BEGIN_DECLS

// ----------------------------------------------------------------------------
// Macros
// ----------------------------------------------------------------------------

#define WYRM_HASH_INVALID WYRM_UWORD_MAX

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
struct wyrm_object;
struct wyrm_object_list;
struct wyrm_object_type;
struct wyrm_pair;
union wyrm_primitive;
struct wyrm_prototype;
struct wyrm_stack;
struct wyrm_state;
struct wyrm_string;
struct wyrm_main_loop_vt;
struct wyrm_work_area;
struct wyrm_value;

#ifndef __cplusplus
typedef struct wyrm_allocator wyrm_allocator;
typedef struct wyrm_allocator_vt wyrm_allocator_vt;
typedef struct wyrm_box wyrm_box;
typedef struct wyrm_class wyrm_class;
typedef struct wyrm_context wyrm_context;
typedef struct wyrm_dstruct wyrm_dstruct;
typedef struct wyrm_dict wyrm_dict;
typedef struct wyrm_fiber wyrm_fiber;
typedef struct wyrm_machine wyrm_machine;
typedef struct wyrm_object_type wyrm_object_type;
typedef struct wyrm_object wyrm_object;
typedef struct wyrm_main_loop wyrm_main_loop;
typedef struct wyrm_main_loop_vt wyrm_main_loop_vt;
typedef struct wyrm_pair wyrm_pair;
typedef union wyrm_primitive wyrm_primitive;
typedef struct wyrm_prototype wyrm_prototype;
typedef struct wyrm_stack wyrm_stack;
typedef struct wyrm_state wyrm_state;
typedef struct wyrm_string wyrm_string;
typedef struct wyrm_value wyrm_value;
typedef struct wyrm_work_area wyrm_work_area;
#endif

// ----------------------------------------------------------------------------
// Core Engine Types
// ----------------------------------------------------------------------------

/**
 * @brief Primitive Types
 */
typedef enum wyrm_type_tag
{
    WYRM_TYPE_TAG_NIL = 0,

    WYRM_TYPE_TAG_WORD,
    WYRM_TYPE_TAG_UWORD,

    WYRM_TYPE_TAG_FUNCTION,
    WYRM_TYPE_TAG_FRAME,

    WYRM_TYPE_TAG_SYMBOL,
    WYRM_TYPE_TAG_VALUE_PTR,

    WYRM_TYPE_TAG_GC_PATH_START,

    WYRM_TYPE_TAG_ERROR,
    WYRM_TYPE_TAG_PAIR,
    WYRM_TYPE_TAG_BOX,
    WYRM_TYPE_TAG_OBJECT,
    WYRM_TYPE_TAG_STR,
    WYRM_TYPE_TAG_FIBER,
    WYRM_TYPE_TAG_DTYPE,
    WYRM_TYPE_TAG_CLASS,

    WYRM_TYPE_TAG_TABLE
} wyrm_type_tag;

/**
 * @brief Symbol table entry
 */
typedef const char* wyrm_symtab_entry;

/**
 * @brief Result states for a wyrm callable invoked via wyrm_exec_fn.
 *
 * Each execution state determines interpretation of the fiber's value stack.
 */
typedef enum wyrm_exec_state_tag
{
    WYRM_EXEC_DONE = 0,     ///< Normal return. stack_values indicates the count of values returned.

    /// Continue execution with the next function
    ///
    /// This result indicates that the pending_fn is set to a function
    /// intended to continue the current execution thread. Fiber variables
    /// are left unmodified.
    WYRM_EXEC_CONTINUE,

    /// Tail call with new arguments
    ///
    /// pending function is a tail call function; stack is expected to
    /// be based on only the call of this function. preserve count must
    /// be set to the desired count of pushed arguments for the call.
    WYRM_EXEC_TAIL_CALL,
} wyrm_exec_state;


/**
 * @brief Uniform Calling Convention for C functions in Wyrm
 *
 * Every call memoizes a frame_start on the fiber's value stack before
 * pushing args; the callee may freely push/pop scratch above frame_start
 * and reports its payload as the trailing N stack values, N given by
 * stack_values; the interpretation of the result is dependent on the
 * exec state contained within the return struct.
 *
 */
typedef wyrm_exec_state (*wyrm_exec_fn)(wyrm_state* state);


/**
 * A machine register sized union acting as fundamental VM register.
 *
 * A wyrm primitive union aligns to the host machine size. A primitive must
 * ALWAYS exist with a type. The wyrm_value provides this pairing, but the
 * primitive may be used independently in some cases.
 */
union wyrm_primitive {
    wyrm_object* gc_object;

    wyrm_word word;
    wyrm_handle handle;
    wyrm_short s_word;
    wyrm_uword uword;
    wyrm_ushort s_uword[2];
    wyrm_float fp;
    wyrm_float_s fp_s;
    wyrm_uintptr tagged_ptr;
    wyrm_error error;
    wyrm_exec_fn cb;
    wyrm_atomic_word ref_count;
    wyrm_sys_thread_id thread_id;
    wyrm_value* value_ptr;
    wyrm_box* box_ptr;
    wyrm_symtab_entry symtab_entry;
    wyrm_pair* pair_ptr;
    void* ptr;
    bool flag;

    wyrm_string* str;
    wyrm_class* cls;
};

/**
 * @brief A typed primitive
 */
struct wyrm_value
{
    wyrm_type_tag type;
    wyrm_primitive data;
};


enum {
    WYRM_PRIMITIVE_SIZE = sizeof(wyrm_primitive)
};

static_assert(WYRM_PRIMITIVE_SIZE >= sizeof(uintptr_t), "Primitive must allow storage of a pointer");

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
