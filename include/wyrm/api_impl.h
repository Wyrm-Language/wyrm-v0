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

WYRM_INLINE void wyrm_gc_info_init_s(wyrm_gc_object* self, wyrm_gc_type gc_type)
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
    case WYRM_GC_TYPE_STR:
        wyrm_string_finalize_f(context, (wyrm_string*) self);
        break;

    case WYRM_GC_TYPE_BOX:
    default:
        break;
    }

}



#ifdef __cplusplus
}
#endif

#endif
