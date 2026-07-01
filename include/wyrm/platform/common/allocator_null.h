#ifndef WYRM_PLATFORM_COMMON_ALLOCATOR_NULL_H_
#define WYRM_PLATFORM_COMMON_ALLOCATOR_NULL_H_

#include <wyrm/types.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const wyrm_allocator_vt wyrm_allocator_null_vt;
typedef wyrm_allocator wyrm_allocator_null;
extern wyrm_allocator_null wyrm_allocator_null_global;

/**
 * Null Allocator
 *
 * Allocation always fails. Free is a noop.
 */
WYRM_INLINE void wyrm_allocator_null_init(wyrm_allocator_null *allocator)
{
    allocator->clz       = &wyrm_allocator_null_vt;
}

#ifdef __cplusplus
}
#endif

#endif
