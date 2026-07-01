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

struct wyrm_context;
struct wyrm_exec_result;
struct wyrm_fiber;
struct wyrm_machine;
struct wyrm_object;
struct wyrm_object_type;
struct wyrm_main_loop;
struct wyrm_object_list;
union wyrm_primitive;
struct wyrm_stack;
struct wyrm_thread;
struct wyrm_type;
struct wyrm_main_loop_vt;
struct wyrm_value;

#ifndef __cplusplus
typedef struct wyrm_context wyrm_context;
typedef struct wyrm_exec_result wyrm_exec_result;
typedef struct wyrm_fiber wyrm_fiber;
typedef struct wyrm_machine wyrm_machine;
typedef struct wyrm_object_type wyrm_object_type;
typedef struct wyrm_object wyrm_object;
typedef struct wyrm_main_loop wyrm_main_loop;
typedef struct wyrm_main_loop_vt wyrm_main_loop_vt;
typedef struct wyrm_object_list wyrm_object_list;
typedef union wyrm_primitive wyrm_primitive;
typedef struct wyrm_stack wyrm_stack;
typedef struct wyrm_thread wyrm_thread;
typedef struct wyrm_type wyrm_type;
typedef struct wyrm_value wyrm_value;
#endif

typedef wyrm_fiber* wyrm_fiber_ref;
typedef const wyrm_object_type* wyrm_object_type_ref;
typedef struct wyrm_main_loop* wyrm_main_loop_ref;
typedef wyrm_thread* wyrm_thread_ref;
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
    // WYRM_EXEC_EXCEPTION, ///< Unhandled condition raised. fiber->exception holds the value. TODO
    // WYRM_EXEC_PENDING,   ///< Callee pushed a new frame; fiber->pending_fn is the next call. TODO
    WYRM_EXEC_DELEGATE,     ///< Tail call; reset stack
    WYRM_EXEC_CONTINUE,     ///< Continue with the current stack
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
    void* ptr;
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
// Wyrm Object
// ----------------------------------------------------------------------------

/**
 * @brief Object Definition
 *
 * Like other VM systems, we reference objects by the object header.
 * Objects are expected to have object header as the first structural
 * member to allow safe type casting.
 */
struct wyrm_object
{
    const wyrm_object_type* type;
};

/**
 * @brief Object Type Definition
 *
 * The Object Type defines the structure and operations that are supported
 * on the memory associated with an object header.
 */
typedef struct wyrm_object_type
{
    wyrm_object head;
    wyrm_object_type_ref super;
} wyrm_object_type;

/**
 * List of wyrm objects
 */
struct wyrm_object_list
{
    wyrm_allocator* allocator;
    wyrm_object** objects;
    wyrm_uword count;
    wyrm_uword capacity;
};

// ----------------------------------------------------------------------------
// Stack
// ----------------------------------------------------------------------------

/**
 * @struct wyrm_stack
 *
 * Stack primitive for the Wyrm interpreter. Stack space is defined by a
 * pointer range (entries_begin, entries_end); Stack grows upward from
 * begin toward end.
 *
 * Stack shape:
 *      [base - 2] Previous base pointer;
 *      [base - 1] Continuation meant to expand after base
 *      [base]
 *      ...  Current value of the stack
 *      [top]
 */
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
    wyrm_context* parent;
    wyrm_stack value_stack;
    wyrm_exec_fn pending_fn; ///< Non-NULL → fiber is pending; NULL → done or exception.
    /* wyrm_value exception; -- TODO */
};

// ----------------------------------------------------------------------------
// Wyrm Main Loop
// ----------------------------------------------------------------------------

/**
 * I/O condition flags for file descriptor events
 */
enum wyrm_io_flag
{
    WYRM_IO_IN = 1,
    WYRM_IO_PRI = 2,
    WYRM_IO_OUT = 4,
    WYRM_IO_ERR = 8,
    WYRM_IO_HUP = 16,
    WYRM_IO_NVAL = 32,
};

/**
 * I/O condition type.
 *
 * A combination of wyrm_io_flag values bitwise OR'd together to indicate the
 * reason for IO handling entry.
 */
typedef wyrm_uword wyrm_io_condition;

/**
 * @brief Main loop source priority abstraction
 *
 * Priority values are backend-agnostic and intentionally limited to a small
 * portable set.
 */
enum wyrm_priority
{
    WYRM_PRIORITY_HIGH,
    WYRM_PRIORITY_DEFAULT,
    WYRM_PRIORITY_IDLE,
};

typedef enum wyrm_priority wyrm_priority;

/**
 * Source callback for idle and timer events
 *
 * This registered callback is invoked according to the registered event type.
 * The `user_data` primitive is registered with the main loop and will be
 * treated as a purely opaque value. If using object reference or memory,
 * then the memory _MUST_ be referenced / managed externally and kept
 * for the lifespan of the callback.
 */
typedef bool (*wyrm_source_cb)(wyrm_primitive user_data);

/**
 * Source callback for file descriptor events
 */
typedef bool (*wyrm_source_handle_cb)(wyrm_handle handle, wyrm_io_condition condition, wyrm_primitive user_data);

/**
 * Main loop virtual table
 */
typedef struct wyrm_main_loop_vt
{
    wyrm_error (*add_fd)(wyrm_main_loop_ref ref, wyrm_primitive *out, wyrm_handle fd, wyrm_io_condition events, wyrm_priority priority, wyrm_source_handle_cb cb, wyrm_primitive ud);
    wyrm_error (*add_timer)(wyrm_main_loop_ref self, wyrm_primitive *out, uint32_t ms, wyrm_priority priority, wyrm_source_cb cb, wyrm_primitive ud);
    wyrm_error (*add_idle)(wyrm_main_loop_ref self, wyrm_primitive *out, wyrm_source_cb cb, wyrm_primitive ud);
    wyrm_error (*add_wakeable)(wyrm_main_loop_ref self, wyrm_primitive *out, wyrm_priority priority, wyrm_source_cb cb, wyrm_primitive ud);
    wyrm_error (*trigger)(wyrm_main_loop_ref self, wyrm_primitive src);
    wyrm_error (*remove)(wyrm_main_loop_ref self, wyrm_primitive src);
    wyrm_error (*iterate)(wyrm_main_loop_ref self, bool may_block);
    wyrm_error (*run)(wyrm_main_loop_ref self);
    wyrm_error (*quit)(wyrm_main_loop_ref self);
} wyrm_main_loop_vt;

/**
 * Main Loop Abstraction
 */
typedef struct wyrm_main_loop
{
    const wyrm_main_loop_vt *vt;
} wyrm_main_loop;


// ----------------------------------------------------------------------------
// Wyrm Thread (OS)
// ----------------------------------------------------------------------------

struct wyrm_thread
{
    int placeholder_;
};

// ----------------------------------------------------------------------------
// Wyrm Context
// ----------------------------------------------------------------------------

struct wyrm_context
{
    wyrm_machine* parent;
    wyrm_fiber* current_fiber;
    wyrm_main_loop* main_loop;

    wyrm_primitive wakeable_source;
};


// ----------------------------------------------------------------------------
// Wyrm Machine
// ----------------------------------------------------------------------------

struct wyrm_machine
{
    wyrm_allocator* allocator;
    wyrm_context* context;
};

#ifdef __cplusplus
}
#endif

#endif
