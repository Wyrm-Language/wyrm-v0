#include "vm_internal.h"

#include <math.h>

#include <wyrm.h>
#include <wyrm/bytes.h>
#include <wyrm/dict.h>
#include <wyrm/error.h>
#include <wyrm/instance.h>
#include <wyrm/list.h>
#include <wyrm/op.h>
#include <wyrm/pair.h>
#include <wyrm/string.h>
#include <wyrm/tuple.h>

/**
 * Arithmetic, comparison and `is`/`cmp3` semantics for the dispatch loop
 * (pypoc/doc/wyc-format.md §6.3, pypoc/wypoc/wyrm_eval_parse_tree.py's
 * BINOPS table). Scope: WORD/UWORD/FLOAT/STR/BOOL operands, the only kinds
 * the six epic-2/M4 target fixtures exercise; INSTANCE operand dispatch
 * (`__add__`-family overloads) is epic 3/4.
 */

static wy_error make_error_value_f(wy_context* ctx, const char* message, wy_value* out)
{
    wy_string* what = WY_NULL;
    wy_error err = wy_string_strdup(ctx, message, &what);
    if (err != WY_ERR_NONE) { return err; }

    wy_error_obj* obj = WY_NULL;
    err = wy_error_obj_new(ctx, WY_NULL, what, wy_value_nil(), &obj);
    if (err != WY_ERR_NONE) { return err; }

    *out = wy_value_object(WY_TYPE_TAG_ERROR, (wy_object*) obj);
    return WY_ERR_NONE;
}

WY_INLINE bool is_numeric_(wy_value v) { return v.type == WY_TYPE_TAG_WORD || v.type == WY_TYPE_TAG_UWORD || v.type == WY_TYPE_TAG_FLOAT; }
WY_INLINE double as_double_(wy_value v) { return v.type == WY_TYPE_TAG_FLOAT ? v.data.fp : (v.type == WY_TYPE_TAG_UWORD ? (double) v.data.uword : (double) v.data.word); }
WY_INLINE wy_word as_word_(wy_value v) { return v.type == WY_TYPE_TAG_UWORD ? (wy_word) v.data.uword : v.data.word; }

static wy_word int_pow_(wy_word base, wy_word exp)
{
    if (exp <= 0) { return 1; }
    wy_word result = 1;
    while (exp > 0) {
        if (exp & 1) { result *= base; }
        base *= base;
        exp >>= 1;
    }
    return result;
}

/** `<`/`<=`/`>`/`>=`/`==`/`!=` and cmp3's -1/0/1, across WORD/FLOAT/STR/BOOL. */
static wy_error compare_f(wy_context* ctx, wy_value lhs, wy_value rhs, int* out_cmp, bool* out_comparable)
{
    WY_UNUSED(ctx);
    *out_comparable = true;
    if (is_numeric_(lhs) && is_numeric_(rhs)) {
        if (lhs.type == WY_TYPE_TAG_FLOAT || rhs.type == WY_TYPE_TAG_FLOAT) {
            double a = as_double_(lhs), b = as_double_(rhs);
            *out_cmp = (a > b) - (a < b);
        } else {
            wy_word a = as_word_(lhs), b = as_word_(rhs);
            *out_cmp = (a > b) - (a < b);
        }
        return WY_ERR_NONE;
    }
    if (lhs.type == WY_TYPE_TAG_STR && rhs.type == WY_TYPE_TAG_STR) {
        int c = wy_string_cmp_f(lhs.data.str, rhs.data.str);
        *out_cmp = (c > 0) - (c < 0);
        return WY_ERR_NONE;
    }
    if (lhs.type == WY_TYPE_TAG_BOOL && rhs.type == WY_TYPE_TAG_BOOL) {
        *out_cmp = (int) lhs.data.flag - (int) rhs.data.flag;
        return WY_ERR_NONE;
    }
    if (lhs.type == WY_TYPE_TAG_NIL && rhs.type == WY_TYPE_TAG_NIL) {
        /* nil is a singleton; two nils are always equal. Previously missing
         * here entirely, so `nil == nil` fell through to the "not
         * comparable" default and WY_OP_EQ's own not-comparable fallback
         * (always false) - `nil is nil` worked (a separate code path,
         * wy_vm_is_f) which is why this went unnoticed until epic 9 hit it. */
        *out_cmp = 0;
        return WY_ERR_NONE;
    }
    if (lhs.type == WY_TYPE_TAG_SYMBOL && rhs.type == WY_TYPE_TAG_SYMBOL) {
        /* Interned, so pointer identity is content equality. Same missing-
         * case bug as nil above - 'foo == 'foo was also always false. */
        *out_cmp = (lhs.data.symtab_entry == rhs.data.symtab_entry) ? 0 : 1;
        return WY_ERR_NONE;
    }
    if (lhs.type == WY_TYPE_TAG_BYTES && rhs.type == WY_TYPE_TAG_BYTES) {
        /* doc/stdlib.md: `==` is byte-for-byte. Ordering (<, cmp3, ...) is
         * not spec'd for bytes; a lexicographic compare (shared prefix,
         * then length) is the least-surprising choice and makes `==`'s
         * "equal length and contents" fall out as `cmp == 0`, matching
         * wy_op_eq's BYTES case (include/wyrm/op.h) by construction. */
        wy_bytes* a = (wy_bytes*) lhs.data.gc_object;
        wy_bytes* b = (wy_bytes*) rhs.data.gc_object;
        wy_uword shared = a->len < b->len ? a->len : b->len;
        int c = (shared == 0) ? 0 : wy_memcmp(a->data, b->data, shared);
        *out_cmp = (c != 0) ? ((c > 0) - (c < 0)) : (int) ((a->len > b->len) - (a->len < b->len));
        return WY_ERR_NONE;
    }
    *out_comparable = false;
    return WY_ERR_NONE;
}

wy_error wy_vm_binop_f(wy_context* ctx, wy_u8 op, wy_value lhs, wy_value rhs, wy_value* out)
{
    switch (op) {
    case WY_OP_ADD:
        if (lhs.type == WY_TYPE_TAG_STR && rhs.type == WY_TYPE_TAG_STR) {
            wy_string* cat = WY_NULL;
            wy_error err = wy_string_concat(ctx, lhs.data.str, rhs.data.str, &cat);
            if (err != WY_ERR_NONE) { return err; }
            *out = wy_value_object(WY_TYPE_TAG_STR, (wy_object*) cat);
            return WY_ERR_NONE;
        }
        break;
    default: break;
    }

    if (is_numeric_(lhs) && is_numeric_(rhs)) {
        bool is_float = (lhs.type == WY_TYPE_TAG_FLOAT || rhs.type == WY_TYPE_TAG_FLOAT);
        double a = as_double_(lhs), b = as_double_(rhs);
        wy_word ai = as_word_(lhs), bi = as_word_(rhs);

        switch (op) {
        case WY_OP_ADD: *out = is_float ? wy_value_float(a + b) : wy_value_word(ai + bi); return WY_ERR_NONE;
        case WY_OP_SUB: *out = is_float ? wy_value_float(a - b) : wy_value_word(ai - bi); return WY_ERR_NONE;
        case WY_OP_MUL: *out = is_float ? wy_value_float(a * b) : wy_value_word(ai * bi); return WY_ERR_NONE;
        case WY_OP_DIV:  /* true division: always float, per BINOPS' operator.truediv */
            if (b == 0.0) { return make_error_value_f(ctx, "division by zero", out); }
            *out = wy_value_float(a / b);
            return WY_ERR_NONE;
        case WY_OP_MOD:
            if (is_float) {
                if (b == 0.0) { return make_error_value_f(ctx, "modulo by zero", out); }
                double r = fmod(a, b);
                if (r != 0.0 && ((r < 0) != (b < 0))) { r += b; }  /* Python modulo sign */
                *out = wy_value_float(r);
            } else {
                if (bi == 0) { return make_error_value_f(ctx, "modulo by zero", out); }
                wy_word r = ai % bi;
                if (r != 0 && ((r < 0) != (bi < 0))) { r += bi; }
                *out = wy_value_word(r);
            }
            return WY_ERR_NONE;
        case WY_OP_POW:
            *out = is_float ? wy_value_float(pow(a, b)) : wy_value_word(int_pow_(ai, bi));
            return WY_ERR_NONE;
        case WY_OP_BAND: *out = wy_value_word(ai & bi); return WY_ERR_NONE;
        case WY_OP_BOR:  *out = wy_value_word(ai | bi); return WY_ERR_NONE;
        case WY_OP_BXOR: *out = wy_value_word(ai ^ bi); return WY_ERR_NONE;
        case WY_OP_SHL:
            if (bi < 0) { return make_error_value_f(ctx, "negative shift count", out); }
            *out = wy_value_word(ai << bi);
            return WY_ERR_NONE;
        case WY_OP_SHR:
            if (bi < 0) { return make_error_value_f(ctx, "negative shift count", out); }
            *out = wy_value_word(ai >> bi);
            return WY_ERR_NONE;
        default: break;
        }
    }

    /* Comparisons work across the wider set (STR, BOOL) that arithmetic doesn't. */
    switch (op) {
    case WY_OP_EQ: case WY_OP_NE: case WY_OP_LT: case WY_OP_LE: case WY_OP_GT: case WY_OP_GE: case WY_OP_CMP3: {
        int cmp = 0; bool comparable = false;
        wy_error err = compare_f(ctx, lhs, rhs, &cmp, &comparable);
        if (err != WY_ERR_NONE) { return err; }
        if (!comparable) {
            if (op == WY_OP_EQ) { *out = wy_value_bool(false); return WY_ERR_NONE; }
            if (op == WY_OP_NE) { *out = wy_value_bool(true); return WY_ERR_NONE; }
            return make_error_value_f(ctx, "values are not comparable", out);
        }
        switch (op) {
        case WY_OP_EQ:   *out = wy_value_bool(cmp == 0); return WY_ERR_NONE;
        case WY_OP_NE:   *out = wy_value_bool(cmp != 0); return WY_ERR_NONE;
        case WY_OP_LT:   *out = wy_value_bool(cmp < 0);  return WY_ERR_NONE;
        case WY_OP_LE:   *out = wy_value_bool(cmp <= 0); return WY_ERR_NONE;
        case WY_OP_GT:   *out = wy_value_bool(cmp > 0);  return WY_ERR_NONE;
        case WY_OP_GE:   *out = wy_value_bool(cmp >= 0); return WY_ERR_NONE;
        case WY_OP_CMP3: *out = wy_value_word(cmp);      return WY_ERR_NONE;
        default: break;
        }
    }
    default: break;
    }

    return make_error_value_f(ctx, "unsupported operand types", out);
}

/**
 * `is` (wyc-format.md §6.3): `a2` names a primitive type by string, a class
 * value, or a tuple of either.
 */
wy_error wy_vm_is_f(wy_context* ctx, wy_value value, wy_value type_operand, wy_value* out)
{
    if (type_operand.type == WY_TYPE_TAG_STR) {
        wy_string* name = type_operand.data.str;
        bool match = false;
        if (wy_strncmp_f(name->str, "int", name->len) == 0 && name->len == 3) {
            match = value.type == WY_TYPE_TAG_WORD || value.type == WY_TYPE_TAG_UWORD;
        } else if (wy_strncmp_f(name->str, "float", name->len) == 0 && name->len == 5) {
            match = value.type == WY_TYPE_TAG_FLOAT;
        } else if (wy_strncmp_f(name->str, "bool", name->len) == 0 && name->len == 4) {
            match = value.type == WY_TYPE_TAG_BOOL;
        } else if (wy_strncmp_f(name->str, "str", name->len) == 0 && name->len == 3) {
            match = value.type == WY_TYPE_TAG_STR;
        } else if (wy_strncmp_f(name->str, "nil", name->len) == 0 && name->len == 3) {
            match = value.type == WY_TYPE_TAG_NIL;
        } else if ((wy_strncmp_f(name->str, "sym", name->len) == 0 && name->len == 3)
                   || (wy_strncmp_f(name->str, "symbol", name->len) == 0 && name->len == 6)) {
            /* "sym" is the spelling the language actually uses (pypoc's
             * PRIMITIVE_TYPES: str/int/float/bool/sym/bytes); only the
             * longer "symbol" was listed here, so `x is sym` returned an
             * "unknown primitive type name" error value for EVERY value,
             * which an `if` then took as true. Found by epic 10's M1 port,
             * whose sexpr walks ask `node is sym` constantly - the same
             * omission epic 9 found for `bytes` just above. */
            match = value.type == WY_TYPE_TAG_SYMBOL;
        } else if (wy_strncmp_f(name->str, "error", name->len) == 0 && name->len == 5) {
            match = wy_value_is_error(value);
        } else if (wy_strncmp_f(name->str, "list", name->len) == 0 && name->len == 4) {
            match = value.type == WY_TYPE_TAG_LIST;
        } else if (wy_strncmp_f(name->str, "tuple", name->len) == 0 && name->len == 5) {
            match = value.type == WY_TYPE_TAG_TUPLE;
        } else if (wy_strncmp_f(name->str, "dict", name->len) == 0 && name->len == 4) {
            match = value.type == WY_TYPE_TAG_TABLE;
        } else if (wy_strncmp_f(name->str, "pair", name->len) == 0 && name->len == 4) {
            match = value.type == WY_TYPE_TAG_PAIR;
        } else if (wy_strncmp_f(name->str, "bytes", name->len) == 0 && name->len == 5) {
            /* epic 7 added the bytes primitive type and epic 9's M1 found
             * this was missing here: `x is bytes` compiled fine (pypoc's
             * own primitive-type table already lists "bytes") but faulted
             * "unknown primitive type name" at runtime on the C VM. */
            match = value.type == WY_TYPE_TAG_BYTES;
        } else {
            return make_error_value_f(ctx, "unknown primitive type name", out);
        }
        *out = wy_value_bool(match);
        return WY_ERR_NONE;
    }

    if (type_operand.type == WY_TYPE_TAG_CLASS) {
        wy_class* target = (wy_class*) type_operand.data.gc_object;
        if (value.type == WY_TYPE_TAG_ERROR && value.data.gc_object) {
            wy_error_obj* err = (wy_error_obj*) value.data.gc_object;
            *out = wy_value_bool(wy_class_is_ancestor_f(err->cls, target));
            return WY_ERR_NONE;
        }
        if (value.type == WY_TYPE_TAG_INSTANCE && value.data.gc_object) {
             wy_instance* inst = (wy_instance*) value.data.gc_object;
             *out = wy_value_bool(wy_class_is_ancestor_f(inst->cls, target));
             return WY_ERR_NONE;
        }
        *out = wy_value_bool(false);
        return WY_ERR_NONE;
    }

    if (type_operand.type == WY_TYPE_TAG_TUPLE) {
        wy_tuple* tup = (wy_tuple*) type_operand.data.gc_object;
        for (wy_uword i = 0; i < tup->count; i++) {
            wy_value match_val;
            wy_error err = wy_vm_is_f(ctx, value, tup->items[i], &match_val);
            if (err != WY_ERR_NONE) return err;
            if (wy_value_truthy(match_val)) {
                *out = wy_value_bool(true);
                return WY_ERR_NONE;
            }
        }
        *out = wy_value_bool(false);
        return WY_ERR_NONE;
    }

    return make_error_value_f(ctx, "invalid type operand for `is`", out);
}

wy_error wy_vm_getidx_f(wy_context* ctx, wy_value obj, wy_value idx, wy_value* out)
{
    if (obj.type == WY_TYPE_TAG_LIST) {
        if (idx.type != WY_TYPE_TAG_WORD && idx.type != WY_TYPE_TAG_UWORD) {
            return make_error_value_f(ctx, "list index must be an integer", out);
        }
        wy_uword i = (wy_uword) as_word_(idx);
        wy_value* v = wy_list_at_f((wy_list*) obj.data.gc_object, i);
        if (!v) return make_error_value_f(ctx, "list index out of range", out);
        *out = *v;
        return WY_ERR_NONE;
    }
    if (obj.type == WY_TYPE_TAG_TUPLE) {
        if (idx.type != WY_TYPE_TAG_WORD && idx.type != WY_TYPE_TAG_UWORD) {
            return make_error_value_f(ctx, "tuple index must be an integer", out);
        }
        wy_uword i = (wy_uword) as_word_(idx);
        wy_value* v = wy_tuple_at_f((wy_tuple*) obj.data.gc_object, i);
        if (!v) return make_error_value_f(ctx, "tuple index out of range", out);
        *out = *v;
        return WY_ERR_NONE;
    }
    if (obj.type == WY_TYPE_TAG_TABLE) {
        wy_value* v = wy_dict_get(ctx, (wy_dict*) obj.data.gc_object, idx.type, idx.data);
        if (!v) return make_error_value_f(ctx, "key not found in dict", out);
        *out = *v;
        return WY_ERR_NONE;
    }
    if (obj.type == WY_TYPE_TAG_STR) {
        if (idx.type != WY_TYPE_TAG_WORD && idx.type != WY_TYPE_TAG_UWORD) {
            return make_error_value_f(ctx, "string index must be an integer", out);
        }
        wy_uword target = (wy_uword) as_word_(idx);
        wy_string* s = obj.data.str;
        wy_uword offset = 0, i = 0;
        for (; offset < s->len && i < target; i++) {
            offset += wy_utf8_decode_f(s->str, s->len, offset, &(wy_u32){0});
        }
        if (offset >= s->len || i != target) return make_error_value_f(ctx, "string index out of range", out);
        wy_u32 cp;
        wy_utf8_decode_f(s->str, s->len, offset, &cp);
        *out = wy_value_word((wy_word) cp);
        return WY_ERR_NONE;
    }
    if (obj.type == WY_TYPE_TAG_PAIR) {
        if (idx.type != WY_TYPE_TAG_WORD && idx.type != WY_TYPE_TAG_UWORD) {
            return make_error_value_f(ctx, "pair index must be an integer", out);
        }
        if (as_word_(idx) != 0) return make_error_value_f(ctx, "pair index out of range", out);
        *out = ((wy_pair*) obj.data.gc_object)->car;
        return WY_ERR_NONE;
    }
    if (obj.type == WY_TYPE_TAG_BYTES) {
        if (idx.type != WY_TYPE_TAG_WORD && idx.type != WY_TYPE_TAG_UWORD) {
            return make_error_value_f(ctx, "bytes index must be an integer", out);
        }
        wy_bytes* b = (wy_bytes*) obj.data.gc_object;
        wy_word i = as_word_(idx);
        if (i < 0 || (wy_uword) i >= b->len) return make_error_value_f(ctx, "bytes index out of range", out);
        *out = wy_value_word(b->data[i]);
        return WY_ERR_NONE;
    }
    return make_error_value_f(ctx, "object is not indexable", out);
}

wy_error wy_vm_setidx_f(wy_context* ctx, wy_value obj, wy_value idx, wy_value src)
{
    wy_value dummy;
    if (obj.type == WY_TYPE_TAG_LIST) {
        if (idx.type != WY_TYPE_TAG_WORD && idx.type != WY_TYPE_TAG_UWORD) {
            return make_error_value_f(ctx, "list index must be an integer", &dummy);
        }
        wy_uword i = (wy_uword) as_word_(idx);
        wy_error err = wy_list_set((wy_list*) obj.data.gc_object, i, src);
        if (err != WY_ERR_NONE) return make_error_value_f(ctx, "list index out of range", &dummy);
        return WY_ERR_NONE;
    }
    if (obj.type == WY_TYPE_TAG_TABLE) {
        wy_error err = wy_dict_set(ctx, (wy_dict*) obj.data.gc_object, idx.type, idx.data, src.type, src.data);
        if (err != WY_ERR_NONE) return make_error_value_f(ctx, "dict set failed", &dummy);
        return WY_ERR_NONE;
    }
    if (obj.type == WY_TYPE_TAG_PAIR) {
        if (idx.type != WY_TYPE_TAG_WORD && idx.type != WY_TYPE_TAG_UWORD) {
            return make_error_value_f(ctx, "pair index must be an integer", &dummy);
        }
        if (as_word_(idx) != 0) return make_error_value_f(ctx, "pair index out of range", &dummy);
        ((wy_pair*) obj.data.gc_object)->car = src;
        return WY_ERR_NONE;
    }
    if (obj.type == WY_TYPE_TAG_BYTES) {
        if (idx.type != WY_TYPE_TAG_WORD && idx.type != WY_TYPE_TAG_UWORD) {
            return make_error_value_f(ctx, "bytes index must be an integer", &dummy);
        }
        if (src.type != WY_TYPE_TAG_WORD && src.type != WY_TYPE_TAG_UWORD) {
            return make_error_value_f(ctx, "bytes value must be an integer", &dummy);
        }
        wy_bytes* b = (wy_bytes*) obj.data.gc_object;
        wy_word i = as_word_(idx);
        wy_word v = as_word_(src);
        if (i < 0 || (wy_uword) i >= b->len) return make_error_value_f(ctx, "bytes index out of range", &dummy);
        if (v < 0 || v > 255) return make_error_value_f(ctx, "bytes value must be 0-255", &dummy);
        b->data[i] = (wy_u8) v;
        return WY_ERR_NONE;
    }
    return make_error_value_f(ctx, "object does not support item assignment", &dummy);
}

wy_error wy_vm_in_f(wy_context* ctx, wy_value item, wy_value container, wy_value* out)
{
    if (container.type == WY_TYPE_TAG_LIST) {
        wy_list* list = (wy_list*) container.data.gc_object;
        for (wy_uword i = 0; i < list->count; i++) {
            if (wy_op_eq(ctx, item.type, item.data, list->items[i].type, list->items[i].data)) {
                *out = wy_value_bool(true);
                return WY_ERR_NONE;
            }
        }
        *out = wy_value_bool(false);
        return WY_ERR_NONE;
    }
    if (container.type == WY_TYPE_TAG_TUPLE) {
        wy_tuple* tup = (wy_tuple*) container.data.gc_object;
        for (wy_uword i = 0; i < tup->count; i++) {
            if (wy_op_eq(ctx, item.type, item.data, tup->items[i].type, tup->items[i].data)) {
                *out = wy_value_bool(true);
                return WY_ERR_NONE;
            }
        }
        *out = wy_value_bool(false);
        return WY_ERR_NONE;
    }
    if (container.type == WY_TYPE_TAG_TABLE) {
        wy_value* v = wy_dict_get(ctx, (wy_dict*) container.data.gc_object, item.type, item.data);
        *out = wy_value_bool(v != WY_NULL);
        return WY_ERR_NONE;
    }
    if (container.type == WY_TYPE_TAG_STR) {
        if (item.type != WY_TYPE_TAG_STR) {
             return make_error_value_f(ctx, "'in <string>' requires string as left operand", out);
        }
        wy_string* s = container.data.str;
        wy_string* sub = item.data.str;
        if (sub->len == 0) { *out = wy_value_bool(true); return WY_ERR_NONE; }
        if (sub->len > s->len) { *out = wy_value_bool(false); return WY_ERR_NONE; }
        
        bool found = false;
        for (wy_uword i = 0; i <= s->len - sub->len; i++) {
            if (wy_memcmp(s->str + i, sub->str, sub->len) == 0) {
                found = true;
                break;
            }
        }
        *out = wy_value_bool(found);
        return WY_ERR_NONE;
    }
    return make_error_value_f(ctx, "object is not a container", out);
}

wy_error wy_vm_unary_f(wy_context* ctx, wy_u8 op, wy_value src, wy_value* out)
{
    switch (op) {
    case WY_OP_NEG:
        if (src.type == WY_TYPE_TAG_FLOAT) { *out = wy_value_float(-src.data.fp); return WY_ERR_NONE; }
        if (is_numeric_(src)) { *out = wy_value_word(-as_word_(src)); return WY_ERR_NONE; }
        return make_error_value_f(ctx, "unsupported operand type for negation", out);
    case WY_OP_INV:
        if (is_numeric_(src) && src.type != WY_TYPE_TAG_FLOAT) { *out = wy_value_word(~as_word_(src)); return WY_ERR_NONE; }
        return make_error_value_f(ctx, "unsupported operand type for bitwise inverse", out);
    case WY_OP_NOT:
        *out = wy_value_bool(!wy_value_truthy(src));
        return WY_ERR_NONE;
    default:
        return make_error_value_f(ctx, "unsupported unary opcode", out);
    }
}
