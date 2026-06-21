#include <wyrm/platform/hosted/allocator_cmem.h>

static wyrm_allocator_cmem wyrm_allocator_hosted_sys = {
    .base = {
        .clz = &wyrm_allocator_cmem_vt
    }
};

wyrm_allocator* wyrm_allocator_hosted_get_sys(void)
{
    return &wyrm_allocator_hosted_sys.base;
}
