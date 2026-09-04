#include <doctest/doctest.h>

#include <wyrm.h>
#include <test_common/test_main_loop_fixture.h>
#include <test_common/test_context_fixture.h>

TEST_SUITE("context")
{
    TEST_CASE("init succeeds and registers a wakeable") {
        test_main_loop_fixture loop;
        wyrm_context ctx{};
        REQUIRE_EQ(wyrm_context_init_s(&ctx), WYRM_ERR_NONE);
        REQUIRE_EQ(wyrm_context_attach_loop(&ctx, loop), WYRM_ERR_NONE);
        REQUIRE_EQ(ctx.main_loop, loop.ptr());
        REQUIRE_NE(ctx.wakeable_source.ptr, WYRM_NULL);

        wyrm_context_finalize_f(&ctx);
    }
}
