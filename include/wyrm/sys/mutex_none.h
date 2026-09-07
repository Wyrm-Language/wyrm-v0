#ifndef WYRM_SYS_MUTEX_NONE_H_
#define WYRM_SYS_MUTEX_NONE_H_

#include <wyrm/sys/mutex.h>
#include <wyrm/sys/errors.h>

#if defined(WY_THREAD_USE_NONE) && WY_THREAD_USE_NONE

struct wy_sys_mutex
{
    char _unused;
};

static inline wy_error wy_sys_mutex_init(struct wy_sys_mutex *mutex, const char *name)
{
    WY_UNUSED(mutex);
    WY_UNUSED(name);
    return WY_ERR_NONE;
}

static inline wy_error wy_sys_mutex_lock(struct wy_sys_mutex *mutex)
{
    WY_UNUSED(mutex);
    return WY_ERR_NONE;
}

static inline wy_error wy_sys_mutex_unlock(struct wy_sys_mutex *mutex)
{
    WY_UNUSED(mutex);
    return WY_ERR_NONE;
}

static inline void wy_sys_mutex_unlock_f(struct wy_sys_mutex *mutex)
{
    WY_UNUSED(mutex);
}

#endif
#endif
