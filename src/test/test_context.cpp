#include <doctest/doctest.h>

#include <wyrm.h>
#include <test_common/test_main_loop_fixture.h>
#include <test_common/test_fiber_fixture.h>

TEST_SUITE("context")
{
    TEST_CASE("init succeeds and registers a wakeable") {
        test_main_loop_fixture loop;
        wyrm_context ctx;
        REQUIRE(wyrm_context_init_s(&ctx, loop) == WYRM_ERR_NONE);
        REQUIRE_EQ(ctx.main_loop, loop.ptr());
        REQUIRE_NE(ctx.wakeable_source.ptr, WYRM_NULL);
    }

    TEST_CASE("attach_fiber with invalid parent") {
        test_main_loop_fixture loop;
        wyrm_context ctx;
        REQUIRE(wyrm_context_init_s(&ctx, loop.ptr()) == WYRM_ERR_NONE);
        CHECK(wyrm_context_attach_fiber(&ctx, nullptr) == WYRM_ERR_INVAL);
    }

    TEST_CASE("attach_fiber set fiber parent") {
        test_main_loop_fixture loop;
        test_fiber_fixture fiber;

        wyrm_context ctx;

        REQUIRE(wyrm_context_init_s(&ctx, loop.ptr()) == WYRM_ERR_NONE);
        CHECK(wyrm_context_attach_fiber(&ctx, fiber.ptr()) == WYRM_ERR_NONE);
        CHECK(wyrm_fiber_get_context(fiber.ptr()) == &ctx);
    }
}
