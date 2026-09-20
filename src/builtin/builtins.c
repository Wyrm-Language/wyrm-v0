#include <wyrm/builtins.h>

#include <wyrm/context.h>
#include <wyrm/machine.h>
#include <wyrm/module.h>
#include <wyrm/native.h>
#include <wyrm/string.h>
#include <wyrm/sys/string.h>
#include <wyrm/value.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * Value -> text formatting (design_c_vm.md §5, `_to_str` in
 * pypoc/wypoc/wyrm_builtins.py / wyrm_eval_parse_tree.py).
 * ------------------------------------------------------------------------- */

enum { WY_BUILTIN_FMT_BUF_SIZE = 64 };

/**
 * Shortest decimal string that round-trips back to `value` through
 * `strtod` (matches Python's `repr(float)`, which is what the pypoc
 * reference's `_to_str` relies on via `str(float)`).
 *
 * Tries increasing `%.*g` precision from 1 to 17 significant digits,
 * stopping at the first that reads back bit-identical. 17 significant
 * digits always round-trips an IEEE-754 binary64, so this always
 * terminates with a match. Ensures the result still "looks like" a float
 * (has a '.', 'e', or is inf/nan) so `1.0` never prints as the bare `1`.
 */
static wy_uword format_float_(double value, char* buf, wy_uword buf_size)
{
    if (isnan(value)) {
        int n = snprintf(buf, buf_size, "nan");
        return (n > 0) ? (wy_uword) n : 0;
    }
    if (isinf(value)) {
        int n = snprintf(buf, buf_size, value < 0 ? "-inf" : "inf");
        return (n > 0) ? (wy_uword) n : 0;
    }

    int written = 0;
    for (int precision = 1; precision <= 17; precision++) {
        int n = snprintf(buf, buf_size, "%.*g", precision, value);
        if (n <= 0 || (wy_uword) n >= buf_size) { continue; }
        written = n;
        if (strtod(buf, WY_NULL) == value) { break; }
    }
    if (written <= 0) { return 0; }

    bool has_float_marker = false;
    for (int i = 0; i < written; i++) {
        char c = buf[i];
        if (c == '.' || c == 'e' || c == 'E' || c == 'n' /* nan/inf, defensive */) {
            has_float_marker = true;
            break;
        }
    }
    if (!has_float_marker && (wy_uword)(written + 2) < buf_size) {
        buf[written] = '.';
        buf[written + 1] = '0';
        written += 2;
        buf[written] = '\0';
    }
    return (wy_uword) written;
}

/**
 * Format one value into `buf` (capacity `buf_size`), or for STR values hand
 * back a direct pointer into the string's own storage via `*text`/`*len`
 * (no copy). Returns the number of bytes to write starting at `*text`.
 */
static wy_uword format_value_(wy_value value, char* buf, wy_uword buf_size, const char** text)
{
    switch (value.type) {
    case WY_TYPE_TAG_STR: {
        wy_string* str = value.data.str;
        if (str == WY_NULL) { *text = ""; return 0; }
        *text = str->str;
        return str->len;
    }
    case WY_TYPE_TAG_NIL:
        *text = "nil";
        return 3;
    case WY_TYPE_TAG_BOOL:
        *text = value.data.flag ? "true" : "false";
        return value.data.flag ? 4 : 5;
    case WY_TYPE_TAG_WORD: {
        int n = snprintf(buf, buf_size, "%lld", (long long) value.data.word);
        *text = buf;
        return (n > 0) ? (wy_uword) n : 0;
    }
    case WY_TYPE_TAG_UWORD: {
        int n = snprintf(buf, buf_size, "%llu", (unsigned long long) value.data.uword);
        *text = buf;
        return (n > 0) ? (wy_uword) n : 0;
    }
    case WY_TYPE_TAG_FLOAT: {
        wy_uword n = format_float_((double) value.data.fp, buf, buf_size);
        *text = buf;
        return n;
    }
    case WY_TYPE_TAG_SYMBOL: {
        /* Best effort, not exercised by the epic 2 fixtures: symbols print
         * their own text, like a bare identifier. */
        const char* sym = value.data.symtab_entry;
        *text = (sym != WY_NULL) ? sym : "";
        return wy_strlen_f(*text);
    }
    case WY_TYPE_TAG_ERROR:
        if (value.data.gc_object == WY_NULL) {
            /* Unset: not exercised by the epic 2 fixtures, low priority. */
            *text = "Unset";
            return 5;
        }
        *text = "error";
        return 5;
    default:
        /* Best effort for anything else (other heap objects): not
         * exercised by the epic 2 fixtures, low priority. */
        *text = "<object>";
        return 8;
    }
}

/** Write `len` bytes through the context's output hook, a no-op if unset. */
static void io_write_(wy_context* context, const char* bytes, wy_uword len)
{
    if (context->io.write == WY_NULL || len == 0) { return; }
    context->io.write(context, bytes, len, context->io.ud);
}

static void io_write_str_(wy_context* context, const char* cstr)
{
    io_write_(context, cstr, wy_strlen_f(cstr));
}

/** Shared body for `print`/`println`: space-join every arg's rendering. */
static wy_error write_joined_(wy_context* context, wy_value* args, wy_uword argc, bool trailing_newline)
{
    char buf[WY_BUILTIN_FMT_BUF_SIZE];
    for (wy_uword i = 0; i < argc; i++) {
        if (i > 0) { io_write_str_(context, " "); }
        const char* text = WY_NULL;
        wy_uword len = format_value_(args[i], buf, sizeof(buf), &text);
        io_write_(context, text, len);
    }
    if (trailing_newline) { io_write_str_(context, "\n"); }
    return WY_ERR_NONE;
}

/*
 * `println(a, b, ...)`: each argument space-joined and stringified per
 * format_value_, followed by a trailing "\n". Confirmed byte-exact against
 * test/bytecode/{arith,control_flow,multiret}.out.
 */
static wy_error builtin_println_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    wy_error err = write_joined_(context, args, argc, true);
    for (wy_uword i = 0; i < nres; i++) { out[i] = wy_value_nil(); }
    return err;
}

/*
 * `print(a, b, ...)`: same space-joined rendering as println, no trailing
 * newline. Confirmed byte-exact (including the space-join, contra a first
 * reading of the newline placement) against test/bytecode/hello_3.out:
 * `print("Magic Number: ", magic_number, "\n")` -> `Magic Number:  3 \n`
 * (the trailing "\n" is a literal argument, joined with its own leading
 * space like any other argument).
 */
static wy_error builtin_print_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    wy_error err = write_joined_(context, args, argc, false);
    for (wy_uword i = 0; i < nres; i++) { out[i] = wy_value_nil(); }
    return err;
}

/* -------------------------------------------------------------------------
 * Module assembly
 * ------------------------------------------------------------------------- */

enum { WY_BUILTINS_COUNT = 3 };

typedef struct builtin_leaf_entry_
{
    const char* name;
    wy_u8 min_argc;
    wy_u8 max_argc;
    wy_native_leaf_fn fn;
} builtin_leaf_entry_;

static const builtin_leaf_entry_ leaf_builtins_[] = {
    { "println", 0, 255, builtin_println_ },
    { "print",   0, 255, builtin_print_ },
};

wy_error wy_builtins_new(wy_context* context, wy_module** out)
{
    if (context == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }

    wy_module* module = wy_module_new_f(context);
    if (module == WY_NULL) { return WY_ERR_NOMEM; }
    module->state = WY_MODULE_BUILTIN;

    module->global_count = WY_BUILTINS_COUNT;
    module->globals = wy_context_gc_alloc(context, sizeof(wy_value) * WY_BUILTINS_COUNT);
    module->fill_layer = wy_context_gc_alloc(context, sizeof(wy_u8) * WY_BUILTINS_COUNT);
    module->fill_source = wy_context_gc_alloc(context, sizeof(wy_symbol) * WY_BUILTINS_COUNT);
    if (module->globals == WY_NULL || module->fill_layer == WY_NULL || module->fill_source == WY_NULL) {
        return WY_ERR_NOMEM;
    }
    for (wy_uword i = 0; i < WY_BUILTINS_COUNT; i++) {
        module->globals[i] = wy_value_unset();
        module->fill_layer[i] = 0;
        module->fill_source[i] = WY_NULL;
    }

    wy_allocator* allocator = wy_context_get_machine(context)->allocator;
    wy_error err = wy_slot_dict_expand_f(&module->exports, allocator, WY_BUILTINS_COUNT * 2);
    if (err != WY_ERR_NONE) { return err; }

    wy_uword slot = 0;

    for (wy_uword i = 0; i < sizeof(leaf_builtins_) / sizeof(leaf_builtins_[0]); i++) {
        const builtin_leaf_entry_* entry = &leaf_builtins_[i];

        wy_symbol sym;
        err = wy_context_intern(context, entry->name, wy_strlen_f(entry->name), &sym);
        if (err != WY_ERR_NONE) { return err; }

        wy_native* native = WY_NULL;
        err = wy_native_leaf_new(context, sym, entry->min_argc, entry->max_argc, entry->fn, &native);
        if (err != WY_ERR_NONE) { return err; }

        module->globals[slot] = wy_value_object(WY_TYPE_TAG_NATIVE, (wy_object*) native);
        err = wy_slot_dict_add_entry(&module->exports, sym, slot);
        if (err != WY_ERR_NONE) { return err; }
        slot++;
    }

    {
        wy_symbol sym;
        err = wy_context_intern(context, "nil", 3, &sym);
        if (err != WY_ERR_NONE) { return err; }

        module->globals[slot] = wy_value_nil();
        err = wy_slot_dict_add_entry(&module->exports, sym, slot);
        if (err != WY_ERR_NONE) { return err; }
        slot++;
    }

    WY_ASSERT(slot == WY_BUILTINS_COUNT);

    *out = module;
    return WY_ERR_NONE;
}
