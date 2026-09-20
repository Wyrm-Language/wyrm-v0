#ifndef WYRM_CLASS_H
#define WYRM_CLASS_H

#include <wyrm/fwd.h>
#include <wyrm/module.h>
#include <wyrm/slot.h>
#include <wyrm/symtab_entry.h>
#include <wyrm/value.h>

WY_BEGIN_DECLS

enum {
    WY_CLASS_ERROR = 0x01
};

enum { WY_CLASS_MSG_MAP_SIZE = WY_CLASS_MAX_MESSAGES };

/** Ancestor distance meaning "wildcard, matches anything" (design_c_vm.md §7). */
#define WY_WILDCARD_DISTANCE 0xFFFFu

/**
 * One realised slot (design_c_vm.md §7, wyc-format.md §8.6). A plain slot
 * has both `getter`/`setter` Unset and is read/written directly by index
 * (`getslot`/`setslot`) or by name (`getattr`/`setattr`, resolved through
 * this table). A slot with either set is virtual: `getattr`/`setattr`
 * dispatch to that function instead of touching storage; `getslot`/
 * `setslot` still address the (unused) storage slot directly.
 */
typedef struct wy_class_slot
{
    wy_symbol name;
    wy_value default_value;
    wy_value getter;   /**< FUNCTION value, or Unset for a plain slot */
    wy_value setter;   /**< FUNCTION value, or Unset for a plain/read-only slot */
} wy_class_slot;

/**
 * One `msg_map` row (design_c_vm.md §7, wyc-format.md §8.6): a message
 * identity bound to the body realised for this class. `msg` stays WY_NULL
 * until epic 4/M2 wires up message identities (`wy_message`); M1 only
 * fills `body`.
 */
typedef struct wy_class_msg_entry
{
    wy_message* msg;
    wy_value body;   /**< FUNCTION value */
} wy_class_msg_entry;

/**
 * A realised class (design_c_vm.md §7). Supersedes the epic 3
 * `wy_prototype`-based stub wholesale (design §7: "The `wy_prototype`-based
 * class (256 reserved slots) is replaced").
 *
 * `wy_class_new` still builds a bare class (no super/slots/messages) for
 * the five builtin error classes (`src/builtin/builtins.c`), which do not
 * go through `wy_class_realise_f`.
 */
struct wy_class
{
    wy_object object;
    wy_symbol name;
    wy_class* super;
    wy_module* module;   /**< owning module; WY_NULL for a bare (builtin) class */

    wy_u16 slot_count;   /**< inherited + own, base-first (wyc-format.md §8.6) */
    wy_u16 depth;        /**< 0 for a class with no super, else super->depth + 1 */
    wy_u8 flags;         /**< WY_CLASS_ERROR */
    wy_u8 msg_count;     /**< <= WY_CLASS_MSG_MAP_SIZE */

    wy_class_slot* slots;   /**< slot_count entries, base-first */
    wy_class_msg_entry msg_map[WY_CLASS_MSG_MAP_SIZE];

    wy_slot_dict statics;   /**< name -> module global slot index */
    wy_value init;           /**< FUNCTION value, or Unset for no constructor */
};

extern const wy_object_type wy_type_class;

/** Allocate a bare class: no super, no slots, no messages, Unset init. */
wy_error wy_class_new(wy_context* context, wy_class** out);

WY_INLINE void wy_class_set_name_f(wy_class* self, wy_primitive name);

/**
 * Realise `module->class_protos[class_idx]` into a `wy_class` (wyc-format.md
 * §8.6, design_c_vm.md §7): resolves the superclass via its global slot,
 * computes base-first slot layout and depth, builds each slot's default/
 * getter/setter, and fills `msg_map`, `statics` and `init`. Idempotent:
 * caches the result in `module->classes[class_idx]` and returns the cached
 * class on a repeat call for the same index.
 *
 * @return WY_ERR_NONE, WY_ERR_RANGE for a bad `class_idx`, WY_ERR_NOMEM, or
 *   WY_ERR_BAD_TYPE if the superclass global slot does not hold a class
 *   value at realisation time (wyc-format.md §8.6's "read at the moment the
 *   class is realised").
 */
wy_error wy_class_realise_f(wy_context* context, wy_module* module, wy_uword class_idx, wy_class** out);

/**
 * Ancestor distance from `cls` to `target`: 0 if `cls == target`, 1 for
 * `cls->super == target`, and so on. WY_WILDCARD_DISTANCE if `target` is
 * not an ancestor of `cls` (design_c_vm.md §7's dispatch ranking).
 */
wy_uword wy_class_distance_f(wy_class* cls, wy_class* target);

/** Boolean ancestor check (`is`): true iff `wy_class_distance_f` is finite. */
WY_INLINE bool wy_class_is_ancestor_f(wy_class* cls, wy_class* target)
{
    return wy_class_distance_f(cls, target) != WY_WILDCARD_DISTANCE;
}

/**
 * Find a slot by name, walking `cls`, then `cls->super`, and so on
 * (design_c_vm.md §7's `getattr`/`setattr` scan). `*index_out`, if given,
 * receives the slot's base-first storage index (for `getslot`/`setslot`-
 * compatible access to a plain slot).
 *
 * @return the slot, or WY_NULL if no ancestor declares that name.
 */
wy_class_slot* wy_class_find_slot_f(wy_class* cls, wy_symbol name, wy_uword* index_out);

/**
 * Find the applicable constructor for `cls` (design_c_vm.md §7,
 * wyc-format.md §8.6's `i` field): `cls`'s own `init` if set, else the
 * nearest ancestor's - "the class or any ancestor defines one"
 * (wyrm_eval_parse_tree.py's `instantiate`). A compiled class's `init` is
 * already the fully resolved constructor (no multi-dispatch ranking is
 * needed at this layer - see epic_4_report.md's M2 notes), so this is a
 * plain walk, not a dispatch.
 *
 * @return the FUNCTION value to call, or Unset if no ancestor has one.
 */
wy_value wy_class_find_init_f(wy_class* cls);

/**
 * Set the primitive for the class name
 */
WY_INLINE void wy_class_set_name_f(wy_class* self, wy_primitive name)
{
    self->name = name.symtab_entry;
}

WY_END_DECLS

#endif
