#include <wyrm/platform/hosted/allocator_cmem.h>
#include <stdlib.h>

union cmem_header
{
    wy_uword sz;
    char d;
    max_align_t pad_;
};


wy_allocator_cmem* wy_allocator_cmem_new(void)
{
    wy_allocator_cmem* allocator = malloc(sizeof(wy_allocator_cmem));
    if (allocator == WY_NULL) { return WY_NULL; }
    wy_allocator_cmem_init(allocator);
    return allocator;
}


void wy_allocator_cmem_destroy(wy_allocator_cmem* allocator)
{
    free(allocator);
}


static void* genc_alloc(wy_allocator* clz, wy_uword n)
{
    WY_ASSERT(clz && clz->clz == &wy_allocator_cmem_vt);
    wy_allocator_cmem* allocator = (wy_allocator_cmem*) clz;
    if (n == 0) { return NULL; }

    wy_uword req_size = sizeof(union cmem_header) + n;

    union cmem_header* data = (union cmem_header*) malloc(req_size);
    if (data == WY_NULL) { return WY_NULL; }

    data->sz = req_size;
    allocator->active_size += req_size;
    return (void*) (data + 1);
}

static void* genc_realloc(wy_allocator* clz, void* buffer, wy_uword n)
{
    WY_ASSERT(clz && clz->clz == &wy_allocator_cmem_vt);
    wy_allocator_cmem* allocator = (wy_allocator_cmem*) clz;
    if (n == 0) { return buffer; }

    void* shift_buffer = buffer;
    wy_uword new_size = n + sizeof(union cmem_header);
    wy_uword old_size = 0;

    if (buffer != WY_NULL) {
        union cmem_header* data = ((union cmem_header*) buffer) - 1;
        old_size = data->sz;
        shift_buffer = data;
    }

    union cmem_header* new_buffer = realloc(shift_buffer, new_size);
    if (new_buffer == WY_NULL) { return WY_NULL; }

    new_buffer->sz = new_size;
    allocator->active_size += new_size;
    allocator->active_size -= (allocator->active_size < old_size) ? allocator->active_size : old_size;
    return (void*) (new_buffer + 1);
}

static void genc_free(wy_allocator* clz, void* buffer)
{
    WY_ASSERT(clz && clz->clz == &wy_allocator_cmem_vt);
    wy_allocator_cmem* allocator = (wy_allocator_cmem*) clz;
    if (buffer == WY_NULL) { return; }

    union cmem_header* data = ((union cmem_header*) buffer) - 1;
    allocator->active_size -= (allocator->active_size < data->sz) ? allocator->active_size : data->sz;

    free(data);
}

static wy_uword genc_estimate_heap_size(wy_allocator* clz)
{
    wy_allocator_cmem* allocator = (wy_allocator_cmem*) clz;
    return allocator->active_size;
}

const wy_allocator_vt wy_allocator_cmem_vt = {
    .alloc = genc_alloc,
    .realloc = genc_realloc,
    .free = genc_free,
    .estimate_heap_size = genc_estimate_heap_size
};
