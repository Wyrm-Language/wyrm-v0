#include "wsys.h"

wy_exec_state wy_mod_sys_stop(wy_context* context)
{
    if (context != WY_NULL &&
        context != WY_NULL &&
        context->main_loop)
    {
        wy_main_loop_quit(context->main_loop);
    }
    return WY_EXEC_DONE;
}
