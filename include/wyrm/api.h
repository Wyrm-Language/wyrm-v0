#ifndef WYRM_API_H_
#define WYRM_API_H_

#include <wyrm/types.h>

#ifdef __cplusplus
extern "C" {
#endif

const char* wyrm_lib_implementation(void);

WYRM_INLINE void wyrm_state_init_s(wyrm_state* state);
WYRM_INLINE wyrm_state* wyrm_state_new(wyrm_allocator* mem);
WYRM_INLINE wyrm_error wyrm_state_delete(wyrm_state* state);
WYRM_INLINE bool wyrm_state_check_flag_f(wyrm_state* state, wyrm_state_flag flag);

/* Run the active fiber until it hits suspension point */
wyrm_error wyrm_state_exec(wyrm_state* state);
WYRM_INLINE wyrm_uword wyrm_state_value_count(wyrm_state* state);
WYRM_INLINE wyrm_value* wyrm_state_value_n(wyrm_state* state, wyrm_uword idx);
WYRM_INLINE wyrm_error wyrm_state_push(wyrm_state* state, wyrm_value value);

WYRM_INLINE wyrm_error wyrm_state_set_pending(wyrm_state* state, wyrm_exec_fn pending);


WYRM_INLINE bool wyrm_op_eq(wyrm_state* state, wyrm_type_tag lhst, wyrm_primitive lhs, wyrm_type_tag rhst, wyrm_primitive rhs);
WYRM_INLINE wyrm_uword wyrm_op_hash(wyrm_state* state, wyrm_type_tag vt, wyrm_primitive v);


wyrm_error wyrm_machine_init_s(wyrm_machine* self, wyrm_allocator* alloc);
wyrm_error wyrm_machine_attach_context(wyrm_machine* self, wyrm_context* context);
void wyrm_machine_finalize_f(wyrm_machine* self);

wyrm_error wyrm_machine_find_symbol(wyrm_machine* self, const char* cstr, wyrm_primitive* out);
wyrm_error wyrm_machine_insert_symbol(wyrm_machine* self, const char* cstr, wyrm_primitive* out);

wyrm_fiber* wyrm_fiber_create(wyrm_context* context, wyrm_uword stack_len);
static inline wyrm_context* wyrm_fiber_get_context(wyrm_fiber* self);
void wyrm_fiber_finalize_f(wyrm_fiber* self);

wyrm_error wyrm_context_init_s(wyrm_context* self, wyrm_main_loop* loop);
void wyrm_context_finalize_f(wyrm_context* self);
WYRM_INLINE wyrm_machine* wyrm_context_get_machine(wyrm_context* self);




wyrm_error wyrm_box_new(wyrm_context* self, wyrm_box** out);

WYRM_INLINE bool wyrm_string_eq_f(wyrm_string* lhs, wyrm_string* rhs);
WYRM_INLINE wyrm_uword wyrm_string_hash_f(wyrm_string* str);
wyrm_error wyrm_string_strdup(wyrm_context* machine, const char* src, wyrm_string** out_str);
WYRM_INLINE void wyrm_string_finalize_f(wyrm_context* context, wyrm_string* self);




// SCAFFOLDING - TO BE REMOVED
void* wyrm_context_gc_alloc(wyrm_context* context, wyrm_uword dsize);
void wyrm_context_gc_free(wyrm_context* context, void* ptr);
wyrm_error wyrm_context_gc_init(wyrm_context* context, wyrm_gc_object* gc_info, wyrm_type_tag gc_type);
void wyrm_context_push_gc(wyrm_context* context, wyrm_gc_object* gc_info);
void wyrm_context_gc_start_mark(wyrm_context* machine);
void wyrm_context_gc_sweep_f(wyrm_context* machine);



void wyrm_table_init_s(wyrm_table* self,
    wyrm_allocator* allocator,
    wyrm_uword count,
    wyrm_key_hash_value* dense,
    wyrm_uword dense_capacity,
    wyrm_uword* sparse,
    wyrm_uword sparse_capacity);
void wyrm_table_init_f(wyrm_table* self, wyrm_allocator* allocator);
void wyrm_table_finalize_f(wyrm_table* self);
wyrm_value* wyrm_table_get(wyrm_state* state, wyrm_table* self, wyrm_type_tag tag, wyrm_primitive value);
wyrm_error wyrm_table_set(wyrm_state* state, wyrm_table* self, wyrm_type_tag key_type, wyrm_primitive key_value, wyrm_type_tag value_type, wyrm_primitive value);




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
WYRM_INLINE void wyrm_gc_info_init_s(wyrm_gc_object* self, wyrm_type_tag gc_type);
WYRM_INLINE void wyrm_gc_info_finalize_f(wyrm_context* context, wyrm_gc_object* self);

/* ---------- wyrm_fiber inlines --------- */
static inline wyrm_context* wyrm_fiber_get_context(wyrm_fiber* self)
{
    if (!self) { return WYRM_NULL; }
    return self->parent;
}

/* ---------- wyrm_context inlines --------- */
WYRM_INLINE wyrm_machine* wyrm_context_get_machine(wyrm_context* self)
{
    if (!self) { return WYRM_NULL; }
    return self->parent;
}


#ifdef __cplusplus
}
#endif

#include "api_impl.h"

#endif
