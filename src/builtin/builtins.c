#include <wyrm/builtins.h>

#include <wyrm/class.h>
#include <wyrm/context.h>
#include <wyrm/coroutine.h>
#include <wyrm/dict.h>
#include <wyrm/error.h>
#include <wyrm/image.h>
#include <wyrm/list.h>
#include <wyrm/machine.h>
#include <wyrm/module.h>
#include <wyrm/native.h>
#include <wyrm/pair.h>
#include <wyrm/string.h>
#include <wyrm/sys/string.h>
#include <wyrm/tuple.h>
#include <wyrm/value.h>
#include <wyrm/vm.h>

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

/** Write `len` bytes through the context's output hook, a no-op if unset. */
static void io_write_(wy_context* context, const char* bytes, wy_uword len)
{
    if (context->io.write == WY_NULL || len == 0) { return; }
    context->io.write(context, bytes, len, context->io.ud);
}

static void io_write_str_(wy_context* context, const char* cstr)
{
    if (cstr == WY_NULL) return;
    io_write_(context, cstr, wy_strlen_f(cstr));
}

/**
 * Format one value and write it to context's IO.
 */
static wy_error format_value_f(wy_context* context, wy_value value)
{
    char buf[WY_BUILTIN_FMT_BUF_SIZE];
    wy_uword len = 0;

    switch (value.type) {
    case WY_TYPE_TAG_STR: {
        wy_string* str = value.data.str;
        if (str != WY_NULL) io_write_(context, str->str, str->len);
        break;
    }
    case WY_TYPE_TAG_NIL: io_write_str_(context, "nil"); break;
    case WY_TYPE_TAG_BOOL: io_write_str_(context, value.data.flag ? "true" : "false"); break;
    case WY_TYPE_TAG_WORD:
        len = (wy_uword) snprintf(buf, sizeof(buf), "%lld", (long long) value.data.word);
        io_write_(context, buf, len);
        break;
    case WY_TYPE_TAG_UWORD:
        len = (wy_uword) snprintf(buf, sizeof(buf), "%llu", (unsigned long long) value.data.uword);
        io_write_(context, buf, len);
        break;
    case WY_TYPE_TAG_FLOAT:
        len = format_float_((double) value.data.fp, buf, sizeof(buf));
        io_write_(context, buf, len);
        break;
    case WY_TYPE_TAG_SYMBOL:
        io_write_str_(context, value.data.symtab_entry);
        break;
    case WY_TYPE_TAG_ERROR:
        if (value.data.gc_object == WY_NULL) io_write_str_(context, "Unset");
        else io_write_str_(context, "error");
        break;
    case WY_TYPE_TAG_LIST: {
        wy_list* list = (wy_list*) value.data.gc_object;
        io_write_str_(context, "[");
        for (wy_uword i = 0; i < list->count; i++) {
            if (i > 0) io_write_str_(context, ", ");
            format_value_f(context, list->items[i]);
        }
        io_write_str_(context, "]");
        break;
    }
    case WY_TYPE_TAG_TUPLE: {
        wy_tuple* tup = (wy_tuple*) value.data.gc_object;
        io_write_str_(context, "(");
        for (wy_uword i = 0; i < tup->count; i++) {
            if (i > 0) io_write_str_(context, ", ");
            format_value_f(context, tup->items[i]);
        }
        io_write_str_(context, ")");
        break;
    }
    case WY_TYPE_TAG_TABLE: {
        wy_dict* dict = (wy_dict*) value.data.gc_object;
        io_write_str_(context, "{");
        for (wy_uword i = 0; i < dict->count; i++) {
            if (i > 0) io_write_str_(context, ", ");
            format_value_f(context, dict->dense[i].key);
            io_write_str_(context, ": ");
            format_value_f(context, dict->dense[i].value);
        }
        io_write_str_(context, "}");
        break;
    }
    case WY_TYPE_TAG_PAIR: {
        wy_pair* p = (wy_pair*) value.data.gc_object;
        io_write_str_(context, "(");
        format_value_f(context, p->car);
        io_write_str_(context, " . ");
        format_value_f(context, p->cdr);
        io_write_str_(context, ")");
        break;
    }
    default:
        io_write_str_(context, "<object>");
        break;
    }
    return WY_ERR_NONE;
}

/** Shared body for `print`/`println`: space-join every arg's rendering. */
static wy_error write_joined_(wy_context* context, wy_value* args, wy_uword argc, bool trailing_newline)
{
    for (wy_uword i = 0; i < argc; i++) {
        if (i > 0) { io_write_str_(context, " "); }
        format_value_f(context, args[i]);
    }
    if (trailing_newline) { io_write_str_(context, "\n"); }
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
    if (co_v.type != WY_TYPE_TAG_COROUTINE) {
        return coroutine_fault_(context, fiber, "next: argument is not a coroutine");
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

enum { WY_BUILTINS_LEAF_COUNT = 18, WY_BUILTINS_EXEC_COUNT = 2, WY_BUILTINS_CLASS_COUNT = 5,
    /* +1 for the bare `nil` slot, +1 for the `range` prelude coroutine (M4),
     * +1 for the bare `TreeBase` class (M5: decorators fixture registers
     * messages typed on it, wyrm_builtins.py's TREE_BASE_CLASS). */
    WY_BUILTINS_COUNT = WY_BUILTINS_LEAF_COUNT + WY_BUILTINS_EXEC_COUNT + WY_BUILTINS_CLASS_COUNT + 1 + 1 + 1 };

/* Compiled from test/bytecode/embedded/range.wy the same way
 * test/bytecode/embedded/hello_1.c is generated (scripts/build_corpus.py's
 * build_embedded_c, run by hand for this one-off prelude - it is not one of
 * the fixtures that script regenerates automatically). A real .wyc image so
 * `range` is an ordinary coroutine function, not a native leaf/exec type:
 * design_c_vm.md's M4 note says a first-class iterable is easiest once
 * classes/coroutines work, and the reference (pypoc/wypoc/corelib/prelude.wy)
 * defines it in wyrm source for the same reason. */
extern const wy_module_image range_1_image;

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
    case WY_TYPE_TAG_FLOAT: len = format_float_((double) value.data.fp, buffer, sizeof(buffer)); break;
    case WY_TYPE_TAG_SYMBOL: text = value.data.symtab_entry; len = wy_strlen_f(text); break;
    default: return WY_ERR_BAD_TYPE;
    }
    wy_string* result = WY_NULL;
    wy_error err = wy_string_new(context, text, len, &result);
    if (err == WY_ERR_NONE) { out[0] = wy_value_object(WY_TYPE_TAG_STR, (wy_object*) result); }
    return err;
}

static const builtin_leaf_entry_ leaf_builtins_[] = {
    { "println",   0, 255, builtin_println_ },
    { "print",     0, 255, builtin_print_ },
    { "str",       1, 1,   builtin_str_ },
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
};

static const builtin_exec_entry_ exec_builtins_[] = {
    { "next", 1, 1, builtin_next_exec_ },
    { "send", 2, 2, builtin_send_exec_ },
};

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

    /* Root `module` now: globals/fill_layer/fill_source/exports are all
     * consistent from here on, and the range prelude's wy_module_run_init
     * below runs real bytecode, which can hit a GC safepoint. Without this,
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

        module->globals[slot] = wy_value_object(WY_TYPE_TAG_CLASS, (wy_object*) cls);
        err = wy_slot_dict_add_entry(&module->exports, sym, slot);
        if (err != WY_ERR_NONE) { return err; }
        slot++;
    }

    {
        /* Load the compiled `range` prelude as an ordinary module, run its
         * one-time init (defines the coroutine function and gsets it into
         * that module's own global 0), then copy the resulting function
         * value into a builtins slot so bare `range(...)` resolves through
         * the same layer-3 fill_from_builtins path as `println`/`str` -
         * no `import` required (epic_5.md M4's "builtin-adjacent module"). */
        wy_module* range_module = WY_NULL;
        err = wy_module_load_image(context, &range_1_image, &range_module);
        if (err != WY_ERR_NONE) { return err; }
        /* Root range_module before running its init, for the same GC-safety
         * reason as context->builtins above: an unregistered, unreferenced
         * module is invisible to the root scan while its own bytecode runs. */
        err = wy_context_module_register(context, range_module, WY_NULL);
        if (err != WY_ERR_NONE) { return err; }
        err = wy_module_run_init(context, range_module);
        if (err != WY_ERR_NONE) { return err; }

        wy_symbol sym;
        err = wy_context_intern(context, "range", 5, &sym);
        if (err != WY_ERR_NONE) { return err; }

        wy_uword range_slot = wy_slot_dict_get(&range_module->exports, sym);
        WY_ASSERT(range_slot != WY_SLOT_INVALID);

        module->globals[slot] = range_module->globals[range_slot];
        err = wy_slot_dict_add_entry(&module->exports, sym, slot);
        if (err != WY_ERR_NONE) { return err; }
        slot++;
    }

    WY_ASSERT(slot == WY_BUILTINS_COUNT);

    *out = module;
    return WY_ERR_NONE;
}
