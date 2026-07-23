#include <doctest/doctest.h>

#include <wyrm.h>
#include <test_common/test_main_loop_fixture.h>
#include <test_common/test_context_fixture.h>
#include <test_common/test_fiber_fixture.h>

TEST_SUITE("fiber")
{
    TEST_CASE("init succeeds and registers a wakeable") {
        test_fiber_fixture ctx;
        REQUIRE_NE(ctx.ptr(), WYRM_NULL);
    }
}
