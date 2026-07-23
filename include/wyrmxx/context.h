#ifndef WYRMXX_CONTEXT_H_
#define WYRMXX_CONTEXT_H_

#include <wyrm.h>
#include <wyrmxx/main_loop.h>

namespace wyrmxx
{
    class context
    {
    public:
        context(main_loop loop);

        operator wyrm_context*() & { return &context_; }

        wyrm_context context_;
        main_loop main_loop_;
    };
}

#endif
