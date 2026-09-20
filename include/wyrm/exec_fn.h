#ifndef WYRM_EXEC_FN_H
#define WYRM_EXEC_FN_H

#include <wyrm/sys/toolchain.h>
#include <wyrm/fwd.h>
#include <wyrm/primitive.h>

WY_BEGIN_DECLS

/**
 * @brief Result states for a wyrm callable invoked via wy_exec_fn.
 *
 * Each execution context determines interpretation of the fiber's value stack.
 */
typedef enum wy_exec_state_tag
{
    WY_EXEC_DONE = 0,     ///< Normal return. stack_values indicates the count of values returned.

    /// Continue execution with the next function
    ///
    /// This result indicates that the pending_fn is set to a function
    /// intended to continue the current execution thread. Fiber variables
    /// are left unmodified.
    WY_EXEC_CONTINUE,

    /// Tail call with new arguments
    ///
    /// pending function is a tail call function; stack is expected to
    /// be based on only the call of this function. preserve count must
    /// be set to the desired count of pushed arguments for the call.
    WY_EXEC_TAIL_CALL,
} wy_exec_state;

/**
 * @brief Uniform Calling Convention for C functions in Wyrm
 *
 * The context is responsible for selecting the active fiber containing
 * the call stack - including arguments and reserved space for return
 * results. The C function may utilize these as desired. The return
 * result determines the next step in the context.
 */
typedef wy_exec_state (*wy_exec_fn_c_call)(wy_context* context, wy_primitive c_data);

/**
 * @brief A callable and the data it is called with
 *
 * The type of data is dependent on the type of the function. A C function
 *
 */
typedef struct wy_exec_fn_tag
{
    wy_exec_fn_c_call fn;
    wy_primitive c_data;
} wy_exec_fn;

/**
 * Wrap a C function as a callable
 */
WY_INLINE wy_exec_fn wy_exec_fn_create(wy_exec_fn_c_call fn, wy_primitive c_data)
{
    wy_exec_fn e;
    e.fn = fn;
    e.c_data = c_data;
    return e;
}

/**
 * Build the empty callable
 */
WY_INLINE wy_exec_fn wy_exec_fn_create_empty(void)
{
    wy_exec_fn e;
    e.fn = WY_NULL;
    e.c_data = wy_primitive_null();
    return e;
}

WY_INLINE bool wy_exec_fn_is_empty(const wy_exec_fn* fn) { return fn->fn == WY_NULL; }


/**
 * @brief Byte Code Packed Address
 *
 * A byte code packed address specifies the underlying bytecode module _AND_
 * the executable offset into the module.
 */
enum
{
    WY_EXEC_FN_ADDR_BITS   = WY_MAX_ARRAY_LEN_SZ_BITS,          //!< 20
    WY_EXEC_FN_MODULE_BITS = 12,

    WY_EXEC_FN_ADDR_MAX    = (1 << WY_EXEC_FN_ADDR_BITS),       //!< Exclusive
    WY_EXEC_FN_MODULE_MAX  = (1 << WY_EXEC_FN_MODULE_BITS),     //!< Exclusive
};

static_assert((WY_EXEC_FN_ADDR_BITS + WY_EXEC_FN_MODULE_BITS) <= WY_CELL_BITS,
    "packed callable payload must fit within a cell");

/**
 * Pack a module id and code address into a callable's payload
 *
 * The address occupies the low bits so that unpacking it is a single mask.
 */
WY_INLINE wy_primitive wy_exec_fn_b_code_pack(wy_uword module_id, wy_uword address)
{
    WY_ASSERT(module_id < WY_EXEC_FN_MODULE_MAX && address < WY_EXEC_FN_ADDR_MAX);
    return wy_primitive_uword((module_id << WY_EXEC_FN_ADDR_BITS) | address);
}

/**
 * Get the module id half of a packed bytecode payload
 */
WY_INLINE wy_uword wy_exec_fn_b_code_module_id(wy_primitive c_data)
{
    return (c_data.uword >> WY_EXEC_FN_ADDR_BITS) & (WY_EXEC_FN_MODULE_MAX - 1);
}

/**
 * Get the code address half of a packed bytecode payload
 */
WY_INLINE wy_uword wy_exec_fn_b_code_address(wy_primitive c_data)
{
    return c_data.uword & (WY_EXEC_FN_ADDR_MAX - 1);
}

WY_END_DECLS

#endif
