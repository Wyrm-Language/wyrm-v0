#include <wyrm/link.h>
#include <wyrm/module.h>
#include <wyrm/session.h>

#include <wyrm/allocator.h>
#include <wyrm/cbor.h>
#include <wyrm/context.h>
#include <wyrm/function.h>
#include <wyrm/gc_flags.h>
#include <wyrm/image_loader.h>
#include <wyrm/machine.h>
#include <wyrm/string.h>
#include <wyrm/sys/string.h>
#include <wyrm/vm.h>
#include <wyrm/work_area.h>

/* -------------------------------------------------------------------------
 * Small helpers shared by every section loader below.
 * ------------------------------------------------------------------------- */

static bool host_is_little_endian_(void)
{
    const wy_u32 probe = 1;
    return *(const wy_u8*) &probe == 1;
}

/** The value under text key `key` of the map `doc`; WY_ERR_IMAGE if there is none. */
static wy_error field_(wy_cbor_span doc, const char* key, wy_cbor_span* out)
{
    wy_error last_error = wy_cbor_map_find(doc, key, out);
    return last_error == WY_ERR_NONE ? WY_ERR_NONE : WY_ERR_IMAGE;
}

/** As field_ for an optional key: `*found` says whether it was there. */
static wy_error opt_field_(wy_cbor_span doc, const char* key, wy_cbor_span* out, bool* found)
{
    wy_error last_error = wy_cbor_map_find(doc, key, out);
    *found = last_error == WY_ERR_NONE;
    if (last_error == WY_ERR_KEY) { return WY_ERR_NONE; }
    return last_error == WY_ERR_NONE ? WY_ERR_NONE : WY_ERR_IMAGE;
}

/** A required unsigned field no larger than `max`. */
static wy_error field_uint_(wy_cbor_span doc, const char* key, wy_u32 max, wy_u32* out)
{
    wy_cbor_span value;
    wy_error last_error = field_(doc, key, &value);
    if (last_error != WY_ERR_NONE) { return last_error; }
    wy_u32 number;
    last_error = wy_cbor_get_u32(value, &number);
    if (last_error != WY_ERR_NONE || number > max) { return WY_ERR_IMAGE; }
    *out = number;
    return WY_ERR_NONE;
}

/** As field_uint_ for an optional field: `*found` says whether it was there. */
static wy_error opt_field_uint_(wy_cbor_span doc, const char* key, wy_u32 max, wy_u32* out, bool* found)
{
    wy_cbor_span value;
    wy_error last_error = opt_field_(doc, key, &value, found);
    if (last_error != WY_ERR_NONE || !*found) { return last_error; }
    wy_u32 number;
    last_error = wy_cbor_get_u32(value, &number);
    if (last_error != WY_ERR_NONE || number > max) { return WY_ERR_IMAGE; }
    *out = number;
    return WY_ERR_NONE;
}

/** A required array field; `reader` is left at its first element. */
static wy_error field_array_(wy_cbor_span doc, const char* key, wy_cbor_reader* reader, wy_uword* out_count)
{
    wy_cbor_span value;
    wy_error last_error = field_(doc, key, &value);
    if (last_error != WY_ERR_NONE) { return last_error; }
    return wy_cbor_open_array(value, reader, out_count);
}

static wy_error intern_text_(wy_context* context, wy_cbor_span span, wy_symbol* out)
{
    const char* text;
    wy_uword len;
    if (wy_cbor_get_text(span, &text, &len) != WY_ERR_NONE) { return WY_ERR_IMAGE; }
    return wy_context_intern(context, text, len, out);
}

/** A required text field, interned. */
static wy_error field_symbol_(wy_context* context, wy_cbor_span doc, const char* key, wy_symbol* out)
{
    wy_cbor_span value;
    wy_error last_error = field_(doc, key, &value);
    if (last_error != WY_ERR_NONE) { return last_error; }
    return intern_text_(context, value, out);
}

/** Open an array section; a section that isn't there reads as an empty array. */
static wy_error open_section_array_(wy_section_ref section, wy_cbor_reader* reader, wy_uword* out_count)
{
    if (section.data == WY_NULL) {
        *out_count = 0;
        return WY_ERR_NONE;
    }
    wy_cbor_span span = { section.data, section.len };
    return wy_cbor_open_array(span, reader, out_count);
}

/**
 * A `statics[]`/`slot_defaults` value (doc-llm/llm-bytecode.md §4.3): string,
 * integer, float, bool, nil or byte string. Symbols, chars and containers
 * never appear as a static's own value.
 */
static wy_error decode_static_value_(wy_context* context, wy_cbor_span span, wy_value* out)
{
    wy_cbor_item item;
    wy_error last_error = wy_cbor_span_item(span, &item);
    if (last_error != WY_ERR_NONE) { return WY_ERR_IMAGE; }

    switch (item.kind) {
    case WY_CBOR_TEXT:
    case WY_CBOR_BYTES: {
        wy_string* str;
        last_error = wy_string_new(context, (const char*) item.data, (wy_uword) item.n, &str);
        if (last_error != WY_ERR_NONE) { return last_error; }
        // A byte string is a placeholder until wy_bytes exists (epic 7): a
        // wy_string flagged as raw bytes rather than text (see WY_GC_FLAG_BINARY).
        if (item.kind == WY_CBOR_BYTES) { str->object.flags |= WY_GC_FLAG_BINARY; }
        *out = (wy_value) { .type = WY_TYPE_TAG_STR, .data = { .str = str } };
        return WY_ERR_NONE;
    }
    case WY_CBOR_UINT:
    case WY_CBOR_NEGINT: {
        wy_i64 value;
        if (wy_cbor_get_i64(span, &value) != WY_ERR_NONE) { return WY_ERR_IMAGE; }
        if (value < (wy_i64) (-WY_WORD_MAX - 1) || value > (wy_i64) WY_WORD_MAX) { return WY_ERR_IMAGE; }
        *out = wy_value_word((wy_word) value);
        return WY_ERR_NONE;
    }
    case WY_CBOR_FLOAT32:
    case WY_CBOR_FLOAT64:
        *out = wy_value_float((wy_float) item.f);
        return WY_ERR_NONE;
    case WY_CBOR_TRUE:
        *out = wy_value_bool(true);
        return WY_ERR_NONE;
    case WY_CBOR_FALSE:
        *out = wy_value_bool(false);
        return WY_ERR_NONE;
    case WY_CBOR_NIL:
        *out = wy_value_nil();
        return WY_ERR_NONE;
    default:
        return WY_ERR_IMAGE;
    }
}

/* -------------------------------------------------------------------------
 * header (id 1)
 * ------------------------------------------------------------------------- */


static wy_error module_load_section_header_(wy_context* context, wy_module* module, wy_section_ref section)
{
    if (section.data == WY_NULL) { return WY_ERR_IMAGE; }  // required (wy_image_from_bytes already enforces this)

    wy_cbor_span doc = { section.data, section.len };
    wy_error last_error = field_symbol_(context, doc, "n", &module->name);
    if (last_error != WY_ERR_NONE) { return last_error; }

    // The format version lives in the file's preamble, not the header.
    wy_u32 nglobals;
    last_error = field_uint_(doc, "g", 0x7FFFFFFFu, &nglobals);
    if (last_error != WY_ERR_NONE) { return last_error; }

    wy_u32 init_nlocals;
    last_error = field_uint_(doc, "l", 0xFFFFu, &init_nlocals);
    if (last_error != WY_ERR_NONE) { return last_error; }
    module->init_nlocals = (wy_u16) init_nlocals;

    // `u` (referenced-name set) is a writer artifact from before binding was
    // per-name; wyc-format.md §8.1 says a loader MUST ignore it.

    module->global_count = (wy_uword) nglobals;
    if (module->global_count == 0) { return WY_ERR_NONE; }

    module->globals = wy_context_gc_alloc(context, sizeof(wy_value) * module->global_count);
    module->fill_layer = wy_context_gc_alloc(context, sizeof(wy_u8) * module->global_count);
    module->fill_source = wy_context_gc_alloc(context, sizeof(wy_symbol) * module->global_count);
    module->aliases = wy_context_gc_alloc(context, sizeof(wy_value*) * module->global_count);
    if (module->globals == WY_NULL || module->fill_layer == WY_NULL || module->fill_source == WY_NULL ||
        module->aliases == WY_NULL) {
        return WY_ERR_NOMEM;
    }
    for (wy_uword i = 0; i < module->global_count; i++) {
        module->globals[i] = wy_value_unset();
        module->fill_layer[i] = 0;
        module->fill_source[i] = WY_NULL;
        module->aliases[i] = WY_NULL;
    }
    return WY_ERR_NONE;
}

/* -------------------------------------------------------------------------
 * slot_defaults (id 3)
 * ------------------------------------------------------------------------- */

static wy_error module_load_section_slot_defaults_(wy_context* context, wy_module* module, wy_section_ref section)
{
    if (section.data == WY_NULL) { return WY_ERR_NONE; }

    wy_cbor_span span = { section.data, section.len };
    wy_cbor_reader reader;
    wy_uword count;
    wy_error last_error = wy_cbor_open_map(span, &reader, &count);
    if (last_error != WY_ERR_NONE) { return last_error; }

    for (wy_uword i = 0; i < count; i++) {
        // Keys are module-global slot indices, unsigned integers.
        wy_cbor_span key;
        last_error = wy_cbor_next_span(&reader, &key);
        if (last_error != WY_ERR_NONE) { return last_error; }
        wy_u32 slot;
        if (wy_cbor_get_u32(key, &slot) != WY_ERR_NONE) { return WY_ERR_IMAGE; }
        if ((wy_uword) slot >= module->global_count) { return WY_ERR_IMAGE; }

        wy_cbor_span item;
        last_error = wy_cbor_next_span(&reader, &item);
        if (last_error != WY_ERR_NONE) { return last_error; }
        wy_value value;
        last_error = decode_static_value_(context, item, &value);
        if (last_error != WY_ERR_NONE) { return last_error; }
        module->globals[slot] = value;
    }
    return WY_ERR_NONE;
}

/* -------------------------------------------------------------------------
 * symbols (id 4)
 * ------------------------------------------------------------------------- */

static wy_error module_load_section_symbols_(wy_context* context, wy_module* module, wy_section_ref section)
{
    wy_cbor_reader reader;
    wy_error last_error = open_section_array_(section, &reader, &module->symbol_count);
    if (last_error != WY_ERR_NONE) { return last_error; }
    if (module->symbol_count == 0) { return WY_ERR_NONE; }

    module->symbols = wy_context_gc_alloc(context, sizeof(wy_symbol) * module->symbol_count);
    if (module->symbols == WY_NULL) { return WY_ERR_NOMEM; }

    for (wy_uword i = 0; i < module->symbol_count; i++) {
        wy_cbor_span item;
        last_error = wy_cbor_next_span(&reader, &item);
        if (last_error != WY_ERR_NONE) { return last_error; }
        last_error = intern_text_(context, item, &module->symbols[i]);
        if (last_error != WY_ERR_NONE) { return last_error; }
    }
    return WY_ERR_NONE;
}

/* -------------------------------------------------------------------------
 * statics (id 2)
 * ------------------------------------------------------------------------- */

static wy_error module_load_section_statics_(wy_context* context, wy_module* module, wy_section_ref section)
{
    wy_cbor_reader reader;
    wy_error last_error = open_section_array_(section, &reader, &module->static_count);
    if (last_error != WY_ERR_NONE) { return last_error; }
    if (module->static_count == 0) { return WY_ERR_NONE; }

    module->statics = wy_context_gc_alloc(context, sizeof(wy_value) * module->static_count);
    if (module->statics == WY_NULL) { return WY_ERR_NOMEM; }

    for (wy_uword i = 0; i < module->static_count; i++) {
        wy_cbor_span item;
        last_error = wy_cbor_next_span(&reader, &item);
        if (last_error != WY_ERR_NONE) { return last_error; }
        last_error = decode_static_value_(context, item, &module->statics[i]);
        if (last_error != WY_ERR_NONE) { return last_error; }
    }
    return WY_ERR_NONE;
}

/* -------------------------------------------------------------------------
 * functions (id 5)
 * ------------------------------------------------------------------------- */

static wy_error decode_param_(wy_context* context, wy_module* module, wy_cbor_span doc, wy_param* out)
{
    wy_error last_error = field_symbol_(context, doc, "n", &out->name);
    if (last_error != WY_ERR_NONE) { return last_error; }

    out->default_static = -1;
    wy_u32 idx;
    bool found;
    last_error = opt_field_uint_(doc, "d", 0x7FFFFFFFu, &idx, &found);
    if (last_error != WY_ERR_NONE) { return last_error; }
    if (found) {
        if ((wy_uword) idx >= module->static_count) { return WY_ERR_IMAGE; }
        out->default_static = (wy_i32) idx;
    }
    return WY_ERR_NONE;
}

static wy_error decode_function_(wy_context* context, wy_module* module, wy_cbor_span doc, wy_function_proto* out)
{
    wy_error last_error = field_symbol_(context, doc, "n", &out->name);
    if (last_error != WY_ERR_NONE) { return last_error; }

    wy_u32 number;
    last_error = field_uint_(doc, "l", 0xFFFFu, &number);
    if (last_error != WY_ERR_NONE) { return last_error; }
    out->nlocals = (wy_u16) number;

    last_error = field_uint_(doc, "c", 0x7FFFFFFFu, &number);
    if (last_error != WY_ERR_NONE) { return last_error; }
    if ((wy_uword) number >= module->code_len) { return WY_ERR_IMAGE; }
    out->code_offset = number;

    last_error = field_uint_(doc, "f", 0xFFu, &number);
    if (last_error != WY_ERR_NONE) { return last_error; }
    out->flags = (wy_u8) number;

    bool found;
    out->ncaptures = 0;
    last_error = opt_field_uint_(doc, "k", 0xFFFFu, &number, &found);
    if (last_error != WY_ERR_NONE) { return last_error; }
    if (found) { out->ncaptures = (wy_u16) number; }

    wy_cbor_reader reader;
    wy_uword nparams;
    last_error = field_array_(doc, "p", &reader, &nparams);
    if (last_error != WY_ERR_NONE) { return last_error; }
    if (nparams > 0xFFFF) { return WY_ERR_IMAGE; }
    out->nparams = (wy_u16) nparams;
    out->params = WY_NULL;
    if (nparams > 0) {
        out->params = wy_context_gc_alloc(context, sizeof(wy_param) * nparams);
        if (out->params == WY_NULL) { return WY_ERR_NOMEM; }
        for (wy_uword i = 0; i < nparams; i++) {
            wy_cbor_span item;
            last_error = wy_cbor_next_span(&reader, &item);
            if (last_error != WY_ERR_NONE) { return last_error; }
            last_error = decode_param_(context, module, item, &out->params[i]);
            if (last_error != WY_ERR_NONE) { return last_error; }
        }
    }

    out->ndispatch = 0;
    out->dispatch_slots = WY_NULL;
    wy_cbor_span dispatch;
    last_error = opt_field_(doc, "t", &dispatch, &found);
    if (last_error != WY_ERR_NONE) { return last_error; }
    if (found) {
        wy_cbor_reader treader;
        wy_uword ndispatch;
        last_error = wy_cbor_open_array(dispatch, &treader, &ndispatch);
        if (last_error != WY_ERR_NONE) { return last_error; }
        if (ndispatch > 0xFFFF) { return WY_ERR_IMAGE; }
        out->ndispatch = (wy_u16) ndispatch;
        if (ndispatch > 0) {
            out->dispatch_slots = wy_context_gc_alloc(context, sizeof(wy_u16) * ndispatch);
            if (out->dispatch_slots == WY_NULL) { return WY_ERR_NOMEM; }
            for (wy_uword i = 0; i < ndispatch; i++) {
                wy_cbor_span item;
                last_error = wy_cbor_next_span(&treader, &item);
                if (last_error != WY_ERR_NONE) { return last_error; }
                wy_u32 slot;
                if (wy_cbor_get_u32(item, &slot) != WY_ERR_NONE) { return WY_ERR_IMAGE; }
                if ((wy_uword) slot >= module->global_count) { return WY_ERR_IMAGE; }
                out->dispatch_slots[i] = (wy_u16) slot;
            }
        }
    }

    return WY_ERR_NONE;
}

static wy_error module_load_section_functions_(wy_context* context, wy_module* module, wy_section_ref section)
{
    wy_cbor_reader reader;
    wy_error last_error = open_section_array_(section, &reader, &module->function_count);
    if (last_error != WY_ERR_NONE) { return last_error; }
    if (module->function_count == 0) { return WY_ERR_NONE; }

    module->functions = wy_context_gc_alloc(context, sizeof(wy_function_proto) * module->function_count);
    if (module->functions == WY_NULL) { return WY_ERR_NOMEM; }
    // Zeroed, so that finalizing a module whose load failed part way frees only
    // what the entries decoded so far allocated.
    wy_memset(module->functions, 0, sizeof(wy_function_proto) * module->function_count);

    for (wy_uword i = 0; i < module->function_count; i++) {
        wy_cbor_span item;
        last_error = wy_cbor_next_span(&reader, &item);
        if (last_error != WY_ERR_NONE) { return last_error; }
        last_error = decode_function_(context, module, item, &module->functions[i]);
        if (last_error != WY_ERR_NONE) { return last_error; }
    }
    return WY_ERR_NONE;
}

/* -------------------------------------------------------------------------
 * classes (id 6)
 * ------------------------------------------------------------------------- */

/** An optional index field that must be below `limit`; `*out` keeps its value if absent. */
static wy_error opt_field_index_(wy_cbor_span doc, const char* key, wy_uword limit, wy_i32* out)
{
    wy_u32 idx;
    bool found;
    wy_error last_error = opt_field_uint_(doc, key, 0x7FFFFFFFu, &idx, &found);
    if (last_error != WY_ERR_NONE) { return last_error; }
    if (!found) { return WY_ERR_NONE; }
    if ((wy_uword) idx >= limit) { return WY_ERR_IMAGE; }
    *out = (wy_i32) idx;
    return WY_ERR_NONE;
}

/** A required index field that must be below `limit`. */
static wy_error field_index_(wy_cbor_span doc, const char* key, wy_uword limit, wy_u32* out)
{
    wy_error last_error = field_uint_(doc, key, 0x7FFFFFFFu, out);
    if (last_error != WY_ERR_NONE) { return last_error; }
    return (wy_uword) *out >= limit ? WY_ERR_IMAGE : WY_ERR_NONE;
}

static wy_error decode_slot_proto_(wy_context* context, wy_module* module, wy_cbor_span doc, wy_slot_proto* out)
{
    wy_error last_error = field_symbol_(context, doc, "n", &out->name);
    if (last_error != WY_ERR_NONE) { return last_error; }

    out->default_static = -1;
    last_error = opt_field_index_(doc, "d", module->static_count, &out->default_static);
    if (last_error != WY_ERR_NONE) { return last_error; }

    out->getter_fn = -1;
    last_error = opt_field_index_(doc, "g", module->function_count, &out->getter_fn);
    if (last_error != WY_ERR_NONE) { return last_error; }

    out->setter_fn = -1;
    return opt_field_index_(doc, "s", module->function_count, &out->setter_fn);
}

static wy_error decode_class_(wy_context* context, wy_module* module, wy_cbor_span doc, wy_class_proto* out)
{
    wy_error last_error = field_symbol_(context, doc, "n", &out->name);
    if (last_error != WY_ERR_NONE) { return last_error; }

    out->super_slot = -1;
    last_error = opt_field_index_(doc, "s", module->global_count, &out->super_slot);
    if (last_error != WY_ERR_NONE) { return last_error; }

    out->init_fn = -1;
    last_error = opt_field_index_(doc, "i", module->function_count, &out->init_fn);
    if (last_error != WY_ERR_NONE) { return last_error; }

    // sl - always present, may be empty
    wy_cbor_reader reader;
    wy_uword nslots;
    last_error = field_array_(doc, "sl", &reader, &nslots);
    if (last_error != WY_ERR_NONE) { return last_error; }
    if (nslots > 0xFFFF) { return WY_ERR_IMAGE; }
    out->nslots = (wy_u16) nslots;
    out->slots = WY_NULL;
    if (nslots > 0) {
        out->slots = wy_context_gc_alloc(context, sizeof(wy_slot_proto) * nslots);
        if (out->slots == WY_NULL) { return WY_ERR_NOMEM; }
        for (wy_uword i = 0; i < nslots; i++) {
            wy_cbor_span item;
            last_error = wy_cbor_next_span(&reader, &item);
            if (last_error != WY_ERR_NONE) { return last_error; }
            last_error = decode_slot_proto_(context, module, item, &out->slots[i]);
            if (last_error != WY_ERR_NONE) { return last_error; }
        }
    }

    // m - always present, may be empty, <= WY_CLASS_MAX_MESSAGES (§8.6)
    wy_uword nmsgs;
    last_error = field_array_(doc, "m", &reader, &nmsgs);
    if (last_error != WY_ERR_NONE) { return last_error; }
    if (nmsgs > WY_CLASS_MAX_MESSAGES) { return WY_ERR_IMAGE; }
    out->nmsgs = (wy_u16) nmsgs;
    for (wy_uword i = 0; i < nmsgs; i++) {
        wy_cbor_span item;
        last_error = wy_cbor_next_span(&reader, &item);
        if (last_error != WY_ERR_NONE) { return last_error; }

        wy_u32 symidx;
        last_error = field_index_(item, "y", module->symbol_count, &symidx);
        if (last_error != WY_ERR_NONE) { return last_error; }
        wy_u32 fnidx;
        last_error = field_index_(item, "f", module->function_count, &fnidx);
        if (last_error != WY_ERR_NONE) { return last_error; }

        out->msgs[i].name = module->symbols[symidx];
        out->msgs[i].fn = (wy_u16) fnidx;
    }

    // st - always present, may be empty
    wy_uword nstatics;
    last_error = field_array_(doc, "st", &reader, &nstatics);
    if (last_error != WY_ERR_NONE) { return last_error; }
    if (nstatics > 0xFFFF) { return WY_ERR_IMAGE; }
    out->nstatics = (wy_u16) nstatics;
    out->statics = WY_NULL;
    if (nstatics > 0) {
        out->statics = wy_context_gc_alloc(context, sizeof(wy_class_static_proto) * nstatics);
        if (out->statics == WY_NULL) { return WY_ERR_NOMEM; }
        for (wy_uword i = 0; i < nstatics; i++) {
            wy_cbor_span item;
            last_error = wy_cbor_next_span(&reader, &item);
            if (last_error != WY_ERR_NONE) { return last_error; }

            last_error = field_symbol_(context, item, "n", &out->statics[i].name);
            if (last_error != WY_ERR_NONE) { return last_error; }
            wy_u32 gidx;
            last_error = field_index_(item, "g", module->global_count, &gidx);
            if (last_error != WY_ERR_NONE) { return last_error; }
            out->statics[i].global = (wy_u16) gidx;
        }
    }

    return WY_ERR_NONE;
}

static wy_error module_load_section_classes_(wy_context* context, wy_module* module, wy_section_ref section)
{
    wy_cbor_reader reader;
    wy_error last_error = open_section_array_(section, &reader, &module->class_count);
    if (last_error != WY_ERR_NONE) { return last_error; }
    if (module->class_count == 0) { return WY_ERR_NONE; }

    module->class_protos = wy_context_gc_alloc(context, sizeof(wy_class_proto) * module->class_count);
    if (module->class_protos == WY_NULL) { return WY_ERR_NOMEM; }
    // Zeroed, so that finalizing a module whose load failed part way frees only
    // what the entries decoded so far allocated.
    wy_memset(module->class_protos, 0, sizeof(wy_class_proto) * module->class_count);

    for (wy_uword i = 0; i < module->class_count; i++) {
        wy_cbor_span item;
        last_error = wy_cbor_next_span(&reader, &item);
        if (last_error != WY_ERR_NONE) { return last_error; }
        last_error = decode_class_(context, module, item, &module->class_protos[i]);
        if (last_error != WY_ERR_NONE) { return last_error; }
    }
    return WY_ERR_NONE;
}

/* -------------------------------------------------------------------------
 * messages (id 7)
 * ------------------------------------------------------------------------- */

/** One `messages[]` entry (wyc-format.md §8.7): a message identity as a path of symbol indices. */
static wy_error decode_message_(wy_context* context, wy_module* module, wy_cbor_span doc, wy_message_ref* out)
{
    out->path_len = 0;
    out->path = WY_NULL;
    out->bound = WY_NULL;

    wy_cbor_reader preader;
    wy_uword path_len;
    wy_error last_error = field_array_(doc, "p", &preader, &path_len);
    if (last_error != WY_ERR_NONE) { return last_error; }
    if (path_len == 0 || path_len > 0xFFFF) { return WY_ERR_IMAGE; }

    out->path = wy_context_gc_alloc(context, sizeof(wy_u16) * path_len);
    if (out->path == WY_NULL) { return WY_ERR_NOMEM; }
    out->path_len = (wy_u16) path_len;

    for (wy_uword j = 0; j < path_len; j++) {
        wy_cbor_span item;
        last_error = wy_cbor_next_span(&preader, &item);
        if (last_error != WY_ERR_NONE) { return last_error; }
        wy_u32 symidx;
        if (wy_cbor_get_u32(item, &symidx) != WY_ERR_NONE) { return WY_ERR_IMAGE; }
        if ((wy_uword) symidx >= module->symbol_count) { return WY_ERR_IMAGE; }
        out->path[j] = (wy_u16) symidx;
    }
    return WY_ERR_NONE;
}

static wy_error module_load_section_messages_(wy_context* context, wy_module* module, wy_section_ref section)
{
    wy_cbor_reader reader;
    wy_error last_error = open_section_array_(section, &reader, &module->message_count);
    if (last_error != WY_ERR_NONE) { return last_error; }
    if (module->message_count == 0) { return WY_ERR_NONE; }

    module->messages = wy_context_gc_alloc(context, sizeof(wy_message_ref) * module->message_count);
    if (module->messages == WY_NULL) { return WY_ERR_NOMEM; }
    // Zeroed, so that finalizing a module whose load failed part way frees only
    // what the entries decoded so far allocated.
    wy_memset(module->messages, 0, sizeof(wy_message_ref) * module->message_count);

    for (wy_uword i = 0; i < module->message_count; i++) {
        wy_cbor_span item;
        last_error = wy_cbor_next_span(&reader, &item);
        if (last_error != WY_ERR_NONE) { return last_error; }
        last_error = decode_message_(context, module, item, &module->messages[i]);
        if (last_error != WY_ERR_NONE) { return last_error; }
    }
    return WY_ERR_NONE;
}

/* -------------------------------------------------------------------------
 * exports / free (ids 10, 11): "<name>": global slot index
 * ------------------------------------------------------------------------- */

static wy_error module_load_name_slot_dict_(wy_context* context, wy_module* module, wy_section_ref section, wy_slot_dict* out)
{
    if (section.data == WY_NULL) { return WY_ERR_NONE; }

    wy_cbor_span span = { section.data, section.len };
    wy_cbor_reader reader;
    wy_uword count;
    wy_error last_error = wy_cbor_open_map(span, &reader, &count);
    if (last_error != WY_ERR_NONE) { return last_error; }
    if (count == 0) { return WY_ERR_NONE; }

    wy_allocator* allocator = wy_context_get_machine(context)->allocator;
    last_error = wy_slot_dict_expand_f(out, allocator, count * 2);
    if (last_error != WY_ERR_NONE) { return last_error; }

    for (wy_uword i = 0; i < count; i++) {
        wy_cbor_span key;
        last_error = wy_cbor_next_span(&reader, &key);
        if (last_error != WY_ERR_NONE) { return last_error; }
        wy_cbor_span value;
        last_error = wy_cbor_next_span(&reader, &value);
        if (last_error != WY_ERR_NONE) { return last_error; }

        wy_u32 slot;
        if (wy_cbor_get_u32(value, &slot) != WY_ERR_NONE) { return WY_ERR_IMAGE; }
        if ((wy_uword) slot >= module->global_count) { return WY_ERR_IMAGE; }

        wy_symbol sym;
        last_error = intern_text_(context, key, &sym);
        if (last_error != WY_ERR_NONE) { return last_error; }

        last_error = wy_slot_dict_add_entry(out, sym, (wy_uword) slot);
        if (last_error != WY_ERR_NONE) { return WY_ERR_IMAGE; }  // duplicate name
    }
    return WY_ERR_NONE;
}

/* -------------------------------------------------------------------------
 * wy_module object plumbing
 * ------------------------------------------------------------------------- */

void wy_module_init_static_f(wy_module* self)
{
    wy_memset(self, 0, sizeof(wy_module));
    wy_object_init_header_s(&self->head, &wy_module_type);
    self->state = WY_MODULE_LOADED;
}

wy_module* wy_module_new_f(wy_context* context)
{
    wy_module* self = (wy_module*) wy_context_gc_alloc(context, sizeof(wy_module));
    if (!self) { return WY_NULL; }

    wy_module_init_static_f(self);
    wy_context_push_gc(context, WY_MODULE_GET_OBJ(self));
    return self;
}

wy_error wy_module_load_image(wy_context* context, const wy_module_image* image, wy_module** out)
{
    if (context == WY_NULL || image == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }
    if (!host_is_little_endian_()) { return WY_ERR_IMAGE; }  // see doc/vm_impl.md

    wy_module* module = wy_module_new_f(context);
    if (module == WY_NULL) { return WY_ERR_NOMEM; }

    wy_error last_error = module_load_section_header_(context, module, image->sections[WY_SEC_HEADER]);
    if (last_error != WY_ERR_NONE) { return last_error; }

    wy_section_ref code_section = image->sections[WY_SEC_CODE];
    module->code = (const wy_u32*) code_section.data;
    module->code_len = code_section.len / 4;
    if (code_section.len > 0 && ((wy_uintptr) code_section.data % sizeof(wy_u32)) != 0) {
        // A .wyc file puts the code wherever the structured sections before it
        // end. The VM reads it as u32 words, so copy it to an aligned buffer
        // that the module owns.
        wy_allocator* allocator = wy_context_get_machine(context)->allocator;
        module->owned_code = (wy_u32*) wy_allocator_alloc(allocator, code_section.len);
        if (module->owned_code == WY_NULL) { return WY_ERR_NOMEM; }
        wy_memcpy(module->owned_code, code_section.data, code_section.len);
        module->code = module->owned_code;
    }

    last_error = module_load_section_slot_defaults_(context, module, image->sections[WY_SEC_SLOT_DEFAULTS]);
    if (last_error != WY_ERR_NONE) { return last_error; }

    last_error = module_load_section_symbols_(context, module, image->sections[WY_SEC_SYMBOLS]);
    if (last_error != WY_ERR_NONE) { return last_error; }

    last_error = module_load_section_statics_(context, module, image->sections[WY_SEC_STATICS]);
    if (last_error != WY_ERR_NONE) { return last_error; }

    last_error = module_load_section_functions_(context, module, image->sections[WY_SEC_FUNCTIONS]);
    if (last_error != WY_ERR_NONE) { return last_error; }

    last_error = module_load_section_classes_(context, module, image->sections[WY_SEC_CLASSES]);
    if (last_error != WY_ERR_NONE) { return last_error; }

    last_error = module_load_section_messages_(context, module, image->sections[WY_SEC_MESSAGES]);
    if (last_error != WY_ERR_NONE) { return last_error; }

    last_error = module_load_name_slot_dict_(context, module, image->sections[WY_SEC_EXPORTS], &module->exports);
    if (last_error != WY_ERR_NONE) { return last_error; }

    last_error = module_load_name_slot_dict_(context, module, image->sections[WY_SEC_FREE], &module->free_names);
    if (last_error != WY_ERR_NONE) { return last_error; }

    last_error = wy_link_fill_from_builtins(context, module, context->builtins);
    if (last_error != WY_ERR_NONE) { return last_error; }
    module->state = WY_MODULE_LOADED;
    *out = module;
    return WY_ERR_NONE;
}

wy_error wy_module_load_bytes(wy_context* context, const wy_u8* data, wy_uword len, bool take_ownership, wy_module** out)
{
    if (context == WY_NULL || data == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }

    wy_module_image image;
    wy_error last_error = wy_image_from_bytes(data, len, &image);
    if (last_error != WY_ERR_NONE) { return last_error; }

    last_error = wy_module_load_image(context, &image, out);
    if (last_error != WY_ERR_NONE) { return last_error; }

    (*out)->image = data;
    (*out)->image_len = len;
    (*out)->owns_image = take_ownership;
    return WY_ERR_NONE;
}

wy_error wy_module_run_init(wy_context* context, wy_module* module)
{
    if (context == WY_NULL || module == WY_NULL) { return WY_ERR_INVAL; }

    if (module->state == WY_MODULE_READY || module->state == WY_MODULE_BUILTIN) { return WY_ERR_NONE; }
    if (module->state == WY_MODULE_INITIALISING) { return WY_ERR_CYCLE; }
    if (module->state == WY_MODULE_FAILED) { return WY_ERR_LINK; }
    bool registered = false;
    for (wy_uword i = 0; i < context->module_count; i++) {
        if (wy_context_get_module(context, i) == module) { registered = true; break; }
    }
    wy_error last_error = WY_ERR_NONE;
    if (!registered) {
        last_error = wy_context_module_register(context, module, WY_NULL);
        if (last_error != WY_ERR_NONE) { return last_error; }
    }
    last_error = wy_link_fill_from_builtins(context, module, context->builtins);
    if (last_error != WY_ERR_NONE) { return last_error; }
    module->init_proto.nlocals = module->init_nlocals;
    wy_function* init_fn = WY_NULL;
    last_error = wy_function_new(context, module, &module->init_proto, WY_NULL, 0, &init_fn);
    if (last_error != WY_ERR_NONE) { return last_error; }
    module->state = WY_MODULE_INITIALISING;

    wy_value callee = wy_value_object(WY_TYPE_TAG_FUNCTION, (wy_object*) init_fn);
    last_error = wy_vm_call_sync(context, callee, WY_NULL, 0, WY_NULL);

    module->state = (last_error == WY_ERR_NONE) ? WY_MODULE_READY : WY_MODULE_FAILED;
    return last_error;
}

/* A session module's big arrays are reservations from the machine allocator
 * (wyrm/session.h); every other module's are GC-heap allocations. */
static void free_table_(wy_context* context, const wy_module* module, void* ptr)
{
    if (module->session != WY_NULL) {
        if (ptr != WY_NULL) { wy_allocator_free(wy_context_get_machine(context)->allocator, ptr); }
    } else {
        wy_context_gc_free(context, ptr);
    }
}

static void finalize(wy_context* context, wy_object* self_s)
{
    wy_module* self = (wy_module*) self_s;
    wy_allocator* allocator = wy_context_get_machine(context)->allocator;

    for (wy_uword i = 0; i < self->function_count; i++) {
        wy_context_gc_free(context, self->functions[i].params);
        wy_context_gc_free(context, self->functions[i].dispatch_slots);
    }
    free_table_(context, self, self->functions);

    for (wy_uword i = 0; i < self->class_count; i++) {
        wy_context_gc_free(context, self->class_protos[i].slots);
        wy_context_gc_free(context, self->class_protos[i].statics);
    }
    free_table_(context, self, self->class_protos);
    free_table_(context, self, self->classes);

    for (wy_uword i = 0; i < self->message_count; i++) {
        wy_context_gc_free(context, self->messages[i].path);
    }
    free_table_(context, self, self->messages);

    free_table_(context, self, self->globals);
    free_table_(context, self, self->fill_layer);
    free_table_(context, self, self->fill_source);
    free_table_(context, self, self->aliases);
    free_table_(context, self, self->statics);
    free_table_(context, self, self->symbols);
    for (wy_uword i = 0; i < self->wildcard_count; i++) {
        wy_context_gc_free(context, self->wildcards[i].excepts);
    }
    wy_context_gc_free(context, self->wildcards);

    wy_slot_finalize_f(&self->exports, allocator);
    wy_slot_finalize_f(&self->free_names, allocator);

    if (self->owns_image) {
        wy_context_gc_free(context, (void*) self->image);
    }
    if (self->owned_code != WY_NULL) { wy_allocator_free(allocator, self->owned_code); }

    if (self->session != WY_NULL) {
        /* The code reservation and the bookkeeping struct (session.c). */
        wy_allocator_free(allocator, (void*) self->code);
        wy_session_free_(allocator, self->session);
    }
}

static wy_error children_iter_start(wy_context* context, wy_object* self, wy_work_area* wa)
{
    WY_UNUSED(context); WY_UNUSED(self);
    wy_memset(wa, 0, sizeof(wy_work_area));
    return WY_ERR_NONE;
}

static wy_error children_iter_next(wy_context* context, wy_object* object, wy_work_area* wa, const wy_object** child)
{
    WY_UNUSED(context);
    wy_module* self = (wy_module*) object;

    for (;;) {
        wy_uword phase = wa->data[0].uword;

        if (phase == 0) {
            wy_uword idx = wa->data[1].uword;
            if (idx >= self->global_count) {
                wa->data[0].uword = 1;
                wa->data[1].uword = 0;
                continue;
            }
            wa->data[1].uword = idx + 1;
            if (wy_value_is_gc_ref_f(self->globals[idx])) {
                *child = self->globals[idx].data.gc_object;
                return WY_ERR_NONE;
            }
            continue;
        }

        if (phase == 1) {
            wy_uword idx = wa->data[1].uword;
            if (idx >= self->static_count) {
                wa->data[0].uword = 2;
                wa->data[1].uword = 0;
                continue;
            }
            wa->data[1].uword = idx + 1;
            if (wy_value_is_gc_ref_f(self->statics[idx])) {
                *child = self->statics[idx].data.gc_object;
                return WY_ERR_NONE;
            }
            continue;
        }

        if (phase == 2) {
            /* Realised classes (epic 4/M1): module->classes[] is the only
             * reference once a class value drops out of every live
             * register, so it must be a GC root or a cached class can be
             * collected out from under wy_class_realise_f's cache check. */
            wy_uword idx = wa->data[1].uword;
            if (idx >= self->class_count) {
                wa->data[0].uword = 3;
                wa->data[1].uword = 0;
                continue;
            }
            wa->data[1].uword = idx + 1;
            if (self->classes != WY_NULL && self->classes[idx] != WY_NULL) {
                *child = (wy_object*) self->classes[idx];
                return WY_ERR_NONE;
            }
            continue;
        }

        if (phase == 3) {
            /* message_table (epic 4/M2) transitively holds every wy_message
             * this module has bound; tracing the dict alone keeps them all
             * alive without walking messages[].bound separately. */
            wa->data[0].uword = 4;
            if (self->message_table != WY_NULL) {
                *child = (wy_object*) self->message_table;
                return WY_ERR_NONE;
            }
            continue;
        }

        if (phase == 4) {
            wa->data[0].uword = 5;
            wa->data[1].uword = 0;
            if (self->import_path != WY_NULL) {
                *child = (wy_object*) self->import_path;
                return WY_ERR_NONE;
            }
            continue;
        }
        if (phase == 5) {
            wy_uword idx = wa->data[1].uword;
            if (idx < self->wildcard_count) {
                wa->data[1].uword = idx + 1;
                *child = (wy_object*) self->wildcards[idx].target;
                return WY_ERR_NONE;
            }
        }
        return WY_ERR_STOP_ITERATION;
    }
}

const wy_object_type wy_module_type = {
    .object = WY_OBJECT_TYPE_OBJECT_INIT,
    .gc_type = WY_TYPE_TAG_MODULE,

    .children_iter_start = children_iter_start,
    .children_iter_next = children_iter_next,
    .finalize = finalize,
};

/* -------------------------------------------------------------------------
 * Session extension (doc-llm/history/repl/repl-plan.md, M1)
 *
 * A delta image is an ordinary container whose header carries `d: 1` and the
 * module's counts *before* the delta:
 *
 *   bc code words, bf functions, bs statics, by symbols, bk classes,
 *   bm messages, bg globals
 *
 * plus `g` (the total global count after the delta) and `i` (the absolute
 * index of the function that runs this input). Every section holds only the
 * new items; every cross-reference is absolute, so the same decoders that load
 * an ordinary module check them against the growing tables. Jumps are
 * ip-relative, so the code is relocatable. A delta whose base counts differ
 * from the module's is refused: that is the guard against the compiling side
 * and this side having drifted apart.
 * ------------------------------------------------------------------------- */

typedef struct session_delta_
{
    wy_uword base_code, base_functions, base_statics, base_symbols, base_classes, base_messages, base_globals;
    wy_uword globals_after;
    wy_uword init_function;
    wy_uword new_code, new_functions, new_statics, new_symbols, new_classes, new_messages;
} session_delta_;

static wy_error delta_header_uint_(wy_cbor_span doc, const char* key, wy_uword* out)
{
    wy_u32 value;
    wy_error err = field_uint_(doc, key, 0x7FFFFFFFu, &value);
    if (err != WY_ERR_NONE) { return WY_ERR_IMAGE; }
    *out = (wy_uword) value;
    return WY_ERR_NONE;
}

static wy_error delta_section_count_(wy_section_ref section, wy_uword* out)
{
    wy_cbor_reader reader;
    return open_section_array_(section, &reader, out);
}

/* Read and validate the header and section sizes; touches nothing. */
static wy_error delta_read_(const wy_module* module, const wy_module_image* image, session_delta_* d)
{
    wy_section_ref header = image->sections[WY_SEC_HEADER];
    if (header.data == WY_NULL) { return WY_ERR_IMAGE; }
    wy_cbor_span doc = { header.data, header.len };
    wy_error err = WY_ERR_NONE;

    wy_uword marker = 0;
    if (delta_header_uint_(doc, "d", &marker) != WY_ERR_NONE || marker != 1) { return WY_ERR_IMAGE; }
    if ((err = delta_header_uint_(doc, "bc", &d->base_code)) != WY_ERR_NONE) { return err; }
    if ((err = delta_header_uint_(doc, "bf", &d->base_functions)) != WY_ERR_NONE) { return err; }
    if ((err = delta_header_uint_(doc, "bs", &d->base_statics)) != WY_ERR_NONE) { return err; }
    if ((err = delta_header_uint_(doc, "by", &d->base_symbols)) != WY_ERR_NONE) { return err; }
    if ((err = delta_header_uint_(doc, "bk", &d->base_classes)) != WY_ERR_NONE) { return err; }
    if ((err = delta_header_uint_(doc, "bm", &d->base_messages)) != WY_ERR_NONE) { return err; }
    if ((err = delta_header_uint_(doc, "bg", &d->base_globals)) != WY_ERR_NONE) { return err; }
    if ((err = delta_header_uint_(doc, "g", &d->globals_after)) != WY_ERR_NONE) { return err; }
    if ((err = delta_header_uint_(doc, "i", &d->init_function)) != WY_ERR_NONE) { return err; }

    /* The desync guard: the delta was compiled against exactly this module. */
    if (d->base_code != module->code_len || d->base_functions != module->function_count
        || d->base_statics != module->static_count || d->base_symbols != module->symbol_count
        || d->base_classes != module->class_count || d->base_messages != module->message_count
        || d->base_globals != module->global_count) {
        return WY_ERR_IMAGE;
    }
    if (d->globals_after < d->base_globals) { return WY_ERR_IMAGE; }

    wy_section_ref code = image->sections[WY_SEC_CODE];
    if (code.data == WY_NULL || code.len % 4 != 0) { return WY_ERR_IMAGE; }
    d->new_code = code.len / 4;
    if ((err = delta_section_count_(image->sections[WY_SEC_FUNCTIONS], &d->new_functions)) != WY_ERR_NONE) { return err; }
    if ((err = delta_section_count_(image->sections[WY_SEC_STATICS], &d->new_statics)) != WY_ERR_NONE) { return err; }
    if ((err = delta_section_count_(image->sections[WY_SEC_SYMBOLS], &d->new_symbols)) != WY_ERR_NONE) { return err; }
    if ((err = delta_section_count_(image->sections[WY_SEC_CLASSES], &d->new_classes)) != WY_ERR_NONE) { return err; }
    if ((err = delta_section_count_(image->sections[WY_SEC_MESSAGES], &d->new_messages)) != WY_ERR_NONE) { return err; }

    if (d->init_function < d->base_functions || d->init_function >= d->base_functions + d->new_functions) {
        return WY_ERR_IMAGE;  /* the input's function must be one of this delta's */
    }
    return WY_ERR_NONE;
}

/* Undo a partial commit: free what the new entries own and restore the counts. */
static void delta_rollback_(wy_context* context, wy_module* module, const session_delta_* d)
{
    for (wy_uword i = d->base_functions; i < module->session_fill_functions_; i++) {
        wy_context_gc_free(context, module->functions[i].params);
        wy_context_gc_free(context, module->functions[i].dispatch_slots);
    }
    for (wy_uword i = d->base_classes; i < module->session_fill_classes_; i++) {
        wy_context_gc_free(context, module->class_protos[i].slots);
        wy_context_gc_free(context, module->class_protos[i].statics);
    }
    for (wy_uword i = d->base_messages; i < module->session_fill_messages_; i++) {
        wy_context_gc_free(context, module->messages[i].path);
    }
    module->code_len = d->base_code;
    module->function_count = d->base_functions;
    module->static_count = d->base_statics;
    module->symbol_count = d->base_symbols;
    module->class_count = d->base_classes;
    module->message_count = d->base_messages;
    module->global_count = d->base_globals;
}

wy_error wy_module_extend(wy_context* context, wy_module* module, const wy_module_image* image, wy_uword* out_init_function)
{
    if (context == WY_NULL || module == WY_NULL || image == WY_NULL) { return WY_ERR_INVAL; }
    if (!wy_module_is_session(module)) { return WY_ERR_INVAL; }
    if (!host_is_little_endian_()) { return WY_ERR_IMAGE; }

    session_delta_ d;
    wy_memset(&d, 0, sizeof(d));
    wy_error err = delta_read_(module, image, &d);
    if (err != WY_ERR_NONE) { return err; }

    /* Room in every reservation, and the dictionaries' entries, before any change. */
    if ((err = wy_session_check_room(module, WY_SESSION_CODE, d.new_code)) != WY_ERR_NONE) { return err; }
    if ((err = wy_session_check_room(module, WY_SESSION_FUNCTIONS, d.new_functions)) != WY_ERR_NONE) { return err; }
    if ((err = wy_session_check_room(module, WY_SESSION_STATICS, d.new_statics)) != WY_ERR_NONE) { return err; }
    if ((err = wy_session_check_room(module, WY_SESSION_SYMBOLS, d.new_symbols)) != WY_ERR_NONE) { return err; }
    if ((err = wy_session_check_room(module, WY_SESSION_CLASSES, d.new_classes)) != WY_ERR_NONE) { return err; }
    if ((err = wy_session_check_room(module, WY_SESSION_MESSAGES, d.new_messages)) != WY_ERR_NONE) { return err; }
    if ((err = wy_session_check_room(module, WY_SESSION_GLOBALS, d.globals_after - d.base_globals)) != WY_ERR_NONE) { return err; }

    /* Validate the two name dictionaries now (keys and slots), so applying them
     * last cannot fail: a free name must be new, and every slot in range. */
    for (int which = 0; which < 2; which++) {
        wy_section_ref section = image->sections[which == 0 ? WY_SEC_EXPORTS : WY_SEC_FREE];
        if (section.data == WY_NULL) { continue; }
        wy_cbor_span span = { section.data, section.len };
        wy_cbor_reader reader;
        wy_uword count;
        if ((err = wy_cbor_open_map(span, &reader, &count)) != WY_ERR_NONE) { return err; }
        for (wy_uword i = 0; i < count; i++) {
            wy_cbor_span key, value;
            if ((err = wy_cbor_next_span(&reader, &key)) != WY_ERR_NONE) { return err; }
            if ((err = wy_cbor_next_span(&reader, &value)) != WY_ERR_NONE) { return err; }
            wy_u32 slot;
            if (wy_cbor_get_u32(value, &slot) != WY_ERR_NONE || (wy_uword) slot >= d.globals_after) { return WY_ERR_IMAGE; }
            if (which == 1) {
                wy_symbol sym;
                if ((err = intern_text_(context, key, &sym)) != WY_ERR_NONE) { return err; }
                if (wy_slot_dict_get(&module->free_names, sym) != WY_SLOT_INVALID) { return WY_ERR_IMAGE; }
            }
        }
    }

    /* Commit, in load order, growing each count as its entries land so the
     * decoders' bounds checks see the new items. `session_fill_*` track how
     * far each table got, for the rollback. */
    wy_section_ref code = image->sections[WY_SEC_CODE];
    wy_memcpy((void*) (module->code + d.base_code), code.data, d.new_code * sizeof(wy_u32));
    module->code_len = d.base_code + d.new_code;

    for (wy_uword g = d.base_globals; g < d.globals_after; g++) {
        module->globals[g] = wy_value_unset();
        module->fill_layer[g] = 0;
        module->fill_source[g] = WY_NULL;
        module->aliases[g] = WY_NULL;
    }
    module->global_count = d.globals_after;

    module->session_fill_functions_ = d.base_functions;
    module->session_fill_classes_ = d.base_classes;
    module->session_fill_messages_ = d.base_messages;

    err = module_load_section_slot_defaults_(context, module, image->sections[WY_SEC_SLOT_DEFAULTS]);

    wy_cbor_reader reader;
    wy_uword count;
    wy_cbor_span item;

    if (err == WY_ERR_NONE && d.new_symbols > 0) {
        err = open_section_array_(image->sections[WY_SEC_SYMBOLS], &reader, &count);
        for (wy_uword i = 0; i < d.new_symbols && err == WY_ERR_NONE; i++) {
            err = wy_cbor_next_span(&reader, &item);
            if (err == WY_ERR_NONE) { err = intern_text_(context, item, &module->symbols[module->symbol_count]); }
            if (err == WY_ERR_NONE) { module->symbol_count++; }
        }
    }

    if (err == WY_ERR_NONE && d.new_statics > 0) {
        err = open_section_array_(image->sections[WY_SEC_STATICS], &reader, &count);
        for (wy_uword i = 0; i < d.new_statics && err == WY_ERR_NONE; i++) {
            err = wy_cbor_next_span(&reader, &item);
            if (err == WY_ERR_NONE) { err = decode_static_value_(context, item, &module->statics[module->static_count]); }
            if (err == WY_ERR_NONE) { module->static_count++; }
        }
    }

    if (err == WY_ERR_NONE && d.new_functions > 0) {
        err = open_section_array_(image->sections[WY_SEC_FUNCTIONS], &reader, &count);
        for (wy_uword i = 0; i < d.new_functions && err == WY_ERR_NONE; i++) {
            err = wy_cbor_next_span(&reader, &item);
            if (err != WY_ERR_NONE) { break; }
            wy_function_proto* slot = &module->functions[module->function_count];
            wy_memset(slot, 0, sizeof(*slot));
            module->session_fill_functions_ = module->function_count + 1;  /* the entry may own partial allocations */
            err = decode_function_(context, module, item, slot);
            if (err == WY_ERR_NONE) { module->function_count++; }
        }
    }

    if (err == WY_ERR_NONE && d.new_classes > 0) {
        err = open_section_array_(image->sections[WY_SEC_CLASSES], &reader, &count);
        for (wy_uword i = 0; i < d.new_classes && err == WY_ERR_NONE; i++) {
            err = wy_cbor_next_span(&reader, &item);
            if (err != WY_ERR_NONE) { break; }
            wy_class_proto* slot = &module->class_protos[module->class_count];
            wy_memset(slot, 0, sizeof(*slot));
            module->classes[module->class_count] = WY_NULL;
            module->session_fill_classes_ = module->class_count + 1;
            err = decode_class_(context, module, item, slot);
            if (err == WY_ERR_NONE) { module->class_count++; }
        }
    }

    if (err == WY_ERR_NONE && d.new_messages > 0) {
        err = open_section_array_(image->sections[WY_SEC_MESSAGES], &reader, &count);
        for (wy_uword i = 0; i < d.new_messages && err == WY_ERR_NONE; i++) {
            err = wy_cbor_next_span(&reader, &item);
            if (err != WY_ERR_NONE) { break; }
            wy_message_ref* slot = &module->messages[module->message_count];
            module->session_fill_messages_ = module->message_count + 1;
            err = decode_message_(context, module, item, slot);
            if (err == WY_ERR_NONE) { module->message_count++; }
        }
    }

    if (err != WY_ERR_NONE) {
        delta_rollback_(context, module, &d);
        return err;
    }

    /* Names last (already validated): new bindings shadow by replacing the
     * exported name's slot; free names are new by construction. */
    for (int which = 0; which < 2; which++) {
        wy_section_ref section = image->sections[which == 0 ? WY_SEC_EXPORTS : WY_SEC_FREE];
        if (section.data == WY_NULL) { continue; }
        wy_cbor_span span = { section.data, section.len };
        if (wy_cbor_open_map(span, &reader, &count) != WY_ERR_NONE) { continue; }
        for (wy_uword i = 0; i < count; i++) {
            wy_cbor_span key, value;
            if (wy_cbor_next_span(&reader, &key) != WY_ERR_NONE) { break; }
            if (wy_cbor_next_span(&reader, &value) != WY_ERR_NONE) { break; }
            wy_u32 slot = 0;
            (void) wy_cbor_get_u32(value, &slot);
            wy_symbol sym;
            (void) intern_text_(context, key, &sym);
            (void) wy_slot_dict_set(which == 0 ? &module->exports : &module->free_names, sym, (wy_uword) slot);
        }
    }

    err = wy_link_fill_from_builtins(context, module, context->builtins);
    if (err != WY_ERR_NONE) { return err; }
    /* New free names (`std::io::println`) may belong to imports that ran in an
     * earlier input; fill them before this input runs. */
    err = wy_session_refill_(context, module);
    if (err != WY_ERR_NONE) { return err; }

    if (out_init_function != WY_NULL) { *out_init_function = d.init_function; }
    return WY_ERR_NONE;
}

wy_error wy_module_run_function(wy_context* context, wy_module* module, wy_uword function_index, wy_value* out_result)
{
    if (context == WY_NULL || module == WY_NULL || !wy_module_is_session(module)) { return WY_ERR_INVAL; }
    if (function_index >= module->function_count) { return WY_ERR_RANGE; }

    wy_function* fn = WY_NULL;
    wy_error err = wy_function_new(context, module, &module->functions[function_index], WY_NULL, 0, &fn);
    if (err != WY_ERR_NONE) { return err; }

    /* Never touches module->state: one faulting input must not poison the session. */
    wy_value callee = wy_value_object(WY_TYPE_TAG_FUNCTION, (wy_object*) fn);
    wy_value result = wy_value_nil();
    err = wy_vm_call_sync(context, callee, WY_NULL, 0, &result);
    if (err == WY_ERR_NONE && out_result != WY_NULL) { *out_result = result; }
    return err;
}
