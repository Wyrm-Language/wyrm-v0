#include <wyrm/platform/hosted/allocator_cmem.h>
#include <stdlib.h>

union cmem_header
{
    wyrm_uword sz;
    char d;
    max_align_t pad_;
};


wyrm_allocator_cmem* wyrm_allocator_cmem_new(void)
{
    wyrm_allocator_cmem* allocator = malloc(sizeof(wyrm_allocator_cmem));
    if (allocator == WYRM_NULL) { return WYRM_NULL; }
    wyrm_allocator_cmem_init(allocator);
    return allocator;
}


void wyrm_allocator_cmem_destroy(wyrm_allocator_cmem* allocator)
{
    free(allocator);
}


static void* genc_alloc(wyrm_allocator* clz, wyrm_uword n)
{
    WYRM_ASSERT(clz && clz->clz == &wyrm_allocator_cmem_vt);
    wyrm_allocator_cmem* allocator = (wyrm_allocator_cmem*) clz;
    if (n == 0) { return NULL; }

    wyrm_uword req_size = sizeof(union cmem_header) + n;

    union cmem_header* data = (union cmem_header*) malloc(req_size);
    if (data == WYRM_NULL) { return WYRM_NULL; }

    data->sz = req_size;
    allocator->active_size += req_size;
    return (void*) (data + 1);
}

static void* genc_realloc(wyrm_allocator* clz, void* buffer, wyrm_uword n)
{
    WYRM_ASSERT(clz && clz->clz == &wyrm_allocator_cmem_vt);
    wyrm_allocator_cmem* allocator = (wyrm_allocator_cmem*) clz;
    if (n == 0) { return buffer; }

    void* shift_buffer = buffer;
    wyrm_uword new_size = n + sizeof(union cmem_header);
    wyrm_uword old_size = 0;

    if (buffer != WYRM_NULL) {
        union cmem_header* data = ((union cmem_header*) buffer) - 1;
        old_size = data->sz;
        shift_buffer = data;
    }

    union cmem_header* new_buffer = realloc(shift_buffer, new_size);
    if (new_buffer == WYRM_NULL) { return WYRM_NULL; }

    new_buffer->sz = new_size;
    allocator->active_size += new_size;
    allocator->active_size -= (allocator->active_size < old_size) ? allocator->active_size : old_size;
    return (void*) (new_buffer + 1);
}

static void genc_free(wyrm_allocator* clz, void* buffer)
{
    WYRM_ASSERT(clz && clz->clz == &wyrm_allocator_cmem_vt);
    wyrm_allocator_cmem* allocator = (wyrm_allocator_cmem*) clz;
    if (buffer == WYRM_NULL) { return; }

    union cmem_header* data = ((union cmem_header*) buffer) - 1;
    allocator->active_size -= (allocator->active_size < data->sz) ? allocator->active_size : data->sz;

    free(data);
}

static wyrm_uword genc_estimate_heap_size(wyrm_allocator* clz)
{
    wyrm_allocator_cmem* allocator = (wyrm_allocator_cmem*) clz;
    return allocator->active_size;
}

const wyrm_allocator_vt wyrm_allocator_cmem_vt = {
    .alloc = genc_alloc,
    .realloc = genc_realloc,
    .free = genc_free,
    .estimate_heap_size = genc_estimate_heap_size
};
