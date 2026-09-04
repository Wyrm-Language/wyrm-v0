#include <wyrmxx/context.h>
#include <wyrmxx/except.h>
#include <wyrm.h>

namespace wyrmxx
{
    context::context(main_loop loop)
        : context_{}
        , main_loop_{loop}
    {
        check_wyrm_error(wyrm_context_init_s(&context_));
        check_wyrm_error(wyrm_context_attach_loop(&context_, main_loop_));
    }
}
