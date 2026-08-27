#include <wyrm/pair.h>
#include <wyrm/sys/string.h>

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
    wyrm_pair* pair = (wyrm_pair*) object;

    switch (wa->data[0].word) {
    case 0:
        wa->data[0].word++;
        if (wyrm_type_tag_is_gc(pair->car.type) && pair->car.data.gc_object != WYRM_NULL) {
            *child = pair->car.data.gc_object;
            return WYRM_ERR_NONE;
        }
        /* intentional fall through */
    case 1:
        wa->data[0].word++;
        if (wyrm_type_tag_is_gc(pair->cdr.type) && pair->cdr.data.gc_object != WYRM_NULL) {
            *child = pair->cdr.data.gc_object;
            return WYRM_ERR_NONE;
        }
        /* intentional fall through */
    default:
        return WYRM_ERR_STOP_ITERATION;
    }
}


const wyrm_object_type wyrm_pair_type = {
    .object = WYRM_OBJECT_TYPE_OBJECT_INIT,
    .gc_type = WYRM_TYPE_TAG_PAIR,

    .children_iter_start = start_children_iter,
    .children_iter_next = next_children_iter,
};
