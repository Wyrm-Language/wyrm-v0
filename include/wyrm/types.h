#ifndef WYRM_TYPES_H_
#define WYRM_TYPES_H_

#include <wyrm/sys/toolchain.h>
#include <wyrm/sys/atomics.h>
#include <wyrm/sys/errors.h>
#include <wyrm/sys/thread_id.h>

#ifdef __cplusplus
extern "C" {
#endif

// ----------------------------------------------------------------------------
// Forward Definitions
// ----------------------------------------------------------------------------

struct wyrm_exec_result;
struct wyrm_fiber;
union wyrm_primitive;
struct wyrm_stack;
struct wyrm_type;
struct wyrm_value;

#ifndef __cplusplus
typedef struct wyrm_exec_result wyrm_exec_result;
typedef struct wyrm_fiber wyrm_fiber;
typedef union wyrm_primitive wyrm_primitive;
typedef struct wyrm_stack wyrm_stack;
typedef struct wyrm_type wyrm_type;
typedef struct wyrm_value wyrm_value;
#endif

typedef wyrm_fiber* wyrm_fiber_ref;
typedef const wyrm_type* wyrm_type_ref;

// ----------------------------------------------------------------------------
// Allocator
// ----------------------------------------------------------------------------

struct wyrm_allocator;

/// @brief Virtual table for allocator
typedef struct wyrm_allocator_vt {
    void* (*alloc)(struct wyrm_allocator* self, wyrm_uword len);
    void* (*realloc)(struct wyrm_allocator* self, void* buffer, wyrm_uword new_sz);
    void (*free)(struct wyrm_allocator* self, void* buffer);
} wyrm_allocator_vt;

/// @brief Allocator data structure
///
/// The base data structure for an allocator.
typedef struct wyrm_allocator {
    const struct wyrm_allocator_vt* clz;
} wyrm_allocator;


// ----------------------------------------------------------------------------
// Execution State
// ----------------------------------------------------------------------------

/**
 * Result states for a wyrm callable invoked via wyrm_exec_fn.
 *
 * Each execution state determines interpretation of the fiber's value stack.
 */
enum wyrm_exec_state_tag
{
    WYRM_EXEC_DONE = 0,      ///< Normal return. stack_values indicates the count of values returned.
    WYRM_EXEC_EXCEPTION = 1, ///< Unhandled condition raised. fiber->exception holds the value.
    WYRM_EXEC_PENDING = 2,   ///< Callee pushed a new frame; fiber->pending_fn is the next call.
    // WYRM_EXEC_DELEGATE -- TBD, tail call
};
typedef wyrm_ushort wyrm_exec_state;

#ifndef __cplusplus
typedef enum wyrm_exec_state_tag wyrm_exec_state_tag;
#endif

/**
 * Return type of every wyrm_exec_fn call.
 *
 * Optimized to fit within a single register.
 */
struct wyrm_exec_result
{
    wyrm_exec_state state;
    wyrm_ushort stack_values;
};

static inline wyrm_exec_result wyrm_make_exec_result(wyrm_exec_state_tag state, wyrm_ushort count)
{
    wyrm_exec_result result = { .state = (wyrm_exec_state)state, .stack_values = count };
    return result;
}


/**
 *
 * Uniform calling convention for every callable in the system
 *
 * Every call memoizes a frame_start on the fiber's value stack before
 * pushing args; the callee may freely push/pop scratch above frame_start
 * and reports its payload as the trailing N stack values, N given by
 * stack_values; the interpretation of the result is dependent on the
 * exec state contained within the return struct.
 *
 */
typedef wyrm_exec_result (*wyrm_exec_fn)(wyrm_fiber_ref fiber);


// ----------------------------------------------------------------------------
// Wyrm Type
// ----------------------------------------------------------------------------

struct wyrm_type
{
    wyrm_type_ref parent;
    const char* name;

    /// fn destroy(self: Self) -> None
    wyrm_exec_fn destroy;
};


// ----------------------------------------------------------------------------
// Primitive Master Union
// ----------------------------------------------------------------------------

/**
 * A machine register sized union acting as fundamental VM register.
 *
 * A wyrm primitive union aligns to the host machine size. A primitive must
 * ALWAYS exist with a type. The wyrm_value provides this pairing, but the
 * primitive may be used independently in some cases.
 */
union wyrm_primitive {
    wyrm_word word;
    wyrm_handle handle;
    wyrm_short s_word;
    wyrm_uword uword;
    wyrm_ushort s_uword[2];
    wyrm_float fp;
    wyrm_float_s fp_s;
    wyrm_uintptr tagged_ptr;
    wyrm_error error;
    wyrm_primitive* primitive_ptr;
    wyrm_value* value_ptr;
    const wyrm_primitive* const_primitive_ptr;
    wyrm_exec_fn cb;
    wyrm_atomic_word ref_count;
    wyrm_atomic_word* ref_count_ptr;
    wyrm_sys_thread_id thread_id;
};

enum {
    WYRM_PRIMITIVE_SIZE = sizeof(wyrm_primitive)
};

static_assert(WYRM_PRIMITIVE_SIZE >= sizeof(uintptr_t), "Primitive must allow storage of a pointer");

// ----------------------------------------------------------------------------
// Wyrm Value
// ----------------------------------------------------------------------------

struct wyrm_value
{
    wyrm_type_ref type;
    wyrm_primitive data;
};

// ----------------------------------------------------------------------------
// Stack
// ----------------------------------------------------------------------------

struct wyrm_stack
{
    wyrm_value* entries_begin;
    wyrm_value* entries_end;

    wyrm_value* base;
    wyrm_value* top;
};



// ----------------------------------------------------------------------------
// Wyrm Fiber
// ----------------------------------------------------------------------------

struct wyrm_fiber
{
    wyrm_stack value_stack;
    wyrm_exec_fn pending_fn; ///< Non-NULL → fiber is pending; NULL → done or exception.
    wyrm_value exception;    ///< type != NULL → an exception is set.
};


#ifdef __cplusplus
}
#endif

#endif
