#ifndef WYRM_BUILTINS_H_
#define WYRM_BUILTINS_H_

#include <wyrm/fwd.h>
#include <wyrm/sys/errors.h>

WY_BEGIN_DECLS

/**
 * Build the synthetic builtins module (design_c_vm.md §5's "Builtins are a
 * module" paragraph, epic 2/M5).
 *
 * Not loaded from a `.wyc` image: `state = WY_MODULE_BUILTIN`, `exports`
 * populated directly rather than by the BSON name-slot-dict loader. Exposes
 * at least `println`, `print` (leaf natives) and `nil` (a plain value) as
 * global slots reachable through `module->exports`, for `wy_link.c`'s layer
 * 3 fill to read.
 *
 * @return WY_ERR_NONE, WY_ERR_INVAL for a null argument, or WY_ERR_NOMEM
 */
wy_error wy_builtins_new(wy_context* context, wy_module** out);

WY_END_DECLS

#endif
