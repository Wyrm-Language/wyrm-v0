#include <wyrm/builtins.h>

#include <wyrm/bytes.h>
#include <wyrm/bound_msg.h>
#include <wyrm/class.h>
#include <wyrm/context.h>
#include <wyrm/coroutine.h>
#include <wyrm/dict.h>
#include <wyrm/error.h>
#include <wyrm/image.h>
#include <wyrm/instance.h>
#include <wyrm/iter.h>
#include <wyrm/link.h>
#include <wyrm/list.h>
#include <wyrm/machine.h>
#include <wyrm/message.h>
#include <wyrm/module.h>
#include <wyrm/native.h>
#include <wyrm/pair.h>
#include <wyrm/string.h>
#include <wyrm/sys/string.h>
#include <wyrm/tuple.h>
#include <wyrm/value.h>
#include <wyrm/vm.h>

#include "../vm_internal.h"

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
 * `strtod`, laid out like Python's `repr(float)` (what the wypoc reference
 * prints): positional for decimal exponents -4..15, otherwise scientific,
 * and always with a '.' or an exponent so `1.0` never prints as the bare `1`.
 *
 * With `d2` set the exponent is spelled the D2 way (`1e20`, `1.5e-7`), without
 * Python's sign and zero padding (`1e+20`, `1.5e-07`).
 */
static wy_uword format_float_(double value, bool d2, char* buf, wy_uword buf_size)
{
    if (isnan(value)) {
        int n = snprintf(buf, buf_size, "nan");
        return (n > 0) ? (wy_uword) n : 0;
    }
    if (isinf(value)) {
        int n = snprintf(buf, buf_size, value < 0 ? "-inf" : "inf");
        return (n > 0) ? (wy_uword) n : 0;
    }

    /* Shortest significant digits: "%.*e" at increasing precision until the
     * text reads back bit-identical (17 digits always do for binary64). */
    char sci[32];
    for (int precision = 0; precision <= 16; precision++) {
        int n = snprintf(sci, sizeof(sci), "%.*e", precision, value);
        if (n <= 0 || (wy_uword) n >= sizeof(sci)) { return 0; }
        if (strtod(sci, WY_NULL) == value) { break; }
    }

    /* Split "-d.ddde+XX" into sign, digits and decimal exponent. */
    const char* cursor = sci;
    bool negative = false;
    if (*cursor == '-') { negative = true; cursor++; }
    char digits[20];
    wy_uword ndigits = 0;
    for (; *cursor != 'e' && *cursor != '\0'; cursor++) {
        if (*cursor != '.' && ndigits < sizeof(digits)) { digits[ndigits++] = *cursor; }
    }
    long exponent = (*cursor == 'e') ? strtol(cursor + 1, WY_NULL, 10) : 0;
    while (ndigits > 1 && digits[ndigits - 1] == '0') { ndigits--; }

    char out[48];
    wy_uword len = 0;
    if (negative) { out[len++] = '-'; }
    if (exponent >= -4 && exponent < 16) {
        if (exponent < 0) {
            out[len++] = '0';
            out[len++] = '.';
            for (long i = -1; i > exponent; i--) { out[len++] = '0'; }
            for (wy_uword i = 0; i < ndigits; i++) { out[len++] = digits[i]; }
        } else {
            wy_uword int_digits = (wy_uword) exponent + 1;
            for (wy_uword i = 0; i < int_digits; i++) { out[len++] = (i < ndigits) ? digits[i] : '0'; }
            out[len++] = '.';
            if (ndigits > int_digits) {
                for (wy_uword i = int_digits; i < ndigits; i++) { out[len++] = digits[i]; }
            } else {
                out[len++] = '0';
            }
        }
        out[len] = '\0';
    } else {
        out[len++] = digits[0];
        if (ndigits > 1) {
            out[len++] = '.';
            for (wy_uword i = 1; i < ndigits; i++) { out[len++] = digits[i]; }
        }
        int n = snprintf(out + len, sizeof(out) - len, d2 ? "e%ld" : "e%+03ld", exponent);
        if (n <= 0) { return 0; }
        len += (wy_uword) n;
    }
    if (len >= buf_size) { return 0; }
    wy_memcpy(buf, out, len + 1);
    return len;
}

/**
 * Where format_value_ sends its text: the context's IO hook (`to_io`), or a
 * buffer. With `buf` NULL a buffer sink only counts, so `str()` can size the
 * string in a first pass and fill it in a second.
 */
typedef struct format_sink_
{
    wy_context* context;
    bool to_io;
    char* buf;
    wy_uword len;
} format_sink_;

/** Write `len` bytes to the sink. The IO hook is a no-op if unset. */
static void sink_write_(format_sink_* sink, const char* bytes, wy_uword len)
{
    if (len == 0) { return; }
    if (sink->to_io) {
        wy_context* context = sink->context;
        if (context->io.write != WY_NULL) { context->io.write(context, bytes, len, context->io.ud); }
        return;
    }
    if (sink->buf != WY_NULL) { wy_memcpy(sink->buf + sink->len, bytes, len); }
    sink->len += len;
}

static void sink_write_str_(format_sink_* sink, const char* cstr)
{
    if (cstr == WY_NULL) return;
    sink_write_(sink, cstr, wy_strlen_f(cstr));
}

/**
 * A str in D2 form: double-quoted, with \\ \" \n \r \t escaped and any
 * other control character or byte that isn't valid UTF-8 written as \xHH.
 */
static void write_quoted_(format_sink_* sink, const char* str, wy_uword len)
{
    sink_write_str_(sink, "\"");
    wy_uword offset = 0;
    while (offset < len) {
        wy_u32 cp = 0;
        wy_uword seq_len = wy_utf8_decode_f(str, len, offset, &cp);
        wy_u8 byte = (wy_u8) str[offset];
        const char* escape = WY_NULL;
        switch (byte) {
        case '\\': escape = "\\\\"; break;
        case '"': escape = "\\\""; break;
        case '\n': escape = "\\n"; break;
        case '\r': escape = "\\r"; break;
        case '\t': escape = "\\t"; break;
        default: break;
        }
        if (escape != WY_NULL) {
            sink_write_str_(sink, escape);
        } else if (byte < 0x20 || byte == 0x7F || (seq_len == 1 && byte >= 0x80)) {
            char hex[8];
            int n = snprintf(hex, sizeof(hex), "\\x%02X", (unsigned) byte);
            sink_write_(sink, hex, (n > 0) ? (wy_uword) n : 0);
        } else {
            sink_write_(sink, str + offset, seq_len);
        }
        offset += seq_len;
    }
    sink_write_str_(sink, "\"");
}

/**
 * How format_value_ renders a value, by where it sits:
 *
 * - PLAIN: an argument of println/str, or inside a container that is one;
 *   strings print raw.
 * - D2: an element of a pair list. The pair list prints in the D2 Scheme
 *   form shared with the tree dumps (wyrm::sexp_print, wypoc's
 *   sexp_print.py): `(1 (2 3) a "s")`, `(1 . 2)`, nil as `()`, strings
 *   quoted and escaped, a float's exponent as `1e20`.
 * - REPR: inside a list, tuple or dict that is itself inside a pair list:
 *   wypoc's repr form, strings quoted, symbols as `'sym`, nil as `nil`.
 */
typedef enum format_mode_ { FORMAT_PLAIN_, FORMAT_D2_, FORMAT_REPR_ } format_mode_;

static wy_error format_value_f(format_sink_* sink, wy_value value, format_mode_ mode)
{
    /* Elements of a list, tuple or dict. */
    format_mode_ inner = (mode == FORMAT_PLAIN_) ? FORMAT_PLAIN_ : FORMAT_REPR_;
    char buf[WY_BUILTIN_FMT_BUF_SIZE];
    wy_uword len = 0;

    switch (value.type) {
    case WY_TYPE_TAG_STR: {
        wy_string* str = value.data.str;
        if (str == WY_NULL) break;
        if (mode != FORMAT_PLAIN_) write_quoted_(sink, str->str, str->len);
        else sink_write_(sink, str->str, str->len);
        break;
    }
    case WY_TYPE_TAG_NIL: sink_write_str_(sink, (mode == FORMAT_D2_) ? "()" : "nil"); break;
    case WY_TYPE_TAG_BOOL: sink_write_str_(sink, value.data.flag ? "true" : "false"); break;
    case WY_TYPE_TAG_WORD:
        len = (wy_uword) snprintf(buf, sizeof(buf), "%lld", (long long) value.data.word);
        sink_write_(sink, buf, len);
        break;
    case WY_TYPE_TAG_UWORD:
        len = (wy_uword) snprintf(buf, sizeof(buf), "%llu", (unsigned long long) value.data.uword);
        sink_write_(sink, buf, len);
        break;
    case WY_TYPE_TAG_FLOAT:
        len = format_float_((double) value.data.fp, mode == FORMAT_D2_, buf, sizeof(buf));
        sink_write_(sink, buf, len);
        break;
    case WY_TYPE_TAG_SYMBOL:
        if (mode == FORMAT_REPR_) sink_write_str_(sink, "'");
        sink_write_str_(sink, value.data.symtab_entry);
        break;
    case WY_TYPE_TAG_ERROR:
        if (value.data.gc_object == WY_NULL) sink_write_str_(sink, "Unset");
        else sink_write_str_(sink, "error");
        break;
    case WY_TYPE_TAG_LIST: {
        wy_list* list = (wy_list*) value.data.gc_object;
        sink_write_str_(sink, "[");
        for (wy_uword i = 0; i < list->count; i++) {
            if (i > 0) sink_write_str_(sink, ", ");
            format_value_f(sink, list->items[i], inner);
        }
        sink_write_str_(sink, "]");
        break;
    }
    case WY_TYPE_TAG_TUPLE: {
        wy_tuple* tup = (wy_tuple*) value.data.gc_object;
        sink_write_str_(sink, "(");
        for (wy_uword i = 0; i < tup->count; i++) {
            if (i > 0) sink_write_str_(sink, ", ");
            format_value_f(sink, tup->items[i], inner);
        }
        sink_write_str_(sink, ")");
        break;
    }
    case WY_TYPE_TAG_TABLE: {
        wy_dict* dict = (wy_dict*) value.data.gc_object;
        sink_write_str_(sink, "{");
        for (wy_uword i = 0; i < dict->count; i++) {
            if (i > 0) sink_write_str_(sink, ", ");
            format_value_f(sink, dict->dense[i].key, inner);
            sink_write_str_(sink, ": ");
            format_value_f(sink, dict->dense[i].value, inner);
        }
        sink_write_str_(sink, "}");
        break;
    }
    case WY_TYPE_TAG_PAIR: {
        /* Walk the cdr chain iteratively; only nested elements recurse. */
        wy_value cursor = value;
        sink_write_str_(sink, "(");
        bool first = true;
        while (cursor.type == WY_TYPE_TAG_PAIR) {
            wy_pair* p = (wy_pair*) cursor.data.gc_object;
            if (!first) sink_write_str_(sink, " ");
            format_value_f(sink, p->car, FORMAT_D2_);
            first = false;
            cursor = p->cdr;
        }
        if (cursor.type != WY_TYPE_TAG_NIL) {
            sink_write_str_(sink, " . ");
            format_value_f(sink, cursor, FORMAT_D2_);
        }
        sink_write_str_(sink, ")");
        break;
    }
    default:
        sink_write_str_(sink, "<object>");
        break;
    }
    return WY_ERR_NONE;
}

/** `value` rendered by format_value_ into a new string (two passes: size, then fill). */
static wy_error format_to_string_(wy_context* context, wy_value value, wy_string** out_str)
{
    format_sink_ sink = { context, false, WY_NULL, 0 };
    format_value_f(&sink, value, FORMAT_PLAIN_);
    wy_uword total = sink.len;
    char* text = wy_context_gc_alloc(context, total + 1);
    if (text == WY_NULL) { return WY_ERR_NOMEM; }
    sink.buf = text;
    sink.len = 0;
    format_value_f(&sink, value, FORMAT_PLAIN_);
    wy_error err = wy_string_new(context, text, total, out_str);
    wy_context_gc_free(context, text);
    return err;
}

/** Shared body for `print`/`println`: space-join every arg's rendering. */
static wy_error write_joined_(wy_context* context, wy_value* args, wy_uword argc, bool trailing_newline)
{
    format_sink_ sink = { context, true, WY_NULL, 0 };
    for (wy_uword i = 0; i < argc; i++) {
        if (i > 0) { sink_write_str_(&sink, " "); }
        format_value_f(&sink, args[i], FORMAT_PLAIN_);
    }
    if (trailing_newline) { sink_write_str_(&sink, "\n"); }
    return WY_ERR_NONE;
}

/*
 * `println(a, b, ...)`: each argument space-joined and stringified per
 * format_value_, followed by a trailing "\n". Confirmed byte-exact against
 * test/bytecode/{arith,control_flow,multiret}.out.
 */
wy_error wy_builtin_println_body_f(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    wy_error err = write_joined_(context, args, argc, true);
    for (wy_uword i = 0; i < nres; i++) { out[i] = wy_value_nil(); }
    return err;
}

static wy_error builtin_println_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    return wy_builtin_println_body_f(context, args, argc, out, nres);
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
 * Error values and pair/list/tuple/dict leaf natives (epic 3/M3;
 * pypoc/wypoc/wyrm_builtins.py lines 380-599, 629-634, install() 788-859).
 * A bad argument to car/cdr answers a catchable error *value* (matching
 * car/cdr's own reference docstrings); every other native here raises a
 * host TypeError/ValueError in the reference, which has no wyrm-level
 * catch - it becomes a VM fault (a non-NONE wy_error return), same as
 * "wrong argument count" or "native call failed" elsewhere in this file.
 * ------------------------------------------------------------------------- */

static wy_error write_error_value_(wy_context* context, const char* message, wy_value* out)
{
    wy_string* what = WY_NULL;
    wy_error err = wy_string_strdup(context, message, &what);
    if (err != WY_ERR_NONE) { return err; }

    wy_error_obj* obj = WY_NULL;
    err = wy_error_obj_new(context, context->error_class, what, wy_value_nil(), &obj);
    if (err != WY_ERR_NONE) { return err; }

    *out = wy_value_object(WY_TYPE_TAG_ERROR, (wy_object*) obj);
    return WY_ERR_NONE;
}

static const char* type_name_(wy_value v)
{
    switch (v.type) {
    case WY_TYPE_TAG_NIL: return "nil";
    case WY_TYPE_TAG_BOOL: return "bool";
    case WY_TYPE_TAG_WORD: case WY_TYPE_TAG_UWORD: return "int";
    case WY_TYPE_TAG_FLOAT: return "float";
    case WY_TYPE_TAG_STR: return "str";
    case WY_TYPE_TAG_SYMBOL: return "sym";
    case WY_TYPE_TAG_LIST: return "list";
    case WY_TYPE_TAG_TUPLE: return "tuple";
    case WY_TYPE_TAG_TABLE: return "dict";
    case WY_TYPE_TAG_PAIR: return "pair";
    case WY_TYPE_TAG_ERROR: return "error";
    default: return "object";
    }
}

static wy_error builtin_cons_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(argc);
    wy_pair* p = wy_pair_cons_f(context, args[0], args[1]);
    if (p == WY_NULL) { return WY_ERR_NOMEM; }
    out[0] = wy_value_object(WY_TYPE_TAG_PAIR, (wy_object*) p);
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

static wy_error builtin_car_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(argc);
    wy_value x = args[0];
    wy_error err = WY_ERR_NONE;
    if (x.type == WY_TYPE_TAG_PAIR) {
        out[0] = ((wy_pair*) x.data.gc_object)->car;
    } else if (x.type == WY_TYPE_TAG_NIL) {
        err = write_error_value_(context, "car: cannot take car of '() (the empty list)", &out[0]);
    } else if (x.type == WY_TYPE_TAG_LIST) {
        wy_list* l = (wy_list*) x.data.gc_object;
        if (l->count == 0) { err = write_error_value_(context, "car: empty list/array/string has no first element", &out[0]); }
        else { out[0] = l->items[0]; }
    } else if (x.type == WY_TYPE_TAG_TUPLE) {
        wy_tuple* t = (wy_tuple*) x.data.gc_object;
        if (t->count == 0) { err = write_error_value_(context, "car: empty list/array/string has no first element", &out[0]); }
        else { out[0] = t->items[0]; }
    } else if (x.type == WY_TYPE_TAG_STR) {
        wy_string* s = x.data.str;
        if (s->len == 0) {
            err = write_error_value_(context, "car: empty list/array/string has no first element", &out[0]);
        } else {
            wy_uword seq_len = wy_utf8_offset_at_f(s->str, s->len, 1);
            wy_string* ch = WY_NULL;
            err = wy_string_new(context, s->str, seq_len, &ch);
            if (err == WY_ERR_NONE) { out[0] = wy_value_object(WY_TYPE_TAG_STR, (wy_object*) ch); }
        }
    } else {
        char msg[96];
        snprintf(msg, sizeof(msg), "car: not a pair/list/array/string (got %s)", type_name_(x));
        err = write_error_value_(context, msg, &out[0]);
    }
    if (err != WY_ERR_NONE) { return err; }
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

static wy_error builtin_cdr_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(argc);
    wy_value x = args[0];
    wy_error err = WY_ERR_NONE;
    if (x.type == WY_TYPE_TAG_PAIR) {
        out[0] = ((wy_pair*) x.data.gc_object)->cdr;
    } else if (x.type == WY_TYPE_TAG_NIL) {
        err = write_error_value_(context, "cdr: cannot take cdr of '() (the empty list)", &out[0]);
    } else if (x.type == WY_TYPE_TAG_LIST) {
        wy_list* l = (wy_list*) x.data.gc_object;
        if (l->count == 0) {
            err = write_error_value_(context, "cdr: empty list/array/string has no rest", &out[0]);
        } else {
            wy_list* rest = WY_NULL;
            err = wy_list_new(context, l->count - 1, &rest);
            for (wy_uword i = 1; err == WY_ERR_NONE && i < l->count; i++) {
                err = wy_list_push(context, rest, l->items[i]);
            }
            if (err == WY_ERR_NONE) { out[0] = wy_value_object(WY_TYPE_TAG_LIST, (wy_object*) rest); }
        }
    } else if (x.type == WY_TYPE_TAG_TUPLE) {
        wy_tuple* t = (wy_tuple*) x.data.gc_object;
        if (t->count == 0) {
            err = write_error_value_(context, "cdr: empty list/array/string has no rest", &out[0]);
        } else {
            wy_tuple* rest = WY_NULL;
            err = wy_tuple_new(context, &t->items[1], t->count - 1, &rest);
            if (err == WY_ERR_NONE) { out[0] = wy_value_object(WY_TYPE_TAG_TUPLE, (wy_object*) rest); }
        }
    } else if (x.type == WY_TYPE_TAG_STR) {
        wy_string* s = x.data.str;
        if (s->len == 0) {
            err = write_error_value_(context, "cdr: empty list/array/string has no rest", &out[0]);
        } else {
            wy_uword byte_off = wy_utf8_offset_at_f(s->str, s->len, 1);
            wy_string* rest = WY_NULL;
            err = wy_string_new(context, s->str + byte_off, s->len - byte_off, &rest);
            if (err == WY_ERR_NONE) { out[0] = wy_value_object(WY_TYPE_TAG_STR, (wy_object*) rest); }
        }
    } else {
        char msg[96];
        snprintf(msg, sizeof(msg), "cdr: not a pair/list/array/string (got %s)", type_name_(x));
        err = write_error_value_(context, msg, &out[0]);
    }
    if (err != WY_ERR_NONE) { return err; }
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

static wy_error builtin_reverse_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(argc);
    wy_value node = args[0];
    wy_value chain = wy_value_nil();
    while (node.type == WY_TYPE_TAG_PAIR) {
        wy_pair* p = (wy_pair*) node.data.gc_object;
        wy_pair* cell = wy_pair_cons_f(context, p->car, chain);
        if (cell == WY_NULL) { return WY_ERR_NOMEM; }
        chain = wy_value_object(WY_TYPE_TAG_PAIR, (wy_object*) cell);
        node = p->cdr;
    }
    out[0] = chain;
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

static wy_error builtin_nreverse_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(context); WY_UNUSED(argc);
    wy_value container = args[0];
    if (container.type == WY_TYPE_TAG_LIST) {
        wy_list* l = (wy_list*) container.data.gc_object;
        if (l->count > 0) {
            for (wy_uword i = 0, j = l->count - 1; i < j; i++, j--) {
                wy_value tmp = l->items[i];
                l->items[i] = l->items[j];
                l->items[j] = tmp;
            }
        }
        out[0] = container;
    } else if (container.type == WY_TYPE_TAG_NIL) {
        out[0] = container;
    } else if (container.type == WY_TYPE_TAG_PAIR) {
        wy_value prev = wy_value_nil();
        wy_value node = container;
        while (node.type == WY_TYPE_TAG_PAIR) {
            wy_pair* p = (wy_pair*) node.data.gc_object;
            wy_value next = p->cdr;
            p->cdr = prev;
            prev = node;
            node = next;
        }
        out[0] = prev;
    } else {
        return WY_ERR_BAD_TYPE;
    }
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

static wy_error builtin_set_car_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(context); WY_UNUSED(argc);
    if (args[0].type != WY_TYPE_TAG_PAIR) { return WY_ERR_BAD_TYPE; }
    ((wy_pair*) args[0].data.gc_object)->car = args[1];
    out[0] = args[0];
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

static wy_error builtin_set_cdr_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(context); WY_UNUSED(argc);
    if (args[0].type != WY_TYPE_TAG_PAIR) { return WY_ERR_BAD_TYPE; }
    ((wy_pair*) args[0].data.gc_object)->cdr = args[1];
    out[0] = args[0];
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

static wy_error builtin_tuple_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    wy_tuple* t = WY_NULL;
    wy_error err = wy_tuple_new(context, args, argc, &t);
    if (err != WY_ERR_NONE) { return err; }
    out[0] = wy_value_object(WY_TYPE_TAG_TUPLE, (wy_object*) t);
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

static wy_error builtin_copy_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(argc);
    wy_value x = args[0];
    wy_error err = WY_ERR_NONE;
    switch (x.type) {
    case WY_TYPE_TAG_PAIR: {
        wy_pair* p = (wy_pair*) x.data.gc_object;
        wy_pair* np = wy_pair_cons_f(context, p->car, p->cdr);
        if (np == WY_NULL) { return WY_ERR_NOMEM; }
        out[0] = wy_value_object(WY_TYPE_TAG_PAIR, (wy_object*) np);
        break;
    }
    case WY_TYPE_TAG_LIST: {
        wy_list* l = (wy_list*) x.data.gc_object;
        wy_list* nl = WY_NULL;
        err = wy_list_new(context, l->count, &nl);
        for (wy_uword i = 0; err == WY_ERR_NONE && i < l->count; i++) {
            err = wy_list_push(context, nl, l->items[i]);
        }
        if (err != WY_ERR_NONE) { return err; }
        out[0] = wy_value_object(WY_TYPE_TAG_LIST, (wy_object*) nl);
        break;
    }
    case WY_TYPE_TAG_TUPLE: {
        wy_tuple* t = (wy_tuple*) x.data.gc_object;
        wy_tuple* nt = WY_NULL;
        err = wy_tuple_new(context, t->items, t->count, &nt);
        if (err != WY_ERR_NONE) { return err; }
        out[0] = wy_value_object(WY_TYPE_TAG_TUPLE, (wy_object*) nt);
        break;
    }
    case WY_TYPE_TAG_TABLE: {
        wy_dict* d = (wy_dict*) x.data.gc_object;
        wy_dict* nd = WY_NULL;
        err = wy_dict_new(context, &nd);
        for (wy_uword i = 0; err == WY_ERR_NONE && i < d->count; i++) {
            err = wy_dict_set(context, nd, d->dense[i].key.type, d->dense[i].key.data,
                d->dense[i].value.type, d->dense[i].value.data);
        }
        if (err != WY_ERR_NONE) { return err; }
        out[0] = wy_value_object(WY_TYPE_TAG_TABLE, (wy_object*) nd);
        break;
    }
    case WY_TYPE_TAG_NIL: case WY_TYPE_TAG_BOOL: case WY_TYPE_TAG_WORD:
    case WY_TYPE_TAG_UWORD: case WY_TYPE_TAG_FLOAT: case WY_TYPE_TAG_STR:
        out[0] = x;
        break;
    default:
        return WY_ERR_BAD_TYPE;
    }
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

static wy_error builtin_substr_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(argc);
    if (args[0].type != WY_TYPE_TAG_STR) { return WY_ERR_BAD_TYPE; }
    if ((args[1].type != WY_TYPE_TAG_WORD && args[1].type != WY_TYPE_TAG_UWORD)
        || (args[2].type != WY_TYPE_TAG_WORD && args[2].type != WY_TYPE_TAG_UWORD)) {
        return WY_ERR_BAD_TYPE;
    }
    wy_string* s = args[0].data.str;
    wy_word start = args[1].type == WY_TYPE_TAG_UWORD ? (wy_word) args[1].data.uword : args[1].data.word;
    wy_word count = args[2].type == WY_TYPE_TAG_UWORD ? (wy_word) args[2].data.uword : args[2].data.word;
    wy_uword ncp = wy_utf8_codepoint_count_f(s->str, s->len);

    wy_word clip_start = start < 0 ? 0 : start;
    if ((wy_uword) clip_start > ncp) { clip_start = (wy_word) ncp; }
    wy_word end = count < 0 ? clip_start : clip_start + count;
    if ((wy_uword) end > ncp) { end = (wy_word) ncp; }
    if (end < clip_start) { end = clip_start; }

    wy_uword byte_start = wy_utf8_offset_at_f(s->str, s->len, (wy_uword) clip_start);
    wy_uword byte_end = wy_utf8_offset_at_f(s->str, s->len, (wy_uword) end);
    wy_string* sub = WY_NULL;
    wy_error err = wy_string_new(context, s->str + byte_start, byte_end - byte_start, &sub);
    if (err != WY_ERR_NONE) { return err; }
    out[0] = wy_value_object(WY_TYPE_TAG_STR, (wy_object*) sub);
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

static wy_error builtin_append_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(argc);
    if (args[0].type != WY_TYPE_TAG_LIST) { return WY_ERR_BAD_TYPE; }
    wy_error err = wy_list_push(context, (wy_list*) args[0].data.gc_object, args[1]);
    if (err != WY_ERR_NONE) { return err; }
    out[0] = args[0];
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

static wy_error builtin_resize_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(argc);
    if (args[0].type != WY_TYPE_TAG_LIST) { return WY_ERR_BAD_TYPE; }
    if (args[1].type != WY_TYPE_TAG_WORD && args[1].type != WY_TYPE_TAG_UWORD) { return WY_ERR_BAD_TYPE; }
    wy_word count = args[1].type == WY_TYPE_TAG_UWORD ? (wy_word) args[1].data.uword : args[1].data.word;
    if (count < 0) { return WY_ERR_RANGE; }
    wy_list* l = (wy_list*) args[0].data.gc_object;
    if ((wy_uword) count <= l->count) {
        l->count = (wy_uword) count;
    } else {
        while (l->count < (wy_uword) count) {
            wy_error err = wy_list_push(context, l, wy_value_unset());
            if (err != WY_ERR_NONE) { return err; }
        }
    }
    out[0] = args[0];
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

static wy_error builtin_expand_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(argc);
    if (args[0].type != WY_TYPE_TAG_LIST) { return WY_ERR_BAD_TYPE; }
    if (args[2].type != WY_TYPE_TAG_WORD && args[2].type != WY_TYPE_TAG_UWORD) { return WY_ERR_BAD_TYPE; }
    wy_word count = args[2].type == WY_TYPE_TAG_UWORD ? (wy_word) args[2].data.uword : args[2].data.word;
    if (count < 0) { return WY_ERR_RANGE; }
    wy_list* l = (wy_list*) args[0].data.gc_object;
    for (wy_word i = 0; i < count; i++) {
        wy_error err = wy_list_push(context, l, args[1]);
        if (err != WY_ERR_NONE) { return err; }
    }
    out[0] = args[0];
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

static wy_error builtin_remove_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(argc);
    if (args[0].type != WY_TYPE_TAG_TABLE) { return WY_ERR_BAD_TYPE; }
    wy_value removed;
    wy_error err = wy_dict_remove(context, (wy_dict*) args[0].data.gc_object, args[1].type, args[1].data, &removed);
    if (err != WY_ERR_NONE) { return err; }
    out[0] = removed;
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

/*
 * `len(x)`: the number of elements in a collection, matching Python's
 * `len` and the pypoc reference's `length` (wyrm_builtins.py): a str counts
 * Unicode codepoints (not UTF-8 bytes - same rule `car`/`substr` use), a
 * list/tuple counts items, a dict counts entries, bytes counts bytes (epic
 * 7's documented gap, doc/stdlib.md's `len(b)`), and a Pair chain is the
 * number of cons cells walked out to a terminating nil (`()` alone is 0).
 * An improper list (whose final cdr isn't nil) has no well-defined length,
 * same as Scheme's `length`; any other value type faults - like every
 * native here except car/cdr, an argument error is a VM fault
 * (WY_ERR_BAD_TYPE), mirroring the reference's host TypeError.
 */
static wy_error builtin_len_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(context); WY_UNUSED(argc);
    wy_value x = args[0];
    wy_uword n = 0;
    switch (x.type) {
    case WY_TYPE_TAG_NIL:
        n = 0;
        break;
    case WY_TYPE_TAG_STR:
        n = wy_utf8_codepoint_count_f(x.data.str->str, x.data.str->len);
        break;
    case WY_TYPE_TAG_LIST:
        n = ((wy_list*) x.data.gc_object)->count;
        break;
    case WY_TYPE_TAG_TUPLE:
        n = ((wy_tuple*) x.data.gc_object)->count;
        break;
    case WY_TYPE_TAG_TABLE:
        n = ((wy_dict*) x.data.gc_object)->count;
        break;
    case WY_TYPE_TAG_BYTES:
        n = ((wy_bytes*) x.data.gc_object)->len;
        break;
    case WY_TYPE_TAG_PAIR: {
        wy_value node = x;
        while (node.type == WY_TYPE_TAG_PAIR) {
            n++;
            node = ((wy_pair*) node.data.gc_object)->cdr;
        }
        if (node.type != WY_TYPE_TAG_NIL) { return WY_ERR_BAD_TYPE; }
        break;
    }
    default:
        return WY_ERR_BAD_TYPE;
    }
    out[0] = wy_value_word((wy_word) n);
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

/* -------------------------------------------------------------------------
 * `bytes` natives (epic 7, doc/stdlib.md's `### bytes`). Registered both
 * as a bare global (`bytes` construction only) and as native message
 * overloads (install_native_messages_, below) so `b!append(x)` etc. work
 * through the same `!`-dispatch path every other message uses.
 * ------------------------------------------------------------------------- */

/** Read `v` as a plain (non-negative-checked) word; caller range-checks. */
static wy_word bytes_arg_word_(wy_value v)
{
    return v.type == WY_TYPE_TAG_UWORD ? (wy_word) v.data.uword : v.data.word;
}

static bool bytes_arg_is_int_(wy_value v)
{
    return v.type == WY_TYPE_TAG_WORD || v.type == WY_TYPE_TAG_UWORD;
}

static wy_error bytes_construct_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(argc);
    wy_value v = args[0];
    wy_bytes* b = WY_NULL;
    wy_error err;
    if (bytes_arg_is_int_(v)) {
        wy_word n = bytes_arg_word_(v);
        if (n < 0) { return WY_ERR_RANGE; }
        err = wy_bytes_new(context, WY_NULL, 0, &b);
        if (err != WY_ERR_NONE) { return err; }
        err = wy_bytes_reserve(context, b, (wy_uword) n);
        if (err != WY_ERR_NONE) { return err; }
        wy_memset(b->data, 0, (wy_uword) n);
        b->len = (wy_uword) n;
    } else if (v.type == WY_TYPE_TAG_STR) {
        wy_string* s = v.data.str;
        err = wy_bytes_new(context, (const wy_u8*) s->str, s->len, &b);
        if (err != WY_ERR_NONE) { return err; }
    } else if (v.type == WY_TYPE_TAG_BYTES) {
        wy_bytes* src = (wy_bytes*) v.data.gc_object;
        err = wy_bytes_new(context, src->data, src->len, &b);
        if (err != WY_ERR_NONE) { return err; }
    } else {
        return WY_ERR_BAD_TYPE;
    }
    out[0] = wy_value_object(WY_TYPE_TAG_BYTES, (wy_object*) b);
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

static wy_error bytes_append_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(argc);
    if (args[0].type != WY_TYPE_TAG_BYTES) { return WY_ERR_BAD_TYPE; }
    wy_bytes* b = (wy_bytes*) args[0].data.gc_object;
    wy_value v = args[1];
    wy_error err;
    if (bytes_arg_is_int_(v)) {
        wy_word n = bytes_arg_word_(v);
        if (n < 0 || n > 255) { return WY_ERR_RANGE; }
        err = wy_bytes_reserve(context, b, b->len + 1);
        if (err != WY_ERR_NONE) { return err; }
        b->data[b->len++] = (wy_u8) n;
    } else if (v.type == WY_TYPE_TAG_BYTES) {
        wy_bytes* src = (wy_bytes*) v.data.gc_object;
        err = wy_bytes_reserve(context, b, b->len + src->len);
        if (err != WY_ERR_NONE) { return err; }
        wy_memcpy(b->data + b->len, src->data, src->len);
        b->len += src->len;
    } else if (v.type == WY_TYPE_TAG_STR) {
        wy_string* s = v.data.str;
        err = wy_bytes_reserve(context, b, b->len + s->len);
        if (err != WY_ERR_NONE) { return err; }
        wy_memcpy(b->data + b->len, s->str, s->len);
        b->len += s->len;
    } else {
        return WY_ERR_BAD_TYPE;
    }
    out[0] = args[0];
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

static wy_error bytes_resize_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(argc);
    if (args[0].type != WY_TYPE_TAG_BYTES) { return WY_ERR_BAD_TYPE; }
    if (!bytes_arg_is_int_(args[1])) { return WY_ERR_BAD_TYPE; }
    wy_word n = bytes_arg_word_(args[1]);
    if (n < 0) { return WY_ERR_RANGE; }
    wy_bytes* b = (wy_bytes*) args[0].data.gc_object;
    if ((wy_uword) n > b->len) {
        wy_error err = wy_bytes_reserve(context, b, (wy_uword) n);
        if (err != WY_ERR_NONE) { return err; }
        wy_memset(b->data + b->len, 0, (wy_uword) n - b->len);
    }
    b->len = (wy_uword) n;
    out[0] = args[0];
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

static wy_error bytes_slice_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(argc);
    if (args[0].type != WY_TYPE_TAG_BYTES) { return WY_ERR_BAD_TYPE; }
    if (!bytes_arg_is_int_(args[1]) || !bytes_arg_is_int_(args[2])) { return WY_ERR_BAD_TYPE; }
    wy_word start = bytes_arg_word_(args[1]);
    wy_word count = bytes_arg_word_(args[2]);
    wy_bytes* b = (wy_bytes*) args[0].data.gc_object;
    if (start < 0 || count < 0 || (wy_uword) start + (wy_uword) count > b->len) { return WY_ERR_RANGE; }
    wy_bytes* out_b = WY_NULL;
    wy_error err = wy_bytes_new(context, b->data + start, (wy_uword) count, &out_b);
    if (err != WY_ERR_NONE) { return err; }
    out[0] = wy_value_object(WY_TYPE_TAG_BYTES, (wy_object*) out_b);
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

static wy_error bytes_copy_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(argc);
    if (args[0].type != WY_TYPE_TAG_BYTES) { return WY_ERR_BAD_TYPE; }
    wy_bytes* b = (wy_bytes*) args[0].data.gc_object;
    wy_bytes* out_b = WY_NULL;
    wy_error err = wy_bytes_new(context, b->data, b->len, &out_b);
    if (err != WY_ERR_NONE) { return err; }
    out[0] = wy_value_object(WY_TYPE_TAG_BYTES, (wy_object*) out_b);
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

/**
 * Strict UTF-8 validation (doc/stdlib.md: "faults... does not silently
 * substitute or truncate"), matching Python's `bytes.decode("utf-8")`
 * default. `wy_utf8_decode_f` is deliberately lenient (never faults, used
 * by string *indexing*, which must never crash on data already accepted
 * into a wy_string) so it is not reused here - this rejects overlong
 * encodings, truncated sequences, bad continuation bytes, out-of-range
 * codepoints, and surrogate halves.
 */
static bool bytes_valid_utf8_(const wy_u8* data, wy_uword len)
{
    wy_uword i = 0;
    while (i < len) {
        wy_u8 lead = data[i];
        wy_uword seqlen; wy_u32 min_cp; wy_u32 cp;
        if (lead < 0x80) { i += 1; continue; }
        else if ((lead & 0xE0) == 0xC0) { seqlen = 2; min_cp = 0x80; cp = lead & 0x1F; }
        else if ((lead & 0xF0) == 0xE0) { seqlen = 3; min_cp = 0x800; cp = lead & 0x0F; }
        else if ((lead & 0xF8) == 0xF0) { seqlen = 4; min_cp = 0x10000; cp = lead & 0x07; }
        else { return false; }
        if (i + seqlen > len) { return false; }
        for (wy_uword k = 1; k < seqlen; k++) {
            wy_u8 cont = data[i + k];
            if ((cont & 0xC0) != 0x80) { return false; }
            cp = (cp << 6) | (wy_u32) (cont & 0x3F);
        }
        if (cp < min_cp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) { return false; }
        i += seqlen;
    }
    return true;
}

static wy_error bytes_to_str_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(argc);
    if (args[0].type != WY_TYPE_TAG_BYTES) { return WY_ERR_BAD_TYPE; }
    wy_bytes* b = (wy_bytes*) args[0].data.gc_object;
    if (!bytes_valid_utf8_(b->data, b->len)) { return WY_ERR_BAD_TYPE; }
    wy_string* s = WY_NULL;
    wy_error err = wy_string_new(context, (const char*) b->data, b->len, &s);
    if (err != WY_ERR_NONE) { return err; }
    out[0] = wy_value_object(WY_TYPE_TAG_STR, (wy_object*) s);
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

/** at..at+width must fall inside 0..b->len (design_c_vm.md/stdlib.md: pack/unpack never resize). */
static wy_error bytes_pack_bounds_(wy_bytes* b, wy_value at_v, wy_uword width, wy_uword* out_at)
{
    if (!bytes_arg_is_int_(at_v)) { return WY_ERR_BAD_TYPE; }
    wy_word at = bytes_arg_word_(at_v);
    if (at < 0 || (wy_uword) at + width > b->len) { return WY_ERR_RANGE; }
    *out_at = (wy_uword) at;
    return WY_ERR_NONE;
}

static void bytes_write_le_(wy_u8* data, wy_uword at, wy_u64 bits, wy_uword width)
{
    for (wy_uword i = 0; i < width; i++) { data[at + i] = (wy_u8) ((bits >> (8 * i)) & 0xFF); }
}

static wy_u64 bytes_read_le_(const wy_u8* data, wy_uword at, wy_uword width)
{
    wy_u64 bits = 0;
    for (wy_uword i = 0; i < width; i++) { bits |= ((wy_u64) data[at + i]) << (8 * i); }
    return bits;
}

static wy_error bytes_pack_u8_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(context); WY_UNUSED(argc);
    if (args[0].type != WY_TYPE_TAG_BYTES) { return WY_ERR_BAD_TYPE; }
    if (!bytes_arg_is_int_(args[2])) { return WY_ERR_BAD_TYPE; }
    wy_word v = bytes_arg_word_(args[2]);
    if (v < 0 || v > 255) { return WY_ERR_RANGE; }
    wy_bytes* b = (wy_bytes*) args[0].data.gc_object;
    wy_uword at;
    wy_error err = bytes_pack_bounds_(b, args[1], 1, &at);
    if (err != WY_ERR_NONE) { return err; }
    b->data[at] = (wy_u8) v;
    out[0] = args[0];
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

static wy_error bytes_pack_i32_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(context); WY_UNUSED(argc);
    if (args[0].type != WY_TYPE_TAG_BYTES) { return WY_ERR_BAD_TYPE; }
    if (!bytes_arg_is_int_(args[2])) { return WY_ERR_BAD_TYPE; }
    wy_i32 v = (wy_i32) bytes_arg_word_(args[2]);
    wy_u32 bits; wy_memcpy(&bits, &v, sizeof(bits));
    wy_bytes* b = (wy_bytes*) args[0].data.gc_object;
    wy_uword at;
    wy_error err = bytes_pack_bounds_(b, args[1], 4, &at);
    if (err != WY_ERR_NONE) { return err; }
    bytes_write_le_(b->data, at, bits, 4);
    out[0] = args[0];
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

static wy_error bytes_pack_u32_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(context); WY_UNUSED(argc);
    if (args[0].type != WY_TYPE_TAG_BYTES) { return WY_ERR_BAD_TYPE; }
    if (!bytes_arg_is_int_(args[2])) { return WY_ERR_BAD_TYPE; }
    wy_u32 bits = (wy_u32) bytes_arg_word_(args[2]);
    wy_bytes* b = (wy_bytes*) args[0].data.gc_object;
    wy_uword at;
    wy_error err = bytes_pack_bounds_(b, args[1], 4, &at);
    if (err != WY_ERR_NONE) { return err; }
    bytes_write_le_(b->data, at, bits, 4);
    out[0] = args[0];
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

static wy_error bytes_pack_f32_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(context); WY_UNUSED(argc);
    if (args[0].type != WY_TYPE_TAG_BYTES) { return WY_ERR_BAD_TYPE; }
    if (args[2].type != WY_TYPE_TAG_FLOAT) { return WY_ERR_BAD_TYPE; }
    float f = (float) args[2].data.fp;
    wy_u32 bits; wy_memcpy(&bits, &f, sizeof(bits));
    wy_bytes* b = (wy_bytes*) args[0].data.gc_object;
    wy_uword at;
    wy_error err = bytes_pack_bounds_(b, args[1], 4, &at);
    if (err != WY_ERR_NONE) { return err; }
    bytes_write_le_(b->data, at, bits, 4);
    out[0] = args[0];
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

static wy_error bytes_pack_f64_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(context); WY_UNUSED(argc);
    if (args[0].type != WY_TYPE_TAG_BYTES) { return WY_ERR_BAD_TYPE; }
    if (args[2].type != WY_TYPE_TAG_FLOAT) { return WY_ERR_BAD_TYPE; }
    double d = (double) args[2].data.fp;
    wy_u64 bits; wy_memcpy(&bits, &d, sizeof(bits));
    wy_bytes* b = (wy_bytes*) args[0].data.gc_object;
    wy_uword at;
    wy_error err = bytes_pack_bounds_(b, args[1], 8, &at);
    if (err != WY_ERR_NONE) { return err; }
    bytes_write_le_(b->data, at, bits, 8);
    out[0] = args[0];
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

static wy_error bytes_unpack_u8_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(context); WY_UNUSED(argc);
    if (args[0].type != WY_TYPE_TAG_BYTES) { return WY_ERR_BAD_TYPE; }
    wy_bytes* b = (wy_bytes*) args[0].data.gc_object;
    wy_uword at;
    wy_error err = bytes_pack_bounds_(b, args[1], 1, &at);
    if (err != WY_ERR_NONE) { return err; }
    out[0] = wy_value_word(b->data[at]);
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

static wy_error bytes_unpack_i32_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(context); WY_UNUSED(argc);
    if (args[0].type != WY_TYPE_TAG_BYTES) { return WY_ERR_BAD_TYPE; }
    wy_bytes* b = (wy_bytes*) args[0].data.gc_object;
    wy_uword at;
    wy_error err = bytes_pack_bounds_(b, args[1], 4, &at);
    if (err != WY_ERR_NONE) { return err; }
    wy_u32 bits = (wy_u32) bytes_read_le_(b->data, at, 4);
    wy_i32 v; wy_memcpy(&v, &bits, sizeof(v));
    out[0] = wy_value_word(v);
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

static wy_error bytes_unpack_u32_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(context); WY_UNUSED(argc);
    if (args[0].type != WY_TYPE_TAG_BYTES) { return WY_ERR_BAD_TYPE; }
    wy_bytes* b = (wy_bytes*) args[0].data.gc_object;
    wy_uword at;
    wy_error err = bytes_pack_bounds_(b, args[1], 4, &at);
    if (err != WY_ERR_NONE) { return err; }
    wy_u32 bits = (wy_u32) bytes_read_le_(b->data, at, 4);
    out[0] = wy_value_word((wy_word) bits);
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

static wy_error bytes_unpack_f32_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(context); WY_UNUSED(argc);
    if (args[0].type != WY_TYPE_TAG_BYTES) { return WY_ERR_BAD_TYPE; }
    wy_bytes* b = (wy_bytes*) args[0].data.gc_object;
    wy_uword at;
    wy_error err = bytes_pack_bounds_(b, args[1], 4, &at);
    if (err != WY_ERR_NONE) { return err; }
    wy_u32 bits = (wy_u32) bytes_read_le_(b->data, at, 4);
    float f; wy_memcpy(&f, &bits, sizeof(f));
    out[0] = wy_value_float((wy_float) f);
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

static wy_error bytes_unpack_f64_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(context); WY_UNUSED(argc);
    if (args[0].type != WY_TYPE_TAG_BYTES) { return WY_ERR_BAD_TYPE; }
    wy_bytes* b = (wy_bytes*) args[0].data.gc_object;
    wy_uword at;
    wy_error err = bytes_pack_bounds_(b, args[1], 8, &at);
    if (err != WY_ERR_NONE) { return err; }
    wy_u64 bits = bytes_read_le_(b->data, at, 8);
    double d; wy_memcpy(&d, &bits, sizeof(d));
    out[0] = wy_value_float((wy_float) d);
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

/* `range(begin, end)` - the integers begin, begin+1, ..., end-1 (exclusive
 * end; empty when begin >= end), as an iterator: `for x in range(a, b)`
 * and `next(r)` both work on it. Native, so a for-loop over it never
 * switches fibers. Arguments must be integers. */
static wy_error builtin_range_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(argc);
    if (args[0].type != WY_TYPE_TAG_WORD || args[1].type != WY_TYPE_TAG_WORD) { return WY_ERR_BAD_TYPE; }
    wy_iterator* it = WY_NULL;
    wy_error err = wy_iterator_new_range(context, args[0].data.word, args[1].data.word, &it);
    if (err != WY_ERR_NONE) { return err; }
    if (nres > 0) { out[0] = wy_value_object(WY_TYPE_TAG_ITER, (wy_object*) it); }
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

/* -------------------------------------------------------------------------
 * Coroutine natives: `next`/`send` (design_c_vm.md §3). Both are exec
 * natives - they may switch `ctx->current_fiber` onto the coroutine's own
 * fiber and only complete once it yields or returns - so unlike every
 * native above, they run through src/vm.c's exec-native call bridge
 * (WY_OP_CALL's WY_NATIVE_EXEC branch), not wy_vm_call_leaf_f.
 * ------------------------------------------------------------------------- */

static wy_value coroutine_stop_iteration_value_(wy_context* context)
{
    wy_string* what = WY_NULL;
    (void) wy_string_strdup(context, "StopIteration", &what);
    wy_error_obj* obj = WY_NULL;
    if (wy_error_obj_new(context, context->stop_iteration_class, what, wy_value_nil(), &obj) != WY_ERR_NONE) {
        return wy_value_word(-1);
    }
    return wy_value_object(WY_TYPE_TAG_ERROR, (wy_object*) obj);
}

static wy_exec_state coroutine_fault_(wy_context* context, wy_fiber* fiber, const char* message)
{
    wy_value err_v;
    if (write_error_value_(context, message, &err_v) != WY_ERR_NONE) { err_v = wy_value_word(-1); }
    fiber->fault = err_v;
    return WY_EXEC_FAULT;
}

/**
 * `next(co)`: resume `co` (or its innermost delegate) with a sent value of
 * `nil`. DONE answers StopIteration synchronously with no fiber switch;
 * otherwise `co->resumer` becomes the calling fiber and control switches to
 * `co`'s own fiber until it yields or returns.
 */
static wy_exec_state builtin_next_exec_(wy_context* context, wy_primitive c_data)
{
    WY_UNUSED(c_data);
    wy_fiber* fiber = context->current_fiber;
    wy_value co_v = *wy_fiber_value_n(fiber, 0);
    if (co_v.type == WY_TYPE_TAG_ITER) {
        /* An iterator (e.g. range(...)): step it in place, no fiber switch. */
        wy_value item;
        wy_error err = wy_iterator_next(context, (wy_iterator*) co_v.data.gc_object, &item);
        if (err == WY_ERR_STOP_ITERATION) {
            item = coroutine_stop_iteration_value_(context);
        } else if (err != WY_ERR_NONE) {
            return coroutine_fault_(context, fiber, "next: iterator failed");
        }
        wy_context_set_result(context, 0, item);
        return WY_EXEC_DONE;
    }
    if (co_v.type != WY_TYPE_TAG_COROUTINE) {
        return coroutine_fault_(context, fiber, "next: argument is not a coroutine or iterator");
    }
    wy_coroutine* co = wy_coroutine_innermost_f((wy_coroutine*) co_v.data.gc_object);
    if (co->state == WY_CO_DONE) {
        wy_context_set_result(context, 0, coroutine_stop_iteration_value_(context));
        return WY_EXEC_DONE;
    }
    if (co->state == WY_CO_SUSPENDED) {
        co->fiber->current_frame->l[co->yield_base] = wy_value_nil();
    }
    co->resumer = fiber;
    co->state = WY_CO_RUNNING;
    co->fiber->pending = wy_exec_fn_create(wy_vm_run, wy_primitive_null());
    context->current_fiber = co->fiber;
    return WY_EXEC_SWITCH;
}

/**
 * `send(co, v)`: like `next`, but delivers `v` to a SUSPENDED coroutine's
 * paused `yield`. Sending into a CREATED coroutine (one that has never
 * yielded) is always an error (design_c_vm.md §3), regardless of `v`.
 */
static wy_exec_state builtin_send_exec_(wy_context* context, wy_primitive c_data)
{
    WY_UNUSED(c_data);
    wy_fiber* fiber = context->current_fiber;
    wy_value co_v = *wy_fiber_value_n(fiber, 0);
    wy_value send_v = *wy_fiber_value_n(fiber, 1);
    if (co_v.type != WY_TYPE_TAG_COROUTINE) {
        return coroutine_fault_(context, fiber, "send: first argument is not a coroutine");
    }
    wy_coroutine* co = wy_coroutine_innermost_f((wy_coroutine*) co_v.data.gc_object);
    if (co->state == WY_CO_DONE) {
        wy_context_set_result(context, 0, coroutine_stop_iteration_value_(context));
        return WY_EXEC_DONE;
    }
    if (co->state == WY_CO_CREATED) {
        return coroutine_fault_(context, fiber, "send: coroutine has not started (call next first)");
    }
    co->fiber->current_frame->l[co->yield_base] = send_v;
    co->resumer = fiber;
    co->state = WY_CO_RUNNING;
    co->fiber->pending = wy_exec_fn_create(wy_vm_run, wy_primitive_null());
    context->current_fiber = co->fiber;
    return WY_EXEC_SWITCH;
}

/* -------------------------------------------------------------------------
 * Module assembly
 * ------------------------------------------------------------------------- */

enum { WY_BUILTINS_LEAF_COUNT = 29, WY_BUILTINS_EXEC_COUNT = 2, WY_BUILTINS_CLASS_COUNT = 5,
    /* +1 for the bare `nil` slot, +1 for the bare `TreeBase` class (M5:
     * decorators fixture registers messages typed on it,
     * wyrm_builtins.py's TREE_BASE_CLASS). */
    WY_BUILTINS_COUNT = WY_BUILTINS_LEAF_COUNT + WY_BUILTINS_EXEC_COUNT + WY_BUILTINS_CLASS_COUNT + 1 + 1,
    /* Room for wy_builtins_add (host natives such as the std::io set). */
    WY_BUILTINS_CAPACITY = WY_BUILTINS_COUNT + 16 };


typedef struct builtin_exec_entry_
{
    const char* name;
    wy_u8 min_argc;
    wy_u8 max_argc;
    wy_exec_fn_c_call fn;
} builtin_exec_entry_;

typedef struct builtin_leaf_entry_
{
    const char* name;
    wy_u8 min_argc;
    wy_u8 max_argc;
    wy_native_leaf_fn fn;
} builtin_leaf_entry_;

/* Scalar conversion used by imported code as well as ordinary modules.
 * Container/instance string dispatch remains part of the broader builtin work. */
static wy_error builtin_str_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(argc);
    wy_value value = args[0];
    if (nres == 0) { return WY_ERR_NONE; }
    if (value.type == WY_TYPE_TAG_STR) { out[0] = value; return WY_ERR_NONE; }
    char buffer[WY_BUILTIN_FMT_BUF_SIZE];
    const char* text = buffer;
    wy_uword len;
    switch (value.type) {
    case WY_TYPE_TAG_NIL: text = "nil"; len = 3; break;
    case WY_TYPE_TAG_BOOL: text = value.data.flag ? "true" : "false"; len = wy_strlen_f(text); break;
    case WY_TYPE_TAG_WORD: len = (wy_uword) snprintf(buffer, sizeof(buffer), "%lld", (long long) value.data.word); break;
    case WY_TYPE_TAG_UWORD: len = (wy_uword) snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long) value.data.uword); break;
    case WY_TYPE_TAG_FLOAT: len = format_float_((double) value.data.fp, false, buffer, sizeof(buffer)); break;
    case WY_TYPE_TAG_SYMBOL: text = value.data.symtab_entry; len = wy_strlen_f(text); break;
    case WY_TYPE_TAG_BYTES:
        /* doc/stdlib.md: "N bytes", matching image.py's _static_repr binary-
         * constant format (no separate rendering invented for this VM). */
        len = (wy_uword) snprintf(buffer, sizeof(buffer), "%llu bytes",
            (unsigned long long) ((wy_bytes*) value.data.gc_object)->len);
        break;
    case WY_TYPE_TAG_ERROR: {
        /* An error value renders as its message (Unset - a null object - as "unset"). */
        wy_error_obj* eobj = (wy_error_obj*) value.data.gc_object;
        if (eobj == WY_NULL) { text = "unset"; len = 5; }
        else if (eobj->what == WY_NULL) { text = "error"; len = 5; }
        else { text = eobj->what->str; len = eobj->what->len; }
        break;
    }
    case WY_TYPE_TAG_PAIR: case WY_TYPE_TAG_LIST: case WY_TYPE_TAG_TUPLE: case WY_TYPE_TAG_TABLE: {
        /* Containers render as println does (a pair list in D2 form). */
        wy_string* rendered = WY_NULL;
        wy_error err = format_to_string_(context, value, &rendered);
        if (err == WY_ERR_NONE) { out[0] = wy_value_object(WY_TYPE_TAG_STR, (wy_object*) rendered); }
        return err;
    }
    default: return WY_ERR_BAD_TYPE;
    }
    wy_string* result = WY_NULL;
    wy_error err = wy_string_new(context, text, len, &result);
    if (err == WY_ERR_NONE) { out[0] = wy_value_object(WY_TYPE_TAG_STR, (wy_object*) result); }
    return err;
}

/* `sym(text)` - intern a string as a symbol. The inverse of `str` on a
 * symbol value; what pypoc's runtime provides as a plain builtin and every
 * front-end module (decode.wy, parser.wy, the compiler's own scope reads)
 * takes for granted. A symbol argument answers itself, like `str` does. */
static wy_error builtin_sym_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(argc);
    if (nres == 0) { return WY_ERR_NONE; }
    wy_value value = args[0];
    if (value.type == WY_TYPE_TAG_SYMBOL) { out[0] = value; return WY_ERR_NONE; }
    if (value.type != WY_TYPE_TAG_STR) { return WY_ERR_BAD_TYPE; }
    wy_string* text = (wy_string*) value.data.gc_object;
    wy_symbol sym;
    wy_error err = wy_context_intern(context, text->str, text->len, &sym);
    if (err != WY_ERR_NONE) { return err; }
    out[0] = wy_value_symbol(sym);
    return WY_ERR_NONE;
}

WY_INLINE bool int_text_space_(char c)
{
    return (c == ' ') || (c == '\t') || (c == '\n') || (c == '\r') || (c == '\f') || (c == '\v');
}

/* The value of digit `c` in `base`, or -1 when `c` isn't one. */
static int int_text_digit_(char c, int base)
{
    int d = -1;
    if ((c >= '0') && (c <= '9')) { d = c - '0'; }
    else if ((c >= 'a') && (c <= 'z')) { d = (c - 'a') + 10; }
    else if ((c >= 'A') && (c <= 'Z')) { d = (c - 'A') + 10; }
    return (d < base) ? d : -1;
}

/* Parse `text` as an integer literal, by the tree walker's rules (Python's
 * `int(text, 0)`): surrounding whitespace, an optional sign, then a `0x`,
 * `0o` or `0b` prefix (either case) or a decimal number. A single `_` may
 * separate digits, or follow the prefix. A decimal number with a leading 0
 * must be all zeros (`010` is an error, not octal). WY_ERR_BAD_TYPE for
 * malformed text, WY_ERR_RANGE when the value doesn't fit a wy_word. */
static wy_error int_from_text_(const char* text, wy_uword len, wy_word* out)
{
    wy_uword i = 0;
    while ((len > 0) && int_text_space_(text[len - 1])) { len--; }
    while ((i < len) && int_text_space_(text[i])) { i++; }

    bool negative = false;
    if ((i < len) && ((text[i] == '+') || (text[i] == '-'))) {
        negative = (text[i] == '-');
        i++;
    }

    int base = 10;
    bool prefixed = false;
    if (((i + 1) < len) && (text[i] == '0')) {
        char p = text[i + 1];
        if ((p == 'x') || (p == 'X')) { base = 16; }
        else if ((p == 'o') || (p == 'O')) { base = 8; }
        else if ((p == 'b') || (p == 'B')) { base = 2; }
        else { /* decimal */ }
        if (base != 10) {
            prefixed = true;
            i += 2;
        }
    }

    /* The magnitude limit: |WY_WORD_MIN| for a negative value. */
    const wy_uword limit = negative ? ((wy_uword) WY_WORD_MAX + 1u) : (wy_uword) WY_WORD_MAX;
    wy_uword magnitude = 0;
    wy_uword digits = 0;
    bool leading_zero = false;
    bool nonzero = false;
    bool after_underscore = false;
    wy_error err = WY_ERR_NONE;
    for (; (i < len) && (err == WY_ERR_NONE); i++) {
        char c = text[i];
        if (c == '_') {
            /* After a digit or the prefix (`0x_ff`), never twice in a row. */
            if (((digits == 0) && !prefixed) || after_underscore) { err = WY_ERR_BAD_TYPE; }
            after_underscore = true;
            continue;
        }
        int d = int_text_digit_(c, base);
        if (d < 0) {
            err = WY_ERR_BAD_TYPE;
            continue;
        }
        if ((digits == 0) && (d == 0)) { leading_zero = true; }
        if (d != 0) { nonzero = true; }
        if (magnitude > ((limit - (wy_uword) d) / (wy_uword) base)) {
            err = WY_ERR_RANGE;
            continue;
        }
        magnitude = (magnitude * (wy_uword) base) + (wy_uword) d;
        digits++;
        after_underscore = false;
    }
    if (err == WY_ERR_NONE) {
        if ((digits == 0) || after_underscore) { err = WY_ERR_BAD_TYPE; }
        else if ((base == 10) && leading_zero && nonzero) { err = WY_ERR_BAD_TYPE; }
        else if (negative) {
            /* -(magnitude) without overflowing at the most negative word. */
            *out = (magnitude == ((wy_uword) WY_WORD_MAX + 1u)) ? (-WY_WORD_MAX - 1) : -(wy_word) magnitude;
        } else {
            *out = (wy_word) magnitude;
        }
    }
    return err;
}

/* `int(text)` - the numeric cast: a string parsed as an integer literal
 * (int_from_text_: 0x/0o/0b prefixes, `_` separators, no implicit octal,
 * an error rather than a clamp on overflow; the tree walker's
 * `int(value, 0)`), a float truncated toward zero, an integer answering
 * itself. What decode.wy's `decode_int` and every numeric-literal
 * transform take for granted on the front-end side. */
static wy_error builtin_int_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(context);
    WY_UNUSED(argc);
    if (nres == 0) { return WY_ERR_NONE; }
    wy_value value = args[0];
    if (value.type == WY_TYPE_TAG_WORD) { out[0] = value; return WY_ERR_NONE; }
    if (value.type == WY_TYPE_TAG_FLOAT) {
        out[0] = wy_value_word((wy_word) value.data.fp);
        return WY_ERR_NONE;
    }
    if (value.type == WY_TYPE_TAG_STR) {
        wy_string* text = (wy_string*) value.data.gc_object;
        wy_word parsed = 0;
        wy_error err = int_from_text_(text->str, text->len, &parsed);
        if (err != WY_ERR_NONE) { return err; }
        out[0] = wy_value_word(parsed);
        return WY_ERR_NONE;
    }
    return WY_ERR_BAD_TYPE;
}

/* `float(text)` - the numeric cast into a double: a string parsed with
 * strtod, an integer widened, a float answering itself. The float-literal
 * transform (parser.wy's _mk_float) runs through it. */
static wy_error builtin_float_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(argc);
    if (nres == 0) { return WY_ERR_NONE; }
    wy_value value = args[0];
    if (value.type == WY_TYPE_TAG_FLOAT) { out[0] = value; return WY_ERR_NONE; }
    if (value.type == WY_TYPE_TAG_WORD) {
        out[0] = wy_value_float((double) value.data.word);
        return WY_ERR_NONE;
    }
    if (value.type == WY_TYPE_TAG_STR) {
        wy_string* text = (wy_string*) value.data.gc_object;
        char buffer[64];
        wy_uword len = text->len;
        if (len == 0 || len >= sizeof(buffer)) { return WY_ERR_RANGE; }
        wy_memcpy(buffer, text->str, len);
        buffer[len] = '\0';
        char* end = WY_NULL;
        double parsed = strtod(buffer, &end);
        if (end == buffer) { return WY_ERR_BAD_TYPE; }
        while (*end == ' ' || *end == '\t') { end++; }
        if (*end != '\0') { return WY_ERR_BAD_TYPE; }
        out[0] = wy_value_float(parsed);
        return WY_ERR_NONE;
    }
    return WY_ERR_BAD_TYPE;
}

/* `error_message(e)` - the message text of an error value, as a string.
 * `str(e)` is not the way to get this (stringifying an error value is the
 * one native call this VM refuses), and compilers/drivers need to print
 * compile errors. A non-error argument answers nil. */
static wy_error builtin_error_message_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(context);
    WY_UNUSED(argc);
    if (nres == 0) { return WY_ERR_NONE; }
    wy_value value = args[0];
    if (value.type != WY_TYPE_TAG_ERROR) {
        out[0] = wy_value_nil();
        return WY_ERR_NONE;
    }
    wy_error_obj* obj = (wy_error_obj*) value.data.gc_object;
    if (obj->what == WY_NULL) {
        out[0] = wy_value_nil();
        return WY_ERR_NONE;
    }
    wy_string* result = WY_NULL;
    wy_error err = wy_string_new(context, obj->what->str, obj->what->len, &result);
    if (err == WY_ERR_NONE) { out[0] = wy_value_object(WY_TYPE_TAG_STR, (wy_object*) result); }
    return err;
}

/**
 * `module_exports(path)`: the names module `path` exports, as a list of
 * strings, loading it without running it - the compiler places every name
 * before a module runs (project design/modules.md M2), so it reads each
 * dependency's exports first. A namespace package exports nothing. An
 * error value when the module can't be found or loaded here.
 */
static wy_error builtin_module_exports_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(argc);
    if (nres == 0) { return WY_ERR_NONE; }
    if (args[0].type != WY_TYPE_TAG_STR) {
        return write_error_value_(context, "module_exports: the path must be a string", &out[0]);
    }
    wy_module* module = WY_NULL;
    wy_error err = wy_link_import(context, args[0].data.str, &module);
    if (err == WY_ERR_NOMEM) { return err; }
    if (err != WY_ERR_NONE) {
        char text[256];
        snprintf(text, sizeof(text), "module_exports: cannot load '%.*s' (error %d)",
            (int) args[0].data.str->len, args[0].data.str->str, (int) err);
        return write_error_value_(context, text, &out[0]);
    }
    wy_list* names = WY_NULL;
    err = wy_list_new(context, module->exports.entry_count, &names);
    if (err != WY_ERR_NONE) { return err; }
    wy_value held = wy_value_object(WY_TYPE_TAG_LIST, (wy_object*) names);
    err = wy_context_root_push_f(context, &held);
    if (err != WY_ERR_NONE) { return err; }
    for (wy_uword i = 0; err == WY_ERR_NONE && i < module->exports.capacity; i++) {
        const wy_slot_dict_entry* entry = &module->exports.entry_table[i];
        if (entry->symbol == WY_SYMBOL_INVALID) { continue; }
        wy_string* name = WY_NULL;
        err = wy_string_new(context, entry->symbol, wy_strlen_f(entry->symbol), &name);
        if (err == WY_ERR_NONE) {
            err = wy_list_push(context, names, wy_value_object(WY_TYPE_TAG_STR, (wy_object*) name));
        }
    }
    wy_context_root_pop_f(context);
    if (err != WY_ERR_NONE) { return err; }
    /* Sorted: the exports table is hashed on symbol addresses, and the
     * compiler numbers slots in the order it reads these, which must not
     * depend on memory layout (the self-compile fixed point). Insertion
     * sort - export tables are small. */
    for (wy_uword i = 1; i < names->count; i++) {
        wy_value key = names->items[i];
        const wy_string* k = key.data.str;
        wy_uword j = i;
        while (j > 0) {
            const wy_string* prev = names->items[j - 1].data.str;
            wy_uword n = prev->len < k->len ? prev->len : k->len;
            int c = wy_memcmp(prev->str, k->str, n);
            if (c < 0 || (c == 0 && prev->len <= k->len)) { break; }
            names->items[j] = names->items[j - 1];
            j--;
        }
        names->items[j] = key;
    }
    out[0] = held;
    return WY_ERR_NONE;
}

/**
 * `tree_box(x)`: wrap the sexpr `x` in a `TreeBase` instance (its one
 * `__tree` slot), the receiver a decorator message is dispatched on.
 */
static wy_error builtin_tree_box_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(argc);
    if (nres == 0) { return WY_ERR_NONE; }
    wy_instance* inst = WY_NULL;
    wy_error err = wy_instance_new_f(context, context->tree_base_class, &inst);
    if (err != WY_ERR_NONE) { return err; }
    inst->slots[0] = args[0];
    out[0] = wy_value_object(WY_TYPE_TAG_INSTANCE, (wy_object*) inst);
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

/**
 * `sexpr(x)`: the tree a `TreeBase` box carries; anything else passes
 * through unchanged (so it is the identity on a tree that already is one).
 */
static wy_error builtin_sexpr_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(argc);
    if (nres == 0) { return WY_ERR_NONE; }
    wy_value x = args[0];
    if (x.type == WY_TYPE_TAG_INSTANCE) {
        wy_instance* inst = (wy_instance*) x.data.gc_object;
        if (inst->cls == context->tree_base_class) { x = inst->slots[0]; }
    }
    out[0] = x;
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

/**
 * `bind_message(scope, recv, name)`: dynamic message lookup by name. Finds
 * `name` (a symbol or string) in the message table of module `scope`
 * (adopted by its imports), resolves the overload for `recv` and answers the
 * bound message `recv ! name` without calling it (exactly `getmsg`); the
 * wyrm caller then calls it with ordinary arguments. Answers an error value
 * when the name is not visible in `scope` or no overload matches.
 */
static wy_error builtin_bind_message_(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    WY_UNUSED(argc);
    if (nres == 0) { return WY_ERR_NONE; }
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    if (args[0].type != WY_TYPE_TAG_MODULE) { return WY_ERR_BAD_TYPE; }
    wy_module* scope = (wy_module*) args[0].data.gc_object;

    wy_symbol name;
    if (args[2].type == WY_TYPE_TAG_SYMBOL) {
        name = args[2].data.symtab_entry;
    } else if (args[2].type == WY_TYPE_TAG_STR) {
        wy_error err = wy_context_intern(context, args[2].data.str->str, args[2].data.str->len, &name);
        if (err != WY_ERR_NONE) { return err; }
    } else {
        return WY_ERR_BAD_TYPE;
    }

    wy_message* msg = WY_NULL;
    wy_error err = wy_module_message_lookup_f(context, scope, name, &msg);
    if (err != WY_ERR_NONE) { return err; }
    if (msg == WY_NULL) {
        return write_error_value_(context, "bind_message: no such message in scope", &out[0]);
    }

    wy_value recv = args[1];
    const wy_value* receivers = &recv;
    wy_uword n = 1;
    if (recv.type == WY_TYPE_TAG_TUPLE) {
        wy_tuple* tup = (wy_tuple*) recv.data.gc_object;
        receivers = (tup->count > 0) ? (const wy_value*) tup->items : WY_NULL;
        n = tup->count;
    }
    if (n == 0 || n > WY_DISPATCH_MAX_RECEIVERS) {
        return write_error_value_(context, "bind_message: bad receiver count", &out[0]);
    }
    const wy_overload* ov = WY_NULL;
    char fault_msg[256];
    if (wy_dispatch_resolve_f(msg, receivers, n, WY_NULL, &ov, fault_msg, sizeof(fault_msg)) != WY_ERR_NONE) {
        return write_error_value_(context, fault_msg, &out[0]);
    }
    wy_bound_msg* bm = WY_NULL;
    err = wy_bound_msg_new_f(context, recv, msg, ov->body, &bm);
    if (err != WY_ERR_NONE) { return err; }
    out[0] = wy_value_object(WY_TYPE_TAG_BOUND_MSG, (wy_object*) bm);
    return WY_ERR_NONE;
}

static const builtin_leaf_entry_ leaf_builtins_[] = {
    { "println",   0, 255, builtin_println_ },
    { "print",     0, 255, builtin_print_ },
    { "str",       1, 1,   builtin_str_ },
    { "sym",       1, 1,   builtin_sym_ },
    { "int",       1, 1,   builtin_int_ },
    { "float",     1, 1,   builtin_float_ },
    { "error_message", 1, 1, builtin_error_message_ },
    { "cons",      2, 2,   builtin_cons_ },
    { "pair",      2, 2,   builtin_cons_ },
    { "car",       1, 1,   builtin_car_ },
    { "cdr",       1, 1,   builtin_cdr_ },
    { "reverse",   1, 1,   builtin_reverse_ },
    { "nreverse",  1, 1,   builtin_nreverse_ },
    { "$set_car",  2, 2,   builtin_set_car_ },
    { "$set_cdr",  2, 2,   builtin_set_cdr_ },
    { "tuple",     0, 255, builtin_tuple_ },
    { "copy",      1, 1,   builtin_copy_ },
    { "substr",    3, 3,   builtin_substr_ },
    { "append",    2, 2,   builtin_append_ },
    { "resize",    2, 2,   builtin_resize_ },
    { "expand",    3, 3,   builtin_expand_ },
    { "remove",    2, 2,   builtin_remove_ },
    { "len",       1, 1,   builtin_len_ },
    { "bytes",     1, 1,   bytes_construct_ },
    { "tree_box",  1, 1,   builtin_tree_box_ },
    { "sexpr",     1, 1,   builtin_sexpr_ },
    { "bind_message", 3, 3, builtin_bind_message_ },
    { "module_exports", 1, 1, builtin_module_exports_ },
    { "range",     2, 2,   builtin_range_ },
};

static const builtin_exec_entry_ exec_builtins_[] = {
    { "next", 1, 1, builtin_next_exec_ },
    { "send", 2, 2, builtin_send_exec_ },
};

/**
 * design_c_vm.md §5's builtins `message_table` (epic 7): every native
 * method callable via `!`-message dispatch, keyed by (name, receiver
 * PTYPE). `min_argc`/`max_argc` count the *total* leaf argc including the
 * receiver, exactly like `leaf_builtins_`'s bare-global entries above -
 * `dispatch_native_body_f` (src/vm.c) merges the message's receiver and
 * explicit arguments into one flat array before calling the leaf, so the
 * native itself can't tell it was reached via `!` rather than a bare call.
 *
 * The four LIST/TABLE entries are epic 5's DIVERGES #1 fix (`grown!resize(5)`
 * etc. - `samples/eval_assignments.wy`), reusing the exact same native
 * bodies already registered as bare globals above. The BYTES entries are
 * epic 7's own message set (doc/stdlib.md's `### bytes`); "append" and
 * "resize" each get a *second* overload here alongside their LIST one -
 * `wy_message_add_overload_f` appends, and dispatch ranks by the receiver's
 * actual PTYPE (`src/dispatch.c`'s `overload_distance_f`), so a BYTES
 * receiver never matches the LIST overload or vice versa.
 */
typedef struct builtin_message_entry_
{
    const char* name;
    wy_type_tag receiver;
    wy_native_leaf_fn fn;
    wy_u8 min_argc;
    wy_u8 max_argc;
} builtin_message_entry_;

static const builtin_message_entry_ native_messages_[] = {
    { "resize", WY_TYPE_TAG_LIST,  builtin_resize_, 2, 2 },
    { "expand", WY_TYPE_TAG_LIST,  builtin_expand_, 3, 3 },
    { "append", WY_TYPE_TAG_LIST,  builtin_append_, 2, 2 },
    { "remove", WY_TYPE_TAG_TABLE, builtin_remove_, 2, 2 },

    /* `substr` also exists as a bare global in leaf_builtins_ above (not
     * exposed by the pypoc front end, but reachable from hand-packed
     * bytecode); this entry is what makes it dispatchable as the str message
     * `s ! substr(start, count)` the corelib and doc/stdlib.md's `### str`
     * use. `dispatch_native_body_f` merges the receiver with the explicit
     * arguments, so the same native sees the same flat (s, start, count)
     * either way. */
    { "substr", WY_TYPE_TAG_STR,   builtin_substr_,       3, 3 },

    { "append",     WY_TYPE_TAG_BYTES, bytes_append_,      2, 2 },
    { "resize",     WY_TYPE_TAG_BYTES, bytes_resize_,      2, 2 },
    { "slice",      WY_TYPE_TAG_BYTES, bytes_slice_,       3, 3 },
    { "to_str",     WY_TYPE_TAG_BYTES, bytes_to_str_,      1, 1 },
    { "copy",       WY_TYPE_TAG_BYTES, bytes_copy_,        1, 1 },
    { "pack_u8",    WY_TYPE_TAG_BYTES, bytes_pack_u8_,     3, 3 },
    { "pack_i32",   WY_TYPE_TAG_BYTES, bytes_pack_i32_,    3, 3 },
    { "pack_u32",   WY_TYPE_TAG_BYTES, bytes_pack_u32_,    3, 3 },
    { "pack_f32",   WY_TYPE_TAG_BYTES, bytes_pack_f32_,    3, 3 },
    { "pack_f64",   WY_TYPE_TAG_BYTES, bytes_pack_f64_,    3, 3 },
    { "unpack_u8",  WY_TYPE_TAG_BYTES, bytes_unpack_u8_,   2, 2 },
    { "unpack_i32", WY_TYPE_TAG_BYTES, bytes_unpack_i32_,  2, 2 },
    { "unpack_u32", WY_TYPE_TAG_BYTES, bytes_unpack_u32_,  2, 2 },
    { "unpack_f32", WY_TYPE_TAG_BYTES, bytes_unpack_f32_,  2, 2 },
    { "unpack_f64", WY_TYPE_TAG_BYTES, bytes_unpack_f64_,  2, 2 },
};

static wy_error install_native_messages_(wy_context* context, wy_module* module)
{
    for (wy_uword i = 0; i < sizeof(native_messages_) / sizeof(native_messages_[0]); i++) {
        const builtin_message_entry_* entry = &native_messages_[i];

        wy_symbol sym;
        wy_error err = wy_context_intern(context, entry->name, wy_strlen_f(entry->name), &sym);
        if (err != WY_ERR_NONE) { return err; }

        wy_native* native = WY_NULL;
        err = wy_native_leaf_new(context, sym, entry->min_argc, entry->max_argc, entry->fn, &native);
        if (err != WY_ERR_NONE) { return err; }

        wy_message* msg = WY_NULL;
        err = wy_module_message_by_name_f(context, module, sym, &msg);
        if (err != WY_ERR_NONE) { return err; }

        wy_value types[1] = { wy_value_ptype(entry->receiver) };
        err = wy_message_add_overload_f(context, msg, 1, types,
            wy_value_object(WY_TYPE_TAG_NATIVE, (wy_object*) native));
        if (err != WY_ERR_NONE) { return err; }
    }
    return WY_ERR_NONE;
}

/**
 * The base `error` class and its four predefined subtypes
 * (doc/language-spec.md's Fundamental Types; wyrm_eval_parse_tree.py's
 * ERROR_CLASS/_builtin_error_subtype). `error` itself has no super (a
 * minimal object-root is epic 4 scope); the other four subclass it, so `x
 * is OutOfMemory` also answers true for `x is error` via ancestor-distance
 * (vm_ops.c's is_ancestor). All five carry WY_CLASS_ERROR so a future
 * INSTANCE of any of them (once epic 4 does construct-on-call) satisfies
 * is_error via its class chain, matching design_c_vm.md §4.
 */
typedef struct builtin_class_entry_
{
    const char* name;
    bool subclass_of_error;
} builtin_class_entry_;

static const builtin_class_entry_ error_classes_[] = {
    { "error",          false },
    { "OutOfMemory",    true },
    { "RuntimeError",   true },
    { "OSError",        true },
    { "StopIteration",  true },
};

wy_error wy_builtins_add(wy_context* context, const char* name, wy_value value)
{
    if (context == WY_NULL || name == WY_NULL || context->builtins == WY_NULL) { return WY_ERR_INVAL; }
    wy_module* module = context->builtins;
    if (module->global_count >= WY_BUILTINS_CAPACITY) { return WY_ERR_NOMEM; }

    wy_symbol sym;
    wy_error err = wy_context_intern(context, name, wy_strlen_f(name), &sym);
    if (err != WY_ERR_NONE) { return err; }
    if (wy_slot_dict_get(&module->exports, sym) != WY_SLOT_INVALID) { return WY_ERR_INVAL; }

    wy_uword slot = module->global_count;
    module->globals[slot] = value;
    err = wy_slot_dict_add_entry(&module->exports, sym, slot);
    if (err != WY_ERR_NONE) { return err; }
    module->global_count = slot + 1;
    return WY_ERR_NONE;
}

wy_error wy_builtins_new(wy_context* context, wy_module** out)
{
    if (context == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }

    wy_module* module = wy_module_new_f(context);
    if (module == WY_NULL) { return WY_ERR_NOMEM; }
    module->state = WY_MODULE_BUILTIN;

    /* Spare capacity beyond WY_BUILTINS_COUNT is for wy_builtins_add: host
     * extensions (e.g. the std::io natives) appended after construction. */
    module->global_count = WY_BUILTINS_COUNT;
    module->globals = wy_context_gc_alloc(context, sizeof(wy_value) * WY_BUILTINS_CAPACITY);
    module->fill_layer = wy_context_gc_alloc(context, sizeof(wy_u8) * WY_BUILTINS_CAPACITY);
    module->fill_source = wy_context_gc_alloc(context, sizeof(wy_symbol) * WY_BUILTINS_CAPACITY);
    if (module->globals == WY_NULL || module->fill_layer == WY_NULL || module->fill_source == WY_NULL) {
        return WY_ERR_NOMEM;
    }
    for (wy_uword i = 0; i < WY_BUILTINS_CAPACITY; i++) {
        module->globals[i] = wy_value_unset();
        module->fill_layer[i] = 0;
        module->fill_source[i] = WY_NULL;
    }

    wy_allocator* allocator = wy_context_get_machine(context)->allocator;
    wy_error err = wy_slot_dict_expand_f(&module->exports, allocator, WY_BUILTINS_CAPACITY * 2);
    if (err != WY_ERR_NONE) { return err; }

    /* Root `module` now: globals/fill_layer/fill_source/exports are all
     * consistent from here on, and the allocations below can hit a GC
     * safepoint. Without this,
     * `module` is unreachable (context->builtins is still whatever it was
     * before this call) until the caller assigns the return value - too
     * late once the VM has already run. Setting it here is a harmless
     * no-op for the caller, who writes the same pointer back. */
    context->builtins = module;

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

    for (wy_uword i = 0; i < sizeof(exec_builtins_) / sizeof(exec_builtins_[0]); i++) {
        const builtin_exec_entry_* entry = &exec_builtins_[i];

        wy_symbol sym;
        err = wy_context_intern(context, entry->name, wy_strlen_f(entry->name), &sym);
        if (err != WY_ERR_NONE) { return err; }

        wy_native* native = WY_NULL;
        err = wy_native_exec_new(context, sym, entry->min_argc, entry->max_argc,
            wy_exec_fn_create(entry->fn, wy_primitive_null()), &native);
        if (err != WY_ERR_NONE) { return err; }

        module->globals[slot] = wy_value_object(WY_TYPE_TAG_NATIVE, (wy_object*) native);
        err = wy_slot_dict_add_entry(&module->exports, sym, slot);
        if (err != WY_ERR_NONE) { return err; }
        slot++;
    }

    {
        wy_class* base_error_class = WY_NULL;
        for (wy_uword i = 0; i < sizeof(error_classes_) / sizeof(error_classes_[0]); i++) {
            const builtin_class_entry_* entry = &error_classes_[i];

            wy_symbol sym;
            err = wy_context_intern(context, entry->name, wy_strlen_f(entry->name), &sym);
            if (err != WY_ERR_NONE) { return err; }

            wy_class* cls = WY_NULL;
            err = wy_class_new(context, &cls);
            if (err != WY_ERR_NONE) { return err; }
            wy_class_set_name_f(cls, (wy_primitive) { .symtab_entry = sym });
            cls->flags = WY_CLASS_ERROR;
            cls->super = entry->subclass_of_error ? base_error_class : WY_NULL;
            if (!entry->subclass_of_error) { base_error_class = cls; }

            module->globals[slot] = wy_value_object(WY_TYPE_TAG_CLASS, (wy_object*) cls);
            err = wy_slot_dict_add_entry(&module->exports, sym, slot);
            if (err != WY_ERR_NONE) { return err; }
            slot++;

            if (wy_strcmp_f(entry->name, "StopIteration") == 0) { context->stop_iteration_class = cls; }
            if (wy_strcmp_f(entry->name, "OSError") == 0) { context->os_error_class = cls; }
        }
        context->error_class = base_error_class;
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

    {
        /* Bare `TreeBase` class (wyrm_builtins.py's TREE_BASE_CLASS): the
         * decorator convention's receiver type. A decorator's message is
         * `fn [TreeBase] name(...)` (wyc-format.md §8's message type
         * constraint), but decorators run at compile time - no fixture
         * needs a real TreeBase instance yet, so this stays a bare class
         * with no slots (unlike the reference's single `__tree` slot),
         * matching how the error classes above are also bare. */
        wy_symbol sym;
        err = wy_context_intern(context, "TreeBase", 8, &sym);
        if (err != WY_ERR_NONE) { return err; }

        wy_class* cls = WY_NULL;
        err = wy_class_new(context, &cls);
        if (err != WY_ERR_NONE) { return err; }
        wy_class_set_name_f(cls, (wy_primitive) { .symtab_entry = sym });

        /* The one `__tree` slot carrying the boxed sexpr (tree_box/sexpr). */
        wy_symbol tree_sym;
        err = wy_context_intern(context, "__tree", 6, &tree_sym);
        if (err != WY_ERR_NONE) { return err; }
        cls->slots = wy_context_gc_alloc(context, sizeof(wy_class_slot));
        if (cls->slots == WY_NULL) { return WY_ERR_NOMEM; }
        cls->slots[0].name = tree_sym;
        cls->slots[0].default_value = wy_value_nil();
        cls->slots[0].getter = wy_value_unset();
        cls->slots[0].setter = wy_value_unset();
        cls->slot_count = 1;
        context->tree_base_class = cls;

        module->globals[slot] = wy_value_object(WY_TYPE_TAG_CLASS, (wy_object*) cls);
        err = wy_slot_dict_add_entry(&module->exports, sym, slot);
        if (err != WY_ERR_NONE) { return err; }
        slot++;
    }

    WY_ASSERT(slot == WY_BUILTINS_COUNT);

    err = install_native_messages_(context, module);
    if (err != WY_ERR_NONE) { return err; }

    *out = module;
    return WY_ERR_NONE;
}
