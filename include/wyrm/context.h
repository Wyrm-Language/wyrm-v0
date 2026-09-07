#ifndef WYRM_CONTEXT_H_
#define WYRM_CONTEXT_H_

#include <wyrm/types.h>
#include <wyrm/gc.h>
#include <wyrm/mem_info.h>

/* ------------------------------------------------------------------------- */
/* Context API                                                               */
/* ------------------------------------------------------------------------- */

WYRM_BEGIN_DECLS

struct wy_context
{
    wyrm_machine* parent;
    wyrm_fiber* current_fiber;
    wy_module* root_module;
    wyrm_main_loop* main_loop;
    wyrm_primitive wakeable_source;
    wyrm_gc_arena arena;
};

void wyrm_context_init_s(wyrm_context* self);
void wyrm_context_finalize_f(wyrm_context* self);

wyrm_error wy_context_set_root(wyrm_context* context, wy_module* module);



wyrm_error wyrm_context_attach_loop(wyrm_context* context, wyrm_main_loop* loop);
void wyrm_context_detach_loop(wyrm_context* context);

wyrm_error wyrm_context_activate(wyrm_context* self, wyrm_fiber* fiber);
wyrm_error wyrm_context_attach_fiber(wyrm_context* self, wyrm_fiber* fiber);

void wy_ctx_object_init_header_static_f(wyrm_context* context, wyrm_object* object, const wyrm_object_type* dtype);

WYRM_INLINE wyrm_machine* wyrm_context_get_machine(wyrm_context* self);

void wyrm_context_object_init_header_f(wyrm_context* context, wyrm_object* object, const wyrm_object_type* dtype);

void* wyrm_context_gc_alloc(wyrm_context* context, wyrm_uword dsize);
void* wyrm_context_gc_realloc(wyrm_context* context, void* ptr, wyrm_uword new_size);
void wyrm_context_gc_free(wyrm_context* context, void* ptr);
void wyrm_context_push_gc(wyrm_context* context, wyrm_object* gc_info);

void wyrm_context_gc_full_run(wyrm_state* state, wyrm_context* context);


#define WY_CONTEXT_MEM_INFO_RESERVE_COUNT(context, mem_info, count, type) (wy_context_mem_reserve_count_f((context), (mem_info), (count), sizeof(type)))

wyrm_error wy_context_mem_reserve_f(wy_context* context, wy_mem_info* mem_info, wy_uword sz);
void wy_context_mem_release_f(wy_context* context, wy_mem_info* mem_info);

/**
 * Grow `mem_info` to hold at least `count` blocks of `block_sz` bytes
 */
WYRM_INLINE wy_error wy_context_mem_reserve_count_f(wy_context* context, wy_mem_info* mem_info, wy_uword count, wy_uword block_sz)
{
    if (count > WY_MAX_ARRAY_LEN) { return WYRM_ERR_INVAL; }
    wy_uword mem_sz = wy_mem_info_block_count_sz_f(block_sz, count);
    return wy_context_mem_reserve_f(context, mem_info, mem_sz);
}


/**
 * Gab machine associated with context
 *
 * @param self Context
 * @return Machine associated with the context
 */
WYRM_INLINE wyrm_machine* wyrm_context_get_machine(wyrm_context* self)
{
    if (!self) { return WYRM_NULL; }
    return self->parent;
}



WYRM_END_DECLS

#endif
