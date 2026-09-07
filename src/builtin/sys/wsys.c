#include "wsys.h"

wy_exec_state wy_mod_sys_stop(wy_state* state)
{
    if (state != WY_NULL &&
        state->context != WY_NULL &&
        state->context->main_loop)
    {
        wy_main_loop_quit(state->context->main_loop);
    }
    return WY_EXEC_DONE;
}
