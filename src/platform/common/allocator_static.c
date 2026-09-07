#include <wyrm/platform/common/allocator_static.h>
#include <wyrm/sys/string.h>

#define MAX_ALLOC_SZ (WY_WORD_MAX - sizeof(wy_uword))

static wy_word static_alloc_size(wy_allocator_static* a, const void *buffer)
{
    const char* header = (const char *)buffer - sizeof(wy_word);
    if (header >= a->storage && header < a->storage_current) {
        wy_word sz;
        wy_memcpy(&sz, header, (wy_uword)sizeof(wy_word));
        return sz;
    }

    return -1;
}

static void *static_alloc(wy_allocator *allocator, wy_uword len)
{
    wy_allocator_static *static_allocator = (wy_allocator_static *)allocator;

    WY_ASSERT(allocator && allocator->clz == &wy_allocator_static_vt);
    if (!len) { return WY_NULL; }
    if (len > MAX_ALLOC_SZ) { return WY_NULL; }

    wy_uword mod_len  = len % WY_BYTE_ALIGNMENT;
    wy_uword next_len = len;
    if (mod_len > 0) { next_len += WY_BYTE_ALIGNMENT - mod_len; }

    wy_uword total = (wy_uword)sizeof(wy_word) + next_len;
    if (total > static_allocator->storage_remaining) { return WY_NULL; }

    char *header = static_allocator->storage_current;
    wy_word s_len = (wy_word)len;
    wy_memcpy(header, &s_len, (wy_uword)sizeof(wy_word));

    char *result = header + sizeof(wy_word);
    static_allocator->storage_current   += total;
    static_allocator->storage_remaining -= total;
    return (void *)result;
}

static void *static_realloc(struct wy_allocator *self, void *buffer, wy_uword new_sz)
{
    wy_allocator_static *sa = (wy_allocator_static *)self;
    wy_uword old_sz = 0;

    if (new_sz > MAX_ALLOC_SZ) {
        return WY_NULL;
    }

    // Previous buffer - determine old allocated size, error check allocation
    if (buffer) {
        wy_word last_sz = static_alloc_size(sa, buffer);
        if (last_sz < 0) {
            return WY_NULL;
        }
        old_sz = (wy_uword) last_sz;
    }

    if (old_sz >= new_sz) {
        return buffer;
    }

    void *new_buffer = static_alloc(self, new_sz);
    if (new_buffer && old_sz > 0)
    {
        wy_memcpy(new_buffer, buffer, old_sz);
    }
    return new_buffer;
}

static void static_free(wy_allocator *allocator, void *buffer)
{
    WY_UNUSED(allocator);
    WY_UNUSED(buffer);

    WY_ASSERT(allocator && allocator->clz == &wy_allocator_static_vt);
    WY_ASSERT(!buffer || buffer >= (void *)((wy_allocator_static *)allocator)->storage);
    WY_ASSERT(!buffer || buffer < (void *)((wy_allocator_static *)allocator)->storage_current);
}


static wy_uword static_estimate_heap_size(wy_allocator *allocator)
{
    wy_allocator_static *sa = (wy_allocator_static *)allocator;
    if (sa->storage_current > sa->storage) {
        return sa->storage_current - sa->storage;
    }
    return 0;
}

const wy_allocator_vt wy_allocator_static_vt =
{
    .alloc              = static_alloc,
    .realloc            = static_realloc,
    .free               = static_free,
    .estimate_heap_size = static_estimate_heap_size,
};
