#ifndef WYRM_PLATFORM_HOSTED_ALLOCATOR_CMEM_H_
#define WYRM_PLATFORM_HOSTED_ALLOCATOR_CMEM_H_

#include <wyrm/types.h>
#include <wyrm.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const wy_allocator_vt wy_allocator_cmem_vt;

struct wy_allocator_cmem
{
    wy_allocator base;
    wy_uword active_size;
};

#ifndef __cplusplus
typedef struct wy_allocator_cmem wy_allocator_cmem;
#endif

wy_allocator_cmem* wy_allocator_cmem_new(void);
void wy_allocator_cmem_destroy(wy_allocator_cmem* allocator);

WY_INLINE void wy_allocator_cmem_init(struct wy_allocator_cmem* allocator)
{
    allocator->base.clz = &wy_allocator_cmem_vt;
    allocator->active_size = 0;
}

WY_INLINE wy_allocator* wy_allocator_from_cmem(struct wy_allocator_cmem* allocator)
{
    return &allocator->base;
}


#ifdef __cplusplus
}
#endif

#endif
