#include <wyrm.h>

/**
 * Allocate a new box.
 */
wyrm_error wyrm_box_new(wyrm_context* self, wyrm_box** out)
{
    wyrm_box* box = wyrm_context_gc_alloc(self, sizeof(wyrm_box));
    if (box == WYRM_NULL) { return WYRM_ERR_NOMEM; }

    box->value.type = WYRM_TYPE_TAG_NIL;
    box->value.data.uword = 0;
    wyrm_context_gc_init(self, &box->head, WYRM_TYPE_TAG_BOX);
    *out = box;
    return WYRM_ERR_NONE;
}
