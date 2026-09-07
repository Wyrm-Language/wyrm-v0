#include <wyrm.h>
#include <wyrm/work_area.h>

/**
 * @brief Allocate a new box.
 */
wy_error wy_box_new_f(wy_context* context, wy_box** out)
{
    wy_box* box = wy_context_gc_alloc(context, sizeof(wy_box));
    if (box == WY_NULL) { return WY_ERR_NOMEM; }

    box->value = wy_value_Unset();
    wy_context_object_init_header_f(context, &box->object, &wy_box_type);

    *out = box;
    return WY_ERR_NONE;
}


static wy_error start_children_iter(wy_state* state, wy_object* object, wy_work_area* wa)
{
    WY_UNUSED(state);
    WY_ASSERT(object != WY_NULL);
    wy_memset(wa, 0, sizeof(wy_work_area));
    wa->data[0].word = 0;
    return WY_ERR_NONE;
}

static wy_error next_children_iter(wy_state* state, wy_object* object, wy_work_area* wa, const wy_object** child)
{
    WY_UNUSED(state);
    WY_ASSERT(object != WY_NULL);
    wy_box* box = (wy_box*) object;

    if (wa->data[0].word == 0) {
        wa->data[0].word = 1;
        if (wy_type_tag_is_gc(box->value.type) && box->value.data.gc_object != WY_NULL) {
            *child = box->value.data.gc_object;
            return WY_ERR_NONE;
        }
    }
    return WY_ERR_STOP_ITERATION;
}


const wy_object_type wy_box_type = {
    .object = WY_OBJECT_TYPE_OBJECT_INIT,
    .gc_type = WY_TYPE_TAG_BOX,

    .children_iter_start = start_children_iter,
    .children_iter_next = next_children_iter,
};
