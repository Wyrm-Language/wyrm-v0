#ifndef WYRM_TEST_MACHINE_CONTEXT_H_
#define WYRM_TEST_MACHINE_CONTEXT_H_

#include <doctest/doctest.h>
#include <wyrm.h>

#include <test_common/test_machine_fixture.h>
#include <test_common/test_main_loop_fixture.h>

class test_context_failure : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

struct test_context_fixture : test_machine_fixture
{
    wyrm_context context;
    test_main_loop_fixture main_loop;

    test_context_fixture()
        : context{}
    {
        wyrm_context_init_s(&context, main_loop.ptr());
        wyrm_machine_attach_context(get_machine_ptr(), &context);
    }

    wyrm_context* get_context_ptr()
    {
        return &context;
    }
};

#endif
