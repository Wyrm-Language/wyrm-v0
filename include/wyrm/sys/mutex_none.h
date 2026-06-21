#ifndef WYRM_SYS_MUTEX_NONE_H_
#define WYRM_SYS_MUTEX_NONE_H_

#include <wyrm/sys/mutex.h>
#include <wyrm/sys/errors.h>

#if defined(WYRM_THREAD_USE_NONE) && WYRM_THREAD_USE_NONE

struct wyrm_sys_mutex
{
    char _unused;
};

static inline wyrm_error wyrm_sys_mutex_init(struct wyrm_sys_mutex *mutex, const char *name)
{
    WYRM_UNUSED(mutex);
    WYRM_UNUSED(name);
    return WYRM_ERR_NONE;
}

static inline wyrm_error wyrm_sys_mutex_lock(struct wyrm_sys_mutex *mutex)
{
    WYRM_UNUSED(mutex);
    return WYRM_ERR_NONE;
}

static inline wyrm_error wyrm_sys_mutex_unlock(struct wyrm_sys_mutex *mutex)
{
    WYRM_UNUSED(mutex);
    return WYRM_ERR_NONE;
}

static inline void wyrm_sys_mutex_unlock_f(struct wyrm_sys_mutex *mutex)
{
    WYRM_UNUSED(mutex);
}

#endif
#endif
