#ifndef WYRM_INSTANCE_H_
#define WYRM_INSTANCE_H_

#include <wyrm/class.h>
#include <wyrm/object.h>
#include <wyrm/value.h>

WY_BEGIN_DECLS

struct wy_instance;
#ifndef __cplusplus
typedef struct wy_instance wy_instance;
#endif

extern const wy_object_type wy_instance_type;

/**
 * A class instance (design_c_vm.md §7, wyc-format.md §8.6): `cls`'s
 * base-first slots, storage for both plain and virtual slots (a virtual
 * slot's storage word simply goes unused by `getattr`/`setattr`, which
 * dispatch to its getter/setter instead; `getslot`/`setslot` still address
 * it directly).
 */
struct wy_instance
{
    wy_object object;
    wy_class* cls;
    wy_value slots[];
};

/**
 * Allocate an instance of `cls`: `cls->slot_count` slots, each at its
 * class's declared default (Unset where none), base-first.
 */
wy_error wy_instance_new_f(wy_context* context, wy_class* cls, wy_instance** out);

WY_END_DECLS

#endif
