#include <wyrm.h>


static void table_init_s(wyrm_dict* self,
    wyrm_context* context,
    wyrm_uword count,
    wyrm_key_hash_value* dense,
    wyrm_uword dense_capacity,
    wyrm_uword* sparse,
    wyrm_uword sparse_capacity)
{
    wyrm_memset(self, 0, sizeof(wyrm_dict));
    self->count = count;
    self->dense = dense;
    self->dense_capacity = dense_capacity;
    self->sparse = sparse;
    self->sparse_capacity = sparse_capacity;

    wyrm_context_object_init_header_f(context, &self->object, &wyrm_type_table);
}


wyrm_error wyrm_dict_new(wyrm_context* context, wyrm_dict** out)
{
    if (context == WYRM_NULL || out == WYRM_NULL) { return WYRM_ERR_INVAL; }

    wyrm_dict* table = wyrm_context_gc_alloc(context, sizeof(wyrm_dict));
    if (table == WYRM_NULL) { return WYRM_ERR_NOMEM; }

    table_init_s(table, context, 0, WYRM_NULL, 0, WYRM_NULL, 0);
    *out = table;
    return WYRM_ERR_NONE;
}


static void table_finalize_f(wyrm_context* context, wyrm_object* object)
{
    wyrm_dict* self = (wyrm_dict*) object;
    self->count = 0;
    self->dense_capacity = 0;
    self->sparse_capacity = 0;

    wyrm_context_gc_free(context, self->dense); self->dense = WYRM_NULL;
    wyrm_context_gc_free(context, self->sparse); self->sparse = WYRM_NULL;
}


wyrm_value* wyrm_dict_get(wyrm_state* state, wyrm_dict* self, wyrm_type_tag tag, wyrm_primitive value)
{
    if (self == WYRM_NULL) {return WYRM_NULL; }
    for (wyrm_uword i = 0; i < self->count; i++) {
        if (wyrm_op_eq(state, self->dense[i].key.type, self->dense[i].key.data, tag, value)) {
            return &self->dense[i].value;
        }
    }
    return WYRM_NULL;
}


WYRM_INLINE wyrm_error expand_dict(wyrm_state* state, wyrm_dict* self, wyrm_uword count)
{
    if (count > self->dense_capacity) {
        wyrm_uword new_cap = wyrm_next_array_capacity(self->dense_capacity, 4);
        wyrm_key_hash_value* hash_array = wyrm_context_gc_realloc(state->context, self->dense, new_cap * sizeof(wyrm_key_hash_value));
        if (hash_array == WYRM_NULL) { return WYRM_ERR_NOMEM; }
        self->dense_capacity = new_cap;
        self->dense = hash_array;
    }

    if (count > self->sparse_capacity) {
        wyrm_uword new_cap = wyrm_next_array_capacity(self->sparse_capacity, 6);
        wyrm_uword* sparse = wyrm_context_gc_realloc(state->context, self->sparse, new_cap * sizeof(wyrm_uword));
        if (sparse == WYRM_NULL) { return WYRM_ERR_NOMEM; }
        self->sparse_capacity = new_cap;
        self->sparse = sparse;
    }

    return WYRM_ERR_NONE;
}



wyrm_error wyrm_dict_set(wyrm_state* state, wyrm_dict* self, wyrm_type_tag key_type, wyrm_primitive key_value, wyrm_type_tag value_type, wyrm_primitive value)
{
    wyrm_error last_error = WYRM_ERR_NONE;
    if (self == WYRM_NULL) { return WYRM_ERR_INVAL; }

    wyrm_value* slot = wyrm_dict_get(state, self, key_type, key_value);

    if (!slot) {
        wyrm_uword new_count = self->count + 1;
        last_error = expand_dict(state, self, new_count);
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



static wyrm_error start_children_iter(wyrm_state* state, wyrm_object* object, wyrm_work_area* wa)
{
    WYRM_UNUSED(state); WYRM_UNUSED(object);
    wyrm_memset(wa, 0, sizeof(wyrm_work_area));
    wa->data[0].uword = 0;
    wa->data[1].uword = 0;
    return WYRM_ERR_NONE;
}


static wyrm_error next_children_iter(wyrm_state* state, wyrm_object* object, wyrm_work_area* wa, const wyrm_object** child)
{
    WYRM_UNUSED(state);
    wyrm_dict* self = (wyrm_dict*) object;
    wyrm_uword idx = wa->data[0].word;
    wyrm_uword subidx = wa->data[1].word;

    while (idx < self->count) {
        if (subidx == 0) {
            subidx++;
            if (self->dense[idx].key.type >= WYRM_TYPE_TAG_GC_PATH_START) {
                wa->data[0].uword = idx;
                wa->data[1].uword = subidx;
                *child = self->dense[idx].key.data.gc_object;
                return WYRM_ERR_NONE;
            }
        } else {
            wyrm_word cur_idx = idx;
            subidx = 0; idx++;
            if (self->dense[cur_idx].value.type >= WYRM_TYPE_TAG_GC_PATH_START) {
                wa->data[0].uword = idx;
                wa->data[1].uword = subidx;
                *child = self->dense[cur_idx].value.data.gc_object;
                return WYRM_ERR_NONE;
            }
        }
    }

    return WYRM_ERR_STOP_ITERATION;
}



const wyrm_object_type wyrm_type_table = {
    .object = WYRM_OBJECT_TYPE_OBJECT_INIT,
    .gc_type = WYRM_TYPE_TAG_TABLE,
    .super = &wyrm_type_object,

    .finalize = table_finalize_f,
    .children_iter_start =  start_children_iter,
    .children_iter_next = next_children_iter,
};
