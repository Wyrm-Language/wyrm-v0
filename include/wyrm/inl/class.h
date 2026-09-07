#ifndef WYRM_INL_CLASS_H_
#define WYRM_INL_CLASS_H_

#include <wyrm/types.h>
#include <wyrm/prototype.h>

WY_BEGIN_DECLS

/**
 * Set the primitive for the class name
 */
WY_INLINE void wy_class_set_name_f(wy_class* self, wy_primitive name)
{
    self->sym_name = name;
}

/**
 * Get selector for the given symbol
 */
WY_INLINE wy_uword wy_class_get_slot_selector_f(wy_class* self, wy_symtab_entry sym_name)
{
    return wy_prototype_get_slot_selector_f(&self->prototype, sym_name);
}

WY_INLINE wy_error wy_class_add_slot_f(wy_class* self, wy_symtab_entry slot_name, wy_uword flags)
{
    return wy_prototype_add_slot_f(&self->prototype, slot_name, flags);
}

WY_END_DECLS

#endif
