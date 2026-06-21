#ifndef WYRM_SYS_THREAD_ID_C11_H_
#define WYRM_SYS_THREAD_ID_C11_H_

#include <wyrm/sys/thread_id.h>

#if defined(WYRM_THREAD_USE_C11) && WYRM_THREAD_USE_C11
#include <threads.h>

struct wyrm_sys_thread_id
{
    thrd_t v;
};

// If this fires, thrd_t is too wide for wyrm_primitive on this platform.
// Disable the C11 thread backend and provide a platform-specific alternative.
static_assert(sizeof(struct wyrm_sys_thread_id) <= sizeof(uintptr_t),
              "thrd_t does not fit in wyrm_primitive; C11 thread backend unsupported on this platform");

static inline struct wyrm_sys_thread_id wyrm_sys_get_thread_id(void)
{
    struct wyrm_sys_thread_id id;
    id.v = thrd_current();
    return id;
}

#define WYRM_PRIMITIVE_HAS_THREAD_ID 1

#endif
#endif
