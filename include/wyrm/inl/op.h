#ifndef WYRM_INL_OP_INL_H_
#define WYRM_INL_OP_INL_H_

#include <wyrm/types.h>
#include <wyrm.h>

#ifdef __cplusplus
extern "C" {
#endif


WYRM_INLINE bool wyrm_op_eq(wyrm_state* state, wyrm_type_tag lhst, wyrm_primitive lhs, wyrm_type_tag rhst, wyrm_primitive rhs)
{
    WYRM_UNUSED(state);
    if (lhst != rhst) { return false; }
    switch (lhst) {
    case WYRM_TYPE_TAG_NIL:
        /* primitive value of nil type ignored at runtime, but should be 0 */
        WYRM_ASSERT(lhs.uword == 0 && rhs.uword == 0);
        return true;
    case WYRM_TYPE_TAG_SYMBOL:
        return lhs.symtab_entry == rhs.symtab_entry;
    case WYRM_TYPE_TAG_UWORD:
        return lhs.uword == rhs.uword;
    case WYRM_TYPE_TAG_WORD:
        return lhs.word == rhs.word;
    case WYRM_TYPE_TAG_STR:
        return wyrm_string_eq_f(lhs.str, rhs.str);
    case WYRM_TYPE_TAG_FUNCTION:
        return lhs.cb == rhs.cb;

    case WYRM_TYPE_TAG_TABLE:
    case WYRM_TYPE_TAG_BOX:
    default:
        // TODO:
        return false;
    }
}

WYRM_INLINE wyrm_uword wyrm_op_hash(wyrm_state* state, wyrm_type_tag vt, wyrm_primitive v)
{
    WYRM_UNUSED(state);
    switch (vt) {
    case WYRM_TYPE_TAG_NIL:
        return 0;

    case WYRM_TYPE_TAG_SYMBOL:
        return (wyrm_uword) v.symtab_entry;

    case WYRM_TYPE_TAG_UWORD:
        return v.uword;

    case WYRM_TYPE_TAG_WORD:
        return (wyrm_uword) v.word;

    case WYRM_TYPE_TAG_STR:
        return wyrm_string_hash_f(v.str);

    default:
        return WYRM_HASH_INVALID;
    }
}

#ifdef __cplusplus
}
#endif

#endif
