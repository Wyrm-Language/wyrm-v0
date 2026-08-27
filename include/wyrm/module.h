#ifndef WYRM_MODULE_H_
#define WYRM_MODULE_H_

#include <wyrm/core.h>

WYRM_BEGIN_DECLS

extern const wyrm_object_type wy_module_type;

struct wy_module
{
    wyrm_object head;

    wyrm_uword* slot_sparse;
};

void wy_module_init_static_f(wy_module* self);

WYRM_END_DECLS

#endif
