#ifndef WYRM_LINK_H_
#define WYRM_LINK_H_

#include <wyrm/fwd.h>
#include <wyrm/sys/errors.h>

WY_BEGIN_DECLS

/**
 * Layer 3 fill (wyc-format.md §7.2, design_c_vm.md §5): fill every free-name
 * slot of `module` that is still unfilled (`fill_layer[slot] == 0`) from
 * `builtins`'s exports, by bare-name match.
 *
 * A free name with no matching builtins export is left Unset - not an error
 * at fill time, only at `gget` read time (already handled by the dispatch
 * loop). Full layer 1 (imports) and layer 2 (`import *`) fill are epic 2/M6;
 * not implemented here.
 *
 * A no-op if `module->free_names` is empty, or if `builtins` is WY_NULL.
 *
 * @return WY_ERR_NONE, or WY_ERR_INVAL for a null context/module argument
 */
wy_error wy_link_fill_from_builtins(wy_context* context, wy_module* module, wy_module* builtins);

WY_END_DECLS

#endif
