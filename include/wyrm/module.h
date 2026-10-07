#ifndef WYRM_MODULE_H_
#define WYRM_MODULE_H_

#include <wyrm/image.h>
#include <wyrm/object.h>
#include <wyrm/slot.h>
#include <wyrm/value.h>

WY_BEGIN_DECLS

/**
 * A module's lifecycle state (wyc-format.md §7.1).
 *
 * Loading produces LOADED; published init frames transition through
 * INITIALISING to READY or FAILED. BUILTIN needs no init.
 */
typedef enum wy_module_state
{
    WY_MODULE_LOADED = 0,
    WY_MODULE_INITIALISING,
    WY_MODULE_READY,
    WY_MODULE_FAILED,
    WY_MODULE_BUILTIN,
} wy_module_state;

/** Function flags, `f` in a functions[] entry (wyc-format.md §8.5). */
enum
{
    WY_FN_COROUTINE = 1,
    WY_FN_MESSAGE = 2,
    WY_FN_VARARGS = 4,
    WY_FN_KWARGS = 8,
};

enum { WY_CLASS_MAX_MESSAGES = 16 };

struct wy_session;

/** A function parameter (wyc-format.md §8.5's `p` entries). */
typedef struct wy_param
{
    wy_symbol name;
    wy_i32 default_static;   /**< static pool index, or -1 for none */
} wy_param;

/** One `functions[]` entry (wyc-format.md §8.5). */
typedef struct wy_function_proto
{
    wy_symbol name;
    wy_u32 code_offset;
    wy_u16 nparams;
    wy_u16 nlocals;
    wy_u16 ncaptures;
    wy_u16 ndispatch;
    wy_u8 flags;
    wy_u8 nresults;
    wy_param* params;         /**< nparams entries */
    wy_u16* dispatch_slots;   /**< ndispatch entries: global slot indices */
} wy_function_proto;

/** One `classes[].sl` entry (wyc-format.md §8.6). */
typedef struct wy_slot_proto
{
    wy_symbol name;
    wy_i32 default_static;   /**< static pool index, or -1 for none */
    wy_i32 getter_fn;        /**< function index, or -1 for none (virtual slot) */
    wy_i32 setter_fn;        /**< function index, or -1 for none (virtual slot) */
} wy_slot_proto;

/** One `classes[].m` entry (wyc-format.md §8.6): a message map row. */
typedef struct wy_class_message_proto
{
    wy_symbol name;
    wy_u16 fn;
} wy_class_message_proto;

/** One `classes[].st` entry (wyc-format.md §8.6): a class static. */
typedef struct wy_class_static_proto
{
    wy_symbol name;
    wy_u16 global;
} wy_class_static_proto;

/** One `classes[]` entry (wyc-format.md §8.6). */
typedef struct wy_class_proto
{
    wy_symbol name;
    wy_i32 super_slot;   /**< global slot index, or -1 for object */
    wy_i32 init_fn;      /**< function index, or -1 for none */
    wy_u16 nslots;
    wy_u16 nmsgs;        /**< <= WY_CLASS_MAX_MESSAGES */
    wy_u16 nstatics;
    wy_slot_proto* slots;
    wy_class_message_proto msgs[WY_CLASS_MAX_MESSAGES];
    wy_class_static_proto* statics;
} wy_class_proto;

/* Defined in epic 4; only pointers to it exist before then. */
struct wy_message;
#ifndef __cplusplus
typedef struct wy_message wy_message;
#endif

/**
 * One `messages[]` entry (wyc-format.md §8.7): a message identity, by path.
 * `bound` is always WY_NULL before epic 4 (nothing binds a message yet).
 */
typedef struct wy_message_ref
{
    wy_u16 path_len;
    wy_u16* path;        /**< path_len symbol indices into module->symbols */
    wy_message* bound;
} wy_message_ref;

/**
 * A layer-2 `import mod::*` fill source (wyc-format.md §7.2).
 * The module owns the copied except-symbol array.
 */
typedef struct wy_wildcard
{
    wy_module* target;
    wy_uword except_count;
    wy_symbol* excepts;
} wy_wildcard;

extern const wy_object_type wy_module_type;

/**
 * A loaded `.wyc` module (design_c_vm.md §5).
 *
 * Populated by wy_module_load_image/wy_module_load_bytes through load steps
 * 1-5 of wyc-format.md §7.1: header, slot_defaults, symbols, tables and
 * builtin fill. Step 6 publishes the module before running its init.
 */
struct wy_module
{
    wy_object head;
    wy_symbol name;
    wy_module_state state;
    wy_string* import_path;    /**< full cache key, without symbol truncation */
    wy_function_proto init_proto; /**< stable prototype for inline init frames */

    const wy_u8* image;    /**< the whole file this module was loaded from */
    wy_uword image_len;
    bool owns_image;        /**< whether finalize frees `image` */

    const wy_u32* code;     /**< points into `image`; never copied */
    wy_uword code_len;      /**< word count */
    wy_u16 init_nlocals;    /**< L-frame size for code word offset 0 */

    wy_value* globals;      /**< global_count entries, all Unset until slot_defaults/init */
    wy_uword global_count;
    wy_u8* fill_layer;       /**< global_count entries: winning §7.2 layer; 0 = unfilled, high bit = ambiguity */
    wy_symbol* fill_source;  /**< global_count entries: winning source spelling, for ambiguity faults */
    /**
     * global_count entries, or NULL for a module that never imports (the
     * builtins): where fill_layer has WY_LINK_ALIAS, the imported binding
     * this slot *is* - the defining module's own global, never a copy of its
     * value (project design/modules.md M2, Lisp semantics).
     */
    wy_value** aliases;

    wy_value* statics;
    wy_uword static_count;

    wy_symbol* symbols;
    wy_uword symbol_count;

    wy_function_proto* functions;
    wy_uword function_count;

    wy_class_proto* class_protos;
    wy_class** classes;      /**< NULL until epic 4 realises class objects */
    wy_uword class_count;

    wy_message_ref* messages;
    wy_uword message_count;

    wy_slot_dict exports;      /**< name -> global slot, every global this module defines */
    wy_slot_dict free_names;   /**< name -> global slot, every name this module reads but doesn't define */
    wy_dict* message_table;    /**< NULL until epic 4 */

    wy_wildcard* wildcards;    /**< registered wildcard namespaces */
    wy_uword wildcard_count;

    /**
     * Non-NULL for a REPL session module (wyrm/session.h): the arrays above
     * (code, functions, class_protos, classes, statics, symbols, globals,
     * fill_layer, fill_source, messages) are fixed-capacity reservations
     * that only ever grow in place, so pointers into them (frames' `ip`, a
     * function's proto) stay valid; they belong to the machine allocator, not
     * the GC heap. NULL for every ordinary module.
     */
    struct wy_session* session;
    /* Session extension bookkeeping (module.c): how far an in-progress
     * wy_module_extend has filled each table, so a failure can free what the
     * partial entries own. Unused (0) outside an extend. */
    wy_uword session_fill_functions_;
    wy_uword session_fill_classes_;
    wy_uword session_fill_messages_;
};

#define WY_MODULE_GET_OBJ(self) (&((self)->head))

/** Zero out `self` and initialize its object header (state = WY_MODULE_LOADED). */
void wy_module_init_static_f(wy_module* self);

/** Allocate and zero-initialize an empty, GC-tracked module. */
wy_module* wy_module_new_f(wy_context* context);

/**
 * Load steps 1-5 of wyc-format.md §7.1 from an already-parsed container.
 *
 * Executes nothing; fills free slots from context->builtins when installed.
 * Every table index is bounds-checked against the table it indexes.
 *
 * `image` must outlive the returned module: every pointer the module holds
 * into section payloads (code, string data before it is copied, etc.)
 * points into `image->sections[...].data`.
 *
 * @return WY_ERR_NONE, WY_ERR_INVAL for a null argument, WY_ERR_NOMEM, or
 *   WY_ERR_IMAGE for a malformed table (bad key type, out-of-range index,
 *   an oversized class message map, ...)
 */
wy_error wy_module_load_image(wy_context* context, const wy_module_image* image, wy_module** out);

/**
 * wy_image_from_bytes + wy_module_load_image, owning `data` on success if
 * `take_ownership` is set (freed by the module's finalizer).
 */
wy_error wy_module_load_bytes(wy_context* context, const wy_u8* data, wy_uword len, bool take_ownership, wy_module** out);

/**
 * Load step 6 (wyc-format.md §7.1): run the module's top-level init code -
 * word offset 0 of `code`, a zero-argument, zero-capture call with
 * `nlocals = init_nlocals` - synchronously via wy_vm_call_sync. Host entry
 * only: VM imports push init frames inline. READY/BUILTIN is a no-op;
 * INITIALISING returns CYCLE and FAILED returns LINK.
 *
 * On success sets `module->state = WY_MODULE_READY`; on a fault sets it to
 * WY_MODULE_FAILED (the fault value is left on `context->current_fiber->fault`
 * for the caller to inspect, per wy_vm_call_sync's contract).
 *
 * @return WY_ERR_NONE, WY_ERR_FAULT, or whatever wy_vm_call_sync/wy_function_new returned
 */
wy_error wy_module_run_init(wy_context* context, wy_module* module);

WY_END_DECLS

#endif
