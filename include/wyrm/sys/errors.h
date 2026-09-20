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
enum wy_error {
    WY_ERR_NONE = 0,                  ///< Operation completed successfully
    WY_ERR_CONTINUE = 1,              ///< Operation paused and return result set to continuation
    WY_ERR_INCONSISTENT_STATE = 2,    ///< Operation detected inconsistent state within VM, fatal error

    WY_ERR_UNKNOWN,           ///< General / unknown failure
    WY_ERR_INVAL,             ///< Invalid parameter (such as nullptr)
    WY_ERR_BAD_TYPE,          ///< Unable to convert to given type
    WY_ERR_BUSY,              ///< Object is busy
    WY_ERR_UNBOUND,           ///< Unbound or unknown variable
    WY_ERR_PERM,              ///< Bad permissions
    WY_ERR_EMPTY,             ///< Container is empty
    WY_ERR_CHILDREN,          ///< Container has children
    WY_ERR_RANGE,             ///< Out of range
    WY_ERR_CYCLE,             ///< Cycle detected
    WY_ERR_KEY,               ///< Key not found
    WY_ERR_STACK_OVERFLOW,    ///< Insufficient memory in stack
    WY_ERR_STOP_ITERATION,    ///< Iterator is completed

    WY_ERR_EXISTS,
    WY_ERR_NOMEM,
    WY_ERR_BAD_ARGUMENT_TYPE,
    WY_ERR_NOSUPPORT,     ///< Operation not supported by this backend
    WY_ERR_IMAGE,         ///< Malformed .wyc module image (pypoc/doc/wyc-format.md)
    WY_ERR_LINK,          ///< Module linking/loading failed after a well-formed image parsed
};

#ifndef __cplusplus
typedef enum wy_error wy_error;
#endif

static inline void wy_set_err(wy_error* error_ptr, wy_error error)
{
    if (error_ptr != WY_NULL) {
        *error_ptr = error;
    }
}

static inline bool wy_check_success(wy_error* error_ptr, wy_error error)
{
    if (error != WY_ERR_NONE)
    {
        wy_set_err(error_ptr, error);
        return false;
    }
    return true;
}

#endif
