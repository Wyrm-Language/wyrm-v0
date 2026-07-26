#ifndef WYRM_INL_OBJECT_INL_H_
#define WYRM_INL_OBJECT_INL_H_

#include <wyrm/inl/stack.h>
#include <wyrm/types.h>

#ifdef __cplusplus
extern "C" {
#endif


WYRM_INLINE void wyrm_object_init_header_s(wyrm_object* self, const wyrm_object_type* dtype)
{
    self->dtype = dtype;
    self->flags = 0;
    self->next = WYRM_NULL;
}


WYRM_INLINE void wyrm_object_finalize_f(wyrm_context* context, wyrm_object* self)
{
    WYRM_ASSERT(context != WYRM_NULL && self != WYRM_NULL);
    self->flags |= WYRM_GC_FLAG_FINALIZED;
    if (self->dtype->finalize != WYRM_NULL) {
        self->dtype->finalize(context, self);
    }
}


#ifdef __cplusplus
}
#endif

#endif
