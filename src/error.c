#include <wyrm.h>
#include <wyrm/error.h>
#include <wyrm/work_area.h>

static void finalize_f(wy_context* context, wy_object* object);
static wy_error children_iter_start(wy_context* context, wy_object* object, wy_work_area* wa);
static wy_error children_iter_next(wy_context* context, wy_object* object, wy_work_area* wa, const wy_object** child);

wy_error wy_error_obj_new(wy_context* context, wy_class* cls, wy_string* what, wy_value payload, wy_error_obj** out)
{
    if (context == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }

    wy_error_obj* self = wy_context_gc_alloc(context, sizeof(wy_error_obj));
    if (self == WY_NULL) { return WY_ERR_NOMEM; }

    self->cls = cls;
    self->what = what;
    self->payload = payload;

    wy_context_object_init_header_f(context, &self->object, &wy_error_obj_type);
    *out = self;
    return WY_ERR_NONE;
}


static void finalize_f(wy_context* context, wy_object* object)
{
    WY_UNUSED(context);
    wy_error_obj* self = (wy_error_obj*) object;
    self->cls = WY_NULL;
    self->what = WY_NULL;
    self->payload = wy_value_nil();
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
    wy_error_obj* self = (wy_error_obj*) object;
    wy_word step = wa->data[0].word;

    if (step == 0) {
        wa->data[0].word = 1;
        if (self->what != WY_NULL) {
            *child = (wy_object*) self->what;
            return WY_ERR_NONE;
        }
        step = 1;
    }

    if (step == 1) {
        wa->data[0].word = 2;
        if (wy_value_is_gc_ref_f(self->payload)) {
            *child = self->payload.data.gc_object;
            return WY_ERR_NONE;
        }
        step = 2;
    }

    if (step == 2) {
        wa->data[0].word = 3;
        if (self->cls != WY_NULL) {
            *child = (wy_object*) self->cls;
            return WY_ERR_NONE;
        }
    }

    return WY_ERR_STOP_ITERATION;
}


const wy_object_type wy_error_obj_type = {
    .object = WY_OBJECT_TYPE_OBJECT_INIT,
    .gc_type = WY_TYPE_TAG_ERROR,

    .finalize = finalize_f,
    .children_iter_start = children_iter_start,
    .children_iter_next = children_iter_next,
};
