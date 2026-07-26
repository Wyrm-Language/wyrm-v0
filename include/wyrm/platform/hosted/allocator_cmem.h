#ifndef WYRM_PLATFORM_HOSTED_ALLOCATOR_CMEM_H_
#define WYRM_PLATFORM_HOSTED_ALLOCATOR_CMEM_H_

#include <wyrm/types.h>
#include <wyrm.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const wyrm_allocator_vt wyrm_allocator_cmem_vt;

struct wyrm_allocator_cmem
{
    wyrm_allocator base;
    wyrm_uword active_size;
};

WYRM_INLINE void wyrm_allocator_cmem_init(struct wyrm_allocator_cmem* allocator)
{
    allocator->base.clz = &wyrm_allocator_cmem_vt;
    allocator->active_size = 0;
}

WYRM_INLINE wyrm_allocator* wyrm_allocator_from_cmem(struct wyrm_allocator_cmem* allocator)
{
    return &allocator->base;
}

#ifndef __cplusplus
typedef struct wyrm_allocator_cmem wyrm_allocator_cmem;
#endif

#ifdef __cplusplus
}
#endif

#endif
