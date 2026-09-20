#include <wyrm.h>
#include <wyrm/function.h>
#include <wyrm/work_area.h>

static void finalize_f(wy_context* context, wy_object* object);
static wy_error children_iter_start(wy_context* context, wy_object* object, wy_work_area* wa);
static wy_error children_iter_next(wy_context* context, wy_object* object, wy_work_area* wa, const wy_object** child);

wy_error wy_function_new(wy_context* context, wy_module* module, const wy_function_proto* proto,
    const wy_value* caps, wy_uword ncaps, wy_function** out)
{
    if (context == WY_NULL || module == WY_NULL || proto == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }
    if (ncaps > 0 && caps == WY_NULL) { return WY_ERR_INVAL; }

    wy_function* self = wy_context_gc_alloc(context, sizeof(wy_function) + ncaps * sizeof(wy_value));
    if (self == WY_NULL) { return WY_ERR_NOMEM; }

    self->module = module;
    self->proto = proto;
    self->ncaps = ncaps;
    for (wy_uword i = 0; i < ncaps; ++i) {
        self->caps[i] = caps[i];
    }

    wy_context_object_init_header_f(context, &self->object, &wy_function_type);
    *out = self;
    return WY_ERR_NONE;
}


static void finalize_f(wy_context* context, wy_object* object)
{
    WY_UNUSED(context);
    wy_function* self = (wy_function*) object;
    self->module = WY_NULL;
    self->proto = WY_NULL;
}


static wy_error children_iter_start(wy_context* context, wy_object* object, wy_work_area* wa)
{
    WY_UNUSED(context); WY_UNUSED(object);
    wy_memset(wa, 0, sizeof(wy_work_area));
    wa->data[0].word = -1; /* -1: module not yet yielded; >= 0: caps index */
    return WY_ERR_NONE;
}

static wy_error children_iter_next(wy_context* context, wy_object* object, wy_work_area* wa, const wy_object** child)
{
    WY_UNUSED(context);
    wy_function* self = (wy_function*) object;
    wy_word idx = wa->data[0].word;

    if (idx < 0) {
        wa->data[0].word = 0;
        WY_ASSERT(self->module != WY_NULL);
        *child = (wy_object*) self->module;
        return WY_ERR_NONE;
    }

    while ((wy_uword) idx < self->ncaps) {
        wy_value cur = self->caps[idx];
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


const wy_object_type wy_function_type = {
    .object = WY_OBJECT_TYPE_OBJECT_INIT,
    .gc_type = WY_TYPE_TAG_FUNCTION,

    .finalize = finalize_f,
    .children_iter_start = children_iter_start,
    .children_iter_next = children_iter_next,
};
