#ifndef WYRM_TYPES_H_
#define WYRM_TYPES_H_

#include <wyrm/sys/toolchain.h>
#include <wyrm/sys/atomics.h>
#include <wyrm/sys/errors.h>

#ifdef __cplusplus
extern "C" {
#endif

// ----------------------------------------------------------------------------
// Allocator
// ----------------------------------------------------------------------------

struct wyrm_allocator;

/// @brief Virtual table for allocator
typedef struct wyrm_allocator_vt {
    void* (*alloc)(struct wyrm_allocator* self, wyrm_uword len);
    void* (*realloc)(struct wyrm_allocator* self, void* buffer, wyrm_uword new_sz);
    void (*free)(struct wyrm_allocator* self, void* buffer);
} wyrm_allocator_vt;

/// @brief Allocator data structure
///
/// The base data structure for an allocator.
typedef struct wyrm_allocator {
    const struct wyrm_allocator_vt* clz;
} wyrm_allocator;


#ifdef __cplusplus
}
#endif

#endif
