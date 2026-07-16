#ifndef WYRM_API_H_
#define WYRM_API_H_

#include <wyrm/types.h>

#ifdef __cplusplus
extern "C" {
#endif

const char* wyrm_lib_implementation(void);

wyrm_error wyrm_machine_init_s(wyrm_machine* self, wyrm_allocator* alloc);
wyrm_error wyrm_machine_attach_context(wyrm_machine* self, wyrm_context* context);
wyrm_error wyrm_machine_finalize_f(wyrm_machine* self);

wyrm_error wyrm_machine_find_symbol(wyrm_machine* self, const char* cstr, wyrm_primitive* out);
wyrm_error wyrm_machine_insert_symbol(wyrm_machine* self, const char* cstr, wyrm_primitive* out);


void wyrm_fiber_init(wyrm_fiber* self, wyrm_value* stack, wyrm_uword stack_size);
static inline wyrm_context* wyrm_fiber_get_context(wyrm_fiber* self);

wyrm_error wyrm_context_init_s(wyrm_context* self, wyrm_main_loop* loop);
wyrm_error wyrm_context_attach_fiber(wyrm_context* self, wyrm_fiber* fiber);
static inline wyrm_machine* wyrm_context_get_machine(wyrm_context* self);




wyrm_error wyrm_box_new(wyrm_context* self, wyrm_box** out);


wyrm_error wyrm_string_strdup(wyrm_context* machine, const char* src, wyrm_string** out_str);
WYRM_INLINE void wyrm_string_finalize_f(wyrm_context* context, wyrm_string* self);




// SCAFFOLDING - TO BE REMOVED
void* wyrm_context_gc_alloc(wyrm_context* context, wyrm_uword dsize);
void wyrm_context_gc_free(wyrm_context* context, void* ptr);
wyrm_error wyrm_context_gc_init(wyrm_context* context, wyrm_gc_object* gc_info, wyrm_gc_type gc_type);
void wyrm_context_push_gc(wyrm_context* context, wyrm_gc_object* gc_info);
void wyrm_context_gc_start_mark(wyrm_context* machine);
void wyrm_context_gc_sweep_f(wyrm_context* machine);



void wyrm_dict_init_s(wyrm_dict* self,
    wyrm_allocator* allocator,
    wyrm_uword count,
    wyrm_key_hash_value* dense,
    wyrm_uword dense_capacity,
    wyrm_uword* sparse,
    wyrm_uword sparse_capacity);
void wyrm_dict_init_f(wyrm_dict* self, wyrm_allocator* allocator);
void wyrm_dict_finalize_f(wyrm_dict* self);
wyrm_value* wyrm_dict_get(wyrm_dict* self, wyrm_type_tag tag, wyrm_primitive value);
wyrm_error wyrm_dict_set(wyrm_dict* self, wyrm_type_tag key_type, wyrm_primitive key_value, wyrm_type_tag value_type, wyrm_primitive value);




/* ---- Utility Functions ----- */

WYRM_INLINE wyrm_uword wyrm_hash_buffer(const char* start, const char* end)
{
    wyrm_uword hash = 0;
    for (const char* cur = start; cur != end; ++cur) {
        hash = hash + (wyrm_uword)(*cur);
    }
    return hash;
}


/* ---- GC Objects ----- */
WYRM_INLINE void wyrm_gc_info_init_s(wyrm_gc_object* self, wyrm_gc_type gc_type);
WYRM_INLINE void wyrm_gc_info_finalize_f(wyrm_context* context, wyrm_gc_object* self);

bool wyrm_primitive_eq(wyrm_type_tag lhs_type, wyrm_primitive lhs, wyrm_type_tag rhs_type, wyrm_primitive rhs);

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
