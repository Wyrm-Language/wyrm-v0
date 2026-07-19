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

struct wyrm_allocator;
struct wyrm_allocator_vt;
struct wyrm_box;
struct wyrm_context;
struct wyrm_table;
struct wyrm_exec_result;
struct wyrm_fiber;
struct wyrm_gc_object;
struct wyrm_machine;
struct wyrm_main_loop;
struct wyrm_object_list;
union wyrm_primitive;
struct wyrm_stack;
struct wyrm_state;
struct wyrm_string;
struct wyrm_thread;
struct wyrm_main_loop_vt;
struct wyrm_value;

#ifndef __cplusplus
typedef struct wyrm_allocator wyrm_allocator;
typedef struct wyrm_allocator_vt wyrm_allocator_vt;
typedef struct wyrm_box wyrm_box;
typedef struct wyrm_context wyrm_context;
typedef struct wyrm_table wyrm_table;
typedef struct wyrm_exec_result wyrm_exec_result;
typedef struct wyrm_fiber wyrm_fiber;
typedef struct wyrm_gc_object wyrm_gc_object;
typedef struct wyrm_machine wyrm_machine;
typedef struct wyrm_object_type wyrm_object_type;
typedef struct wyrm_object wyrm_object;
typedef struct wyrm_main_loop wyrm_main_loop;
typedef struct wyrm_main_loop_vt wyrm_main_loop_vt;
typedef union wyrm_primitive wyrm_primitive;
typedef struct wyrm_stack wyrm_stack;
typedef struct wyrm_state wyrm_state;
typedef struct wyrm_string wyrm_string;
typedef struct wyrm_thread wyrm_thread;
typedef struct wyrm_value wyrm_value;
#endif

typedef wyrm_fiber* wyrm_fiber_ref;
typedef wyrm_main_loop* wyrm_main_loop_ref;
typedef wyrm_thread* wyrm_thread_ref;

// ----------------------------------------------------------------------------
// Standardized Enumerations and Values
// ----------------------------------------------------------------------------

#define WYRM_HASH_INVALID WYRM_UWORD_MAX

/**
 * Primitive Types
 */
typedef enum wyrm_type_tag
{
    WYRM_TYPE_TAG_NIL = 0,

    WYRM_TYPE_TAG_WORD,
    WYRM_TYPE_TAG_UWORD,

    WYRM_TYPE_TAG_FUNCTION,

    WYRM_TYPE_TAG_SYMBOL,
    WYRM_TYPE_TAG_VALUE_PTR,

    WYRM_GC_PATH_START,
    WYRM_TYPE_TAG_BOX,
    WYRM_TYPE_TAG_STR,

    WYRM_SLOW_PATH_START,
    WYRM_TYPE_TAG_TABLE
} wyrm_type_tag;

typedef enum wyrm_state_flag_tag
{
    WYRM_STATE_FLAG_OWNS_SELF       = 0x0001,
    WYRM_STATE_FLAG_OWNS_MACHINE    = 0x0002,
} wyrm_state_flag;


// ----------------------------------------------------------------------------
// Allocator
// ----------------------------------------------------------------------------

/// @brief Virtual table for allocator
typedef struct wyrm_allocator_vt {
    void* (*alloc)(wyrm_allocator* self, wyrm_uword len);
    void* (*realloc)(wyrm_allocator* self, void* buffer, wyrm_uword new_sz);
    void (*free)(wyrm_allocator* self, void* buffer);
} wyrm_allocator_vt;

/// @brief Allocator data structure
///
/// The base data structure for an allocator.
struct wyrm_allocator {
    const wyrm_allocator_vt* clz;
};


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
typedef wyrm_exec_result (*wyrm_exec_fn)(wyrm_state* state);


// ----------------------------------------------------------------------------
// Wyrm Type
// ----------------------------------------------------------------------------


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
    wyrm_exec_fn cb;
    wyrm_atomic_word ref_count;
    wyrm_sys_thread_id thread_id;
    wyrm_value* value_ptr;
    const char* symtab_entry;
    void* ptr;

    wyrm_gc_object* gc_object;
    wyrm_string* str;
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
    wyrm_type_tag type;
    wyrm_primitive data;
};


// ----------------------------------------------------------------------------
// Wyrm GC Info
// ----------------------------------------------------------------------------

enum
{
    WYRM_GC_STATIC          = 0x001,
    WYRM_GC_FLAG_MARKED     = 0x004,
    WYRM_GC_FLAG_FINALIZED  = 0x008,
};

struct wyrm_gc_object
{
    wyrm_gc_object* next;
    wyrm_uword flags;
    wyrm_type_tag gc_type;
};

// ----------------------------------------------------------------------------
// Wyrm Box
// ----------------------------------------------------------------------------

struct wyrm_box
{
    wyrm_gc_object head;
    wyrm_value value;
};

// ----------------------------------------------------------------------------
// Wyrm String
// ----------------------------------------------------------------------------

struct wyrm_string
{
    wyrm_gc_object head;
    const char* str;
    wyrm_uword len;
    wyrm_uword hash;
};

// ----------------------------------------------------------------------------
// Wyrm Table
// ----------------------------------------------------------------------------

/**
 * Key Hash Value
 */
typedef struct wyrm_key_hash_value
{
    wyrm_value key;
    wyrm_uword key_hash;
    wyrm_value value;
} wyrm_key_hash_value;

/**
 * Dictionary type
 */
struct wyrm_table
{
    wyrm_gc_object obj;

    wyrm_allocator* allocator;

    wyrm_uword count;

    wyrm_key_hash_value* dense;
    wyrm_uword dense_capacity;

    wyrm_uword* sparse;
    wyrm_uword sparse_capacity;
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

    wyrm_gc_object* first;
    wyrm_gc_object* last;
};


// ----------------------------------------------------------------------------
// Wyrm Machine
// ----------------------------------------------------------------------------

struct wyrm_machine_symtab;

struct wyrm_machine
{
    wyrm_allocator* allocator;
    wyrm_context* context;

    struct wyrm_machine_symtab* symtab;
};


// ----------------------------------------------------------------------------
// Wyrm State
// ----------------------------------------------------------------------------




/**
 * @brief State Definition for all Interpreter/Object Calls
 *
 * This structure is intended to live on the stack or heap. Always initialize
 * using the wyrm_state_init_* functions - do not bitwise copy. State objects
 * allow bidirectional communication of complex VM information.
 *
 * Any call that requires memory allocation or interaction with the virtual
 * machine shall utilize a wyrm_state* as the first parameter to the function.
 * This explicitly defines the current machine, context, and fiber.
 *
 */
struct wyrm_state
{
    wyrm_uword state_flags;
    wyrm_machine* machine;
    wyrm_context* context;
    wyrm_fiber* fiber;

    wyrm_allocator* state_alloc_;
};


#ifdef __cplusplus
}
#endif

#endif
