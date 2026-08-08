#ifndef WYRM_INL_SCOPE_H_
#define WYRM_INL_SCOPE_H_

#include <wyrm.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize scope
 */
WYRM_INLINE wyrm_error wyrm_scope_initialize_f(wyrm_context* context, wyrm_scope* self, wyrm_prototype* prototype)
{
    self->prototype = prototype;
    self->slots = WYRM_NULL;

    if (prototype->slot_count > 0) {
        self->slots = (wyrm_value*) wyrm_context_gc_alloc(context, sizeof(wyrm_value) * prototype->slot_count);
        if (self->slots == WYRM_NULL) { return WYRM_ERR_NOMEM; }
        for (wyrm_uword i = 0; i < prototype->slot_count; ++i) {
            self->slots[i].type = WYRM_TYPE_TAG_ERROR;
        }
    }

    return WYRM_ERR_NONE;
}

/**
 * Dereference value in the scope
 */
WYRM_INLINE wyrm_value* wyrm_scope_deref(wyrm_scope* self, wyrm_uword index)
{
    if (self == WYRM_NULL || self->slots == WYRM_NULL || index >= self->prototype->slot_count) { return WYRM_NULL; }
    wyrm_value* slot = &self->slots[index];
    if (slot->type == WYRM_TYPE_TAG_BOX) {
        if (slot->data.box_ptr == WYRM_NULL) { return WYRM_NULL; }
        return &slot->data.box_ptr->value;;
    }
    return slot;
}

#ifdef __cplusplus
}
#endif

#endif
