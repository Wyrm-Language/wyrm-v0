#include <wyrm/session.h>

#include <wyrm/link.h>
#include <wyrm/list.h>
#include <wyrm/machine.h>
#include <wyrm/string.h>

/* The reservation bookkeeping hung off wy_module::session. */
typedef struct session_import_
{
    wy_symbol path;
    wy_module* dep;
} session_import_;

struct wy_session
{
    wy_session_config config;
    session_import_* imports;   /* every module this session imported, for refills */
    wy_uword import_count;
    wy_uword import_capacity;
};

void wy_session_config_default(wy_session_config* out)
{
    out->capacity[WY_SESSION_CODE] = WY_SESSION_DEFAULT_CODE_WORDS;
    out->capacity[WY_SESSION_FUNCTIONS] = WY_SESSION_DEFAULT_FUNCTIONS;
    out->capacity[WY_SESSION_CLASSES] = WY_SESSION_DEFAULT_CLASSES;
    out->capacity[WY_SESSION_STATICS] = WY_SESSION_DEFAULT_STATICS;
    out->capacity[WY_SESSION_SYMBOLS] = WY_SESSION_DEFAULT_SYMBOLS;
    out->capacity[WY_SESSION_GLOBALS] = WY_SESSION_DEFAULT_GLOBALS;
    out->capacity[WY_SESSION_MESSAGES] = WY_SESSION_DEFAULT_MESSAGES;
}

/* count * element bytes, or 0 on overflow (the caller treats 0 as failure). */
static wy_uword reserve_bytes_(wy_uword count, wy_uword element)
{
    if (count == 0 || element == 0) { return 0; }
    if (count > ((wy_uword) -1) / element) { return 0; }
    return count * element;
}

static void* reserve_(wy_allocator* allocator, wy_uword count, wy_uword element)
{
    wy_uword bytes = reserve_bytes_(count, element);
    if (bytes == 0) { return WY_NULL; }
    return wy_allocator_alloc(allocator, bytes);
}

wy_error wy_module_session_new(wy_context* context, const wy_session_config* config, wy_module** out)
{
    if (context == WY_NULL || config == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }
    for (int t = 0; t < WY_SESSION_TABLE_COUNT; t++) {
        if (config->capacity[t] == 0) { return WY_ERR_INVAL; }
    }

    wy_allocator* allocator = wy_context_get_machine(context)->allocator;
    const wy_uword* cap = config->capacity;

    /* Reserve everything into locals first: a failure frees them and leaves no
     * half-built module behind. */
    struct wy_session* session = (struct wy_session*) wy_allocator_alloc(allocator, sizeof(*session));
    wy_u32* code = (wy_u32*) reserve_(allocator, cap[WY_SESSION_CODE], sizeof(wy_u32));
    wy_function_proto* functions = (wy_function_proto*) reserve_(allocator, cap[WY_SESSION_FUNCTIONS], sizeof(wy_function_proto));
    wy_class_proto* class_protos = (wy_class_proto*) reserve_(allocator, cap[WY_SESSION_CLASSES], sizeof(wy_class_proto));
    wy_class** classes = (wy_class**) reserve_(allocator, cap[WY_SESSION_CLASSES], sizeof(wy_class*));
    wy_value* statics = (wy_value*) reserve_(allocator, cap[WY_SESSION_STATICS], sizeof(wy_value));
    wy_symbol* symbols = (wy_symbol*) reserve_(allocator, cap[WY_SESSION_SYMBOLS], sizeof(wy_symbol));
    wy_value* globals = (wy_value*) reserve_(allocator, cap[WY_SESSION_GLOBALS], sizeof(wy_value));
    wy_u8* fill_layer = (wy_u8*) reserve_(allocator, cap[WY_SESSION_GLOBALS], sizeof(wy_u8));
    wy_symbol* fill_source = (wy_symbol*) reserve_(allocator, cap[WY_SESSION_GLOBALS], sizeof(wy_symbol));
    wy_message_ref* messages = (wy_message_ref*) reserve_(allocator, cap[WY_SESSION_MESSAGES], sizeof(wy_message_ref));

    bool ok = session && code && functions && class_protos && classes && statics && symbols && globals
        && fill_layer && fill_source && messages;
    wy_module* module = WY_NULL;
    if (ok) {
        module = wy_module_new_f(context);
        ok = module != WY_NULL;
    }
    if (!ok) {
        wy_allocator_free(allocator, session);
        wy_allocator_free(allocator, code);
        wy_allocator_free(allocator, functions);
        wy_allocator_free(allocator, class_protos);
        wy_allocator_free(allocator, classes);
        wy_allocator_free(allocator, statics);
        wy_allocator_free(allocator, symbols);
        wy_allocator_free(allocator, globals);
        wy_allocator_free(allocator, fill_layer);
        wy_allocator_free(allocator, fill_source);
        wy_allocator_free(allocator, messages);
        return WY_ERR_NOMEM;
    }

    session->config = *config;
    session->imports = WY_NULL;
    session->import_count = 0;
    session->import_capacity = 0;
    module->session = session;
    module->code = code;
    module->code_len = 0;
    module->functions = functions;
    module->function_count = 0;
    module->class_protos = class_protos;
    module->classes = classes;
    module->class_count = 0;
    module->statics = statics;
    module->static_count = 0;
    module->symbols = symbols;
    module->symbol_count = 0;
    module->globals = globals;
    module->fill_layer = fill_layer;
    module->fill_source = fill_source;
    module->global_count = 0;
    module->messages = messages;
    module->message_count = 0;

    /* The name -> slot dictionaries are pre-sized for the whole reservation so
     * appends never need to grow them (same idea as the builtins' spare slots). */
    wy_error err = wy_slot_dict_expand_f(&module->exports, allocator, cap[WY_SESSION_GLOBALS] * 2);
    if (err != WY_ERR_NONE) { return err; }
    err = wy_slot_dict_expand_f(&module->free_names, allocator, cap[WY_SESSION_GLOBALS] * 2);
    if (err != WY_ERR_NONE) { return err; }

    wy_string* path = WY_NULL;
    err = wy_string_strdup(context, "__repl__", &path);
    if (err != WY_ERR_NONE) { return err; }
    module->import_path = path;
    err = wy_context_intern(context, "__repl__", 8, &module->name);
    if (err != WY_ERR_NONE) { return err; }

    module->state = WY_MODULE_READY;
    err = wy_context_module_register(context, module, WY_NULL);
    if (err != WY_ERR_NONE) { return err; }

    *out = module;
    return WY_ERR_NONE;
}

bool wy_module_is_session(const wy_module* module)
{
    return module != WY_NULL && module->session != WY_NULL;
}

wy_uword wy_session_capacity(const wy_module* module, wy_session_table table)
{
    if (!wy_module_is_session(module) || (int) table < 0 || table >= WY_SESSION_TABLE_COUNT) { return 0; }
    return module->session->config.capacity[table];
}

wy_uword wy_session_used(const wy_module* module, wy_session_table table)
{
    if (!wy_module_is_session(module)) { return 0; }
    switch (table) {
    case WY_SESSION_CODE: return module->code_len;
    case WY_SESSION_FUNCTIONS: return module->function_count;
    case WY_SESSION_CLASSES: return module->class_count;
    case WY_SESSION_STATICS: return module->static_count;
    case WY_SESSION_SYMBOLS: return module->symbol_count;
    case WY_SESSION_GLOBALS: return module->global_count;
    case WY_SESSION_MESSAGES: return module->message_count;
    case WY_SESSION_TABLE_COUNT: break;
    }
    return 0;
}

wy_error wy_session_check_room(const wy_module* module, wy_session_table table, wy_uword count)
{
    if (!wy_module_is_session(module) || table >= WY_SESSION_TABLE_COUNT) { return WY_ERR_INVAL; }
    wy_uword cap = module->session->config.capacity[table];
    wy_uword used = wy_session_used(module, table);
    /* Written to not overflow: used <= cap always, so cap - used is safe. */
    if (used > cap || count > cap - used) { return WY_ERR_SESSION_FULL; }
    return WY_ERR_NONE;
}

void wy_session_note_import_(wy_context* context, wy_module* module, wy_symbol path, wy_module* dep)
{
    if (!wy_module_is_session(module)) { return; }
    struct wy_session* session = module->session;
    for (wy_uword i = 0; i < session->import_count; i++) {
        if (session->imports[i].path == path && session->imports[i].dep == dep) { return; }
    }
    wy_allocator* allocator = wy_context_get_machine(context)->allocator;
    if (session->import_count == session->import_capacity) {
        wy_uword capacity = session->import_capacity == 0 ? 16 : session->import_capacity * 2;
        session_import_* grown = (session_import_*) wy_allocator_realloc(allocator, session->imports,
            capacity * sizeof(session_import_));
        if (grown == WY_NULL) { return; }  /* best effort: a missed note only costs a later refill */
        session->imports = grown;
        session->import_capacity = capacity;
    }
    session->imports[session->import_count].path = path;
    session->imports[session->import_count].dep = dep;
    session->import_count++;
}

wy_error wy_session_refill_(wy_context* context, wy_module* module)
{
    if (!wy_module_is_session(module)) { return WY_ERR_NONE; }
    struct wy_session* session = module->session;
    for (wy_uword i = 0; i < session->import_count; i++) {
        wy_error err = wy_link_fill_from_import(context, module, session->imports[i].path, session->imports[i].dep);
        if (err != WY_ERR_NONE) { return err; }
    }
    for (wy_uword i = 0; i < module->wildcard_count; i++) {
        wy_error err = wy_link_fill_from_wildcard(context, module, &module->wildcards[i]);
        if (err != WY_ERR_NONE) { return err; }
    }

    /* A session is the entry module: `__name__` reads "__main__" (the host
     * seeds `__ARGS` itself). Set once, when a free `__name__` first appears. */
    wy_symbol name_sym = WY_NULL;
    wy_error err = wy_context_intern(context, "__name__", 8, &name_sym);
    if (err != WY_ERR_NONE) { return err; }
    wy_uword slot = wy_slot_dict_get(&module->free_names, name_sym);
    if (slot != WY_SLOT_INVALID && wy_value_is_unset(module->globals[slot])) {
        wy_string* main_name = WY_NULL;
        err = wy_string_new(context, "__main__", 8, &main_name);
        if (err != WY_ERR_NONE) { return err; }
        err = wy_link_seed_global(context, module, "__name__", wy_value_object(WY_TYPE_TAG_STR, (wy_object*) main_name));
        if (err != WY_ERR_NONE) { return err; }
    }

    /* `__ARGS` is an empty list in a REPL (there is no script). */
    wy_symbol args_sym = WY_NULL;
    err = wy_context_intern(context, "__ARGS", 6, &args_sym);
    if (err != WY_ERR_NONE) { return err; }
    slot = wy_slot_dict_get(&module->free_names, args_sym);
    if (slot != WY_SLOT_INVALID && wy_value_is_unset(module->globals[slot])) {
        wy_list* empty = WY_NULL;
        err = wy_list_new(context, 0, &empty);
        if (err != WY_ERR_NONE) { return err; }
        err = wy_link_seed_global(context, module, "__ARGS", wy_value_object(WY_TYPE_TAG_LIST, (wy_object*) empty));
        if (err != WY_ERR_NONE) { return err; }
    }
    return WY_ERR_NONE;
}

void wy_session_free_(wy_allocator* allocator, struct wy_session* session)
{
    if (session == WY_NULL) { return; }
    wy_allocator_free(allocator, session->imports);
    wy_allocator_free(allocator, session);
}
