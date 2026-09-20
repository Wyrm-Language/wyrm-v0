#include <wyrm.h>
#include <wyrm/dict.h>
#include <wyrm/op.h>
#include <wyrm/util.h>
#include <wyrm/work_area.h>

#define WY_DICT_INITIAL_CAPACITY 4
#define WY_DICT_SPARSE_FACTOR 2

static void table_init_s(wy_dict* self,
    wy_context* context,
    wy_uword count,
    wy_key_hash_value* dense,
    wy_uword dense_capacity,
    wy_uword* sparse,
    wy_uword sparse_capacity)
{
    wy_memset(self, 0, sizeof(wy_dict));
    self->count = count;
    self->dense = dense;
    self->dense_capacity = dense_capacity;
    self->sparse = sparse;
    self->sparse_capacity = sparse_capacity;

    wy_context_object_init_header_f(context, &self->object, &wy_type_table);
}

wy_error wy_dict_new(wy_context* context, wy_dict** out)
{
    if (context == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }

    wy_dict* table = wy_context_gc_alloc(context, sizeof(wy_dict));
    if (table == WY_NULL) { return WY_ERR_NOMEM; }

    table_init_s(table, context, 0, WY_NULL, 0, WY_NULL, 0);
    *out = table;
    return WY_ERR_NONE;
}

static void table_finalize_f(wy_context* context, wy_object* object)
{
    wy_dict* self = (wy_dict*) object;
    wy_context_gc_free(context, self->dense); self->dense = WY_NULL;
    wy_context_gc_free(context, self->sparse); self->sparse = WY_NULL;
    self->count = 0;
    self->dense_capacity = 0;
    self->sparse_capacity = 0;
}

static wy_uword* find_sparse_slot(wy_dict* self, wy_uword hash, wy_context* ctx, wy_type_tag key_type, wy_primitive key_value)
{
    if (self->sparse_capacity == 0) return WY_NULL;
    wy_uword mask = self->sparse_capacity - 1;
    wy_uword start = hash & mask;
    for (wy_uword i = 0; i < self->sparse_capacity; i++) {
        wy_uword* slot = &self->sparse[(start + i) & mask];
        if (*slot == WY_HASH_INVALID) return slot;
        wy_key_hash_value* kv = &self->dense[*slot];
        if (kv->key_hash == hash && wy_op_eq(ctx, kv->key.type, kv->key.data, key_type, key_value)) {
            return slot;
        }
    }
    return WY_NULL;
}

wy_value* wy_dict_get(wy_context* context, wy_dict* self, wy_type_tag tag, wy_primitive value)
{
    if (self == WY_NULL || self->count == 0) { return WY_NULL; }
    wy_uword hash = wy_op_hash(context, tag, value);
    if (hash == WY_HASH_INVALID) {
        /* Fallback to linear scan if not hashable? No, wy_op_hash should handle all keys. */
        return WY_NULL;
    }
    wy_uword* slot = find_sparse_slot(self, hash, context, tag, value);
    if (slot && *slot != WY_HASH_INVALID) {
        return &self->dense[*slot].value;
    }
    return WY_NULL;
}

static wy_error grow_dict(wy_context* context, wy_dict* self)
{
    wy_uword new_dense_cap = wy_next_array_capacity(self->dense_capacity, WY_DICT_INITIAL_CAPACITY);
    wy_uword new_sparse_cap = wy_util_bit_ceil(new_dense_cap * WY_DICT_SPARSE_FACTOR);

    wy_key_hash_value* new_dense = wy_context_gc_realloc(context, self->dense, new_dense_cap * sizeof(wy_key_hash_value));
    if (new_dense == WY_NULL) return WY_ERR_NOMEM;

    wy_uword* new_sparse = wy_context_gc_alloc(context, new_sparse_cap * sizeof(wy_uword));
    if (new_sparse == WY_NULL) return WY_ERR_NOMEM;

    for (wy_uword i = 0; i < new_sparse_cap; i++) new_sparse[i] = WY_HASH_INVALID;

    self->dense = new_dense;
    self->dense_capacity = new_dense_cap;
    wy_context_gc_free(context, self->sparse);
    self->sparse = new_sparse;
    self->sparse_capacity = new_sparse_cap;

    /* Rehash everything into the new sparse array */
    wy_uword mask = new_sparse_cap - 1;
    for (wy_uword i = 0; i < self->count; i++) {
        wy_uword hash = self->dense[i].key_hash;
        wy_uword start = hash & mask;
        for (wy_uword j = 0; j < new_sparse_cap; j++) {
            wy_uword* slot = &new_sparse[(start + j) & mask];
            if (*slot == WY_HASH_INVALID) {
                *slot = i;
                break;
            }
        }
    }

    return WY_ERR_NONE;
}

wy_error wy_dict_set(wy_context* context, wy_dict* self, wy_type_tag key_type, wy_primitive key_value, wy_type_tag value_type, wy_primitive value)
{
    if (self == WY_NULL) { return WY_ERR_INVAL; }

    wy_uword hash = wy_op_hash(context, key_type, key_value);
    if (hash == WY_HASH_INVALID) return WY_ERR_BAD_TYPE;

    wy_uword* slot = find_sparse_slot(self, hash, context, key_type, key_value);
    if (slot && *slot != WY_HASH_INVALID) {
        wy_key_hash_value* kv = &self->dense[*slot];
        kv->value.type = value_type;
        kv->value.data = value;
        return WY_ERR_NONE;
    }

    /* Grow if load factor > 0.75 */
    if (self->count * 4 >= self->dense_capacity * 3 || self->sparse_capacity == 0) {
        wy_error err = grow_dict(context, self);
        if (err != WY_ERR_NONE) return err;
        slot = find_sparse_slot(self, hash, context, key_type, key_value);
    }

    WY_ASSERT(slot && *slot == WY_HASH_INVALID);
    wy_uword idx = self->count++;
    *slot = idx;
    self->dense[idx].key.type = key_type;
    self->dense[idx].key.data = key_value;
    self->dense[idx].key_hash = hash;
    self->dense[idx].value.type = value_type;
    self->dense[idx].value.data = value;

    return WY_ERR_NONE;
}


wy_error wy_dict_remove(wy_context* context, wy_dict* self, wy_type_tag key_type, wy_primitive key_value, wy_value* out)
{
    if (self == WY_NULL) { return WY_ERR_INVAL; }
    if (self->count == 0) { return WY_ERR_KEY; }

    wy_uword hash = wy_op_hash(context, key_type, key_value);
    if (hash == WY_HASH_INVALID) return WY_ERR_BAD_TYPE;

    wy_uword* slot = find_sparse_slot(self, hash, context, key_type, key_value);
    if (slot == WY_NULL || *slot == WY_HASH_INVALID) { return WY_ERR_KEY; }

    wy_uword dense_idx = *slot;
    if (out != WY_NULL) { *out = self->dense[dense_idx].value; }

    /* Swap-remove from the dense array, then fully rehash the sparse index
     * (simple and correct for open addressing without tombstones; epic 6's
     * performance pass can replace this with backward-shift deletion if the
     * O(n) rehash shows up in profiling). */
    wy_uword last = self->count - 1;
    self->dense[dense_idx] = self->dense[last];
    self->count = last;

    for (wy_uword i = 0; i < self->sparse_capacity; i++) { self->sparse[i] = WY_HASH_INVALID; }
    wy_uword mask = self->sparse_capacity - 1;
    for (wy_uword i = 0; i < self->count; i++) {
        wy_uword h = self->dense[i].key_hash;
        wy_uword start = h & mask;
        for (wy_uword j = 0; j < self->sparse_capacity; j++) {
            wy_uword* s = &self->sparse[(start + j) & mask];
            if (*s == WY_HASH_INVALID) { *s = i; break; }
        }
    }

    return WY_ERR_NONE;
}


static wy_error start_children_iter(wy_context* context, wy_object* object, wy_work_area* wa)
{
    WY_UNUSED(context); WY_UNUSED(object);
    wy_memset(wa, 0, sizeof(wy_work_area));
    wa->data[0].uword = 0;
    wa->data[1].uword = 0;
    return WY_ERR_NONE;
}


static wy_error next_children_iter(wy_context* context, wy_object* object, wy_work_area* wa, const wy_object** child)
{
    WY_UNUSED(context);
    wy_dict* self = (wy_dict*) object;
    wy_uword idx = wa->data[0].word;
    wy_uword subidx = wa->data[1].word;

    while (idx < self->count) {
        if (subidx == 0) {
            subidx++;
            if (wy_value_is_gc_ref_f(self->dense[idx].key)) {
                wa->data[0].uword = idx;
                wa->data[1].uword = subidx;
                *child = self->dense[idx].key.data.gc_object;
                return WY_ERR_NONE;
            }
        } else {
            wy_word cur_idx = idx;
            subidx = 0; idx++;
            if (wy_value_is_gc_ref_f(self->dense[cur_idx].value)) {
                wa->data[0].uword = idx;
                wa->data[1].uword = subidx;
                *child = self->dense[cur_idx].value.data.gc_object;
                return WY_ERR_NONE;
            }
        }
    }

    return WY_ERR_STOP_ITERATION;
}



const wy_object_type wy_type_table = {
    .object = WY_OBJECT_TYPE_OBJECT_INIT,
    .gc_type = WY_TYPE_TAG_TABLE,

    .finalize = table_finalize_f,
    .children_iter_start =  start_children_iter,
    .children_iter_next = next_children_iter,
};
