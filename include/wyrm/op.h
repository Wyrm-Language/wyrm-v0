#ifndef WYRM_OP_H_
#define WYRM_OP_H_

#include <wyrm/primitive.h>
#include <wyrm/string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WY_HASH_INVALID WY_UWORD_MAX

WY_INLINE bool wy_op_eq(wy_state* state, wy_type_tag lhst, wy_primitive lhs, wy_type_tag rhst, wy_primitive rhs);
WY_INLINE wy_uword wy_op_hash(wy_state* state, wy_type_tag vt, wy_primitive v);


WY_INLINE bool wy_op_eq(wy_state* state, wy_type_tag lhst, wy_primitive lhs, wy_type_tag rhst, wy_primitive rhs)
{
    WY_UNUSED(state);
    if (lhst != rhst) { return false; }
    switch (lhst) {
    case WY_TYPE_TAG_NIL:
        /* primitive value of nil type ignored at runtime, but should be 0 */
        WY_ASSERT(lhs.uword == 0 && rhs.uword == 0);
        return true;
    case WY_TYPE_TAG_SYMBOL:
        return lhs.symtab_entry == rhs.symtab_entry;
    case WY_TYPE_TAG_UWORD:
        return lhs.uword == rhs.uword;
    case WY_TYPE_TAG_WORD:
        return lhs.word == rhs.word;
    case WY_TYPE_TAG_STR:
        return wy_string_eq_f(lhs.str, rhs.str);
    case WY_TYPE_TAG_FUNCTION:
        return lhs.cb == rhs.cb;

    case WY_TYPE_TAG_TABLE:
    case WY_TYPE_TAG_BOX:
    default:
        // TODO:
        return false;
    }
}

WY_INLINE wy_uword wy_op_hash(wy_state* state, wy_type_tag vt, wy_primitive v)
{
    WY_UNUSED(state);
    switch (vt) {
    case WY_TYPE_TAG_NIL:
        return 0;

    case WY_TYPE_TAG_SYMBOL:
        return (wy_uword) v.symtab_entry;

    case WY_TYPE_TAG_UWORD:
        return v.uword;

    case WY_TYPE_TAG_WORD:
        return (wy_uword) v.word;

    case WY_TYPE_TAG_STR:
        return wy_string_hash_f(v.str);

    default:
        return WY_HASH_INVALID;
    }
}

#ifdef __cplusplus
}
#endif

#endif
