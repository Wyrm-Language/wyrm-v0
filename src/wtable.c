#include <wyrm.h>

void wyrm_table_init_s(wyrm_table* self,
    wyrm_allocator* allocator,
    wyrm_uword count,
    wyrm_key_hash_value* dense,
    wyrm_uword dense_capacity,
    wyrm_uword* sparse,
    wyrm_uword sparse_capacity)
{
    wyrm_memset(self, 0, sizeof(wyrm_table));
    self->allocator = allocator;
    self->obj.gc_type = WYRM_TYPE_TAG_TABLE;
    self->count = count;
    self->dense = dense;
    self->dense_capacity = dense_capacity;
    self->sparse = sparse;
    self->sparse_capacity = sparse_capacity;
}

void wyrm_table_init_f(wyrm_table* self, wyrm_allocator* allocator)
{
    wyrm_table_init_s(self, allocator, 0, WYRM_NULL, 0, WYRM_NULL, 0);
}

void wyrm_table_finalize_f(wyrm_table* self)
{
    self->count = 0;
    self->dense_capacity = 0;
    self->sparse_capacity = 0;
    wyrm_allocator_free(self->allocator, self->dense); self->dense = WYRM_NULL;
    wyrm_allocator_free(self->allocator, self->sparse); self->sparse = WYRM_NULL;
}


wyrm_value* wyrm_table_get(wyrm_state* state, wyrm_table* self, wyrm_type_tag tag, wyrm_primitive value)
{
    if (self == WYRM_NULL) {return WYRM_NULL; }
    for (wyrm_uword i = 0; i < self->count; i++) {
        if (wyrm_op_eq(state, self->dense[i].key.type, self->dense[i].key.data, tag, value)) {
            return &self->dense[i].value;
        }
    }
    return WYRM_NULL;
}

WYRM_INLINE wyrm_error _expand_dict(wyrm_table* self, wyrm_uword count)
{
    if (count > self->dense_capacity) {
        wyrm_uword new_cap = wyrm_next_array_capacity(self->dense_capacity, 4);
        wyrm_key_hash_value* hash_array = wyrm_allocator_realloc(self->allocator, self->dense, new_cap * sizeof(wyrm_key_hash_value));
        if (hash_array == WYRM_NULL) { return WYRM_ERR_NOMEM; }
        self->dense_capacity = new_cap;
        self->dense = hash_array;
    }

    if (count > self->sparse_capacity) {
        wyrm_uword new_cap = wyrm_next_array_capacity(self->sparse_capacity, 6);
        wyrm_uword* sparse = wyrm_allocator_realloc(self->allocator, self->sparse, new_cap * sizeof(wyrm_uword));
        if (sparse == WYRM_NULL) { return WYRM_ERR_NOMEM; }
        self->sparse_capacity = new_cap;
        self->sparse = sparse;
    }

    return WYRM_ERR_NONE;
}



wyrm_error wyrm_table_set(wyrm_state* state, wyrm_table* self, wyrm_type_tag key_type, wyrm_primitive key_value, wyrm_type_tag value_type, wyrm_primitive value)
{
    wyrm_error last_error = WYRM_ERR_NONE;
    if (self == WYRM_NULL) { return WYRM_ERR_INVAL; }

    wyrm_value* slot = wyrm_table_get(state, self, key_type, key_value);

    if (!slot) {
        wyrm_uword new_count = self->count + 1;
        last_error = _expand_dict(self, new_count);
        if (last_error != WYRM_ERR_NONE) { return last_error; }
        slot = &self->dense[self->count].value;
        self->dense[self->count].key.type = key_type;
        self->dense[self->count].key.data = key_value;
        self->count = new_count;
    }

    slot->type = value_type;
    slot->data = value;
    return WYRM_ERR_NONE;
}
