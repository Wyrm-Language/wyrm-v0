#ifndef WYRM_TEST_MACHINE_CONTEXT_H_
#define WYRM_TEST_MACHINE_CONTEXT_H_

#include <doctest/doctest.h>
#include <wyrm.h>
#include <wyrmxx/main_loop.h>
#include <wyrmxx/context.h>

#include <test_common/test_machine_fixture.h>
#include <test_common/test_main_loop_fixture.h>

struct test_context_fixture : test_machine_fixture
{
    test_main_loop_fixture main_loop_;
    wyrmxx::context context;

    test_context_fixture()
        : main_loop_{}
        , context{main_loop_.get()}
    {
        wyrm_machine_attach_context(&machine, context);
    }

    wyrm_context* get_context_ptr()
    {
        return context;
    }
};

#endif
