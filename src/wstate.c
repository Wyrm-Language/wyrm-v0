#include <wyrm.h>


wy_error wy_state_exec(wy_state* state)
{
    if (state == WY_NULL || state->fiber == WY_NULL) { return WY_ERR_INVAL; }
    return wy_fiber_exec_f(state->fiber, state);
}
