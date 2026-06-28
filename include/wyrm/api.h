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

#endif
