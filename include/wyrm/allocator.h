#ifndef WYRM_ALLOCATOR_H_
#define WYRM_ALLOCATOR_H_

#include <wyrm/sys/toolchain.h>
#include <wyrm/fwd.h>

WY_BEGIN_DECLS

/**
 * @brief Virtual table for allocator
 */
typedef struct wy_allocator_vt {
    void* (*alloc)(wy_allocator* self, wy_uword len);
    void* (*realloc)(wy_allocator* self, void* buffer, wy_uword new_sz);
    void (*free)(wy_allocator* self, void* buffer);
    wy_uword (*estimate_heap_size)(wy_allocator* self);
} wy_allocator_vt;


/**
 * @brief Allocator data structure
 */
struct wy_allocator {
    const wy_allocator_vt* clz;
};


/**
 * Allocates memory of the specified length using the provided allocator.
 *
 * @param self Pointer to the allocator instance used for memory allocation.
 * @param len  The size of memory (in bytes) to allocate.
 * @return A pointer to the allocated memory on success. Returns `WY_NULL` if
 *         allocation fails or if the provided allocator is invalid.
 */
WY_INLINE void* wy_allocator_alloc(wy_allocator* self, wy_uword len) {
    if (self == WY_NULL || self->clz == WY_NULL) { return WY_NULL; }
    return self->clz->alloc(self, len);
}

/**
 * Allocates a memory region sufficient to hold a header with trailing element array
 *
 * @param self       Pointer to the allocator instance used for memory allocation.
 * @param header_sz  The size (in bytes) of the header to be included in the allocation.
 * @param element_sz The size (in bytes) of a single array element.
 * @param count      The number of elements to allocate in the array.
 * @return A pointer to the allocated memory on success. Returns `WY_NULL` if
 *         allocation fails or if the provided allocator is invalid.
 */
WY_INLINE void* wy_allocator_alloc_array(wy_allocator* self, size_t header_sz, wy_uword element_sz, wy_uword count)
{
    if (self == WY_NULL) { return WY_NULL; }
    return wy_allocator_alloc(self, header_sz + (element_sz * count));
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
WY_INLINE void* wy_allocator_realloc(wy_allocator* self, void* buffer, wy_uword len) {
    if (self == WY_NULL || self->clz == WY_NULL) { return WY_NULL; }
    return self->clz->realloc(self, buffer, len);
}

/**
 * Frees previously allocated memory using the provided allocator.
 *
 * @param self   Pointer to the allocator instance used for memory deallocation.
 * @param buffer Pointer to the memory to be freed. If `NULL`, no action is taken.
 */
WY_INLINE void wy_allocator_free(wy_allocator* self, void* buffer) {
    if (self != WY_NULL && self->clz != WY_NULL) {
        self->clz->free(self, buffer);
    }
}

/**
 * Estimate total heap size of the allocator
 *
 * @param self Pointer to the allocator instance used
 * @return Estimated heap usage by this allocator
 */
WY_INLINE wy_uword wy_allocator_estimate_heap_size(wy_allocator* self)
{
    if (self == WY_NULL ||
        self->clz == WY_NULL ||
        self->clz->estimate_heap_size == WY_NULL) {
        return 0;
    }
    return self->clz->estimate_heap_size(self);
}


WY_END_DECLS

#endif
