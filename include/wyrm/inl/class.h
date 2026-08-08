#ifndef WYRM_INL_CLASS_H_
#define WYRM_INL_CLASS_H_

#include <wyrm/types.h>
#include <wyrm/inl/prototype.h>

WYRM_BEGIN_DECLS

/**
 * Set the primitive for the class name
 */
WYRM_INLINE void wyrm_class_set_name_f(wyrm_class* self, wyrm_primitive name)
{
    self->sym_name = name;
}

/**
 * Get selector for the given symbol
 */
WYRM_INLINE wyrm_uword wyrm_class_get_slot_selector_f(wyrm_class* self, wyrm_symtab_entry sym_name)
{
    return wyrm_prototype_get_slot_selector_f(&self->prototype, sym_name);
}

WYRM_INLINE wyrm_error wyrm_class_add_slot_f(wyrm_class* self, wyrm_symtab_entry slot_name, wyrm_uword flags)
{
    return wyrm_prototype_add_slot_f(&self->prototype, slot_name, flags);
}

WYRM_END_DECLS

#endif
