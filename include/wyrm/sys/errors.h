#ifndef WYRM_SYS_ERRORS_H_
#define WYRM_SYS_ERRORS_H_

#include <wyrm/sys/toolchain.h>

/**
 * @brief Error Definition Type
 *
 * This type defines the various errors that Wyrm may report to application
 * code. Errors are intended to map well to C ERRNO, but are defined
 * independently here to allow portability to freestanding / malformed C
 * implementations.
 */
enum wyrm_error {
    WYRM_ERR_NONE = 0,                  ///< Operation completed successfully
    WYRM_ERR_CONTINUE = 1,              ///< Operation paused and return result set to continuation
    WYRM_ERR_INCONSISTENT_STATE = 2,    ///< Operation detected inconsistent state within VM, fatal error

    WYRM_ERR_UNKNOWN,   ///< General / unknown failure
    WYRM_ERR_INVAL,     ///< Invalid parameter (such as nullptr)
    WYRM_ERR_BAD_TYPE,  ///< Unable to convert to given type
    WYRM_ERR_BUSY,      ///< Object is busy
    WYRM_ERR_UNBOUND,   ///< Unbound or unknown variable
    WYRM_ERR_PERM,      ///< Bad permissions
    WYRM_ERR_EMPTY,     ///< Container is empty
    WYRM_ERR_CHILDREN,  ///< Container has children
    WYRM_ERR_RANGE,     ///< Out of range
    WYRM_ERR_CYCLE,     ///< Cycle detected
    WYRM_ERR_KEY,       ///< Key not found

    WYRM_ERR_EXISTS,
    WYRM_ERR_NOMEM,
    WYRM_ERR_BAD_ARGUMENT_TYPE,
    WYRM_ERR_NOSUPPORT,     ///< Operation not supported by this backend
};

#ifndef __cplusplus
typedef enum wyrm_error wyrm_error;
#endif


static inline void wyrm_set_err(wyrm_error* error_ptr, wyrm_error error)
{
    if (error_ptr != WYRM_NULL) {
        *error_ptr = error;
    }
}

static inline bool wyrm_check_success(wyrm_error* error_ptr, wyrm_error error)
{
    if (error != WYRM_ERR_NONE)
    {
        wyrm_set_err(error_ptr, error);
        return false;
    }
    return true;
}

#endif
