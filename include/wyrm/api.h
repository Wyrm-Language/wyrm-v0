#ifndef WYRM_API_H_
#define WYRM_API_H_

#include <wyrm/types.h>
#include <wyrm/box.h>
#include <wyrm/fiber.h>
#include <wyrm/string.h>
#include <wyrm/context.h>
#include <wyrm/machine.h>

#ifdef __cplusplus
extern "C" {
#endif

const char* wy_lib_implementation(void);




/* ------------------------------------------------------------------------- */
/* Primitive Operations                                                      */
/* ------------------------------------------------------------------------- */

WY_INLINE bool wy_op_eq(wy_state* state, wy_type_tag lhst, wy_primitive lhs, wy_type_tag rhst, wy_primitive rhs);
WY_INLINE wy_uword wy_op_hash(wy_state* state, wy_type_tag vt, wy_primitive v);

/* ------------------------------------------------------------------------- */
/* Object API                                                                */
/* ------------------------------------------------------------------------- */

WY_INLINE void wy_object_init_header_s(wy_object* self, const wy_object_type* dtype);
WY_INLINE void wy_object_finalize_f(wy_context* context, wy_object* self);
WY_INLINE wy_error wy_object_children_iter_start(wy_state* state, wy_object* self, wy_work_area* wa);
WY_INLINE wy_error wy_object_children_iter_next_f(wy_state* state, wy_object* self, wy_work_area* wa, const wy_object** object_ptr);

/* ------------------------------------------------------------------------- */
/* State API                                                                 */
/* ------------------------------------------------------------------------- */

WY_INLINE void wy_state_init_s(wy_state* state);
WY_INLINE wy_state* wy_state_new(wy_allocator* mem);
WY_INLINE void wy_state_delete(wy_state* state);
WY_INLINE bool wy_state_check_flag_f(wy_state* state, wy_state_flag flag);

wy_error wy_state_exec(wy_state* state);
WY_INLINE wy_uword wy_state_value_count(wy_state* state);
WY_INLINE wy_value* wy_state_value_n(wy_state* state, wy_uword idx);
WY_INLINE wy_error wy_state_push(wy_state* state, wy_value value);

WY_INLINE wy_error wy_state_set_pending(wy_state* state, wy_exec_fn pending);
WY_INLINE wy_error wy_state_call_continue(wy_state* state, wy_exec_fn result_cb, wy_exec_fn fn, const wy_value* args, wy_uword arg_count);

/* ------------------------------------------------------------------------- */
/* Main Loop API                                                             */
/* ------------------------------------------------------------------------- */

WY_INLINE wy_error wy_main_loop_add_fd(wy_main_loop* ref, wy_primitive *out, wy_handle fd, wy_io_condition events, wy_priority priority, wy_source_handle_cb cb, wy_primitive ud);
WY_INLINE wy_error wy_main_loop_add_timer(wy_main_loop* self, wy_primitive *out, uint32_t ms, wy_priority priority, wy_source_cb cb, wy_primitive ud);
WY_INLINE wy_error wy_main_loop_add_idle(wy_main_loop* self, wy_primitive *out, wy_source_cb cb, wy_primitive ud);
WY_INLINE wy_error wy_main_loop_add_wakeable(wy_main_loop* self, wy_primitive *out, wy_priority priority, wy_source_cb cb, wy_primitive ud);
WY_INLINE wy_error wy_main_loop_trigger(wy_main_loop* self, wy_primitive src);
WY_INLINE wy_error wy_main_loop_remove(wy_main_loop* self, wy_primitive src);
WY_INLINE wy_error wy_main_loop_iterate(wy_main_loop* self, bool may_block);
WY_INLINE wy_error wy_main_loop_run(wy_main_loop* self);
WY_INLINE wy_error wy_main_loop_quit(wy_main_loop* self);


/* ------------------------------------------------------------------------- */
/* Primitives                                                                */
/* ------------------------------------------------------------------------- */

#define WY_PRIMITIVE_PTR(dtype, v) ((dtype*) (v).ptr)

WY_INLINE wy_primitive wy_primitive_int(wy_word value) { const wy_primitive v = {.word = value}; return v; }
WY_INLINE wy_primitive wy_primitive_ptr(void* value) { const wy_primitive v = {.ptr = value}; return v; }

WY_INLINE wy_value wy_value_word(wy_word value)
{
    wy_value v = { .type = WY_TYPE_TAG_WORD, .data = wy_primitive_int(value) };
    return v;
}


/* ------------------------------------------------------------------------- */
/* Class                                                                     */
/* ------------------------------------------------------------------------- */

extern const wy_object_type wy_type_class;

wy_error wy_class_new(wy_context* context, wy_class** out);
WY_INLINE void wy_class_set_name_f(wy_class* self, wy_primitive name);
WY_INLINE wy_error wy_class_add_slot_f(wy_class* self, wy_symtab_entry slot_name, wy_uword flags);
WY_INLINE wy_uword wy_class_get_slot_selector_f(wy_class* self, wy_symtab_entry sym_name);


/* ------------------------------------------------------------------------- */
/* Dict                                                                      */
/* ------------------------------------------------------------------------- */

extern const wy_object_type wy_type_table;

wy_error wy_dict_new(wy_context* self, wy_dict** out);
wy_value* wy_dict_get(wy_state* state, wy_dict* self, wy_type_tag tag, wy_primitive value);
wy_error wy_dict_set(wy_state* state, wy_dict* self, wy_type_tag key_type, wy_primitive key_value, wy_type_tag value_type, wy_primitive value);


#ifdef __cplusplus
}
#endif


#include <wyrm/inl/util.h>
#include <wyrm/allocator.h>
#include <wyrm/inl/class.h>
#include <wyrm/stack.h>
#include <wyrm/fiber.h>
#include <wyrm/inl/state.h>
#include <wyrm/object.h>
#include <wyrm/inl/op.h>
#include <wyrm/inl/main_loop.h>

#endif
