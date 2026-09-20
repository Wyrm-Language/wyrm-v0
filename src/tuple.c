#include <wyrm.h>
#include <wyrm/tuple.h>
#include <wyrm/work_area.h>

static void finalize_f(wy_context* context, wy_object* object);
static wy_error children_iter_start(wy_context* context, wy_object* object, wy_work_area* wa);
static wy_error children_iter_next(wy_context* context, wy_object* object, wy_work_area* wa, const wy_object** child);

wy_error wy_tuple_new(wy_context* context, const wy_value* items, wy_uword count, wy_tuple** out)
{
    if (context == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }
    if (count > 0 && items == WY_NULL) { return WY_ERR_INVAL; }

    wy_tuple* tup = wy_context_gc_alloc(context, sizeof(wy_tuple) + count * sizeof(wy_value));
    if (tup == WY_NULL) { return WY_ERR_NOMEM; }

    tup->count = count;
    for (wy_uword i = 0; i < count; ++i) {
        tup->items[i] = items[i];
    }

    wy_context_object_init_header_f(context, &tup->object, &wy_tuple_type);
    *out = tup;
    return WY_ERR_NONE;
}


static void finalize_f(wy_context* context, wy_object* object)
{
    WY_UNUSED(context); WY_UNUSED(object);
    /* no owned buffers: items[] is stored inline */
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
    wy_tuple* self = (wy_tuple*) object;
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


const wy_object_type wy_tuple_type = {
    .object = WY_OBJECT_TYPE_OBJECT_INIT,
    .gc_type = WY_TYPE_TAG_TUPLE,

    .finalize = finalize_f,
    .children_iter_start = children_iter_start,
    .children_iter_next = children_iter_next,
};
