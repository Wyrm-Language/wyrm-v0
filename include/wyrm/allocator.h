#ifndef WYRM_ALLOCATOR_H_
#define WYRM_ALLOCATOR_H_

#include <wyrm/types.h>

typedef wyrm_allocator wy_allocator;

WYRM_BEGIN_DECLS

/**
 * Allocates memory of the specified length using the provided allocator.
 *
 * @param self Pointer to the allocator instance used for memory allocation.
 * @param len  The size of memory (in bytes) to allocate.
 * @return A pointer to the allocated memory on success. Returns `WYRM_NULL` if
 *         allocation fails or if the provided allocator is invalid.
 */
WYRM_INLINE void* wyrm_allocator_alloc(wyrm_allocator* self, wyrm_uword len) {
    if (self == WYRM_NULL || self->clz == WYRM_NULL) { return WYRM_NULL; }
    return self->clz->alloc(self, len);
}

/**
 * Allocates a memory region sufficient to hold a header with trailing element array
 *
 * @param self       Pointer to the allocator instance used for memory allocation.
 * @param header_sz  The size (in bytes) of the header to be included in the allocation.
 * @param element_sz The size (in bytes) of a single array element.
 * @param count      The number of elements to allocate in the array.
 * @return A pointer to the allocated memory on success. Returns `WYRM_NULL` if
 *         allocation fails or if the provided allocator is invalid.
 */
WYRM_INLINE void* wyrm_allocator_alloc_array(wyrm_allocator* self, size_t header_sz, wyrm_uword element_sz, wyrm_uword count)
{
    if (self == WYRM_NULL) { return WYRM_NULL; }
    return wyrm_allocator_alloc(self, header_sz + (element_sz * count));
}

/**
 * Reallocates memory for a specified buffer to a new size using the provided allocator.
 *
 * @param self   Pointer to the allocator instance to use for reallocation.
 * @param buffer Pointer to the existing memory buffer to be resized. Can be `NULL` to allocate a new buffer.
 * @param len    The new size of the memory buffer (in bytes).
 * @return A pointer to the reallocated memory buffer on success. Returns `NULL` if the
 *         reallocation fails or if the provided allocator is invalid.
 */
WYRM_INLINE void* wyrm_allocator_realloc(wyrm_allocator* self, void* buffer, wyrm_uword len) {
    if (self == WYRM_NULL || self->clz == WYRM_NULL) { return WYRM_NULL; }
    return self->clz->realloc(self, buffer, len);
}

/**
 * Frees previously allocated memory using the provided allocator.
 *
 * @param self   Pointer to the allocator instance used for memory deallocation.
 * @param buffer Pointer to the memory to be freed. If `NULL`, no action is taken.
 */
WYRM_INLINE void wyrm_allocator_free(wyrm_allocator* self, void* buffer) {
    if (self != WYRM_NULL && self->clz != WYRM_NULL) {
        self->clz->free(self, buffer);
    }
}

/**
 * Estimate total heap size of the allocator
 *
 * @param self Pointer to the allocator instance used
 * @return Estimated heap usage by this allocator
 */
WYRM_INLINE wyrm_uword wyrm_allocator_estimate_heap_size(wyrm_allocator* self)
{
    if (self == WYRM_NULL ||
        self->clz == WYRM_NULL ||
        self->clz->estimate_heap_size == WYRM_NULL) {
        return 0;
    }
    return self->clz->estimate_heap_size(self);
}


WYRM_END_DECLS

#endif
