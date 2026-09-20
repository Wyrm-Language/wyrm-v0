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

/** A binding in the scope namespace: module exports, class statics, or
 * function-owner exports. Never instance attributes. */
wy_error wy_link_scope_member(wy_value owner, wy_symbol name, wy_value** out);

/** Find or load and publish a dependency, without executing it. The caller
 * must initialise a LOADED result inline on its current fiber. */
wy_error wy_link_import(wy_context* context, wy_string* path, wy_module** out);

WY_END_DECLS
#endif
