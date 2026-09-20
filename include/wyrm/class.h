#ifndef WYRM_CLASS_H
#define WYRM_CLASS_H

#include <wyrm/fwd.h>
#include <wyrm/prototype.h>

WY_BEGIN_DECLS


enum {
    WY_CLASS_ERROR = 0x01
};

struct wy_class
{
    wy_prototype prototype;
    wy_class* super;
    wy_primitive sym_name;
    wy_uword flags;
};

extern const wy_object_type wy_type_class;

wy_error wy_class_new(wy_context* context, wy_class** out);
WY_INLINE void wy_class_set_name_f(wy_class* self, wy_primitive name);
WY_INLINE wy_error wy_class_add_slot_f(wy_class* self, wy_symtab_entry slot_name, wy_uword flags);
WY_INLINE wy_uword wy_class_get_slot_selector_f(wy_class* self, wy_symtab_entry sym_name);

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
