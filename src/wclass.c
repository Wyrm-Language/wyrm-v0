#include <wyrm.h>

#define SLOT_COUNT 256

wy_error wy_class_new(wy_context* context, wy_class** out)
{
    wy_class* cls = wy_context_gc_alloc(context, sizeof(wy_class));
    if (!cls) { return WY_ERR_NOMEM; }

    cls->super = WY_NULL;
    cls->sym_name.symtab_entry = WY_NULL;

    wy_prototype_init_f(&cls->prototype);
    wy_error last_error = wy_prototype_reserve_f(context, &cls->prototype, SLOT_COUNT);
    if (last_error != WY_ERR_NONE) {
        wy_context_gc_free(context, cls);
        return WY_ERR_NOMEM;
    }

    wy_context_object_init_header_f(context, &cls->prototype.object, &wy_type_class);
    *out = cls;
    return WY_ERR_NONE;
}


static void finalize(wy_context* context, wy_object* self)
{
    wy_class* cls = (wy_class*) self;
    wy_prototype_finalize_f(context, &cls->prototype);
}


static wy_error children_iter_start(wy_state* state, wy_object* self, wy_work_area* wa)
{
    WY_UNUSED(state); WY_UNUSED(self);
    memset(wa, 0, sizeof(wy_work_area));
    wa->data[0].flag = false;
    return WY_ERR_NONE;
}

static wy_error children_iter_next(wy_state* state, wy_object* self, wy_work_area* wa, const wy_object** child)
{
    WY_UNUSED(state);
    wy_class* cls = (wy_class*) self;
    bool done = wa->data[0].flag;
    wa->data[0].flag = true;
    if (done || cls->super == WY_NULL) {
        return WY_ERR_STOP_ITERATION;
    }
    *child = (wy_object*) cls->super;
    return WY_ERR_NONE;
}

const wy_object_type wy_type_class = {
    .object = WY_OBJECT_TYPE_OBJECT_INIT,
    .gc_type = WY_TYPE_TAG_CLASS,

    .finalize = finalize,
    .children_iter_start = children_iter_start,
    .children_iter_next = children_iter_next,
 };
