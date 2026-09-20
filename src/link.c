#include <wyrm.h>
#include <wyrm/link.h>
#include <wyrm/error.h>
#include <wyrm/function.h>
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

wy_error wy_link_fill(wy_context* ctx, wy_module* module, wy_uword slot,
    wy_value value, wy_u8 layer, wy_symbol source)
{
    if (ctx == WY_NULL || module == WY_NULL || slot >= module->global_count ||
        layer < 1 || layer > 3) { return WY_ERR_INVAL; }
    wy_u8 current = module->fill_layer[slot] & WY_LINK_LAYER_MASK;
    if (current == 0 || layer < current) {
        module->globals[slot] = value;
        module->fill_layer[slot] = layer;
        module->fill_source[slot] = source;
        return WY_ERR_NONE;
    }
    if (layer > current || module->fill_source[slot] == source ||
        identical_(module->globals[slot], value)) { return WY_ERR_NONE; }
    if (module->fill_layer[slot] & WY_LINK_AMBIGUOUS) { return WY_ERR_NONE; }

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
    module->fill_layer[slot] |= WY_LINK_AMBIGUOUS;
    return WY_ERR_NONE;
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
    *out = &module->globals[slot];
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
    wy_uword len = strlen(path);
    for (wy_uword i = 0; i < module->free_names.capacity; i++) {
        const wy_slot_dict_entry* entry = &module->free_names.entry_table[i];
        const char* name = entry->symbol;
        if (name == WY_SYMBOL_INVALID || strncmp(name, path, len) != 0) { continue; }
        wy_value value = wy_value_object(WY_TYPE_TAG_MODULE, (wy_object*) dep);
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
                value = *binding;
                if (end == WY_NULL) { break; }
                step = end + 2;
            }
            if (missing) { continue; }
        }
        wy_error err = wy_link_fill(ctx, module, entry->slot, value, 1, path);
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
        wy_error err = wy_link_fill(ctx, module, entry->slot, *value, 2, wildcard->target->name);
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

wy_error wy_link_import(wy_context* ctx, wy_string* path, wy_module** out)
{
    if (ctx == WY_NULL || path == WY_NULL || out == WY_NULL || path->len == 0) { return WY_ERR_INVAL; }
    /* Reject empty components and embedded NULs before handing the path to a host. */
    wy_uword start = 0;
    for (wy_uword i = 0; i < path->len; i++) {
        if (path->str[i] == '\0') { return WY_ERR_INVAL; }
        if (path->str[i] == ':') {
            if (i == start || i + 1 >= path->len || path->str[i + 1] != ':') { return WY_ERR_INVAL; }
            i++;
            start = i + 1;
        }
    }
    if (start == path->len) { return WY_ERR_INVAL; }
    for (wy_uword i = 0; i < ctx->module_count; i++) {
        wy_module* module = wy_context_get_module(ctx, i);
        bool match = module->import_path ? wy_string_eq_f(module->import_path, path) :
            (module->name && strlen(module->name) == path->len && memcmp(module->name, path->str, path->len) == 0);
        if (!match) { continue; }
        if (module->state == WY_MODULE_INITIALISING) { return WY_ERR_CYCLE; }
        if (module->state == WY_MODULE_FAILED) { return WY_ERR_LINK; }
        *out = module;
        return WY_ERR_NONE;
    }
    if (ctx->import_hook == WY_NULL) { return WY_ERR_UNBOUND; }
    wy_u8* bytes = WY_NULL;
    wy_uword len = 0;
    wy_error err = ctx->import_hook(ctx, path->str, path->len, &bytes, &len, ctx->import_ud);
    if (err != WY_ERR_NONE) { return err; }
    wy_module* dep = WY_NULL;
    err = wy_module_load_bytes(ctx, bytes, len, true, &dep);
    if (err != WY_ERR_NONE) { wy_context_gc_free(ctx, bytes); return err; }
    dep->import_path = path;
    err = wy_context_intern(ctx, path->str, path->len, &dep->name);
    if (err != WY_ERR_NONE) { return err; }
    err = wy_context_module_register(ctx, dep, WY_NULL);
    if (err != WY_ERR_NONE) { return err; }
    *out = dep;
    return WY_ERR_NONE;
}
