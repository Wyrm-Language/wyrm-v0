#ifndef WYRM_SYS_MUTEX_H_
#define WYRM_SYS_MUTEX_H_

#include <wyrm/sys/errors.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// @brief OS Mutex storage — opaque; defined by the active platform backend
struct wy_sys_mutex;

/// @brief Initialize an OS mutex
static inline wy_error wy_sys_mutex_init(struct wy_sys_mutex *mutex, const char *name);

/// @brief Lock an OS mutex (blocking)
static inline wy_error wy_sys_mutex_lock(struct wy_sys_mutex *mutex);

/// @brief Unlock an OS mutex
static inline wy_error wy_sys_mutex_unlock(struct wy_sys_mutex *mutex);

/// @brief Unlock an OS mutex — fast path, preconditions assumed verified by caller
static inline void wy_sys_mutex_unlock_f(struct wy_sys_mutex *mutex);

#ifdef __cplusplus
}
#endif

// ---- Platform backend selection ----

#include <wyrm/sys/thread_common.h>

#include <wyrm/sys/mutex_c11.h>
#include <wyrm/sys/mutex_none.h>

#ifndef __cplusplus
typedef struct wy_sys_mutex wy_sys_mutex;
#endif

#endif
