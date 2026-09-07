#ifndef WYRM_PLATFORM_COMMON_ALLOCATOR_STATIC_H_
#define WYRM_PLATFORM_COMMON_ALLOCATOR_STATIC_H_

#include <wyrm/allocator.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const wy_allocator_vt wy_allocator_static_vt;

/**
 * @brief Bump-pointer allocator backed by a caller-supplied aligned buffer.
 *
 * Each allocation is preceded by a wy_uword header storing the original
 * requested size; realloc reads this header to bound the copy.  Free is a
 * no-op; memory is reclaimed only when the allocator itself is discarded.
 * Suitable for fixed-size arenas and bare-metal targets.
 */
typedef struct wy_allocator_static
{
    wy_allocator base;

    char *storage;
    char *storage_current;
    size_t storage_remaining;
} wy_allocator_static;

/**
 * @brief Initialise a static allocator over an aligned buffer.
 *
 * @param allocator       Allocator to initialise.
 * @param aligned_storage Caller-owned buffer; must satisfy WY_BYTE_ALIGNMENT.
 * @param size            Size of the buffer in bytes.
 */
static inline void wy_allocator_static_init(struct wy_allocator_static *allocator,
                                               char *aligned_storage,
                                               size_t size)
{
    WY_ASSERT_ALIGNED(aligned_storage);
    allocator->base.clz       = &wy_allocator_static_vt;
    allocator->storage         = aligned_storage;
    allocator->storage_current = aligned_storage;
    allocator->storage_remaining = size;
}

static inline wy_allocator *wy_allocator_from_static(struct wy_allocator_static *ws)
{
    if (ws == WY_NULL) { return WY_NULL; }
    WY_ASSERT(ws->base.clz == &wy_allocator_static_vt);
    return &ws->base;
}

static inline struct wy_allocator_static *wy_allocator_to_static(wy_allocator *wa)
{
    if (wa == WY_NULL || wa->clz != &wy_allocator_static_vt) { return WY_NULL; }
    return (struct wy_allocator_static *)wa;
}

#ifdef __cplusplus
}
#endif

#endif
