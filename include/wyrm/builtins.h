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

/**
 * The shared body behind bare `println` (space-joined `format_value_f`
 * rendering of each arg, trailing "\n", writes through `context->io.write`)
 * - exposed so `std::io::println` (src/platform/hosted/io_native.c) can
 * reuse the exact same rendering rather than duplicating it, since this VM
 * already has its own `println`/`print` design distinct from the reference
 * corelib/std/io.wy wrapper (epic 5/M4's report).
 */
wy_error wy_builtin_println_body_f(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres);

WY_END_DECLS

#endif
