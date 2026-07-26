#include <wyrm/platform/common/allocator_static.h>
#include <wyrm/sys/string.h>

#define MAX_ALLOC_SZ (WYRM_WORD_MAX - sizeof(wyrm_uword))

static wyrm_word static_alloc_size(wyrm_allocator_static* a, const void *buffer)
{
    const char* header = (const char *)buffer - sizeof(wyrm_word);
    if (header >= a->storage && header < a->storage_current) {
        wyrm_word sz;
        wyrm_memcpy(&sz, header, (wyrm_uword)sizeof(wyrm_word));
        return sz;
    }

    return -1;
}

static void *static_alloc(wyrm_allocator *allocator, wyrm_uword len)
{
    wyrm_allocator_static *static_allocator = (wyrm_allocator_static *)allocator;

    WYRM_ASSERT(allocator && allocator->clz == &wyrm_allocator_static_vt);
    if (!len) { return WYRM_NULL; }
    if (len > MAX_ALLOC_SZ) { return WYRM_NULL; }

    wyrm_uword mod_len  = len % WYRM_BYTE_ALIGNMENT;
    wyrm_uword next_len = len;
    if (mod_len > 0) { next_len += WYRM_BYTE_ALIGNMENT - mod_len; }

    wyrm_uword total = (wyrm_uword)sizeof(wyrm_word) + next_len;
    if (total > static_allocator->storage_remaining) { return WYRM_NULL; }

    char *header = static_allocator->storage_current;
    wyrm_word s_len = (wyrm_word)len;
    wyrm_memcpy(header, &s_len, (wyrm_uword)sizeof(wyrm_word));

    char *result = header + sizeof(wyrm_word);
    static_allocator->storage_current   += total;
    static_allocator->storage_remaining -= total;
    return (void *)result;
}

static void *static_realloc(struct wyrm_allocator *self, void *buffer, wyrm_uword new_sz)
{
    wyrm_allocator_static *sa = (wyrm_allocator_static *)self;
    wyrm_uword old_sz = 0;

    if (new_sz > MAX_ALLOC_SZ) {
        return WYRM_NULL;
    }

    // Previous buffer - determine old allocated size, error check allocation
    if (buffer) {
        wyrm_word last_sz = static_alloc_size(sa, buffer);
        if (last_sz < 0) {
            return WYRM_NULL;
        }
        old_sz = (wyrm_uword) last_sz;
    }

    if (old_sz >= new_sz) {
        return buffer;
    }

    void *new_buffer = static_alloc(self, new_sz);
    if (new_buffer && old_sz > 0)
    {
        wyrm_memcpy(new_buffer, buffer, old_sz);
    }
    return new_buffer;
}

static void static_free(wyrm_allocator *allocator, void *buffer)
{
    WYRM_UNUSED(allocator);
    WYRM_UNUSED(buffer);

    WYRM_ASSERT(allocator && allocator->clz == &wyrm_allocator_static_vt);
    WYRM_ASSERT(!buffer || buffer >= (void *)((wyrm_allocator_static *)allocator)->storage);
    WYRM_ASSERT(!buffer || buffer < (void *)((wyrm_allocator_static *)allocator)->storage_current);
}


static wyrm_uword static_estimate_heap_size(wyrm_allocator *allocator)
{
    wyrm_allocator_static *sa = (wyrm_allocator_static *)allocator;
    if (sa->storage_current > sa->storage) {
        return sa->storage_current - sa->storage;
    }
    return 0;
}

const wyrm_allocator_vt wyrm_allocator_static_vt =
{
    .alloc              = static_alloc,
    .realloc            = static_realloc,
    .free               = static_free,
    .estimate_heap_size = static_estimate_heap_size,
};
