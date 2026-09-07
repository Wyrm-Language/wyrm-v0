#include <wyrm/platform/hosted/allocator_cmem.h>

static wy_allocator_cmem wy_allocator_hosted_sys = {
    .base = {
        .clz = &wy_allocator_cmem_vt
    }
};

wy_allocator* wy_allocator_hosted_get_sys(void)
{
    return &wy_allocator_hosted_sys.base;
}
