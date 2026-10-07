#ifndef WYRM_LINK_H_
#define WYRM_LINK_H_

#include <wyrm/module.h>

WY_BEGIN_DECLS

/** Low bits are the winning layer; the high bit marks a deferred ambiguity
 * fault stored in globals[slot]. An ordinary error value has no marker. */
enum { WY_LINK_AMBIGUOUS = 128, WY_LINK_LAYER_MASK = 127 };

/** Stronger (lower numbered) layer wins. Same-source/identical-value fills
 * are no-ops, including after ambiguity. Allocation errors return immediately;
 * ambiguity itself is deferred until the global is read. */
wy_error wy_link_fill(wy_context* context, wy_module* module, wy_uword slot,
    wy_value value, wy_u8 layer, wy_symbol source);
wy_error wy_link_fill_from_builtins(wy_context* context, wy_module* module, wy_module* builtins);
wy_error wy_link_fill_from_import(wy_context* context, wy_module* module, wy_symbol path, wy_module* dep);
wy_error wy_link_fill_from_wildcard(wy_context* context, wy_module* module, const wy_wildcard* wildcard);
wy_error wy_link_register_wildcard(wy_context* context, wy_module* module,
    wy_module* dep, const wy_value* excepts, wy_uword count, wy_uword* index);

/** Make `dep`'s messages visible in `module`'s message table after a
 * successful import: each entry is adopted by reference (the same
 * wy_message the defining module's `class` ops and `reg_msg`s registered
 * into), so a bare `recv ! name(...)` in `module` dispatches on the
 * dependency's overloads - the compiled counterpart of the tree walker's
 * import-time adoption (pypoc/wypoc/wyrm_eval_parse_tree.py
 * _adopt_messages/_merge_message). A name `module` already holds wins;
 * re-adopting the same message is a no-op. */
wy_error wy_link_adopt_messages(wy_context* context, wy_module* module, wy_module* dep);

/** A binding in the scope namespace: module exports, class statics, or
 * function-owner exports. Never instance attributes. */
wy_error wy_link_scope_member(wy_value owner, wy_symbol name, wy_value** out);

/**
 * Seed a host/loader-supplied global (`__name__`, `__ARGS`) into the module's
 * free-name slot before its init runs. A module that never reads `name` is
 * left untouched (WY_ERR_NONE).
 */
wy_error wy_link_seed_global(wy_context* context, wy_module* module, const char* name, wy_value value);

/** Find or load and publish a dependency, without executing it. The caller
 * must initialise a LOADED result inline on its current fiber. */
wy_error wy_link_import(wy_context* context, wy_string* path, wy_module** out);

/**
 * wy_link_import for the first `len` bytes of `path`. `prefix` marks a
 * package loaded on the way to one of its children (design/modules.md M1):
 * one that is still initialising is then answered as it is, INITIALISING,
 * instead of WY_ERR_CYCLE, and the caller must neither run nor fill from it.
 */
wy_error wy_link_import_ex(wy_context* context, wy_string* path, wy_uword len, bool prefix, wy_module** out);

WY_END_DECLS
#endif
