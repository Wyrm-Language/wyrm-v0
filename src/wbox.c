#include <wyrm.h>
#include <wyrm/work_area.h>

/**
 * @brief Allocate a new box.
 */
wyrm_error wyrm_box_new_f(wyrm_context* context, wyrm_box** out)
{
    wyrm_box* box = wyrm_context_gc_alloc(context, sizeof(wyrm_box));
    if (box == WYRM_NULL) { return WYRM_ERR_NOMEM; }

    box->value = wyrm_value_Unset();
    wyrm_context_object_init_header_f(context, &box->object, &wyrm_box_type);

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
        if (wyrm_type_tag_is_gc(box->value.type) && box->value.data.gc_object != WYRM_NULL) {
            *child = box->value.data.gc_object;
            return WYRM_ERR_NONE;
        }
    }
    return WYRM_ERR_STOP_ITERATION;
}


const wyrm_object_type wyrm_box_type = {
    .object = WYRM_OBJECT_TYPE_OBJECT_INIT,
    .gc_type = WYRM_TYPE_TAG_BOX,

    .children_iter_start = start_children_iter,
    .children_iter_next = next_children_iter,
};
