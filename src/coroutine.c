#include <wyrm.h>
#include <wyrm/coroutine.h>
#include <wyrm/work_area.h>

wy_error wy_coroutine_new_f(wy_context* context, wy_fiber* fiber, wy_coroutine** out)
{
    if (context == WY_NULL || fiber == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }

    wy_coroutine* self = wy_context_gc_alloc(context, sizeof(wy_coroutine));
    if (self == WY_NULL) { return WY_ERR_NOMEM; }

    self->fiber = fiber;
    self->resumer = WY_NULL;
    self->delegate = WY_NULL;
    self->outer = WY_NULL;
    self->delegate_dst = WY_NULL;
    self->yield_base = 0;
    self->state = WY_CO_CREATED;
    self->result = wy_value_nil();

    wy_context_object_init_header_f(context, &self->object, &wy_coroutine_type);
    *out = self;
    return WY_ERR_NONE;
}


static void finalize_f(wy_context* context, wy_object* object)
{
    WY_UNUSED(context);
    wy_coroutine* self = (wy_coroutine*) object;
    self->fiber = WY_NULL;
    self->resumer = WY_NULL;
    self->delegate = WY_NULL;
    self->outer = WY_NULL;
    self->delegate_dst = WY_NULL;
}


static wy_error children_iter_start(wy_context* context, wy_object* object, wy_work_area* wa)
{
    WY_UNUSED(context); WY_UNUSED(object);
    wy_memset(wa, 0, sizeof(wy_work_area));
    wa->data[0].word = 0;
    return WY_ERR_NONE;
}

/**
 * Children (design_c_vm.md §3): fiber, resumer, delegate, outer, result -
 * exactly what a `wy_coroutine` can reach that isn't already reachable
 * through the value stack it was created from.
 */
static wy_error children_iter_next(wy_context* context, wy_object* object, wy_work_area* wa, const wy_object** child)
{
    WY_UNUSED(context);
    wy_coroutine* self = (wy_coroutine*) object;
    wy_word step = wa->data[0].word;

    if (step == 0) {
        wa->data[0].word = 1;
        if (self->fiber != WY_NULL) { *child = (wy_object*) self->fiber; return WY_ERR_NONE; }
        step = 1;
    }
    if (step == 1) {
        wa->data[0].word = 2;
        if (self->resumer != WY_NULL) { *child = (wy_object*) self->resumer; return WY_ERR_NONE; }
        step = 2;
    }
    if (step == 2) {
        wa->data[0].word = 3;
        if (self->delegate != WY_NULL) { *child = (wy_object*) self->delegate; return WY_ERR_NONE; }
        step = 3;
    }
    if (step == 3) {
        wa->data[0].word = 4;
        if (self->outer != WY_NULL) { *child = (wy_object*) self->outer; return WY_ERR_NONE; }
        step = 4;
    }
    if (step == 4) {
        wa->data[0].word = 5;
        if (wy_value_is_gc_ref_f(self->result)) { *child = self->result.data.gc_object; return WY_ERR_NONE; }
    }
    return WY_ERR_STOP_ITERATION;
}


const wy_object_type wy_coroutine_type = {
    .object = WY_OBJECT_TYPE_OBJECT_INIT,
    .gc_type = WY_TYPE_TAG_COROUTINE,

    .finalize = finalize_f,
    .children_iter_start = children_iter_start,
    .children_iter_next = children_iter_next,
};
