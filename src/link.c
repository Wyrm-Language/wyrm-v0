#include <wyrm.h>
#include <wyrm/link.h>
#include <wyrm/session.h>
#include <wyrm/dict.h>
#include <wyrm/error.h>
#include <wyrm/function.h>
#include <wyrm/image.h>
#include <wyrm/string.h>

#include <stdio.h>
#include <string.h>

/* Heap values compare by identity, never by their contents. Immediate values
 * have no allocation identity; compare their active primitive field. */
static bool identical_(wy_value a, wy_value b)
{
    if (a.type != b.type) { return false; }
    switch (a.type) {
    case WY_TYPE_TAG_NIL: return true;
    case WY_TYPE_TAG_BOOL: return a.data.flag == b.data.flag;
    case WY_TYPE_TAG_WORD: return a.data.word == b.data.word;
    case WY_TYPE_TAG_UWORD: case WY_TYPE_TAG_PTYPE: return a.data.uword == b.data.uword;
    case WY_TYPE_TAG_FLOAT: return a.data.fp == b.data.fp;
    case WY_TYPE_TAG_SYMBOL: return a.data.symtab_entry == b.data.symtab_entry;
    default: return a.data.gc_object == b.data.gc_object;
    }
}

wy_value* wy_link_binding(wy_module* module, wy_uword slot)
{
    if (module->fill_layer != WY_NULL && module->aliases != WY_NULL &&
        (module->fill_layer[slot] & WY_LINK_ALIAS) && module->aliases[slot] != WY_NULL) {
        return module->aliases[slot];
    }
    return &module->globals[slot];
}

/* Two fills of one slot agree when they are the same binding, or when both
 * hold the same module: a module binding names the module itself, so two
 * wildcards that each pass on `std` are not ambiguous. */
static bool same_binding_(const wy_value* a, wy_value a_value, const wy_value* b, wy_value b_value)
{
    if (a != WY_NULL && a == b) { return true; }
    if (a_value.type == WY_TYPE_TAG_MODULE && b_value.type == WY_TYPE_TAG_MODULE) {
        return a_value.data.gc_object == b_value.data.gc_object;
    }
    /* Plain values (builtins, a module reached as a whole path) keep the
     * old rule: the same value from two sources is one thing. */
    return a == WY_NULL && b == WY_NULL && identical_(a_value, b_value);
}

static wy_error fill_(wy_context* ctx, wy_module* module, wy_uword slot,
    wy_value value, wy_value* binding, wy_u8 layer, wy_symbol source)
{
    if (ctx == WY_NULL || module == WY_NULL || slot >= module->global_count ||
        layer < 1 || layer > 3) { return WY_ERR_INVAL; }
    if (binding != WY_NULL && module->aliases == WY_NULL) {
        /* No aliases table (a module that never imports): copy. */
        value = *binding;
        binding = WY_NULL;
    }
    wy_u8 current = module->fill_layer[slot] & WY_LINK_LAYER_MASK;
    if (current == 0 || layer < current) {
        if (binding != WY_NULL) {
            module->aliases[slot] = binding;
            module->globals[slot] = wy_value_unset();
            module->fill_layer[slot] = (wy_u8) (layer | WY_LINK_ALIAS);
        } else {
            if (module->aliases != WY_NULL) { module->aliases[slot] = WY_NULL; }
            module->globals[slot] = value;
            module->fill_layer[slot] = layer;
        }
        module->fill_source[slot] = source;
        return WY_ERR_NONE;
    }
    if (layer > current || module->fill_source[slot] == source) { return WY_ERR_NONE; }
    if (module->fill_layer[slot] & WY_LINK_AMBIGUOUS) { return WY_ERR_NONE; }
    wy_value* held = (module->fill_layer[slot] & WY_LINK_ALIAS) ? module->aliases[slot] : WY_NULL;
    wy_value held_value = held != WY_NULL ? *held : module->globals[slot];
    wy_value new_value = binding != WY_NULL ? *binding : value;
    if (same_binding_(held, held_value, binding, new_value)) { return WY_ERR_NONE; }

    wy_symbol name = "<global>";
    for (wy_uword i = 0; i < module->free_names.capacity; i++) {
        wy_slot_dict_entry* entry = &module->free_names.entry_table[i];
        if (entry->symbol != WY_SYMBOL_INVALID && entry->slot == slot) { name = entry->symbol; break; }
    }
    char text[512];
    snprintf(text, sizeof(text), "ambiguous name '%s': supplied by both '%s' and '%s'",
        name, module->fill_source[slot] ? module->fill_source[slot] : "<unknown>",
        source ? source : "<unknown>");
    wy_string* what = WY_NULL;
    wy_error err = wy_string_strdup(ctx, text, &what);
    if (err != WY_ERR_NONE) { return err; }
    wy_error_obj* marker = WY_NULL;
    err = wy_error_obj_new(ctx, WY_NULL, what, wy_value_nil(), &marker);
    if (err != WY_ERR_NONE) { return err; }
    marker->code = WY_ERR_AMBIGUOUS;
    module->globals[slot] = wy_value_object(WY_TYPE_TAG_ERROR, (wy_object*) marker);
    if (module->aliases != WY_NULL) { module->aliases[slot] = WY_NULL; }
    module->fill_layer[slot] = (wy_u8) ((module->fill_layer[slot] & WY_LINK_LAYER_MASK) | WY_LINK_AMBIGUOUS);
    return WY_ERR_NONE;
}

wy_error wy_link_fill(wy_context* ctx, wy_module* module, wy_uword slot,
    wy_value value, wy_u8 layer, wy_symbol source)
{
    return fill_(ctx, module, slot, value, WY_NULL, layer, source);
}

wy_error wy_link_fill_binding(wy_context* ctx, wy_module* module, wy_uword slot,
    wy_value* binding, wy_u8 layer, wy_symbol source)
{
    if (binding == WY_NULL) { return WY_ERR_INVAL; }
    return fill_(ctx, module, slot, wy_value_unset(), binding, layer, source);
}

wy_error wy_link_scope_member(wy_value owner, wy_symbol name, wy_value** out)
{
    if (name == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }
    wy_module* module;
    wy_slot_dict* slots;
    switch (owner.type) {
    case WY_TYPE_TAG_MODULE:
        module = (wy_module*) owner.data.gc_object;
        slots = &module->exports;
        break;
    case WY_TYPE_TAG_FUNCTION:
        module = ((wy_function*) owner.data.gc_object)->module;
        slots = &module->exports;
        break;
    case WY_TYPE_TAG_CLASS: {
        wy_class* cls = (wy_class*) owner.data.gc_object;
        module = cls->module;
        slots = &cls->statics;
        break;
    }
    default: return WY_ERR_BAD_TYPE;
    }
    if (module == WY_NULL) { return WY_ERR_UNBOUND; }
    wy_uword slot = wy_slot_dict_get(slots, name);
    if (slot == WY_SLOT_INVALID || slot >= module->global_count) { return WY_ERR_UNBOUND; }
    /* An exported name that is itself imported stands for the original
     * binding (a re-export, design/modules.md M2). */
    *out = wy_link_binding(module, slot);
    return WY_ERR_NONE;
}

wy_error wy_link_fill_from_builtins(wy_context* ctx, wy_module* module, wy_module* builtins)
{
    if (ctx == WY_NULL || module == WY_NULL) { return WY_ERR_INVAL; }
    if (builtins == WY_NULL) { return WY_ERR_NONE; }
    for (wy_uword i = 0; i < module->free_names.capacity; i++) {
        const wy_slot_dict_entry* entry = &module->free_names.entry_table[i];
        if (entry->symbol == WY_SYMBOL_INVALID || strstr(entry->symbol, "::") != WY_NULL) { continue; }
        wy_value* value;
        if (wy_link_scope_member(wy_value_object(WY_TYPE_TAG_MODULE, (wy_object*) builtins), entry->symbol, &value) != WY_ERR_NONE) { continue; }
        wy_error err = wy_link_fill(ctx, module, entry->slot, *value, 3, builtins->name);
        if (err != WY_ERR_NONE) { return err; }
    }
    return WY_ERR_NONE;
}

wy_error wy_link_fill_from_import(wy_context* ctx, wy_module* module, wy_symbol path, wy_module* dep)
{
    if (ctx == WY_NULL || module == WY_NULL || path == WY_NULL || dep == WY_NULL) { return WY_ERR_INVAL; }
    wy_session_note_import_(ctx, module, path, dep);  /* no-op unless `module` is a REPL session */
    wy_uword len = strlen(path);
    for (wy_uword i = 0; i < module->free_names.capacity; i++) {
        const wy_slot_dict_entry* entry = &module->free_names.entry_table[i];
        const char* name = entry->symbol;
        if (name == WY_SYMBOL_INVALID || strncmp(name, path, len) != 0) { continue; }
        wy_value value = wy_value_object(WY_TYPE_TAG_MODULE, (wy_object*) dep);
        wy_value* found = WY_NULL;  /* the member's binding; NULL for the module itself */
        if (name[len] != '\0') {
            if (name[len] != ':' || name[len + 1] != ':') { continue; }
            const char* step = name + len + 2;
            bool missing = false;
            for (;;) {
                const char* end = strstr(step, "::");
                wy_symbol symbol;
                wy_error err = wy_context_intern(ctx, step, end ? (wy_uword)(end - step) : strlen(step), &symbol);
                if (err != WY_ERR_NONE) { return err; }
                wy_value* binding;
                if (wy_link_scope_member(value, symbol, &binding) != WY_ERR_NONE) { missing = true; break; }
                found = binding;
                value = *binding;
                if (end == WY_NULL) { break; }
                step = end + 2;
            }
            if (missing) { continue; }
        }
        /* A member is its own binding, shared, not a copy (design/modules.md
         * M2); only the module itself is a plain value. */
        wy_error err = found != WY_NULL ? wy_link_fill_binding(ctx, module, entry->slot, found, 1, path) :
            wy_link_fill(ctx, module, entry->slot, value, 1, path);
        if (err != WY_ERR_NONE) { return err; }
    }
    return WY_ERR_NONE;
}

wy_error wy_link_fill_from_wildcard(wy_context* ctx, wy_module* module, const wy_wildcard* wildcard)
{
    if (ctx == WY_NULL || module == WY_NULL || wildcard == WY_NULL) { return WY_ERR_INVAL; }
    for (wy_uword i = 0; i < module->free_names.capacity; i++) {
        const wy_slot_dict_entry* entry = &module->free_names.entry_table[i];
        if (entry->symbol == WY_SYMBOL_INVALID || strstr(entry->symbol, "::") != WY_NULL) { continue; }
        bool excluded = false;
        for (wy_uword j = 0; j < wildcard->except_count; j++) {
            if (entry->symbol == wildcard->excepts[j]) { excluded = true; break; }
        }
        if (excluded) { continue; }
        wy_value* value;
        if (wy_link_scope_member(wy_value_object(WY_TYPE_TAG_MODULE, (wy_object*) wildcard->target), entry->symbol, &value) != WY_ERR_NONE) { continue; }
        wy_error err = wy_link_fill_binding(ctx, module, entry->slot, value, 2, wildcard->target->name);
        if (err != WY_ERR_NONE) { return err; }
    }
    return WY_ERR_NONE;
}

wy_error wy_link_register_wildcard(wy_context* ctx, wy_module* module,
    wy_module* dep, const wy_value* excepts, wy_uword count, wy_uword* index)
{
    if (ctx == WY_NULL || module == WY_NULL || dep == WY_NULL || index == WY_NULL ||
        (count && excepts == WY_NULL)) { return WY_ERR_INVAL; }
    if (count > WY_MAX_ARRAY_LEN / sizeof(wy_symbol) ||
        module->wildcard_count >= WY_MAX_ARRAY_LEN / sizeof(wy_wildcard)) { return WY_ERR_RANGE; }
    for (wy_uword i = 0; i < count; i++) {
        if (excepts[i].type != WY_TYPE_TAG_SYMBOL) { return WY_ERR_BAD_TYPE; }
    }
    wy_symbol* names = count ? wy_context_gc_alloc(ctx, count * sizeof(wy_symbol)) : WY_NULL;
    if (count && names == WY_NULL) { return WY_ERR_NOMEM; }
    for (wy_uword i = 0; i < count; i++) { names[i] = excepts[i].data.symtab_entry; }
    wy_wildcard* entries = wy_context_gc_realloc(ctx, module->wildcards,
        (module->wildcard_count + 1) * sizeof(wy_wildcard));
    if (entries == WY_NULL) { wy_context_gc_free(ctx, names); return WY_ERR_NOMEM; }
    module->wildcards = entries;
    *index = module->wildcard_count++;
    entries[*index] = (wy_wildcard) { dep, count, names };
    return WY_ERR_NONE;
}

wy_error wy_link_adopt_messages(wy_context* ctx, wy_module* module, wy_module* dep)
{
    if (ctx == WY_NULL || module == WY_NULL || dep == WY_NULL) { return WY_ERR_INVAL; }
    if (dep == module || dep->message_table == WY_NULL) { return WY_ERR_NONE; }

    const wy_dict* source = dep->message_table;
    for (wy_uword i = 0; i < source->count; i++) {
        const wy_key_hash_value* entry = &source->dense[i];
        if (entry->key.type != WY_TYPE_TAG_SYMBOL || entry->value.type != WY_TYPE_TAG_MESSAGE) { continue; }
        wy_symbol name = entry->key.data.symtab_entry;

        /* The importer's own definition (or an earlier adoption) wins;
         * adopting the same canonical message twice is a no-op. This is
         * _merge_message's first three rules. Its _AmbiguousMessage marker
         * for two genuinely different same-named messages has no C
         * counterpart: first-claimed wins here, which only diverges from
         * the walker on programs the walker itself faults at the point of
         * use - the qualified `mod::name` send is the way out in both. */
        wy_value* existing = module->message_table == WY_NULL ? WY_NULL :
            wy_dict_get(ctx, module->message_table, WY_TYPE_TAG_SYMBOL,
                (wy_primitive) { .symtab_entry = name });
        if (existing != WY_NULL) { continue; }

        if (module->message_table == WY_NULL) {
            wy_error err = wy_dict_new(ctx, &module->message_table);
            if (err != WY_ERR_NONE) { return err; }
        }
        wy_error err = wy_dict_set(ctx, module->message_table, WY_TYPE_TAG_SYMBOL,
            (wy_primitive) { .symtab_entry = name }, WY_TYPE_TAG_MESSAGE, entry->value.data);
        if (err != WY_ERR_NONE) { return err; }
    }
    return WY_ERR_NONE;
}

wy_error wy_link_seed_global(wy_context* ctx, wy_module* module, const char* name, wy_value value)
{
    if (ctx == WY_NULL || module == WY_NULL || name == WY_NULL) { return WY_ERR_INVAL; }
    wy_symbol sym = WY_NULL;
    wy_error err = wy_context_intern(ctx, name, strlen(name), &sym);
    if (err != WY_ERR_NONE) { return err; }
    wy_uword slot = wy_slot_dict_get(&module->free_names, sym);
    if (slot == WY_SLOT_INVALID) { return WY_ERR_NONE; }
    module->globals[slot] = value;
    if (module->fill_layer != WY_NULL) { module->fill_layer[slot] &= WY_LINK_LAYER_MASK; }
    return WY_ERR_NONE;
}

wy_error wy_link_import(wy_context* ctx, wy_string* path, wy_module** out)
{
    if (path == WY_NULL) { return WY_ERR_INVAL; }
    return wy_link_import_ex(ctx, path, path->len, false, out);
}

/** True when a registered module lives under `path` (`path::...`): a host
 * that registers `std::expand` itself makes `std` a namespace package. */
static bool has_registered_children_(wy_context* ctx, const char* path, wy_uword len)
{
    for (wy_uword i = 0; i < ctx->module_count; i++) {
        wy_module* module = wy_context_get_module(ctx, i);
        if (module == WY_NULL || module->import_path == WY_NULL) { continue; }
        const wy_string* other = module->import_path;
        if (other->len > len + 2 && memcmp(other->str, path, len) == 0 &&
            other->str[len] == ':' && other->str[len + 1] == ':') {
            return true;
        }
    }
    return false;
}

/** A module that is a namespace package: no code, nothing to initialise. */
static wy_error namespace_module_(wy_context* ctx, wy_module** out)
{
    wy_module* dep = wy_module_new_f(ctx);
    if (dep == WY_NULL) { return WY_ERR_NOMEM; }
    dep->state = WY_MODULE_BUILTIN;
    *out = dep;
    return WY_ERR_NONE;
}

wy_error wy_link_import_ex(wy_context* ctx, wy_string* path, wy_uword len, bool prefix, wy_module** out)
{
    if (ctx == WY_NULL || path == WY_NULL || out == WY_NULL || len == 0 || len > path->len) { return WY_ERR_INVAL; }
    /* Reject empty components and embedded NULs before handing the path to a host. */
    wy_uword start = 0;
    for (wy_uword i = 0; i < len; i++) {
        if (path->str[i] == '\0') { return WY_ERR_INVAL; }
        if (path->str[i] == ':') {
            if (i == start || i + 1 >= len || path->str[i + 1] != ':') { return WY_ERR_INVAL; }
            i++;
            start = i + 1;
        }
    }
    if (start == len) { return WY_ERR_INVAL; }
    for (wy_uword i = 0; i < ctx->module_count; i++) {
        wy_module* module = wy_context_get_module(ctx, i);
        if (module == WY_NULL) { continue; }  /* unregistered */
        bool match = module->import_path ?
            (module->import_path->len == len && memcmp(module->import_path->str, path->str, len) == 0) :
            (module->name && strlen(module->name) == len && memcmp(module->name, path->str, len) == 0);
        if (!match) { continue; }
        if (module->state == WY_MODULE_INITIALISING) {
            /* design/modules.md M1: a package still running its own init is
             * passed through on the way to one of its children; only the
             * whole path of an import can close a cycle. */
            if (!prefix) { return WY_ERR_CYCLE; }
            *out = module;
            return WY_ERR_NONE;
        }
        if (module->state == WY_MODULE_FAILED) { return WY_ERR_LINK; }
        *out = module;
        return WY_ERR_NONE;
    }
    wy_u8* bytes = WY_NULL;
    wy_uword bytes_len = 0;
    const wy_module_image* image = WY_NULL;
    wy_error err = ctx->import_hook == WY_NULL ? WY_ERR_UNBOUND :
        ctx->import_hook(ctx, path->str, len, &bytes, &bytes_len, &image, ctx->import_ud);
    if (err == WY_ERR_UNBOUND && has_registered_children_(ctx, path->str, len)) { err = WY_ERR_NONE; }
    if (err != WY_ERR_NONE) { return err; }
    wy_module* dep = WY_NULL;
    if (image != WY_NULL) {
        /* Static image (builtin module table): zero-copy, nothing to free. */
        err = wy_module_load_image(ctx, image, &dep);
        if (err != WY_ERR_NONE) { return err; }
    } else if (bytes != WY_NULL) {
        err = wy_module_load_bytes(ctx, bytes, bytes_len, true, &dep);
        if (err != WY_ERR_NONE) { wy_context_gc_free(ctx, bytes); return err; }
    } else {
        err = namespace_module_(ctx, &dep);
        if (err != WY_ERR_NONE) { return err; }
    }
    if (len != path->len) {
        /* A prefix spelling: the module's own path is the leading part. */
        err = wy_string_new(ctx, path->str, len, &path);
        if (err != WY_ERR_NONE) { return err; }
    }
    dep->import_path = path;
    err = wy_context_intern(ctx, path->str, path->len, &dep->name);
    if (err != WY_ERR_NONE) { return err; }
    err = wy_context_module_register(ctx, dep, WY_NULL);
    if (err != WY_ERR_NONE) { return err; }
    /* Python semantics: an imported module's `__name__` is its import path
     * (the entry module's is "__main__", seeded by the host). */
    err = wy_link_seed_global(ctx, dep, "__name__", wy_value_object(WY_TYPE_TAG_STR, (wy_object*) path));
    if (err != WY_ERR_NONE) { return err; }
    *out = dep;
    return WY_ERR_NONE;
}
