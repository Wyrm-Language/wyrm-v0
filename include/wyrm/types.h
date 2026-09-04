#ifndef WYRM_TYPES_H_
#define WYRM_TYPES_H_

#include <wyrm/core.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum wyrm_state_flag_tag
{
    WYRM_STATE_FLAG_OWNS_SELF       = 0x0001,
} wyrm_state_flag;

// ----------------------------------------------------------------------------
// Allocator
// ----------------------------------------------------------------------------

/// @brief Virtual table for allocator
typedef struct wyrm_allocator_vt {
    void* (*alloc)(wyrm_allocator* self, wyrm_uword len);
    void* (*realloc)(wyrm_allocator* self, void* buffer, wyrm_uword new_sz);
    void (*free)(wyrm_allocator* self, void* buffer);
    wyrm_uword (*estimate_heap_size)(wyrm_allocator* self);
} wyrm_allocator_vt;

/// @brief Allocator data structure
///
/// The base data structure for an allocator.
struct wyrm_allocator {
    const wyrm_allocator_vt* clz;
};


// ----------------------------------------------------------------------------
// Wyrm GC Info
// ----------------------------------------------------------------------------

enum
{
    WYRM_GC_STATIC          = 0x001,
    WYRM_GC_FLAG_MARKED     = 0x004,
    WYRM_GC_FLAG_FINALIZED  = 0x008,
    WYRM_GC_FLAG_RO         = 0x010,
};


extern const wyrm_object_type wyrm_type_type;
extern const wyrm_object_type wyrm_type_object;

#define WYRM_OBJECT_STATIC_INITIALIZER(DTYPE)   { .dtype = DTYPE, .next = WYRM_NULL, .flags = (WYRM_GC_STATIC | WYRM_GC_FLAG_RO)  }
#define WYRM_OBJECT_TYPE_OBJECT_INIT WYRM_OBJECT_STATIC_INITIALIZER(&wyrm_type_type)


// ----------------------------------------------------------------------------
// Prototype & Scope
// ----------------------------------------------------------------------------

struct wyrm_scope
{
    wyrm_object object;
    wyrm_prototype* prototype;
    wyrm_value* slots;
};


// ----------------------------------------------------------------------------
// Wyrm Dict
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
struct wyrm_dict
{
    wyrm_object object;

    wyrm_allocator* allocator;

    wyrm_uword count;

    wyrm_key_hash_value* dense;
    wyrm_uword dense_capacity;

    wyrm_uword* sparse;
    wyrm_uword sparse_capacity;
};

// ----------------------------------------------------------------------------
// DStruct
// ----------------------------------------------------------------------------

/**
 * DStruct
 */
typedef struct wyrm_dstruct_type
{
    const char* name;
} wyrm_dstruct_type;

typedef void (*wyrm_dstruct_finalizer)(wyrm_context* state, wyrm_dstruct* dstruct);

/**
 * Datastruct
 */
struct wyrm_dstruct
{
    wyrm_object obj;
    const wyrm_dstruct_type* dtype;

    wyrm_dstruct_finalizer finalizer;

    void* data;
};


// ----------------------------------------------------------------------------
// Class
// ----------------------------------------------------------------------------

struct wyrm_class
{
    wyrm_prototype prototype;
    wyrm_class* super;
    wyrm_primitive sym_name;
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
    wyrm_error (*add_fd)(wyrm_main_loop* ref, wyrm_primitive *out, wyrm_handle fd, wyrm_io_condition events, wyrm_priority priority, wyrm_source_handle_cb cb, wyrm_primitive ud);
    wyrm_error (*add_timer)(wyrm_main_loop* self, wyrm_primitive *out, uint32_t ms, wyrm_priority priority, wyrm_source_cb cb, wyrm_primitive ud);
    wyrm_error (*add_idle)(wyrm_main_loop* self, wyrm_primitive *out, wyrm_source_cb cb, wyrm_primitive ud);
    wyrm_error (*add_wakeable)(wyrm_main_loop* self, wyrm_primitive *out, wyrm_priority priority, wyrm_source_cb cb, wyrm_primitive ud);
    wyrm_error (*trigger)(wyrm_main_loop* self, wyrm_primitive src);
    wyrm_error (*remove)(wyrm_main_loop* self, wyrm_primitive src);
    wyrm_error (*iterate)(wyrm_main_loop* self, bool may_block);
    wyrm_error (*run)(wyrm_main_loop* self);
    wyrm_error (*quit)(wyrm_main_loop* self);
} wyrm_main_loop_vt;

/**
 * Main Loop Abstraction
 */
typedef struct wyrm_main_loop
{
    const wyrm_main_loop_vt *vt;
} wyrm_main_loop;





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
