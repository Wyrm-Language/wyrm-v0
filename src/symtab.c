#include <wyrm/symtab.h>
#include <wyrm/util.h>
#include <wyrm/sys/string.h>

enum
{
    WY_SYMTAB_INITIAL_CAPACITY = 64,
    WY_SYMTAB_MAX_SIG_CODEPOINTS = 31,
};

wy_uword wy_symtab_significant_prefix_len(const char* utf8, wy_uword len)
{
    wy_uword offset = 0;
    wy_uword codepoints = 0;

    while (offset < len) {
        wy_u8 byte = (wy_u8) utf8[offset];
        bool is_continuation = (byte & 0xC0) == 0x80;

        if (!is_continuation) {
            if (codepoints == WY_SYMTAB_MAX_SIG_CODEPOINTS) { break; }
            codepoints = codepoints + 1;
        }
        offset = offset + 1;
    }

    return offset;
}

static wy_uword hash_slot_(wy_u32 hash, wy_uword capacity)
{
    return (wy_uword) hash % capacity;
}

/**
 * Find either the existing node matching (hash, sig_len, sig_bytes) or the
 * first empty slot where such a node could be inserted.
 */
static wy_symtab_node** find_slot_(wy_symtab_node** buckets, wy_uword capacity,
                                    wy_u32 hash, wy_uword sig_len, const char* sig_bytes)
{
    wy_uword start = hash_slot_(hash, capacity);
    for (wy_uword i = 0; i < capacity; ++i) {
        wy_symtab_node** slot = &buckets[(start + i) % capacity];
        wy_symtab_node* node = *slot;
        if (node == WY_NULL) { return slot; }
        if (node->hash == hash && node->sig_len == sig_len &&
            wy_memcmp(node->text, sig_bytes, sig_len) == 0)
        {
            return slot;
        }
    }
    return WY_NULL;
}

static bool grow_f(wy_symtab* self)
{
    wy_uword new_capacity = wy_next_array_capacity(self->capacity, WY_SYMTAB_INITIAL_CAPACITY);
    wy_symtab_node** new_buckets = wy_allocator_alloc(self->allocator, sizeof(wy_symtab_node*) * new_capacity);
    if (new_buckets == WY_NULL) { return false; }

    for (wy_uword i = 0; i < new_capacity; ++i) { new_buckets[i] = WY_NULL; }

    for (wy_uword i = 0; i < self->capacity; ++i) {
        wy_symtab_node* node = self->buckets[i];
        if (node == WY_NULL) { continue; }
        wy_symtab_node** slot = find_slot_(new_buckets, new_capacity, node->hash, node->sig_len, node->text);
        WY_ASSERT(slot != WY_NULL);
        *slot = node;
    }

    wy_allocator_free(self->allocator, self->buckets);
    self->buckets = new_buckets;
    self->capacity = new_capacity;
    return true;
}

wy_error wy_symtab_init_f(wy_symtab* self, wy_allocator* allocator)
{
    if (self == WY_NULL || allocator == WY_NULL) { return WY_ERR_INVAL; }

    wy_symtab_node** buckets = wy_allocator_alloc(allocator, sizeof(wy_symtab_node*) * WY_SYMTAB_INITIAL_CAPACITY);
    if (buckets == WY_NULL) { return WY_ERR_NOMEM; }

    for (wy_uword i = 0; i < WY_SYMTAB_INITIAL_CAPACITY; ++i) { buckets[i] = WY_NULL; }

    self->allocator = allocator;
    self->buckets = buckets;
    self->capacity = WY_SYMTAB_INITIAL_CAPACITY;
    self->count = 0;
    return WY_ERR_NONE;
}

void wy_symtab_finalize_f(wy_symtab* self)
{
    if (self == WY_NULL) { return; }

    if (self->buckets != WY_NULL) {
        for (wy_uword i = 0; i < self->capacity; ++i) {
            if (self->buckets[i] != WY_NULL) {
                wy_allocator_free(self->allocator, self->buckets[i]);
                self->buckets[i] = WY_NULL;
            }
        }
        wy_allocator_free(self->allocator, self->buckets);
        self->buckets = WY_NULL;
    }

    self->capacity = 0;
    self->count = 0;
}

wy_symbol wy_symtab_lookup(wy_symtab* self, const char* utf8, wy_uword len)
{
    if (self == WY_NULL || (utf8 == WY_NULL && len > 0)) { return WY_SYMBOL_INVALID; }

    wy_uword sig_len = wy_symtab_significant_prefix_len(utf8, len);
    wy_u32 hash = sig_len == 0 ? FNV1A_SEED : wy_util_fnv1a_buffer((void*) utf8, sig_len);

    wy_symtab_node** slot = find_slot_(self->buckets, self->capacity, hash, sig_len, utf8);
    if (slot == WY_NULL || *slot == WY_NULL) { return WY_SYMBOL_INVALID; }
    return (*slot)->text;
}

wy_symbol wy_symtab_intern(wy_symtab* self, const char* utf8, wy_uword len)
{
    if (self == WY_NULL || (utf8 == WY_NULL && len > 0)) { return WY_SYMBOL_INVALID; }

    wy_uword sig_len = wy_symtab_significant_prefix_len(utf8, len);
    wy_u32 hash = sig_len == 0 ? FNV1A_SEED : wy_util_fnv1a_buffer((void*) utf8, sig_len);

    wy_symtab_node** slot = find_slot_(self->buckets, self->capacity, hash, sig_len, utf8);
    if (slot != WY_NULL && *slot != WY_NULL) {
        return (*slot)->text;
    }

    /* Grow if load factor would exceed ~75%, or the table is full (slot == WY_NULL). */
    if (slot == WY_NULL || (self->count + 1) * 4 > self->capacity * 3) {
        if (!grow_f(self)) { return WY_SYMBOL_INVALID; }
        slot = find_slot_(self->buckets, self->capacity, hash, sig_len, utf8);
        WY_ASSERT(slot != WY_NULL && *slot == WY_NULL);
    }

    wy_symtab_node* node = wy_allocator_alloc(self->allocator, sizeof(wy_symtab_node) + len + 1);
    if (node == WY_NULL) { return WY_SYMBOL_INVALID; }

    node->hash = hash;
    node->sig_len = (wy_u16) sig_len;
    node->len = (wy_u16) len;
    if (len > 0) { wy_memcpy(node->text, utf8, len); }
    node->text[len] = '\0';

    *slot = node;
    self->count = self->count + 1;

    return node->text;
}
