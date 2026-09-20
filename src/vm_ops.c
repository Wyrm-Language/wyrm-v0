#include "vm_internal.h"

#include <math.h>

#include <wyrm.h>
#include <wyrm/error.h>
#include <wyrm/string.h>

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
 * `is` (wyc-format.md §6.3): `a2` names a primitive type by string (the only
 * form the epic-2/M4 target fixtures use), a class value, or a tuple of
 * either. Class/tuple forms are epic 3/4; this only resolves the primitive
 * name case, faulting (via a returned error) on the rest.
 */
wy_error wy_vm_is_f(wy_context* ctx, wy_value value, wy_value type_operand, wy_value* out)
{
    if (type_operand.type != WY_TYPE_TAG_STR) {
        return make_error_value_f(ctx, "`is` against a class or tuple is not supported until epic 3/4", out);
    }
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
    } else if (wy_strncmp_f(name->str, "symbol", name->len) == 0 && name->len == 6) {
        match = value.type == WY_TYPE_TAG_SYMBOL;
    } else if (wy_strncmp_f(name->str, "error", name->len) == 0 && name->len == 5) {
        match = wy_value_is_error(value);
    } else {
        return make_error_value_f(ctx, "unknown primitive type name", out);
    }
    *out = wy_value_bool(match);
    return WY_ERR_NONE;
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
