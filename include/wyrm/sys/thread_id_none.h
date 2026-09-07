#ifndef WYRM_SYS_THREAD_ID_NONE_H_
#define WYRM_SYS_THREAD_ID_NONE_H_

#include <wyrm/sys/thread_id.h>

#if defined(WY_THREAD_USE_NONE) && WY_THREAD_USE_NONE

struct wy_sys_thread_id
{
    wy_uword v;
};

static inline struct wy_sys_thread_id wy_sys_get_thread_id(void)
{
    struct wy_sys_thread_id id;
    id.v = 1u; // single-thread sentinel — always the same "thread"
    return id;
}

#define WY_PRIMITIVE_HAS_THREAD_ID 1

#endif
#endif
