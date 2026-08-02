#ifndef WYRM_FIBER_H_
#define WYRM_FIBER_H_

#include <wyrm/types.h>

/* ------------------------------------------------------------------------- */
/* Fiber API                                                                 */
/* ------------------------------------------------------------------------- */

WYRM_BEGIN_DECLS

wyrm_fiber* wyrm_fiber_create(wyrm_context* context, wyrm_uword stack_len);
WYRM_INLINE wyrm_context* wyrm_fiber_get_context(wyrm_fiber* self);
void wyrm_fiber_finalize_f(wyrm_fiber* self);
wyrm_error wyrm_fiber_exec_f(wyrm_fiber* self, wyrm_state* state);

WYRM_END_DECLS

#include <wyrm/inl/fiber.h>

#endif
