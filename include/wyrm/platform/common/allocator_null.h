#ifndef WYRM_PLATFORM_COMMON_ALLOCATOR_NULL_H_
#define WYRM_PLATFORM_COMMON_ALLOCATOR_NULL_H_

#include <wyrm/types.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const wy_allocator_vt wy_allocator_null_vt;
typedef wy_allocator wy_allocator_null;
extern wy_allocator_null wy_allocator_null_global;

/**
 * Null Allocator
 *
 * Allocation always fails. Free is a noop.
 */
WY_INLINE void wy_allocator_null_init(wy_allocator_null *allocator)
{
    allocator->clz       = &wy_allocator_null_vt;
}

#ifdef __cplusplus
}
#endif

#endif
