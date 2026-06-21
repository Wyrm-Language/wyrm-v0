#ifndef WYRM_PLATFORM_COMMON_THREAD_SYSTEM_NONE_H_
#define WYRM_PLATFORM_COMMON_THREAD_SYSTEM_NONE_H_

#include <wyrm/types.h>
#include <wyrm/threads.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const wyrm_thread_system_vt wyrm_thread_system_none_vt;

/**
 * @brief Stub thread system for single-threaded / bare-metal targets.
 *
 * Thread creation is not supported (returns WYRM_ERR_NOSUPPORT).
 * Mutexes are no-ops.
 * Thread-specific data is backed by a fixed global table — values are shared
 * across the single thread of execution.
 */
struct wyrm_thread_system_none
{
    wyrm_thread_system base;
};

#ifndef __cplusplus
typedef struct wyrm_thread_system_none wyrm_thread_system_none;
#endif

/**
 * @brief Initialise the stub thread system.
 *
 * @param self       Thread system to initialise.
 * @param allocator  Allocator used for mutex object storage.
 */
static inline void wyrm_thread_system_none_init(struct wyrm_thread_system_none *self,
                                                 wyrm_allocator *allocator)
{
    self->base.head.type               = WYRM_NULL;
    self->base.head.parent             = WYRM_NULL;
    self->base.head.children.arr       = WYRM_NULL;
    self->base.head.children.count     = 0;
    self->base.head.children.allocator = WYRM_NULL;
    self->base.head.children.capacity  = 0;
    self->base.vt                      = &wyrm_thread_system_none_vt;
    self->base.allocator               = allocator;
}

static inline wyrm_thread_system_ref
wyrm_thread_system_ref_from_none(struct wyrm_thread_system_none *self)
{
    return &self->base;
}

#ifdef __cplusplus
}
#endif

#endif
