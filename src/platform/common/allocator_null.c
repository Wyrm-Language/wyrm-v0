#include <wyrm.h>
#include <wyrm/platform/common/allocator_null.h>


static void *null_alloc(wy_allocator *allocator, wy_uword len)
{
    WY_UNUSED(allocator);
    WY_UNUSED(len);
    return WY_NULL;
}

static void *null_realloc(struct wy_allocator *self, void *buffer, wy_uword new_sz)
{
    WY_UNUSED(self);
    WY_UNUSED(buffer);
    WY_UNUSED(new_sz);
    return WY_NULL;
}

static void null_free(wy_allocator *allocator, void *buffer)
{
    WY_UNUSED(allocator);
    WY_UNUSED(buffer);
}

static wy_uword null_estimate_heap_size(wy_allocator *allocator)
{
    WY_UNUSED(allocator);
    return 0;
}

const wy_allocator_vt wy_allocator_null_vt =
{
    .alloc              = null_alloc,
    .realloc            = null_realloc,
    .free               = null_free,
    .estimate_heap_size = null_estimate_heap_size,
};

wy_allocator wy_allocator_null_global = {
    .clz = &wy_allocator_null_vt,
};
