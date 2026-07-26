#include <wyrm.h>

const wyrm_object_type wyrm_type_box = {
    .object = WYRM_OBJECT_TYPE_OBJECT_INIT,
    .gc_type = WYRM_TYPE_TAG_BOX,
    .super = &wyrm_type_object
};

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
