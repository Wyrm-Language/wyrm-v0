#ifndef WYRM_EXEC_FN_H
#define WYRM_EXEC_FN_H

#include <wyrm/sys/toolchain.h>
#include <wyrm/fwd.h>

WYRM_BEGIN_DECLS

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

WYRM_END_DECLS

#endif
