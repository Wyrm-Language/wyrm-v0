#ifndef WYRM_TYPES_H_
#define WYRM_TYPES_H_

#include <wyrm/core.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum wy_state_flag_tag
{
    WY_STATE_FLAG_OWNS_SELF       = 0x0001,
} wy_state_flag;

// ----------------------------------------------------------------------------
// Allocator
// ----------------------------------------------------------------------------

/// @brief Virtual table for allocator
typedef struct wy_allocator_vt {
    void* (*alloc)(wy_allocator* self, wy_uword len);
    void* (*realloc)(wy_allocator* self, void* buffer, wy_uword new_sz);
    void (*free)(wy_allocator* self, void* buffer);
    wy_uword (*estimate_heap_size)(wy_allocator* self);
} wy_allocator_vt;

/// @brief Allocator data structure
///
/// The base data structure for an allocator.
struct wy_allocator {
    const wy_allocator_vt* clz;
};


// ----------------------------------------------------------------------------
// Prototype & Scope
// ----------------------------------------------------------------------------

struct wy_scope
{
    wy_object object;
    wy_prototype* prototype;
    wy_value* slots;
};


// ----------------------------------------------------------------------------
// Wyrm Dict
// ----------------------------------------------------------------------------

/**
 * Key Hash Value
 */
typedef struct wy_key_hash_value
{
    wy_value key;
    wy_uword key_hash;
    wy_value value;
} wy_key_hash_value;

/**
 * Dictionary type
 */
struct wy_dict
{
    wy_object object;

    wy_allocator* allocator;

    wy_uword count;

    wy_key_hash_value* dense;
    wy_uword dense_capacity;

    wy_uword* sparse;
    wy_uword sparse_capacity;
};

// ----------------------------------------------------------------------------
// DStruct
// ----------------------------------------------------------------------------

/**
 * DStruct
 */
typedef struct wy_dstruct_type
{
    const char* name;
} wy_dstruct_type;

typedef void (*wy_dstruct_finalizer)(wy_context* state, wy_dstruct* dstruct);

/**
 * Datastruct
 */
struct wy_dstruct
{
    wy_object obj;
    const wy_dstruct_type* dtype;

    wy_dstruct_finalizer finalizer;

    void* data;
};


// ----------------------------------------------------------------------------
// Class
// ----------------------------------------------------------------------------

struct wy_class
{
    wy_prototype prototype;
    wy_class* super;
    wy_primitive sym_name;
};

// ----------------------------------------------------------------------------
// Wyrm Main Loop
// ----------------------------------------------------------------------------

/**
 * I/O condition flags for file descriptor events
 */
enum wy_io_flag
{
    WY_IO_IN = 1,
    WY_IO_PRI = 2,
    WY_IO_OUT = 4,
    WY_IO_ERR = 8,
    WY_IO_HUP = 16,
    WY_IO_NVAL = 32,
};

/**
 * I/O condition type.
 *
 * A combination of wy_io_flag values bitwise OR'd together to indicate the
 * reason for IO handling entry.
 */
typedef wy_uword wy_io_condition;

/**
 * @brief Main loop source priority abstraction
 *
 * Priority values are backend-agnostic and intentionally limited to a small
 * portable set.
 */
enum wy_priority
{
    WY_PRIORITY_HIGH,
    WY_PRIORITY_DEFAULT,
    WY_PRIORITY_IDLE,
};

typedef enum wy_priority wy_priority;

/**
 * Source callback for idle and timer events
 *
 * This registered callback is invoked according to the registered event type.
 * The `user_data` primitive is registered with the main loop and will be
 * treated as a purely opaque value. If using object reference or memory,
 * then the memory _MUST_ be referenced / managed externally and kept
 * for the lifespan of the callback.
 */
typedef bool (*wy_source_cb)(wy_primitive user_data);

/**
 * Source callback for file descriptor events
 */
typedef bool (*wy_source_handle_cb)(wy_handle handle, wy_io_condition condition, wy_primitive user_data);

/**
 * Main loop virtual table
 */
typedef struct wy_main_loop_vt
{
    wy_error (*add_fd)(wy_main_loop* ref, wy_primitive *out, wy_handle fd, wy_io_condition events, wy_priority priority, wy_source_handle_cb cb, wy_primitive ud);
    wy_error (*add_timer)(wy_main_loop* self, wy_primitive *out, uint32_t ms, wy_priority priority, wy_source_cb cb, wy_primitive ud);
    wy_error (*add_idle)(wy_main_loop* self, wy_primitive *out, wy_source_cb cb, wy_primitive ud);
    wy_error (*add_wakeable)(wy_main_loop* self, wy_primitive *out, wy_priority priority, wy_source_cb cb, wy_primitive ud);
    wy_error (*trigger)(wy_main_loop* self, wy_primitive src);
    wy_error (*remove)(wy_main_loop* self, wy_primitive src);
    wy_error (*iterate)(wy_main_loop* self, bool may_block);
    wy_error (*run)(wy_main_loop* self);
    wy_error (*quit)(wy_main_loop* self);
} wy_main_loop_vt;

/**
 * Main Loop Abstraction
 */
typedef struct wy_main_loop
{
    const wy_main_loop_vt *vt;
} wy_main_loop;





// ----------------------------------------------------------------------------
// Wyrm Machine
// ----------------------------------------------------------------------------



// ----------------------------------------------------------------------------
// Wyrm State
// ----------------------------------------------------------------------------


/**
 * @brief State Definition for all Interpreter/Object Calls
 *
 * This structure is intended to live on the stack or heap. Always initialize
 * using the wy_state_init_* functions - do not bitwise copy. State objects
 * allow bidirectional communication of complex VM information.
 *
 * Any call that requires memory allocation or interaction with the virtual
 * machine shall utilize a wy_state* as the first parameter to the function.
 * This explicitly defines the current machine, context, and fiber.
 *
 */
struct wy_state
{
    wy_uword state_flags;
    wy_machine* machine;
    wy_context* context;
    wy_fiber* fiber;

    wy_allocator* state_alloc_;
};


#ifdef __cplusplus
}
#endif

#endif
