#ifndef WYRM_API_H_
#define WYRM_API_H_

#include <wyrm/types.h>

#ifdef __cplusplus
extern "C" {
#endif

const char* wyrm_lib_implementation(void);




wyrm_error wyrm_machine_init_s(wyrm_machine* self, wyrm_allocator* alloc);
wyrm_error wyrm_machine_attach_context(wyrm_machine* self, wyrm_context* context);
void wyrm_machine_finalize_f(wyrm_machine* self);

wyrm_error wyrm_machine_find_symbol(wyrm_machine* self, const char* cstr, wyrm_primitive* out);
wyrm_error wyrm_machine_insert_symbol(wyrm_machine* self, const char* cstr, wyrm_primitive* out);


/* ------------------------------------------------------------------------- */
/* Primitive Operations                                                      */
/* ------------------------------------------------------------------------- */

WYRM_INLINE bool wyrm_op_eq(wyrm_state* state, wyrm_type_tag lhst, wyrm_primitive lhs, wyrm_type_tag rhst, wyrm_primitive rhs);
WYRM_INLINE wyrm_uword wyrm_op_hash(wyrm_state* state, wyrm_type_tag vt, wyrm_primitive v);

/* ------------------------------------------------------------------------- */
/* Object API                                                                */
/* ------------------------------------------------------------------------- */

WYRM_INLINE void wyrm_object_init_header_s(wyrm_object* self, const wyrm_object_type* dtype);
WYRM_INLINE void wyrm_object_finalize_f(wyrm_context* context, wyrm_object* self);

/* ------------------------------------------------------------------------- */
/* State API                                                                 */
/* ------------------------------------------------------------------------- */

WYRM_INLINE void wyrm_state_init_s(wyrm_state* state);
WYRM_INLINE wyrm_state* wyrm_state_new(wyrm_allocator* mem);
WYRM_INLINE void wyrm_state_delete(wyrm_state* state);
WYRM_INLINE bool wyrm_state_check_flag_f(wyrm_state* state, wyrm_state_flag flag);

wyrm_error wyrm_state_exec(wyrm_state* state);
WYRM_INLINE wyrm_uword wyrm_state_value_count(wyrm_state* state);
WYRM_INLINE wyrm_value* wyrm_state_value_n(wyrm_state* state, wyrm_uword idx);
WYRM_INLINE wyrm_error wyrm_state_push(wyrm_state* state, wyrm_value value);

WYRM_INLINE wyrm_error wyrm_state_set_pending(wyrm_state* state, wyrm_exec_fn pending);
WYRM_INLINE wyrm_error wyrm_state_call_continue(wyrm_state* state, wyrm_exec_fn result_cb, wyrm_exec_fn fn, const wyrm_value* args, wyrm_uword arg_count);


/* ------------------------------------------------------------------------- */
/* Fiber API                                                                 */
/* ------------------------------------------------------------------------- */

wyrm_fiber* wyrm_fiber_create(wyrm_context* context, wyrm_uword stack_len);
static inline wyrm_context* wyrm_fiber_get_context(wyrm_fiber* self);
void wyrm_fiber_finalize_f(wyrm_fiber* self);
wyrm_error wyrm_fiber_exec_f(wyrm_fiber* self, wyrm_state* state);


/* ------------------------------------------------------------------------- */
/* Context API                                                               */
/* ------------------------------------------------------------------------- */
wyrm_error wyrm_context_init_s(wyrm_context* self, wyrm_main_loop* loop);
void wyrm_context_finalize_f(wyrm_context* self);
wyrm_error wyrm_context_activate(wyrm_context* self, wyrm_fiber* fiber);
wyrm_error wyrm_context_attach_fiber(wyrm_context* self, wyrm_fiber* fiber);

WYRM_INLINE wyrm_machine* wyrm_context_get_machine(wyrm_context* self);

void wyrm_context_object_init_header_f(wyrm_context* context, wyrm_object* object, const wyrm_object_type* dtype);

/* ------------------------------------------------------------------------- */
/* Main Loop API                                                             */
/* ------------------------------------------------------------------------- */

WYRM_INLINE wyrm_error wyrm_main_loop_add_fd(wyrm_main_loop* ref, wyrm_primitive *out, wyrm_handle fd, wyrm_io_condition events, wyrm_priority priority, wyrm_source_handle_cb cb, wyrm_primitive ud);
WYRM_INLINE wyrm_error wyrm_main_loop_add_timer(wyrm_main_loop* self, wyrm_primitive *out, uint32_t ms, wyrm_priority priority, wyrm_source_cb cb, wyrm_primitive ud);
WYRM_INLINE wyrm_error wyrm_main_loop_add_idle(wyrm_main_loop* self, wyrm_primitive *out, wyrm_source_cb cb, wyrm_primitive ud);
WYRM_INLINE wyrm_error wyrm_main_loop_add_wakeable(wyrm_main_loop* self, wyrm_primitive *out, wyrm_priority priority, wyrm_source_cb cb, wyrm_primitive ud);
WYRM_INLINE wyrm_error wyrm_main_loop_trigger(wyrm_main_loop* self, wyrm_primitive src);
WYRM_INLINE wyrm_error wyrm_main_loop_remove(wyrm_main_loop* self, wyrm_primitive src);
WYRM_INLINE wyrm_error wyrm_main_loop_iterate(wyrm_main_loop* self, bool may_block);
WYRM_INLINE wyrm_error wyrm_main_loop_run(wyrm_main_loop* self);
WYRM_INLINE wyrm_error wyrm_main_loop_quit(wyrm_main_loop* self);


/* ------------------------------------------------------------------------- */
/* Primitives                                                                */
/* ------------------------------------------------------------------------- */

#define WYRM_PRIMITIVE_PTR(dtype, v) ((dtype*) (v).ptr)

WYRM_INLINE wyrm_primitive wyrm_primitive_null(void) { const wyrm_primitive v = {.ptr = WYRM_NULL}; return v; }
WYRM_INLINE wyrm_primitive wyrm_primitive_int(wyrm_word value) { const wyrm_primitive v = {.word = value}; return v; }
WYRM_INLINE wyrm_primitive wyrm_primitive_ptr(void* value) { const wyrm_primitive v = {.ptr = value}; return v; }

WYRM_INLINE wyrm_value wyrm_value_word(wyrm_word value)
{
    wyrm_value v = { .type = WYRM_TYPE_TAG_WORD, .data = wyrm_primitive_int(value) };
    return v;
}


wyrm_error wyrm_box_new(wyrm_context* self, wyrm_box** out);

WYRM_INLINE bool wyrm_string_eq_f(wyrm_string* lhs, wyrm_string* rhs);
WYRM_INLINE wyrm_uword wyrm_string_hash_f(wyrm_string* str);
wyrm_error wyrm_string_strdup(wyrm_context* machine, const char* src, wyrm_string** out_str);




// SCAFFOLDING - TO BE REMOVED
void* wyrm_context_gc_alloc(wyrm_context* context, wyrm_uword dsize);
void wyrm_context_gc_free(wyrm_context* context, void* ptr);
void wyrm_context_push_gc(wyrm_context* context, wyrm_object* gc_info);
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

#include <wyrm/inl/util.h>
#include <wyrm/inl/allocator.h>
#include <wyrm/inl/stack.h>
#include <wyrm/inl/fiber.h>
#include <wyrm/inl/state.h>
#include <wyrm/inl/object.h>
#include <wyrm/inl/string.h>
#include <wyrm/inl/op.h>
#include <wyrm/inl/main_loop.h>


#endif
