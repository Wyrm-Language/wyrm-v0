#ifndef WYRM_STATE_H_
#define WYRM_STATE_H_

#include <wyrm/types.h>
#include <wyrm/sys/toolchain.h>

/* ------------------------------------------------------------------------- */
/* State API                                                                 */
/* ------------------------------------------------------------------------- */

WY_BEGIN_DECLS

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

WY_END_DECLS

#include <wyrm/inl/state.h>

#endif
