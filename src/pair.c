#include <wyrm/pair.h>
#include <wyrm/sys/string.h>

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
    wy_pair* pair = (wy_pair*) object;

    switch (wa->data[0].word) {
    case 0:
        wa->data[0].word++;
        if (wy_type_tag_is_gc(pair->car.type) && pair->car.data.gc_object != WY_NULL) {
            *child = pair->car.data.gc_object;
            return WY_ERR_NONE;
        }
        /* intentional fall through */
    case 1:
        wa->data[0].word++;
        if (wy_type_tag_is_gc(pair->cdr.type) && pair->cdr.data.gc_object != WY_NULL) {
            *child = pair->cdr.data.gc_object;
            return WY_ERR_NONE;
        }
        /* intentional fall through */
    default:
        return WY_ERR_STOP_ITERATION;
    }
}


const wy_object_type wy_pair_type = {
    .object = WY_OBJECT_TYPE_OBJECT_INIT,
    .gc_type = WY_TYPE_TAG_PAIR,

    .children_iter_start = start_children_iter,
    .children_iter_next = next_children_iter,
};
