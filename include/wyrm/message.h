#ifndef WYRM_MESSAGE_H_
#define WYRM_MESSAGE_H_

#include <wyrm/module.h>
#include <wyrm/object.h>
#include <wyrm/value.h>

WY_BEGIN_DECLS

enum { WY_OVERLOAD_MAX_ARITY = 16 };

/**
 * One registered overload of a message (design_c_vm.md §7). `types[k]` for
 * `k < arity` is a CLASS value (ancestor-distance constraint), a PTYPE
 * value (exact primitive-tag constraint), or nil for a wildcard;
 * `types[arity..WY_OVERLOAD_MAX_ARITY)` is unused padding.
 */
typedef struct wy_overload
{
    wy_u16 arity;
    wy_value types[WY_OVERLOAD_MAX_ARITY];
    wy_value body;   /**< FUNCTION value */
} wy_overload;

/**
 * A message identity (design_c_vm.md §7, wyc-format.md §8.7): the runtime
 * binding behind a `messages[]` entry, shared by every path that resolves
 * to the same name in `owner`. Bound on first read into
 * `module->message_table` (single component) - epic 4/M2 does not resolve
 * qualified (`mod::name`) paths, that needs epic 5's import machinery.
 */
struct wy_message
{
    wy_object object;
    wy_symbol name;
    wy_module* owner;

    wy_overload* overloads;
    wy_uword overload_count;
    wy_uword overload_capacity;

    /**
     * True iff any overload has arity 1 with a wildcard or PTYPE
     * constraint at position 0 - kept up to date by
     * wy_message_add_overload_f so `msg`'s single-INSTANCE-receiver fast
     * path (design_c_vm.md §7) can check it in O(1) instead of rescanning
     * `overloads` on every dispatch.
     */
    bool has_wildcard_or_ptype_arity1;
};

extern const wy_object_type wy_message_type;

wy_error wy_message_new_f(wy_context* context, wy_symbol name, wy_module* owner, wy_message** out);

/** Append `(types[0..arity), body)` as a new overload. `arity` must be <= WY_OVERLOAD_MAX_ARITY. */
wy_error wy_message_add_overload_f(wy_context* context, wy_message* self, wy_u16 arity, const wy_value* types, wy_value body);

/**
 * Resolve (or create, on first mention) the single-component message
 * identity named `name` in `module->message_table` (wyc-format.md §7.3).
 * The shared primitive behind both `wy_module_resolve_message_f` and class
 * realisation's `m[]`-entry registration (design_c_vm.md §7), which names
 * its message identities directly rather than through a `messages[]` path.
 */
wy_error wy_module_message_by_name_f(wy_context* context, wy_module* module, wy_symbol name, wy_message** out);

/**
 * Resolve `module->messages[message_idx]` to its `wy_message` identity
 * (wyc-format.md §7.3 "resolving one entry"): binds and caches into
 * `messages[message_idx].bound` on first call. A single-component path
 * resolves (or creates, on first mention) the identity in
 * `module->message_table`; a qualified path is WY_ERR_NOSUPPORT this
 * milestone (epic 5 scope).
 */
wy_error wy_module_resolve_message_f(wy_context* context, wy_module* module, wy_uword message_idx, wy_message** out);

WY_END_DECLS

#endif
