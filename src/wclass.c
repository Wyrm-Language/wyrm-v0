#include <wyrm.h>

#define SLOT_COUNT 256

wyrm_error wyrm_class_new(wyrm_context* context, wyrm_class** out)
{
    wyrm_class* cls = wyrm_context_gc_alloc(context, sizeof(wyrm_class));
    if (!cls) { return WYRM_ERR_NOMEM; }

    cls->super = WYRM_NULL;
    cls->sym_name.symtab_entry = WYRM_NULL;

    wyrm_prototype_init_f(&cls->prototype);
    wyrm_error last_error = wyrm_prototype_reserve_f(context, &cls->prototype, SLOT_COUNT);
    if (last_error != WYRM_ERR_NONE) {
        wyrm_context_gc_free(context, cls);
        return WYRM_ERR_NOMEM;
    }

    wyrm_context_object_init_header_f(context, &cls->prototype.object, &wyrm_type_class);
    *out = cls;
    return WYRM_ERR_NONE;
}


static void finalize(wyrm_context* context, wyrm_object* self)
{
    wyrm_class* cls = (wyrm_class*) self;
    wyrm_prototype_finalize_f(context, &cls->prototype);
}


static wyrm_error children_iter_start(wyrm_state* state, wyrm_object* self, wyrm_work_area* wa)
{
    WYRM_UNUSED(state); WYRM_UNUSED(self);
    memset(wa, 0, sizeof(wyrm_work_area));
    wa->data[0].flag = false;
    return WYRM_ERR_NONE;
}

static wyrm_error children_iter_next(wyrm_state* state, wyrm_object* self, wyrm_work_area* wa, const wyrm_object** child)
{
    WYRM_UNUSED(state);
    wyrm_class* cls = (wyrm_class*) self;
    bool done = wa->data[0].flag;
    wa->data[0].flag = true;
    if (done || cls->super == WYRM_NULL) {
        return WYRM_ERR_STOP_ITERATION;
    }
    *child = (wyrm_object*) cls->super;
    return WYRM_ERR_NONE;
}

const wyrm_object_type wyrm_type_class = {
    .object = WYRM_OBJECT_TYPE_OBJECT_INIT,
    .gc_type = WYRM_TYPE_TAG_CLASS,

    .finalize = finalize,
    .children_iter_start = children_iter_start,
    .children_iter_next = children_iter_next,
 };
