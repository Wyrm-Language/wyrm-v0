#ifndef WYRM_API_H_
#define WYRM_API_H_

#include <wyrm/types.h>

#ifdef __cplusplus
extern "C" {
#endif

const char* wyrm_lib_implementation(void);

void wyrm_fiber_init(wyrm_fiber* self, wyrm_value* stack, wyrm_uword stack_size);
static inline wyrm_context* wyrm_fiber_get_context(wyrm_fiber* self);

wyrm_error wyrm_context_init_s(wyrm_context* self, wyrm_main_loop* loop);
wyrm_error wyrm_context_attach_fiber(wyrm_context* self, wyrm_fiber* fiber);
static inline wyrm_machine* wyrm_context_get_machine(wyrm_context* self);

wyrm_error wyrm_machine_init_s(wyrm_machine* self, wyrm_allocator* alloc);
wyrm_error wyrm_machine_attach_context(wyrm_machine* self, wyrm_context* context);

WYRM_INLINE void wyrm_object_list_init_s(wyrm_object_list* object_list, wyrm_allocator* allocator, wyrm_object** obj_list, wyrm_uword capacity);
WYRM_INLINE void wyrm_object_list_init_f(wyrm_object_list* object_list, wyrm_allocator* allocator);
WYRM_INLINE wyrm_object* wyrm_object_list_idx_f(wyrm_object_list* object_list, wyrm_uword idx);
WYRM_INLINE wyrm_error wyrm_object_list_push(wyrm_object_list* object_list, wyrm_object* obj);

/* ---------- wyrm_fiber inlines --------- */
static inline wyrm_context* wyrm_fiber_get_context(wyrm_fiber* self)
{
    if (!self) { return WYRM_NULL; }
    return self->parent;
}

/* ---------- wyrm_context inlines --------- */
static inline wyrm_machine* wyrm_context_get_machine(wyrm_context* self)
{
    if (!self) { return WYRM_NULL; }
    return self->parent;
}


#ifdef __cplusplus
}
#endif

#include "api_impl.h"

#endif
