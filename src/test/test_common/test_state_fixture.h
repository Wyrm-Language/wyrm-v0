#ifndef WYRM_TEST_STATE_FIXTURE_H_
#define WYRM_TEST_STATE_FIXTURE_H_

#include <doctest/doctest.h>
#include <wyrm.h>

#include <test_common/test_machine_fixture.h>
#include <test_common/test_main_loop_fixture.h>
#include <test_common/test_context_fixture.h>

struct test_state_fixture : wy_state
{
    test_state_fixture() : wy_state{}
    {
        machine = f_ctx.get_machine_ptr();
        context = f_ctx.get_context_ptr();
    }

    wy_allocator* get_allocator_ptr() { return f_ctx.allocator.ptr(); }
    wy_context* get_context_ptr() { return f_ctx.get_context_ptr(); }

    test_context_fixture f_ctx;
};

#endif
