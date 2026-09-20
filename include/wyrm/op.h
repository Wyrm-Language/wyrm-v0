#ifndef WYRM_OP_H_
#define WYRM_OP_H_

#include <wyrm/bytes.h>
#include <wyrm/primitive.h>
#include <wyrm/string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WY_HASH_INVALID WY_UWORD_MAX

WY_INLINE bool wy_op_eq(wy_context* context, wy_type_tag lhst, wy_primitive lhs, wy_type_tag rhst, wy_primitive rhs);
WY_INLINE wy_uword wy_op_hash(wy_context* context, wy_type_tag vt, wy_primitive v);


WY_INLINE bool wy_op_eq(wy_context* context, wy_type_tag lhst, wy_primitive lhs, wy_type_tag rhst, wy_primitive rhs)
{
    WY_UNUSED(context);
    if (lhst != rhst) { return false; }
    switch (lhst) {
    case WY_TYPE_TAG_NIL:
        return true;
    case WY_TYPE_TAG_BOOL:
        return lhs.flag == rhs.flag;
    case WY_TYPE_TAG_SYMBOL:
        return lhs.symtab_entry == rhs.symtab_entry;
    case WY_TYPE_TAG_PTYPE:
    case WY_TYPE_TAG_UWORD:
        return lhs.uword == rhs.uword;
    case WY_TYPE_TAG_WORD:
        return lhs.word == rhs.word;
    case WY_TYPE_TAG_FLOAT:
        return lhs.fp == rhs.fp;
    case WY_TYPE_TAG_STR:
        return wy_string_eq_f(lhs.str, rhs.str);

    case WY_TYPE_TAG_ERROR:
        /* Unset vs specific error object */
        return lhs.gc_object == rhs.gc_object;

    case WY_TYPE_TAG_BYTES: {
        /* doc/stdlib.md: bytes `==` is byte-for-byte, unlike the pointer-
         * identity default every other GC container still uses below. */
        if (lhs.gc_object == rhs.gc_object) { return true; }
        wy_bytes* a = (wy_bytes*) lhs.gc_object;
        wy_bytes* b = (wy_bytes*) rhs.gc_object;
        return a->len == b->len && (a->len == 0 || wy_memcmp(a->data, b->data, a->len) == 0);
    }

    default:
        /* Pointer identity for other GC objects for now */
        if (wy_type_is_object(lhst)) {
            return lhs.gc_object == rhs.gc_object;
        }
        return false;
    }
}

WY_INLINE wy_uword wy_op_hash(wy_context* context, wy_type_tag vt, wy_primitive v)
{
    WY_UNUSED(context);
    switch (vt) {
    case WY_TYPE_TAG_NIL:
        return 0;

    case WY_TYPE_TAG_BOOL:
        return v.flag ? 1 : 0;

    case WY_TYPE_TAG_SYMBOL:
        return (wy_uword) (uintptr_t) v.symtab_entry;

    case WY_TYPE_TAG_PTYPE:
    case WY_TYPE_TAG_UWORD:
        return v.uword;

    case WY_TYPE_TAG_WORD:
        return (wy_uword) v.word;

    case WY_TYPE_TAG_FLOAT:
        return wy_util_rehash(v.tagged_ptr);

    case WY_TYPE_TAG_STR:
        return wy_string_hash_f(v.str);

    default:
        if (wy_type_is_object(vt)) {
            return (wy_uword) (uintptr_t) v.gc_object;
        }
        return WY_HASH_INVALID;
    }
}

#ifdef __cplusplus
}
#endif

#endif
