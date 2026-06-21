#ifndef WYRM_SYS_THREAD_ID_NONE_H_
#define WYRM_SYS_THREAD_ID_NONE_H_

#include <wyrm/sys/thread_id.h>

#if defined(WYRM_THREAD_USE_NONE) && WYRM_THREAD_USE_NONE

struct wyrm_sys_thread_id
{
    wyrm_uword v;
};

static inline struct wyrm_sys_thread_id wyrm_sys_get_thread_id(void)
{
    struct wyrm_sys_thread_id id;
    id.v = 1u; // single-thread sentinel — always the same "thread"
    return id;
}

#define WYRM_PRIMITIVE_HAS_THREAD_ID 1

#endif
#endif
