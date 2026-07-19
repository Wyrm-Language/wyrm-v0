#ifndef WYRM_API_IMPL_H_
#define WYRM_API_IMPL_H_

#include <wyrm/types.h>
#include <wyrm/internal_api.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef WYRM_OBJECT_LIST_INITIAL_SZ
#define WYRM_OBJECT_LIST_INITIAL_SZ 4
#endif

/* ------------------------------------------------------------------------- */
/* Operations                                                                */
/* ------------------------------------------------------------------------- */

WYRM_INLINE bool wyrm_op_eq(wyrm_type_tag lhst, wyrm_primitive lhs, wyrm_type_tag rhst, wyrm_primitive rhs)
{
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
    case WYRM_TYPE_TAG_VALUE_PTR:
        return lhs.ptr == rhs.ptr;

    case WYRM_TYPE_TAG_TABLE:
    case WYRM_TYPE_TAG_BOX:
    default:
        // TODO:
        return false;
    }
}

WYRM_INLINE wyrm_uword wyrm_op_hash(wyrm_type_tag vt, wyrm_primitive v)
{
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


WYRM_INLINE bool wyrm_string_eq_f(wyrm_string* lhs, wyrm_string* rhs)
{
    WYRM_ASSERT(lhs != WYRM_NULL && rhs != WYRM_NULL);
    if (lhs == rhs) { return true; }
    if (lhs->hash != rhs->hash) { return false; }
    if (lhs->len != rhs->len) { return false; }
    if (lhs->len == 0) { return true; }
    return wyrm_strncmp_f(lhs->str, rhs->str, lhs->len) == 0;
}

WYRM_INLINE wyrm_uword wyrm_string_hash_f(wyrm_string* str)
{
    if (!str) { return 0; }
    return str->hash;
}


WYRM_INLINE void wyrm_string_finalize_f(wyrm_context* context, wyrm_string* self)
{
    wyrm_context_gc_free(context, (void*) self->str);
    self->str = WYRM_NULL;
    self->len = 0;
    self->hash = 0;
}

/* ------------------------------------------------------------------------- */
/* GC Info */
/* ------------------------------------------------------------------------- */

WYRM_INLINE void wyrm_gc_info_init_s(wyrm_gc_object* self, wyrm_type_tag gc_type)
{
    self->flags = 0;
    self->gc_type = gc_type;
    self->next = WYRM_NULL;
}

WYRM_INLINE void wyrm_gc_info_finalize_f(wyrm_context* context, wyrm_gc_object* self)
{
    if (!self) { return; }
    self->flags |= WYRM_GC_FLAG_FINALIZED;

    switch (self->gc_type)
    {
    case WYRM_TYPE_TAG_STR:
        wyrm_string_finalize_f(context, (wyrm_string*) self);
        break;

    case WYRM_TYPE_TAG_BOX:
    default:
        break;
    }

}



#ifdef __cplusplus
}
#endif

#endif
