#include <wyrm.h>
#include <wyrm/dict.h>
#include <wyrm/op.h>
#include <wyrm/util.h>
#include <wyrm/work_area.h>

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
    self->count = 0;
    self->dense_capacity = 0;
    self->sparse_capacity = 0;

    wy_context_gc_free(context, self->dense); self->dense = WY_NULL;
    wy_context_gc_free(context, self->sparse); self->sparse = WY_NULL;
}


wy_value* wy_dict_get(wy_state* state, wy_dict* self, wy_type_tag tag, wy_primitive value)
{
    if (self == WY_NULL) {return WY_NULL; }
    for (wy_uword i = 0; i < self->count; i++) {
        if (wy_op_eq(state, self->dense[i].key.type, self->dense[i].key.data, tag, value)) {
            return &self->dense[i].value;
        }
    }
    return WY_NULL;
}


WY_INLINE wy_error expand_dict(wy_state* state, wy_dict* self, wy_uword count)
{
    if (count > self->dense_capacity) {
        wy_uword new_cap = wy_next_array_capacity(self->dense_capacity, 4);
        wy_key_hash_value* hash_array = wy_context_gc_realloc(state->context, self->dense, new_cap * sizeof(wy_key_hash_value));
        if (hash_array == WY_NULL) { return WY_ERR_NOMEM; }
        self->dense_capacity = new_cap;
        self->dense = hash_array;
    }

    if (count > self->sparse_capacity) {
        wy_uword new_cap = wy_next_array_capacity(self->sparse_capacity, 6);
        wy_uword* sparse = wy_context_gc_realloc(state->context, self->sparse, new_cap * sizeof(wy_uword));
        if (sparse == WY_NULL) { return WY_ERR_NOMEM; }
        self->sparse_capacity = new_cap;
        self->sparse = sparse;
    }

    return WY_ERR_NONE;
}



wy_error wy_dict_set(wy_state* state, wy_dict* self, wy_type_tag key_type, wy_primitive key_value, wy_type_tag value_type, wy_primitive value)
{
    wy_error last_error = WY_ERR_NONE;
    if (self == WY_NULL) { return WY_ERR_INVAL; }

    wy_value* slot = wy_dict_get(state, self, key_type, key_value);

    if (!slot) {
        wy_uword new_count = self->count + 1;
        last_error = expand_dict(state, self, new_count);
        if (last_error != WY_ERR_NONE) { return last_error; }
        slot = &self->dense[self->count].value;
        self->dense[self->count].key.type = key_type;
        self->dense[self->count].key.data = key_value;
        self->count = new_count;
    }

    slot->type = value_type;
    slot->data = value;
    return WY_ERR_NONE;
}



static wy_error start_children_iter(wy_state* state, wy_object* object, wy_work_area* wa)
{
    WY_UNUSED(state); WY_UNUSED(object);
    wy_memset(wa, 0, sizeof(wy_work_area));
    wa->data[0].uword = 0;
    wa->data[1].uword = 0;
    return WY_ERR_NONE;
}


static wy_error next_children_iter(wy_state* state, wy_object* object, wy_work_area* wa, const wy_object** child)
{
    WY_UNUSED(state);
    wy_dict* self = (wy_dict*) object;
    wy_uword idx = wa->data[0].word;
    wy_uword subidx = wa->data[1].word;

    while (idx < self->count) {
        if (subidx == 0) {
            subidx++;
            if (wy_type_is_object(self->dense[idx].key.type)) {
                wa->data[0].uword = idx;
                wa->data[1].uword = subidx;
                *child = self->dense[idx].key.data.gc_object;
                return WY_ERR_NONE;
            }
        } else {
            wy_word cur_idx = idx;
            subidx = 0; idx++;
            if (wy_type_is_object(self->dense[cur_idx].value.type)) {
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
