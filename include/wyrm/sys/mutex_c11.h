#ifndef WYRM_SYS_MUTEX_C11_H_
#define WYRM_SYS_MUTEX_C11_H_

#include <wyrm/sys/mutex.h>
#include <wyrm/sys/toolchain.h>
#include <wyrm/sys/errors.h>

#if defined(WY_THREAD_USE_C11) && WY_THREAD_USE_C11
#include <threads.h>

struct wy_sys_mutex
{
    struct {
        mtx_t v;
        const char *name;
    } impl;
};

static inline wy_error wy_sys_mutex_init(struct wy_sys_mutex *mutex, const char *name)
{
    if (mutex == WY_NULL) { return WY_ERR_INVAL; }
    mutex->impl.name = name;
    mtx_init(&mutex->impl.v, mtx_plain);
    return WY_ERR_NONE;
}

static inline wy_error wy_sys_mutex_lock(struct wy_sys_mutex *mutex)
{
    if (mutex == WY_NULL) { return WY_ERR_INVAL; }
    if (mtx_lock(&mutex->impl.v) != thrd_success) { return WY_ERR_UNKNOWN; }
    return WY_ERR_NONE;
}

static inline wy_error wy_sys_mutex_unlock(struct wy_sys_mutex *mutex)
{
    if (mutex == WY_NULL) { return WY_ERR_INVAL; }
    if (mtx_unlock(&mutex->impl.v) != thrd_success) { return WY_ERR_UNKNOWN; }
    return WY_ERR_NONE;
}

static inline void wy_sys_mutex_unlock_f(struct wy_sys_mutex *mutex)
{
#if defined(WY_ASSERT_CHECKS) && WY_ASSERT_CHECKS
    WY_ASSERT(mutex != WY_NULL);
    int _r = mtx_unlock(&mutex->impl.v);
    WY_ASSERT(_r == thrd_success);
#else
    (void) mtx_unlock(&mutex->impl.v);
#endif
}

#endif
#endif
