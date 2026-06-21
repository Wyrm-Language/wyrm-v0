#ifndef WYRM_SYS_MUTEX_C11_H_
#define WYRM_SYS_MUTEX_C11_H_

#include <wyrm/sys/mutex.h>
#include <wyrm/sys/toolchain.h>
#include <wyrm/sys/errors.h>

#if defined(WYRM_THREAD_USE_C11) && WYRM_THREAD_USE_C11
#include <threads.h>

struct wyrm_sys_mutex
{
    struct {
        mtx_t v;
        const char *name;
    } impl;
};

static inline wyrm_error wyrm_sys_mutex_init(struct wyrm_sys_mutex *mutex, const char *name)
{
    if (mutex == WYRM_NULL) { return WYRM_ERR_INVAL; }
    mutex->impl.name = name;
    mtx_init(&mutex->impl.v, mtx_plain);
    return WYRM_ERR_NONE;
}

static inline wyrm_error wyrm_sys_mutex_lock(struct wyrm_sys_mutex *mutex)
{
    if (mutex == WYRM_NULL) { return WYRM_ERR_INVAL; }
    if (mtx_lock(&mutex->impl.v) != thrd_success) { return WYRM_ERR_UNKNOWN; }
    return WYRM_ERR_NONE;
}

static inline wyrm_error wyrm_sys_mutex_unlock(struct wyrm_sys_mutex *mutex)
{
    if (mutex == WYRM_NULL) { return WYRM_ERR_INVAL; }
    if (mtx_unlock(&mutex->impl.v) != thrd_success) { return WYRM_ERR_UNKNOWN; }
    return WYRM_ERR_NONE;
}

static inline void wyrm_sys_mutex_unlock_f(struct wyrm_sys_mutex *mutex)
{
#if defined(WYRM_ASSERT_CHECKS) && WYRM_ASSERT_CHECKS
    WYRM_ASSERT(mutex != WYRM_NULL);
    int _r = mtx_unlock(&mutex->impl.v);
    WYRM_ASSERT(_r == thrd_success);
#else
    (void) mtx_unlock(&mutex->impl.v);
#endif
}

#endif
#endif
