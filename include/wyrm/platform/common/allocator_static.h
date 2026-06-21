#ifndef WYRM_PLATFORM_COMMON_ALLOCATOR_STATIC_H_
#define WYRM_PLATFORM_COMMON_ALLOCATOR_STATIC_H_

#include <wyrm/types.h>
#include <wyrm/internal_api.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const wyrm_allocator_vt wyrm_allocator_static_vt;

/**
 * @brief Bump-pointer allocator backed by a caller-supplied aligned buffer.
 *
 * Each allocation is preceded by a wyrm_uword header storing the original
 * requested size; realloc reads this header to bound the copy.  Free is a
 * no-op; memory is reclaimed only when the allocator itself is discarded.
 * Suitable for fixed-size arenas and bare-metal targets.
 */
typedef struct wyrm_allocator_static
{
    wyrm_allocator base;

    char *storage;
    char *storage_current;
    size_t storage_remaining;
} wyrm_allocator_static;

/**
 * @brief Initialise a static allocator over an aligned buffer.
 *
 * @param allocator       Allocator to initialise.
 * @param aligned_storage Caller-owned buffer; must satisfy WYRM_BYTE_ALIGNMENT.
 * @param size            Size of the buffer in bytes.
 */
static inline void wyrm_allocator_static_init(struct wyrm_allocator_static *allocator,
                                               char *aligned_storage,
                                               size_t size)
{
    WYRM_ASSERT_ALIGNED(aligned_storage);
    allocator->base.clz       = &wyrm_allocator_static_vt;
    allocator->storage         = aligned_storage;
    allocator->storage_current = aligned_storage;
    allocator->storage_remaining = size;
}

static inline wyrm_allocator *wyrm_allocator_from_static(struct wyrm_allocator_static *ws)
{
    if (ws == WYRM_NULL) { return WYRM_NULL; }
    WYRM_ASSERT(ws->base.clz == &wyrm_allocator_static_vt);
    return &ws->base;
}

static inline struct wyrm_allocator_static *wyrm_allocator_to_static(wyrm_allocator *wa)
{
    if (wa == WYRM_NULL || wa->clz != &wyrm_allocator_static_vt) { return WYRM_NULL; }
    return (struct wyrm_allocator_static *)wa;
}

#ifdef __cplusplus
}
#endif

#endif
