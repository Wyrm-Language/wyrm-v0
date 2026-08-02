#ifndef WYRM_STATE_H_
#define WYRM_STATE_H_

#include <wyrm/types.h>
#include <wyrm/sys/toolchain.h>

/* ------------------------------------------------------------------------- */
/* State API                                                                 */
/* ------------------------------------------------------------------------- */

WYRM_BEGIN_DECLS

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

WYRM_END_DECLS

#include <wyrm/inl/state.h>

#endif
