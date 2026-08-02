#ifndef WYRM_CONTEXT_H_
#define WYRM_CONTEXT_H_

#include <wyrm/types.h>

/* ------------------------------------------------------------------------- */
/* Context API                                                               */
/* ------------------------------------------------------------------------- */

WYRM_BEGIN_DECLS

wyrm_error wyrm_context_init_s(wyrm_context* self, wyrm_main_loop* loop);
void wyrm_context_finalize_f(wyrm_context* self);
wyrm_error wyrm_context_activate(wyrm_context* self, wyrm_fiber* fiber);
wyrm_error wyrm_context_attach_fiber(wyrm_context* self, wyrm_fiber* fiber);

WYRM_INLINE wyrm_dict* wyrm_context_get_root_f(wyrm_context* context);
WYRM_INLINE wyrm_error wyrm_context_set_root_f(wyrm_context* context, wyrm_dict* root);
WYRM_INLINE wyrm_machine* wyrm_context_get_machine(wyrm_context* self);

void wyrm_context_object_init_header_f(wyrm_context* context, wyrm_object* object, const wyrm_object_type* dtype);

void* wyrm_context_gc_alloc(wyrm_context* context, wyrm_uword dsize);
void* wyrm_context_gc_realloc(wyrm_context* context, void* ptr, wyrm_uword new_size);
void wyrm_context_gc_free(wyrm_context* context, void* ptr);
void wyrm_context_push_gc(wyrm_context* context, wyrm_object* gc_info);

void wyrm_context_gc_full_run(wyrm_state* state, wyrm_context* context);

WYRM_END_DECLS

#include <wyrm/inl/context.h>

#endif
