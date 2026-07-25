#include <wyrm.h>


wyrm_error wyrm_state_exec(wyrm_state* state)
{
    if (state == WYRM_NULL || state->fiber == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return wyrm_fiber_exec_f(state->fiber, state);
}
