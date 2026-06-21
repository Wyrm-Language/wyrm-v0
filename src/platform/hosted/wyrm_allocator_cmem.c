#include <wyrm/platform/hosted/allocator_cmem.h>
#include <stdlib.h>

static void* genc_alloc(wyrm_allocator* clz, wyrm_uword n)
{
    WYRM_UNUSED(clz);
    WYRM_ASSERT(clz && clz->clz == &wyrm_allocator_cmem_vt);
    if (n == 0) { return NULL; }
    return malloc(n);
}

static void* genc_realloc(wyrm_allocator* clz, void* buffer, wyrm_uword n)
{
    WYRM_UNUSED(clz);
    WYRM_ASSERT(clz && clz->clz == &wyrm_allocator_cmem_vt);
    if (n == 0) { return buffer; }
    return realloc(buffer, n);
}

static void genc_free(wyrm_allocator* clz, void* buffer)
{
    WYRM_UNUSED(clz);
    WYRM_ASSERT(clz && clz->clz == &wyrm_allocator_cmem_vt);
    free(buffer);
}

const wyrm_allocator_vt wyrm_allocator_cmem_vt = {
    .alloc = genc_alloc,
    .realloc = genc_realloc,
    .free = genc_free
};
