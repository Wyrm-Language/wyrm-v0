#ifndef WYRM_INL_SCOPE_H_
#define WYRM_INL_SCOPE_H_

#include <wyrm.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize scope
 */
WY_INLINE wy_error wy_scope_initialize_f(wy_context* context, wy_scope* self, wy_prototype* prototype)
{
    self->prototype = prototype;
    self->slots = WY_NULL;

    if (prototype->slot_count > 0) {
        self->slots = (wy_value*) wy_context_gc_alloc(context, sizeof(wy_value) * prototype->slot_count);
        if (self->slots == WY_NULL) { return WY_ERR_NOMEM; }
        for (wy_uword i = 0; i < prototype->slot_count; ++i) {
            self->slots[i].type = WY_TYPE_TAG_ERROR;
        }
    }

    return WY_ERR_NONE;
}

/**
 * Dereference value in the scope
 */
WY_INLINE wy_value* wy_scope_deref(wy_scope* self, wy_uword index)
{
    if (self == WY_NULL || self->slots == WY_NULL || index >= self->prototype->slot_count) { return WY_NULL; }
    wy_value* slot = &self->slots[index];
    if (slot->type == WY_TYPE_TAG_BOX) {
        if (slot->data.box_ptr == WY_NULL) { return WY_NULL; }
        return &slot->data.box_ptr->value;;
    }
    return slot;
}

#ifdef __cplusplus
}
#endif

#endif
