#include <wyrm.h>
#include <wyrm/platform/common/allocator_null.h>


static void *null_alloc(wyrm_allocator *allocator, wyrm_uword len)
{
    WYRM_UNUSED(allocator);
    WYRM_UNUSED(len);
    return WYRM_NULL;
}

static void *null_realloc(struct wyrm_allocator *self, void *buffer, wyrm_uword new_sz)
{
    WYRM_UNUSED(self);
    WYRM_UNUSED(buffer);
    WYRM_UNUSED(new_sz);
    return WYRM_NULL;
}

static void null_free(wyrm_allocator *allocator, void *buffer)
{
    WYRM_UNUSED(allocator);
    WYRM_UNUSED(buffer);
}

static wyrm_uword null_estimate_heap_size(wyrm_allocator *allocator)
{
    WYRM_UNUSED(allocator);
    return 0;
}

const wyrm_allocator_vt wyrm_allocator_null_vt =
{
    .alloc              = null_alloc,
    .realloc            = null_realloc,
    .free               = null_free,
    .estimate_heap_size = null_estimate_heap_size,
};

wyrm_allocator wyrm_allocator_null_global = {
    .clz = &wyrm_allocator_null_vt,
};
