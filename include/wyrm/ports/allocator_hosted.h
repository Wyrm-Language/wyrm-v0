#ifndef WYRM_PLATFORM_HOSTED_ALLOCATOR_HOSTED_H_
#define WYRM_PLATFORM_HOSTED_ALLOCATOR_HOSTED_H_

#include <wyrm/allocator.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Retrieve the default system memory allocator.
 *
 * This function returns the default system memory allocator. If the host
 * does not support dynamic memory allocation, this function will return NULL,
 * and the wyrm machine will need initialized with a manually specified system
 * allocator.
 *
 * @return System memory allocator
 */
wy_allocator* wy_allocator_hosted_get_sys(void);

#ifdef __cplusplus
}
#endif

#endif
