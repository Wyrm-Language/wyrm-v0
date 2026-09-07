#ifndef WYRM_EXEC_FN_H
#define WYRM_EXEC_FN_H

#include <wyrm/sys/toolchain.h>
#include <wyrm/fwd.h>

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
 * Every call memoizes a frame_start on the fiber's value stack before
 * pushing args; the callee may freely push/pop scratch above frame_start
 * and reports its payload as the trailing N stack values, N given by
 * stack_values; the interpretation of the result is dependent on the
 * exec context contained within the return struct.
 *
 */
typedef wy_exec_state (*wy_exec_fn)(wy_context* context);

WY_END_DECLS

#endif
