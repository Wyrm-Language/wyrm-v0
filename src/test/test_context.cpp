#include <doctest/doctest.h>

#include <wyrm.h>
#include <test_common/test_main_loop_fixture.h>
#include <test_common/test_context_fixture.h>

TEST_SUITE("context")
{
    TEST_CASE("init succeeds and registers a wakeable") {
        test_main_loop_fixture loop;
        wy_context ctx{};
        wy_context_init_s(&ctx);
        REQUIRE_EQ(wy_context_attach_loop(&ctx, loop), WY_ERR_NONE);
        REQUIRE_EQ(ctx.main_loop, loop.ptr());
        REQUIRE_NE(ctx.wakeable_source.ptr, WY_NULL);

        wy_context_finalize_f(&ctx);
    }
}
