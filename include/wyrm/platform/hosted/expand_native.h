#ifndef WYRM_PLATFORM_HOSTED_EXPAND_NATIVE_H_
#define WYRM_PLATFORM_HOSTED_EXPAND_NATIVE_H_

#include <wyrm/context.h>

WY_BEGIN_DECLS

/**
 * `std::expand` (epic 10a D8/D10): compile-time decorator expansion in a
 * throwaway VM.
 *
 *   expand(tree, scope_image: bytes, entry: str) -> tree | error
 *
 * Creates a fresh machine and context (own allocator and heap, flagged
 * `expansion`, builtins but no `std::io` and no other host module), copies
 * `tree` into it, loads `scope_image` (a compiled module holding only the
 * compiling module's imports) and initialises it - its imports are resolved
 * by the parent's import hook, host code that reads the images; the child
 * itself never touches a file. Then loads module `entry` and calls its
 * `expand_tree(tree, scope)` on the child, copies the answered tree back into
 * the calling context and destroys the child.
 *
 * Only tree-shaped data crosses (nil, bool, numbers, str, symbol, pair, list,
 * tuple); anything else is an error value naming what was found. Called
 * inside an expansion VM it answers an error value ("expansion is not
 * re-entrant"): nesting depth is fixed at one.
 *
 * Every failure is an ordinary error value, never a fault: a compile step
 * reports it.
 */
wy_error wy_expand_module_new(wy_context* context, wy_module** out);

/**
 * Bytes the expansion VMs created so far left allocated after teardown
 * (always 0 unless something leaks); tests assert on it. Process-wide.
 */
wy_uword wy_expand_leaked_bytes(void);

/** Register `std::expand` (call after wy_io_module_install, which also
 * registers the implicit parent package `std`). */
wy_error wy_expand_module_install(wy_context* context);

WY_END_DECLS

#endif
