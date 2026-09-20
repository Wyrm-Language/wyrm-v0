#include <wyrm.h>
#include <wyrm/bound_msg.h>
#include <wyrm/sys/string.h>
#include <wyrm/work_area.h>

wy_error wy_bound_msg_new_f(wy_context* context, wy_value receiver, wy_message* msg, wy_value body, wy_bound_msg** out)
{
    if (context == WY_NULL || msg == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }

    wy_bound_msg* self = wy_context_gc_alloc(context, sizeof(wy_bound_msg));
    if (self == WY_NULL) { return WY_ERR_NOMEM; }

    self->receiver = receiver;
    self->msg = msg;
    self->body = body;

    wy_context_object_init_header_f(context, (wy_object*) self, &wy_bound_msg_type);
    *out = self;
    return WY_ERR_NONE;
}


static void finalize(wy_context* context, wy_object* object)
{
    WY_UNUSED(context);
    wy_bound_msg* self = (wy_bound_msg*) object;
    self->msg = WY_NULL;
}

enum { CI_RECEIVER = 0, CI_MSG, CI_BODY, CI_DONE };

static wy_error children_iter_start(wy_context* context, wy_object* object, wy_work_area* wa)
{
    WY_UNUSED(context); WY_UNUSED(object);
    wy_memset(wa, 0, sizeof(wy_work_area));
    wa->data[0].word = CI_RECEIVER;
    return WY_ERR_NONE;
}

static wy_error children_iter_next(wy_context* context, wy_object* object, wy_work_area* wa, const wy_object** child)
{
    WY_UNUSED(context);
    wy_bound_msg* self = (wy_bound_msg*) object;

    if (wa->data[0].word == CI_RECEIVER) {
        wa->data[0].word = CI_MSG;
        if (wy_value_is_gc_ref_f(self->receiver)) {
            *child = self->receiver.data.gc_object;
            return WY_ERR_NONE;
        }
    }
    if (wa->data[0].word == CI_MSG) {
        wa->data[0].word = CI_BODY;
        if (self->msg != WY_NULL) {
            *child = (wy_object*) self->msg;
            return WY_ERR_NONE;
        }
    }
    if (wa->data[0].word == CI_BODY) {
        wa->data[0].word = CI_DONE;
        if (wy_value_is_gc_ref_f(self->body)) {
            *child = self->body.data.gc_object;
            return WY_ERR_NONE;
        }
    }
    return WY_ERR_STOP_ITERATION;
}

const wy_object_type wy_bound_msg_type = {
    .object = WY_OBJECT_TYPE_OBJECT_INIT,
    .gc_type = WY_TYPE_TAG_BOUND_MSG,

    .finalize = finalize,
    .children_iter_start = children_iter_start,
    .children_iter_next = children_iter_next,
};
