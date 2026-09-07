#ifndef WYRM_CONTEXT_H_
#define WYRM_CONTEXT_H_

#include <wyrm/fwd.h>
#include <wyrm/gc.h>
#include <wyrm/mem_info.h>

/* ------------------------------------------------------------------------- */
/* Context API                                                               */
/* ------------------------------------------------------------------------- */

WY_BEGIN_DECLS

struct wy_context
{
    wy_machine* parent;
    wy_fiber* current_fiber;
    wy_module* root_module;
    wy_main_loop* main_loop;
    wy_primitive wakeable_source;
    wy_gc_arena arena;
};

void wy_context_init_s(wy_context* self);
void wy_context_finalize_f(wy_context* self);

wy_error wy_context_set_root(wy_context* context, wy_module* module);



wy_error wy_context_attach_loop(wy_context* context, wy_main_loop* loop);
void wy_context_detach_loop(wy_context* context);

wy_error wy_context_activate(wy_context* self, wy_fiber* fiber);
wy_error wy_context_attach_fiber(wy_context* self, wy_fiber* fiber);

void wy_ctx_object_init_header_static_f(wy_context* context, wy_object* object, const wy_object_type* dtype);

WY_INLINE wy_machine* wy_context_get_machine(wy_context* self);

void wy_context_object_init_header_f(wy_context* context, wy_object* object, const wy_object_type* dtype);

void* wy_context_gc_alloc(wy_context* context, wy_uword dsize);
void* wy_context_gc_realloc(wy_context* context, void* ptr, wy_uword new_size);
void wy_context_gc_free(wy_context* context, void* ptr);
void wy_context_push_gc(wy_context* context, wy_object* gc_info);

void wy_context_gc_full_run(wy_state* state, wy_context* context);


#define WY_CONTEXT_MEM_INFO_RESERVE_COUNT(context, mem_info, count, type) (wy_context_mem_reserve_count_f((context), (mem_info), (count), sizeof(type)))

wy_error wy_context_mem_reserve_f(wy_context* context, wy_mem_info* mem_info, wy_uword sz);
void wy_context_mem_release_f(wy_context* context, wy_mem_info* mem_info);

/**
 * Grow `mem_info` to hold at least `count` blocks of `block_sz` bytes
 */
WY_INLINE wy_error wy_context_mem_reserve_count_f(wy_context* context, wy_mem_info* mem_info, wy_uword count, wy_uword block_sz)
{
    if (count > WY_MAX_ARRAY_LEN) { return WY_ERR_INVAL; }
    wy_uword mem_sz = wy_mem_info_block_count_sz_f(block_sz, count);
    return wy_context_mem_reserve_f(context, mem_info, mem_sz);
}


/**
 * Gab machine associated with context
 *
 * @param self Context
 * @return Machine associated with the context
 */
WY_INLINE wy_machine* wy_context_get_machine(wy_context* self)
{
    if (!self) { return WY_NULL; }
    return self->parent;
}



WY_END_DECLS

#endif
