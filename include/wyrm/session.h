#ifndef WYRM_SESSION_H_
#define WYRM_SESSION_H_

/*
 * REPL session module (doc/repl-plan.md).
 *
 * A session is one wy_module that is extended by each input instead of being
 * loaded once. A running VM holds raw pointers into a module's arrays (a
 * frame's `ip` into `code`, a wy_function's `proto` into `functions`), so those
 * arrays are reserved at full capacity when the session is created and only
 * ever appended to; they never move. Running out of a reservation is a clean
 * WY_ERR_SESSION_FULL, reported *before* anything is changed.
 *
 * The reservations come from the machine allocator (not the GC heap, whose
 * pressure accounting they would distort). On a hosted system a large malloc is
 * mapped lazily, so only the pages actually used cost memory; nothing here
 * touches the unused part.
 */

#include <wyrm/context.h>
#include <wyrm/module.h>

WY_BEGIN_DECLS

/** The arrays a session reserves. */
typedef enum wy_session_table
{
    WY_SESSION_CODE = 0,   /**< 32-bit instruction words */
    WY_SESSION_FUNCTIONS,
    WY_SESSION_CLASSES,    /**< class_protos and the realised classes[] */
    WY_SESSION_STATICS,
    WY_SESSION_SYMBOLS,
    WY_SESSION_GLOBALS,    /**< globals, fill_layer, fill_source, and the exports/free dicts */
    WY_SESSION_MESSAGES,
    WY_SESSION_TABLE_COUNT
} wy_session_table;

/** Capacities (element counts) for each reservation. Zero is invalid. */
typedef struct wy_session_config
{
    wy_uword capacity[WY_SESSION_TABLE_COUNT];
} wy_session_config;

/** Defaults: 16 MiB of code (4 Mi words) and tables sized to the compiler's 65535-entry pool limit. */
enum
{
    WY_SESSION_DEFAULT_CODE_WORDS = 4u * 1024u * 1024u,
    WY_SESSION_DEFAULT_FUNCTIONS = 65536u,
    WY_SESSION_DEFAULT_CLASSES = 4096u,
    /* The compiler caps every pool at 65535 entries (16-bit operands), so a
     * larger reservation would only be address space nothing can use. */
    WY_SESSION_DEFAULT_STATICS = 65536u,
    WY_SESSION_DEFAULT_SYMBOLS = 65536u,
    WY_SESSION_DEFAULT_GLOBALS = 65536u,
    WY_SESSION_DEFAULT_MESSAGES = 16384u,
};

/** The default configuration. */
void wy_session_config_default(wy_session_config* out);

/**
 * Create a session module: reserve every table, name it "__repl__", register
 * it with the context, and leave it READY with nothing in it.
 *
 * @return WY_ERR_NONE, WY_ERR_INVAL (null argument or a zero capacity), or
 *   WY_ERR_NOMEM (a reservation failed; nothing is leaked).
 */
wy_error wy_module_session_new(wy_context* context, const wy_session_config* config, wy_module** out);

/** True when `module` is a session module. */
bool wy_module_is_session(const wy_module* module);

/** The reserved capacity of `table`, or 0 if `module` is not a session. */
wy_uword wy_session_capacity(const wy_module* module, wy_session_table table);

/** How much of `table` is in use (its element count in the module). */
wy_uword wy_session_used(const wy_module* module, wy_session_table table);

/**
 * Whether `count` more elements fit in `table`: WY_ERR_NONE, or
 * WY_ERR_SESSION_FULL. Checks only; changes nothing.
 */
wy_error wy_session_check_room(const wy_module* module, wy_session_table table, wy_uword count);

/*
 * Internal (not ABI). A session module is one module that imports over its
 * whole life, but an import fills only the free names that exist when it runs.
 * A later input can introduce new free names (`std::io::println`) whose
 * import ran in an earlier input, so the session remembers its imports and
 * re-runs their fills after every extend.
 */
void wy_session_note_import_(wy_context* context, wy_module* module, wy_symbol path, wy_module* dep);
wy_error wy_session_refill_(wy_context* context, wy_module* module);
void wy_session_free_(wy_allocator* allocator, struct wy_session* session);

/**
 * Extend `module` (a session) with a delta image (see "Session extension" in
 * src/module.c for the format): decode and append its symbols, statics,
 * functions, classes and messages, copy its code into the reserve, add its
 * globals and names, and fill the new free names from the builtins.
 *
 * All-or-nothing: on any error the module is exactly as before. WY_ERR_IMAGE
 * for a malformed delta or one whose base counts do not match this module's
 * (the compiler and this side have drifted apart), WY_ERR_SESSION_FULL when a
 * reservation would overflow (checked before anything changes), WY_ERR_INVAL
 * for a non-session module.
 *
 * @param out_init_function set to the absolute index of the function that runs
 *   this input (the delta header's `i`); may be NULL.
 */
wy_error wy_module_extend(wy_context* context, wy_module* module, const wy_module_image* image, wy_uword* out_init_function);

/**
 * Run function `function_index` of a session with no arguments and answer its
 * first result. Unlike wy_module_run_init this never changes the module's
 * state: a fault is returned and the session carries on.
 */
wy_error wy_module_run_function(wy_context* context, wy_module* module, wy_uword function_index, wy_value* out_result);

WY_END_DECLS

#endif
