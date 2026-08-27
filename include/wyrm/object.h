#ifndef WYRM_WOBJECT_H_
#define WYRM_WOBJECT_H_

#include <wyrm/core.h>

#define WY_OBJECT_INITIALIZER(DTYPE) { .dtype = DTYPE, .flags = 0, .next = WYRM_NULL }
#define WY_OBJECT_INITIALIZER_S(DTYPE) { .dtype = DTYPE, .flags = WYRM_GC_STATIC, .next = WYRM_NULL }

WYRM_BEGIN_DECLS

WYRM_INLINE void wyrm_object_init_header_s(wyrm_object* self, const wyrm_object_type* dtype)
{
    self->dtype = dtype;
    self->flags = 0;
    self->next = WYRM_NULL;
}


WYRM_INLINE void wy_object_init_static_f(wyrm_object* self, const wyrm_object_type* dtype)
{
    wyrm_object_init_header_s(self, dtype);
    self->flags |= WYRM_GC_STATIC;
}


WYRM_INLINE void wyrm_object_finalize_f(wyrm_context* context, wyrm_object* self)
{
    WYRM_ASSERT(context != WYRM_NULL && self != WYRM_NULL);
    self->flags |= WYRM_GC_FLAG_FINALIZED;
    if (self->dtype->finalize != WYRM_NULL) {
        self->dtype->finalize(context, self);
    }
}


WYRM_INLINE wyrm_error wyrm_object_children_iter_start(wyrm_state* state, wyrm_object* self, wyrm_work_area* wa)
{
    if (self == WYRM_NULL || self->dtype == WYRM_NULL || wa == WYRM_NULL) { return WYRM_ERR_INVAL; }
    if (self->dtype->children_iter_start == WYRM_NULL ||
        self->dtype->children_iter_next == WYRM_NULL) { return WYRM_ERR_NOSUPPORT; }
    return self->dtype->children_iter_start(state, self, wa);
}


WYRM_INLINE wyrm_error wyrm_object_children_iter_next_f(wyrm_state* state, wyrm_object* self, wyrm_work_area* wa, const wyrm_object** object_ptr)
{
    return self->dtype->children_iter_next(state, self, wa, object_ptr);
}




WYRM_END_DECLS

#endif
