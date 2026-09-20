#include <wyrm.h>
#include <wyrm/instance.h>
#include <wyrm/sys/string.h>
#include <wyrm/work_area.h>

wy_error wy_instance_new_f(wy_context* context, wy_class* cls, wy_instance** out)
{
    if (context == WY_NULL || cls == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }

    wy_instance* self = wy_context_gc_alloc(context, sizeof(wy_instance) + sizeof(wy_value) * cls->slot_count);
    if (self == WY_NULL) { return WY_ERR_NOMEM; }

    self->cls = cls;
    for (wy_uword i = 0; i < cls->slot_count; i++) {
        self->slots[i] = cls->slots[i].default_value;
    }

    wy_context_object_init_header_f(context, &self->object, &wy_instance_type);
    *out = self;
    return WY_ERR_NONE;
}


static void finalize(wy_context* context, wy_object* object)
{
    WY_UNUSED(context);
    wy_instance* self = (wy_instance*) object;
    self->cls = WY_NULL;
}


static wy_error children_iter_start(wy_context* context, wy_object* object, wy_work_area* wa)
{
    WY_UNUSED(context); WY_UNUSED(object);
    wy_memset(wa, 0, sizeof(wy_work_area));
    wa->data[0].word = -1; /* -1: cls not yet yielded; >= 0: slot index */
    return WY_ERR_NONE;
}

static wy_error children_iter_next(wy_context* context, wy_object* object, wy_work_area* wa, const wy_object** child)
{
    WY_UNUSED(context);
    wy_instance* self = (wy_instance*) object;
    wy_word idx = wa->data[0].word;

    if (idx < 0) {
        wa->data[0].word = 0;
        WY_ASSERT(self->cls != WY_NULL);
        *child = (wy_object*) self->cls;
        return WY_ERR_NONE;
    }

    while ((wy_uword) idx < self->cls->slot_count) {
        wy_value cur = self->slots[idx];
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


const wy_object_type wy_instance_type = {
    .object = WY_OBJECT_TYPE_OBJECT_INIT,
    .gc_type = WY_TYPE_TAG_INSTANCE,

    .finalize = finalize,
    .children_iter_start = children_iter_start,
    .children_iter_next = children_iter_next,
};
