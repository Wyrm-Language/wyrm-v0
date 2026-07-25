#include "wsys.h"

wyrm_exec_result wyrm_mod_sys_stop(wyrm_state* state)
{
    if (state != WYRM_NULL &&
        state->context != WYRM_NULL &&
        state->context->main_loop)
    {
        wyrm_main_loop_quit(state->context->main_loop);
    }
    return wyrm_make_exec_result(WYRM_EXEC_DONE, 0);
}
