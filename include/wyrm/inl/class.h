#ifndef WYRM_INL_CLASS_H_
#define WYRM_INL_CLASS_H_

#include <wyrm/types.h>

#define WYRM_SLOT_COUNT 128
#define WYRM_BAD_SLOT WYRM_UWORD_MAX

WYRM_BEGIN_DECLS

/**
 * Set the primitive for the class name
 */
WYRM_INLINE void wyrm_class_set_name_f(wyrm_class* self, wyrm_primitive name)
{
    self->sym_name = name;
}


/**
 * Add slot to the class (construction helper)
 */
WYRM_INLINE wyrm_error wyrm_class_add_slot_f(wyrm_class* self, wyrm_primitive slot_name, wyrm_type_tag slot_type)
{
    if (self->slot_count >= WYRM_SLOT_COUNT) { return WYRM_ERR_NOMEM; }
    self->slots[self->slot_count].sym_name = slot_name;
    self->slots[self->slot_count].type_tag = slot_type;
    self->slot_count++;
    return WYRM_ERR_NONE;
}


/**
 * Get selector for the given symbol
 */
WYRM_INLINE wyrm_uword wyrm_class_get_slot_selector_f(wyrm_class* self, wyrm_primitive sym_name)
{
    for (wyrm_uword i = 0; i < self->slot_count; i++) {
        if (self->slots[i].sym_name.symtab_entry == sym_name.symtab_entry) {
            return i;
        }
    }
    return WYRM_BAD_SLOT;
}


WYRM_END_DECLS

#endif
