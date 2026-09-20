#include <wyrm/module.h>

#include <wyrm/allocator.h>
#include <wyrm/bson.h>
#include <wyrm/context.h>
#include <wyrm/gc_flags.h>
#include <wyrm/image_loader.h>
#include <wyrm/machine.h>
#include <wyrm/string.h>
#include <wyrm/sys/string.h>
#include <wyrm/work_area.h>

/* -------------------------------------------------------------------------
 * Small helpers shared by every section loader below.
 * ------------------------------------------------------------------------- */

static bool host_is_little_endian_(void)
{
    const wy_u32 probe = 1;
    return *(const wy_u8*) &probe == 1;
}

/** Count the elements of a BSON array without decoding them. */
static wy_error bson_array_count_(const wy_u8* data, wy_uword len, wy_uword* out_count)
{
    wy_bson_array_reader reader;
    wy_error last_error = wy_bson_array_reader_start(&reader, data, len);
    if (last_error != WY_ERR_NONE) { return last_error; }

    wy_uword count = 0;
    wy_u8 tag;
    const wy_u8* buffer;
    wy_uword length;
    for (;;) {
        last_error = wy_bson_array_reader_get(&reader, &tag, &buffer, &length);
        if (last_error == WY_ERR_STOP_ITERATION) { break; }
        if (last_error != WY_ERR_NONE) { return WY_ERR_IMAGE; }
        count++;
    }
    *out_count = count;
    return WY_ERR_NONE;
}

static wy_error intern_bson_string_(wy_context* context, const wy_u8* buffer, wy_symbol* out)
{
    const char* text;
    wy_uword len;
    wy_bson_get_str(buffer, &text, &len);
    return wy_context_intern(context, text, len, out);
}

/**
 * A `statics[]`/`slot_defaults` value (wyc-format.md §8.2/§8.3): string,
 * int32, double, bool, null or binary. A document or array here is a
 * format error - those tags never appear as a static's own value.
 */
static wy_error decode_static_value_(wy_context* context, wy_u8 tag, const wy_u8* buffer, wy_uword len, wy_value* out)
{
    switch (tag) {
    case WY_BSON_TAG_STRING: {
        const char* text;
        wy_uword slen;
        wy_bson_get_str(buffer, &text, &slen);
        wy_string* str;
        wy_error last_error = wy_string_new(context, text, slen, &str);
        if (last_error != WY_ERR_NONE) { return last_error; }
        *out = (wy_value) { .type = WY_TYPE_TAG_STR, .data = { .str = str } };
        return WY_ERR_NONE;
    }
    case WY_BSON_TAG_I32:
        *out = wy_value_word((wy_word) wy_bson_get_i32(buffer));
        return WY_ERR_NONE;
    case WY_BSON_TAG_DOUBLE: {
        double value;
        wy_error last_error = wy_bson_get_double_f(buffer, len, &value);
        if (last_error != WY_ERR_NONE) { return last_error; }
        *out = wy_value_float((wy_float) value);
        return WY_ERR_NONE;
    }
    case WY_BSON_TAG_BOOL:
        *out = wy_value_bool(wy_bson_get_bool(buffer));
        return WY_ERR_NONE;
    case WY_BSON_TAG_NIL:
        *out = wy_value_nil();
        return WY_ERR_NONE;
    case WY_BSON_TAG_BINARY: {
        // Placeholder until wy_bytes exists (epic 7): a wy_string flagged
        // as raw bytes rather than text (see WY_GC_FLAG_BINARY).
        const wy_u8* data;
        wy_u8 subtype;
        wy_uword blen;
        wy_error last_error = wy_bson_get_binary_f(buffer, len, &data, &subtype, &blen);
        if (last_error != WY_ERR_NONE) { return WY_ERR_IMAGE; }
        wy_string* str;
        last_error = wy_string_new(context, (const char*) data, blen, &str);
        if (last_error != WY_ERR_NONE) { return last_error; }
        str->object.flags |= WY_GC_FLAG_BINARY;
        *out = (wy_value) { .type = WY_TYPE_TAG_STR, .data = { .str = str } };
        return WY_ERR_NONE;
    }
    default:
        return WY_ERR_IMAGE;
    }
}

static wy_error parse_decimal_key_(const char* text, wy_uword* out)
{
    if (text == WY_NULL || text[0] == '\0') { return WY_ERR_IMAGE; }
    wy_uword value = 0;
    for (const char* p = text; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') { return WY_ERR_IMAGE; }
        value = value * 10 + (wy_uword) (*p - '0');
    }
    *out = value;
    return WY_ERR_NONE;
}

/* -------------------------------------------------------------------------
 * header (id 1)
 * ------------------------------------------------------------------------- */

static wy_error module_load_section_header_(wy_context* context, wy_module* module, wy_section_ref section)
{
    if (section.data == WY_NULL) { return WY_ERR_IMAGE; }  // required (wy_image_from_bytes already enforces this)

    wy_bson_doc_reader doc;
    wy_error last_error = wy_bson_doc_reader_start(&doc, section.data, section.len);
    if (last_error != WY_ERR_NONE) { return last_error; }

    wy_u8 tag;
    const wy_u8* buffer;
    wy_uword len;

    last_error = wy_bson_doc_reader_find(&doc, "n", &tag, &buffer, &len);
    if (last_error != WY_ERR_NONE || tag != WY_BSON_TAG_STRING) { return WY_ERR_IMAGE; }
    last_error = intern_bson_string_(context, buffer, &module->name);
    if (last_error != WY_ERR_NONE) { return last_error; }

    last_error = wy_bson_doc_reader_find(&doc, "v", &tag, &buffer, &len);
    if (last_error != WY_ERR_NONE || tag != WY_BSON_TAG_I32) { return WY_ERR_IMAGE; }
    if (wy_bson_get_i32(buffer) != 1) { return WY_ERR_IMAGE; }

    last_error = wy_bson_doc_reader_find(&doc, "g", &tag, &buffer, &len);
    if (last_error != WY_ERR_NONE || tag != WY_BSON_TAG_I32) { return WY_ERR_IMAGE; }
    wy_i32 nglobals = wy_bson_get_i32(buffer);
    if (nglobals < 0) { return WY_ERR_IMAGE; }

    last_error = wy_bson_doc_reader_find(&doc, "l", &tag, &buffer, &len);
    if (last_error != WY_ERR_NONE || tag != WY_BSON_TAG_I32) { return WY_ERR_IMAGE; }
    wy_i32 init_nlocals = wy_bson_get_i32(buffer);
    if (init_nlocals < 0 || init_nlocals > 0xFFFF) { return WY_ERR_IMAGE; }
    module->init_nlocals = (wy_u16) init_nlocals;

    // `u` (referenced-name set) is a writer artifact from before binding was
    // per-name; wyc-format.md §8.1 says a loader MUST ignore it.

    module->global_count = (wy_uword) nglobals;
    if (module->global_count == 0) { return WY_ERR_NONE; }

    module->globals = wy_context_gc_alloc(context, sizeof(wy_value) * module->global_count);
    module->fill_layer = wy_context_gc_alloc(context, sizeof(wy_u8) * module->global_count);
    module->fill_source = wy_context_gc_alloc(context, sizeof(wy_symbol) * module->global_count);
    if (module->globals == WY_NULL || module->fill_layer == WY_NULL || module->fill_source == WY_NULL) {
        return WY_ERR_NOMEM;
    }
    for (wy_uword i = 0; i < module->global_count; i++) {
        module->globals[i] = wy_value_unset();
        module->fill_layer[i] = 0;
        module->fill_source[i] = WY_NULL;
    }
    return WY_ERR_NONE;
}

/* -------------------------------------------------------------------------
 * slot_defaults (id 3)
 * ------------------------------------------------------------------------- */

static wy_error module_load_section_slot_defaults_(wy_context* context, wy_module* module, wy_section_ref section)
{
    if (section.data == WY_NULL) { return WY_ERR_NONE; }

    wy_bson_doc_reader doc;
    wy_error last_error = wy_bson_doc_reader_start(&doc, section.data, section.len);
    if (last_error != WY_ERR_NONE) { return last_error; }

    wy_u8 tag;
    const char* key;
    const wy_u8* buffer;
    wy_uword len;
    for (;;) {
        last_error = wy_bson_doc_reader_get(&doc, &tag, &key, &buffer, &len);
        if (last_error == WY_ERR_STOP_ITERATION) { break; }
        if (last_error != WY_ERR_NONE) { return WY_ERR_IMAGE; }

        wy_uword slot;
        if (parse_decimal_key_(key, &slot) != WY_ERR_NONE) { return WY_ERR_IMAGE; }
        if (slot >= module->global_count) { return WY_ERR_IMAGE; }

        wy_value value;
        last_error = decode_static_value_(context, tag, buffer, len, &value);
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
    if (section.data == WY_NULL) { return WY_ERR_NONE; }

    wy_error last_error = bson_array_count_(section.data, section.len, &module->symbol_count);
    if (last_error != WY_ERR_NONE) { return last_error; }
    if (module->symbol_count == 0) { return WY_ERR_NONE; }

    module->symbols = wy_context_gc_alloc(context, sizeof(wy_symbol) * module->symbol_count);
    if (module->symbols == WY_NULL) { return WY_ERR_NOMEM; }

    wy_bson_array_reader reader;
    wy_bson_array_reader_start(&reader, section.data, section.len);
    for (wy_uword i = 0; i < module->symbol_count; i++) {
        wy_u8 tag;
        const wy_u8* buffer;
        wy_uword len;
        last_error = wy_bson_array_reader_get(&reader, &tag, &buffer, &len);
        if (last_error != WY_ERR_NONE || tag != WY_BSON_TAG_STRING) { return WY_ERR_IMAGE; }
        last_error = intern_bson_string_(context, buffer, &module->symbols[i]);
        if (last_error != WY_ERR_NONE) { return last_error; }
    }
    return WY_ERR_NONE;
}

/* -------------------------------------------------------------------------
 * statics (id 2)
 * ------------------------------------------------------------------------- */

static wy_error module_load_section_statics_(wy_context* context, wy_module* module, wy_section_ref section)
{
    if (section.data == WY_NULL) { return WY_ERR_NONE; }

    wy_error last_error = bson_array_count_(section.data, section.len, &module->static_count);
    if (last_error != WY_ERR_NONE) { return last_error; }
    if (module->static_count == 0) { return WY_ERR_NONE; }

    module->statics = wy_context_gc_alloc(context, sizeof(wy_value) * module->static_count);
    if (module->statics == WY_NULL) { return WY_ERR_NOMEM; }

    wy_bson_array_reader reader;
    wy_bson_array_reader_start(&reader, section.data, section.len);
    for (wy_uword i = 0; i < module->static_count; i++) {
        wy_u8 tag;
        const wy_u8* buffer;
        wy_uword len;
        last_error = wy_bson_array_reader_get(&reader, &tag, &buffer, &len);
        if (last_error != WY_ERR_NONE) { return WY_ERR_IMAGE; }
        last_error = decode_static_value_(context, tag, buffer, len, &module->statics[i]);
        if (last_error != WY_ERR_NONE) { return last_error; }
    }
    return WY_ERR_NONE;
}

/* -------------------------------------------------------------------------
 * functions (id 5)
 * ------------------------------------------------------------------------- */

static wy_error decode_param_(wy_context* context, wy_module* module, const wy_u8* buffer, wy_uword len, wy_param* out)
{
    wy_bson_doc_reader doc;
    wy_error last_error = wy_bson_doc_reader_start(&doc, buffer, len);
    if (last_error != WY_ERR_NONE) { return last_error; }

    wy_u8 tag;
    const wy_u8* field;
    wy_uword flen;

    last_error = wy_bson_doc_reader_find(&doc, "n", &tag, &field, &flen);
    if (last_error != WY_ERR_NONE || tag != WY_BSON_TAG_STRING) { return WY_ERR_IMAGE; }
    last_error = intern_bson_string_(context, field, &out->name);
    if (last_error != WY_ERR_NONE) { return last_error; }

    out->default_static = -1;
    last_error = wy_bson_doc_reader_find(&doc, "d", &tag, &field, &flen);
    if (last_error == WY_ERR_NONE) {
        if (tag != WY_BSON_TAG_I32) { return WY_ERR_IMAGE; }
        wy_i32 idx = wy_bson_get_i32(field);
        if (idx < 0 || (wy_uword) idx >= module->static_count) { return WY_ERR_IMAGE; }
        out->default_static = idx;
    } else if (last_error != WY_ERR_KEY) {
        return WY_ERR_IMAGE;
    }
    return WY_ERR_NONE;
}

static wy_error decode_function_(wy_context* context, wy_module* module, const wy_u8* buffer, wy_uword len, wy_function_proto* out)
{
    wy_bson_doc_reader doc;
    wy_error last_error = wy_bson_doc_reader_start(&doc, buffer, len);
    if (last_error != WY_ERR_NONE) { return last_error; }

    wy_u8 tag;
    const wy_u8* field;
    wy_uword flen;

    last_error = wy_bson_doc_reader_find(&doc, "n", &tag, &field, &flen);
    if (last_error != WY_ERR_NONE || tag != WY_BSON_TAG_STRING) { return WY_ERR_IMAGE; }
    last_error = intern_bson_string_(context, field, &out->name);
    if (last_error != WY_ERR_NONE) { return last_error; }

    last_error = wy_bson_doc_reader_find(&doc, "l", &tag, &field, &flen);
    if (last_error != WY_ERR_NONE || tag != WY_BSON_TAG_I32) { return WY_ERR_IMAGE; }
    wy_i32 nlocals = wy_bson_get_i32(field);
    if (nlocals < 0 || nlocals > 0xFFFF) { return WY_ERR_IMAGE; }
    out->nlocals = (wy_u16) nlocals;

    last_error = wy_bson_doc_reader_find(&doc, "c", &tag, &field, &flen);
    if (last_error != WY_ERR_NONE || tag != WY_BSON_TAG_I32) { return WY_ERR_IMAGE; }
    wy_i32 code_offset = wy_bson_get_i32(field);
    if (code_offset < 0 || (wy_uword) code_offset >= module->code_len) { return WY_ERR_IMAGE; }
    out->code_offset = (wy_u32) code_offset;

    last_error = wy_bson_doc_reader_find(&doc, "f", &tag, &field, &flen);
    if (last_error != WY_ERR_NONE || tag != WY_BSON_TAG_I32) { return WY_ERR_IMAGE; }
    wy_i32 flags = wy_bson_get_i32(field);
    if (flags < 0 || flags > 0xFF) { return WY_ERR_IMAGE; }
    out->flags = (wy_u8) flags;

    out->ncaptures = 0;
    last_error = wy_bson_doc_reader_find(&doc, "k", &tag, &field, &flen);
    if (last_error == WY_ERR_NONE) {
        if (tag != WY_BSON_TAG_I32) { return WY_ERR_IMAGE; }
        wy_i32 ncaptures = wy_bson_get_i32(field);
        if (ncaptures < 0 || ncaptures > 0xFFFF) { return WY_ERR_IMAGE; }
        out->ncaptures = (wy_u16) ncaptures;
    } else if (last_error != WY_ERR_KEY) {
        return WY_ERR_IMAGE;
    }

    out->nresults = 1;
    last_error = wy_bson_doc_reader_find(&doc, "r", &tag, &field, &flen);
    if (last_error == WY_ERR_NONE) {
        if (tag != WY_BSON_TAG_I32) { return WY_ERR_IMAGE; }
        wy_i32 nresults = wy_bson_get_i32(field);
        if (nresults < 0 || nresults > 0xFF) { return WY_ERR_IMAGE; }
        out->nresults = (wy_u8) nresults;
    } else if (last_error != WY_ERR_KEY) {
        return WY_ERR_IMAGE;
    }

    last_error = wy_bson_doc_reader_find(&doc, "p", &tag, &field, &flen);
    if (last_error != WY_ERR_NONE || tag != WY_BSON_TAG_ARRAY) { return WY_ERR_IMAGE; }
    wy_uword nparams;
    last_error = bson_array_count_(field, flen, &nparams);
    if (last_error != WY_ERR_NONE) { return last_error; }
    if (nparams > 0xFFFF) { return WY_ERR_IMAGE; }
    out->nparams = (wy_u16) nparams;
    out->params = WY_NULL;
    if (nparams > 0) {
        out->params = wy_context_gc_alloc(context, sizeof(wy_param) * nparams);
        if (out->params == WY_NULL) { return WY_ERR_NOMEM; }
        wy_bson_array_reader preader;
        wy_bson_array_reader_start(&preader, field, flen);
        for (wy_uword i = 0; i < nparams; i++) {
            wy_u8 ptag;
            const wy_u8* pbuf;
            wy_uword plen;
            last_error = wy_bson_array_reader_get(&preader, &ptag, &pbuf, &plen);
            if (last_error != WY_ERR_NONE || ptag != WY_BSON_TAG_DOCUMENT) { return WY_ERR_IMAGE; }
            last_error = decode_param_(context, module, pbuf, plen, &out->params[i]);
            if (last_error != WY_ERR_NONE) { return last_error; }
        }
    }

    out->ndispatch = 0;
    out->dispatch_slots = WY_NULL;
    last_error = wy_bson_doc_reader_find(&doc, "t", &tag, &field, &flen);
    if (last_error == WY_ERR_NONE) {
        if (tag != WY_BSON_TAG_ARRAY) { return WY_ERR_IMAGE; }
        wy_uword ndispatch;
        last_error = bson_array_count_(field, flen, &ndispatch);
        if (last_error != WY_ERR_NONE) { return last_error; }
        if (ndispatch > 0xFFFF) { return WY_ERR_IMAGE; }
        out->ndispatch = (wy_u16) ndispatch;
        if (ndispatch > 0) {
            out->dispatch_slots = wy_context_gc_alloc(context, sizeof(wy_u16) * ndispatch);
            if (out->dispatch_slots == WY_NULL) { return WY_ERR_NOMEM; }
            wy_bson_array_reader treader;
            wy_bson_array_reader_start(&treader, field, flen);
            for (wy_uword i = 0; i < ndispatch; i++) {
                wy_u8 ttag;
                const wy_u8* tbuf;
                wy_uword tlen;
                last_error = wy_bson_array_reader_get(&treader, &ttag, &tbuf, &tlen);
                if (last_error != WY_ERR_NONE || ttag != WY_BSON_TAG_I32) { return WY_ERR_IMAGE; }
                wy_i32 slot = wy_bson_get_i32(tbuf);
                if (slot < 0 || (wy_uword) slot >= module->global_count) { return WY_ERR_IMAGE; }
                out->dispatch_slots[i] = (wy_u16) slot;
            }
        }
    } else if (last_error != WY_ERR_KEY) {
        return WY_ERR_IMAGE;
    }

    return WY_ERR_NONE;
}

static wy_error module_load_section_functions_(wy_context* context, wy_module* module, wy_section_ref section)
{
    if (section.data == WY_NULL) { return WY_ERR_NONE; }

    wy_error last_error = bson_array_count_(section.data, section.len, &module->function_count);
    if (last_error != WY_ERR_NONE) { return last_error; }
    if (module->function_count == 0) { return WY_ERR_NONE; }

    module->functions = wy_context_gc_alloc(context, sizeof(wy_function_proto) * module->function_count);
    if (module->functions == WY_NULL) { return WY_ERR_NOMEM; }

    wy_bson_array_reader reader;
    wy_bson_array_reader_start(&reader, section.data, section.len);
    for (wy_uword i = 0; i < module->function_count; i++) {
        wy_u8 tag;
        const wy_u8* buffer;
        wy_uword len;
        last_error = wy_bson_array_reader_get(&reader, &tag, &buffer, &len);
        if (last_error != WY_ERR_NONE || tag != WY_BSON_TAG_DOCUMENT) { return WY_ERR_IMAGE; }
        last_error = decode_function_(context, module, buffer, len, &module->functions[i]);
        if (last_error != WY_ERR_NONE) { return last_error; }
    }
    return WY_ERR_NONE;
}

/* -------------------------------------------------------------------------
 * classes (id 6)
 * ------------------------------------------------------------------------- */

static wy_error decode_slot_proto_(wy_context* context, wy_module* module, const wy_u8* buffer, wy_uword len, wy_slot_proto* out)
{
    wy_bson_doc_reader doc;
    wy_error last_error = wy_bson_doc_reader_start(&doc, buffer, len);
    if (last_error != WY_ERR_NONE) { return last_error; }

    wy_u8 tag;
    const wy_u8* field;
    wy_uword flen;

    last_error = wy_bson_doc_reader_find(&doc, "n", &tag, &field, &flen);
    if (last_error != WY_ERR_NONE || tag != WY_BSON_TAG_STRING) { return WY_ERR_IMAGE; }
    last_error = intern_bson_string_(context, field, &out->name);
    if (last_error != WY_ERR_NONE) { return last_error; }

    out->default_static = -1;
    last_error = wy_bson_doc_reader_find(&doc, "d", &tag, &field, &flen);
    if (last_error == WY_ERR_NONE) {
        if (tag != WY_BSON_TAG_I32) { return WY_ERR_IMAGE; }
        wy_i32 idx = wy_bson_get_i32(field);
        if (idx < 0 || (wy_uword) idx >= module->static_count) { return WY_ERR_IMAGE; }
        out->default_static = idx;
    } else if (last_error != WY_ERR_KEY) {
        return WY_ERR_IMAGE;
    }

    out->getter_fn = -1;
    last_error = wy_bson_doc_reader_find(&doc, "g", &tag, &field, &flen);
    if (last_error == WY_ERR_NONE) {
        if (tag != WY_BSON_TAG_I32) { return WY_ERR_IMAGE; }
        wy_i32 idx = wy_bson_get_i32(field);
        if (idx < 0 || (wy_uword) idx >= module->function_count) { return WY_ERR_IMAGE; }
        out->getter_fn = idx;
    } else if (last_error != WY_ERR_KEY) {
        return WY_ERR_IMAGE;
    }

    out->setter_fn = -1;
    last_error = wy_bson_doc_reader_find(&doc, "s", &tag, &field, &flen);
    if (last_error == WY_ERR_NONE) {
        if (tag != WY_BSON_TAG_I32) { return WY_ERR_IMAGE; }
        wy_i32 idx = wy_bson_get_i32(field);
        if (idx < 0 || (wy_uword) idx >= module->function_count) { return WY_ERR_IMAGE; }
        out->setter_fn = idx;
    } else if (last_error != WY_ERR_KEY) {
        return WY_ERR_IMAGE;
    }

    return WY_ERR_NONE;
}

static wy_error decode_class_(wy_context* context, wy_module* module, const wy_u8* buffer, wy_uword len, wy_class_proto* out)
{
    wy_bson_doc_reader doc;
    wy_error last_error = wy_bson_doc_reader_start(&doc, buffer, len);
    if (last_error != WY_ERR_NONE) { return last_error; }

    wy_u8 tag;
    const wy_u8* field;
    wy_uword flen;

    last_error = wy_bson_doc_reader_find(&doc, "n", &tag, &field, &flen);
    if (last_error != WY_ERR_NONE || tag != WY_BSON_TAG_STRING) { return WY_ERR_IMAGE; }
    last_error = intern_bson_string_(context, field, &out->name);
    if (last_error != WY_ERR_NONE) { return last_error; }

    out->super_slot = -1;
    last_error = wy_bson_doc_reader_find(&doc, "s", &tag, &field, &flen);
    if (last_error == WY_ERR_NONE) {
        if (tag != WY_BSON_TAG_I32) { return WY_ERR_IMAGE; }
        wy_i32 slot = wy_bson_get_i32(field);
        if (slot < 0 || (wy_uword) slot >= module->global_count) { return WY_ERR_IMAGE; }
        out->super_slot = slot;
    } else if (last_error != WY_ERR_KEY) {
        return WY_ERR_IMAGE;
    }

    out->init_fn = -1;
    last_error = wy_bson_doc_reader_find(&doc, "i", &tag, &field, &flen);
    if (last_error == WY_ERR_NONE) {
        if (tag != WY_BSON_TAG_I32) { return WY_ERR_IMAGE; }
        wy_i32 idx = wy_bson_get_i32(field);
        if (idx < 0 || (wy_uword) idx >= module->function_count) { return WY_ERR_IMAGE; }
        out->init_fn = idx;
    } else if (last_error != WY_ERR_KEY) {
        return WY_ERR_IMAGE;
    }

    // sl - always present, may be empty
    last_error = wy_bson_doc_reader_find(&doc, "sl", &tag, &field, &flen);
    if (last_error != WY_ERR_NONE || tag != WY_BSON_TAG_ARRAY) { return WY_ERR_IMAGE; }
    wy_uword nslots;
    last_error = bson_array_count_(field, flen, &nslots);
    if (last_error != WY_ERR_NONE) { return last_error; }
    if (nslots > 0xFFFF) { return WY_ERR_IMAGE; }
    out->nslots = (wy_u16) nslots;
    out->slots = WY_NULL;
    if (nslots > 0) {
        out->slots = wy_context_gc_alloc(context, sizeof(wy_slot_proto) * nslots);
        if (out->slots == WY_NULL) { return WY_ERR_NOMEM; }
        wy_bson_array_reader reader;
        wy_bson_array_reader_start(&reader, field, flen);
        for (wy_uword i = 0; i < nslots; i++) {
            wy_u8 stag;
            const wy_u8* sbuf;
            wy_uword slen;
            last_error = wy_bson_array_reader_get(&reader, &stag, &sbuf, &slen);
            if (last_error != WY_ERR_NONE || stag != WY_BSON_TAG_DOCUMENT) { return WY_ERR_IMAGE; }
            last_error = decode_slot_proto_(context, module, sbuf, slen, &out->slots[i]);
            if (last_error != WY_ERR_NONE) { return last_error; }
        }
    }

    // m - always present, may be empty, <= WY_CLASS_MAX_MESSAGES (§8.6)
    last_error = wy_bson_doc_reader_find(&doc, "m", &tag, &field, &flen);
    if (last_error != WY_ERR_NONE || tag != WY_BSON_TAG_ARRAY) { return WY_ERR_IMAGE; }
    wy_uword nmsgs;
    last_error = bson_array_count_(field, flen, &nmsgs);
    if (last_error != WY_ERR_NONE) { return last_error; }
    if (nmsgs > WY_CLASS_MAX_MESSAGES) { return WY_ERR_IMAGE; }
    out->nmsgs = (wy_u16) nmsgs;
    if (nmsgs > 0) {
        wy_bson_array_reader reader;
        wy_bson_array_reader_start(&reader, field, flen);
        for (wy_uword i = 0; i < nmsgs; i++) {
            wy_u8 mtag;
            const wy_u8* mbuf;
            wy_uword mlen;
            last_error = wy_bson_array_reader_get(&reader, &mtag, &mbuf, &mlen);
            if (last_error != WY_ERR_NONE || mtag != WY_BSON_TAG_DOCUMENT) { return WY_ERR_IMAGE; }

            wy_bson_doc_reader mdoc;
            last_error = wy_bson_doc_reader_start(&mdoc, mbuf, mlen);
            if (last_error != WY_ERR_NONE) { return last_error; }

            wy_u8 ytag;
            const wy_u8* ybuf;
            wy_uword ylen;
            last_error = wy_bson_doc_reader_find(&mdoc, "y", &ytag, &ybuf, &ylen);
            if (last_error != WY_ERR_NONE || ytag != WY_BSON_TAG_I32) { return WY_ERR_IMAGE; }
            wy_i32 symidx = wy_bson_get_i32(ybuf);
            if (symidx < 0 || (wy_uword) symidx >= module->symbol_count) { return WY_ERR_IMAGE; }

            wy_u8 ftag;
            const wy_u8* fbuf;
            wy_uword flen2;
            last_error = wy_bson_doc_reader_find(&mdoc, "f", &ftag, &fbuf, &flen2);
            if (last_error != WY_ERR_NONE || ftag != WY_BSON_TAG_I32) { return WY_ERR_IMAGE; }
            wy_i32 fnidx = wy_bson_get_i32(fbuf);
            if (fnidx < 0 || (wy_uword) fnidx >= module->function_count) { return WY_ERR_IMAGE; }

            out->msgs[i].name = module->symbols[symidx];
            out->msgs[i].fn = (wy_u16) fnidx;
        }
    }

    // st - always present, may be empty
    last_error = wy_bson_doc_reader_find(&doc, "st", &tag, &field, &flen);
    if (last_error != WY_ERR_NONE || tag != WY_BSON_TAG_ARRAY) { return WY_ERR_IMAGE; }
    wy_uword nstatics;
    last_error = bson_array_count_(field, flen, &nstatics);
    if (last_error != WY_ERR_NONE) { return last_error; }
    if (nstatics > 0xFFFF) { return WY_ERR_IMAGE; }
    out->nstatics = (wy_u16) nstatics;
    out->statics = WY_NULL;
    if (nstatics > 0) {
        out->statics = wy_context_gc_alloc(context, sizeof(wy_class_static_proto) * nstatics);
        if (out->statics == WY_NULL) { return WY_ERR_NOMEM; }
        wy_bson_array_reader reader;
        wy_bson_array_reader_start(&reader, field, flen);
        for (wy_uword i = 0; i < nstatics; i++) {
            wy_u8 sttag;
            const wy_u8* stbuf;
            wy_uword stlen;
            last_error = wy_bson_array_reader_get(&reader, &sttag, &stbuf, &stlen);
            if (last_error != WY_ERR_NONE || sttag != WY_BSON_TAG_DOCUMENT) { return WY_ERR_IMAGE; }

            wy_bson_doc_reader stdoc;
            last_error = wy_bson_doc_reader_start(&stdoc, stbuf, stlen);
            if (last_error != WY_ERR_NONE) { return last_error; }

            wy_u8 ntag;
            const wy_u8* nbuf;
            wy_uword nlen;
            last_error = wy_bson_doc_reader_find(&stdoc, "n", &ntag, &nbuf, &nlen);
            if (last_error != WY_ERR_NONE || ntag != WY_BSON_TAG_STRING) { return WY_ERR_IMAGE; }
            last_error = intern_bson_string_(context, nbuf, &out->statics[i].name);
            if (last_error != WY_ERR_NONE) { return last_error; }

            wy_u8 gtag;
            const wy_u8* gbuf;
            wy_uword glen;
            last_error = wy_bson_doc_reader_find(&stdoc, "g", &gtag, &gbuf, &glen);
            if (last_error != WY_ERR_NONE || gtag != WY_BSON_TAG_I32) { return WY_ERR_IMAGE; }
            wy_i32 gidx = wy_bson_get_i32(gbuf);
            if (gidx < 0 || (wy_uword) gidx >= module->global_count) { return WY_ERR_IMAGE; }
            out->statics[i].global = (wy_u16) gidx;
        }
    }

    return WY_ERR_NONE;
}

static wy_error module_load_section_classes_(wy_context* context, wy_module* module, wy_section_ref section)
{
    if (section.data == WY_NULL) { return WY_ERR_NONE; }

    wy_error last_error = bson_array_count_(section.data, section.len, &module->class_count);
    if (last_error != WY_ERR_NONE) { return last_error; }
    if (module->class_count == 0) { return WY_ERR_NONE; }

    module->class_protos = wy_context_gc_alloc(context, sizeof(wy_class_proto) * module->class_count);
    if (module->class_protos == WY_NULL) { return WY_ERR_NOMEM; }

    wy_bson_array_reader reader;
    wy_bson_array_reader_start(&reader, section.data, section.len);
    for (wy_uword i = 0; i < module->class_count; i++) {
        wy_u8 tag;
        const wy_u8* buffer;
        wy_uword len;
        last_error = wy_bson_array_reader_get(&reader, &tag, &buffer, &len);
        if (last_error != WY_ERR_NONE || tag != WY_BSON_TAG_DOCUMENT) { return WY_ERR_IMAGE; }
        last_error = decode_class_(context, module, buffer, len, &module->class_protos[i]);
        if (last_error != WY_ERR_NONE) { return last_error; }
    }
    return WY_ERR_NONE;
}

/* -------------------------------------------------------------------------
 * messages (id 7)
 * ------------------------------------------------------------------------- */

static wy_error module_load_section_messages_(wy_context* context, wy_module* module, wy_section_ref section)
{
    if (section.data == WY_NULL) { return WY_ERR_NONE; }

    wy_error last_error = bson_array_count_(section.data, section.len, &module->message_count);
    if (last_error != WY_ERR_NONE) { return last_error; }
    if (module->message_count == 0) { return WY_ERR_NONE; }

    module->messages = wy_context_gc_alloc(context, sizeof(wy_message_ref) * module->message_count);
    if (module->messages == WY_NULL) { return WY_ERR_NOMEM; }

    wy_bson_array_reader reader;
    wy_bson_array_reader_start(&reader, section.data, section.len);
    for (wy_uword i = 0; i < module->message_count; i++) {
        wy_u8 tag;
        const wy_u8* buffer;
        wy_uword len;
        last_error = wy_bson_array_reader_get(&reader, &tag, &buffer, &len);
        if (last_error != WY_ERR_NONE || tag != WY_BSON_TAG_DOCUMENT) { return WY_ERR_IMAGE; }

        wy_bson_doc_reader mdoc;
        last_error = wy_bson_doc_reader_start(&mdoc, buffer, len);
        if (last_error != WY_ERR_NONE) { return last_error; }

        wy_u8 ptag;
        const wy_u8* pbuf;
        wy_uword plen;
        last_error = wy_bson_doc_reader_find(&mdoc, "p", &ptag, &pbuf, &plen);
        if (last_error != WY_ERR_NONE || ptag != WY_BSON_TAG_ARRAY) { return WY_ERR_IMAGE; }

        wy_uword path_len;
        last_error = bson_array_count_(pbuf, plen, &path_len);
        if (last_error != WY_ERR_NONE) { return last_error; }
        if (path_len == 0 || path_len > 0xFFFF) { return WY_ERR_IMAGE; }

        module->messages[i].path_len = (wy_u16) path_len;
        module->messages[i].bound = WY_NULL;
        module->messages[i].path = wy_context_gc_alloc(context, sizeof(wy_u16) * path_len);
        if (module->messages[i].path == WY_NULL) { return WY_ERR_NOMEM; }

        wy_bson_array_reader preader;
        wy_bson_array_reader_start(&preader, pbuf, plen);
        for (wy_uword j = 0; j < path_len; j++) {
            wy_u8 sytag;
            const wy_u8* sybuf;
            wy_uword sylen;
            last_error = wy_bson_array_reader_get(&preader, &sytag, &sybuf, &sylen);
            if (last_error != WY_ERR_NONE || sytag != WY_BSON_TAG_I32) { return WY_ERR_IMAGE; }
            wy_i32 symidx = wy_bson_get_i32(sybuf);
            if (symidx < 0 || (wy_uword) symidx >= module->symbol_count) { return WY_ERR_IMAGE; }
            module->messages[i].path[j] = (wy_u16) symidx;
        }
    }
    return WY_ERR_NONE;
}

/* -------------------------------------------------------------------------
 * exports / free (ids 10, 11): "<name>": int32 global slot index
 * ------------------------------------------------------------------------- */

static wy_error module_load_name_slot_dict_(wy_context* context, wy_module* module, wy_section_ref section, wy_slot_dict* out)
{
    if (section.data == WY_NULL) { return WY_ERR_NONE; }

    wy_uword count = 0;
    {
        wy_bson_doc_reader doc;
        wy_error last_error = wy_bson_doc_reader_start(&doc, section.data, section.len);
        if (last_error != WY_ERR_NONE) { return last_error; }
        wy_u8 tag;
        const char* key;
        const wy_u8* buffer;
        wy_uword len;
        for (;;) {
            last_error = wy_bson_doc_reader_get(&doc, &tag, &key, &buffer, &len);
            if (last_error == WY_ERR_STOP_ITERATION) { break; }
            if (last_error != WY_ERR_NONE) { return WY_ERR_IMAGE; }
            count++;
        }
    }
    if (count == 0) { return WY_ERR_NONE; }

    wy_allocator* allocator = wy_context_get_machine(context)->allocator;
    wy_error last_error = wy_slot_dict_expand_f(out, allocator, count * 2);
    if (last_error != WY_ERR_NONE) { return last_error; }

    wy_bson_doc_reader doc;
    last_error = wy_bson_doc_reader_start(&doc, section.data, section.len);
    if (last_error != WY_ERR_NONE) { return last_error; }
    wy_u8 tag;
    const char* key;
    const wy_u8* buffer;
    wy_uword len;
    for (;;) {
        last_error = wy_bson_doc_reader_get(&doc, &tag, &key, &buffer, &len);
        if (last_error == WY_ERR_STOP_ITERATION) { break; }
        if (last_error != WY_ERR_NONE) { return WY_ERR_IMAGE; }
        if (tag != WY_BSON_TAG_I32) { return WY_ERR_IMAGE; }
        wy_i32 slot = wy_bson_get_i32(buffer);
        if (slot < 0 || (wy_uword) slot >= module->global_count) { return WY_ERR_IMAGE; }

        wy_symbol sym;
        last_error = wy_context_intern(context, key, wy_strlen_f(key), &sym);
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

static void finalize(wy_context* context, wy_object* self_s)
{
    wy_module* self = (wy_module*) self_s;
    wy_allocator* allocator = wy_context_get_machine(context)->allocator;

    for (wy_uword i = 0; i < self->function_count; i++) {
        wy_context_gc_free(context, self->functions[i].params);
        wy_context_gc_free(context, self->functions[i].dispatch_slots);
    }
    wy_context_gc_free(context, self->functions);

    for (wy_uword i = 0; i < self->class_count; i++) {
        wy_context_gc_free(context, self->class_protos[i].slots);
        wy_context_gc_free(context, self->class_protos[i].statics);
    }
    wy_context_gc_free(context, self->class_protos);
    wy_context_gc_free(context, self->classes);

    for (wy_uword i = 0; i < self->message_count; i++) {
        wy_context_gc_free(context, self->messages[i].path);
    }
    wy_context_gc_free(context, self->messages);

    wy_context_gc_free(context, self->globals);
    wy_context_gc_free(context, self->fill_layer);
    wy_context_gc_free(context, self->fill_source);
    wy_context_gc_free(context, self->statics);
    wy_context_gc_free(context, self->symbols);
    wy_context_gc_free(context, self->wildcards);

    wy_slot_finalize_f(&self->exports, allocator);
    wy_slot_finalize_f(&self->free_names, allocator);

    if (self->owns_image) {
        wy_context_gc_free(context, (void*) self->image);
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
            if (idx >= self->static_count) { return WY_ERR_STOP_ITERATION; }
            wa->data[1].uword = idx + 1;
            if (wy_value_is_gc_ref_f(self->statics[idx])) {
                *child = self->statics[idx].data.gc_object;
                return WY_ERR_NONE;
            }
            continue;
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
