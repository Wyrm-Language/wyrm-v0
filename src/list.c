#include <wyrm.h>
#include <wyrm/list.h>
#include <wyrm/util.h>
#include <wyrm/work_area.h>

enum { WY_LIST_INITIAL_CAPACITY = 4 };

static void finalize_f(wy_context* context, wy_object* object);
static wy_error children_iter_start(wy_context* context, wy_object* object, wy_work_area* wa);
static wy_error children_iter_next(wy_context* context, wy_object* object, wy_work_area* wa, const wy_object** child);

wy_error wy_list_new(wy_context* context, wy_uword initial_capacity, wy_list** out)
{
    if (context == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }

    wy_list* self = wy_context_gc_alloc(context, sizeof(wy_list));
    if (self == WY_NULL) { return WY_ERR_NOMEM; }

    self->count = 0;
    self->capacity = 0;
    self->items = WY_NULL;

    if (initial_capacity > 0) {
        self->items = wy_context_gc_alloc(context, initial_capacity * sizeof(wy_value));
        if (self->items == WY_NULL) {
            wy_context_gc_free(context, self);
            return WY_ERR_NOMEM;
        }
        self->capacity = initial_capacity;
    }

    wy_context_object_init_header_f(context, &self->object, &wy_list_type);
    *out = self;
    return WY_ERR_NONE;
}


wy_error wy_list_push(wy_context* context, wy_list* self, wy_value value)
{
    if (context == WY_NULL || self == WY_NULL) { return WY_ERR_INVAL; }

    if (self->count == self->capacity) {
        wy_uword new_capacity = wy_next_array_capacity(self->capacity, WY_LIST_INITIAL_CAPACITY);
        wy_value* new_items = wy_context_gc_realloc(context, self->items, new_capacity * sizeof(wy_value));
        if (new_items == WY_NULL) { return WY_ERR_NOMEM; }
        self->items = new_items;
        self->capacity = new_capacity;
    }

    self->items[self->count] = value;
    self->count++;
    return WY_ERR_NONE;
}


wy_error wy_list_set(wy_list* self, wy_uword index, wy_value value)
{
    if (self == WY_NULL) { return WY_ERR_INVAL; }
    if (index >= self->count) { return WY_ERR_RANGE; }
    self->items[index] = value;
    return WY_ERR_NONE;
}


static void finalize_f(wy_context* context, wy_object* object)
{
    wy_list* self = (wy_list*) object;
    wy_context_gc_free(context, self->items);
    self->items = WY_NULL;
    self->count = 0;
    self->capacity = 0;
}


static wy_error children_iter_start(wy_context* context, wy_object* object, wy_work_area* wa)
{
    WY_UNUSED(context); WY_UNUSED(object);
    wy_memset(wa, 0, sizeof(wy_work_area));
    wa->data[0].word = 0;
    return WY_ERR_NONE;
}

static wy_error children_iter_next(wy_context* context, wy_object* object, wy_work_area* wa, const wy_object** child)
{
    WY_UNUSED(context);
    wy_list* self = (wy_list*) object;
    wy_word idx = wa->data[0].word;

    while ((wy_uword) idx < self->count) {
        wy_value cur = self->items[idx];
        idx++;
        if (wy_value_is_gc_ref_f(cur)) {
            *child = cur.data.gc_object;
            wa->data[0].word = idx;
            return WY_ERR_NONE;
        }
    }

    wa->data[0].word = idx;
    return WY_ERR_STOP_ITERATION;
}


const wy_object_type wy_list_type = {
    .object = WY_OBJECT_TYPE_OBJECT_INIT,
    .gc_type = WY_TYPE_TAG_LIST,

    .finalize = finalize_f,
    .children_iter_start = children_iter_start,
    .children_iter_next = children_iter_next,
};
