#include <wyrm.h>

/**
 * Allocate a new box.
 */
wyrm_error wyrm_box_new(wyrm_context* self, wyrm_box** out)
{
    wyrm_box* box = wyrm_context_gc_alloc(self, sizeof(wyrm_box));
    if (box == WYRM_NULL) { return WYRM_ERR_NOMEM; }

    wyrm_object_init_header_s(&box->object, &wyrm_type_box);

    box->value.type = WYRM_TYPE_TAG_NIL;
    box->value.data.uword = 0;
    *out = box;
    return WYRM_ERR_NONE;
}


static wyrm_error start_children_iter(wyrm_state* state, wyrm_object* object, wyrm_work_area* wa)
{
    WYRM_UNUSED(state);
    WYRM_ASSERT(object != WYRM_NULL);
    wyrm_memset(wa, 0, sizeof(wyrm_work_area));
    wa->data[0].word = 0;
    return WYRM_ERR_NONE;
}

static wyrm_error next_children_iter(wyrm_state* state, wyrm_object* object, wyrm_work_area* wa, const wyrm_object** child)
{
    WYRM_UNUSED(state);
    WYRM_ASSERT(object != WYRM_NULL);
    wyrm_box* box = (wyrm_box*) object;

    if (wa->data[0].word == 0) {
        wa->data[0].word = 1;
        if (box->value.type >= WYRM_TYPE_TAG_GC_PATH_START) {
            *child = box->value.data.gc_object;
            return WYRM_ERR_NONE;
        }
    }
    return WYRM_ERR_STOP_ITERATION;
}


const wyrm_object_type wyrm_type_box = {
    .object = WYRM_OBJECT_TYPE_OBJECT_INIT,
    .gc_type = WYRM_TYPE_TAG_BOX,

    .children_iter_start = start_children_iter,
    .children_iter_next = next_children_iter,
};
