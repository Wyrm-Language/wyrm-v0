#include <doctest/doctest.h>

#include <wyrm.h>
#include <wyrm/module.h>
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

    TEST_CASE("modules register with dense ids and resolve back") {
        test_context_fixture ctx;

        CHECK_EQ(wy_context_module_count(ctx.get_context_ptr()), 0);
        CHECK_EQ(wy_context_get_module(ctx.get_context_ptr(), 0), WY_NULL);

        wy_module* first = wy_module_new_f(ctx.get_context_ptr());
        wy_module* second = wy_module_new_f(ctx.get_context_ptr());
        REQUIRE_NE(first, WY_NULL);
        REQUIRE_NE(second, WY_NULL);

        wy_uword first_id = WY_IDX_INVALID;
        wy_uword second_id = WY_IDX_INVALID;
        REQUIRE_EQ(wy_context_module_register(ctx.get_context_ptr(), first, &first_id), WY_ERR_NONE);
        REQUIRE_EQ(wy_context_module_register(ctx.get_context_ptr(), second, &second_id), WY_ERR_NONE);

        // Ids are assigned in registration order and index the list directly
        CHECK_EQ(first_id, 0);
        CHECK_EQ(second_id, 1);
        CHECK_EQ(wy_context_module_count(ctx.get_context_ptr()), 2);
        CHECK_EQ(wy_context_get_module(ctx.get_context_ptr(), first_id), first);
        CHECK_EQ(wy_context_get_module(ctx.get_context_ptr(), second_id), second);

        // One past the end is not a module
        CHECK_EQ(wy_context_get_module(ctx.get_context_ptr(), 2), WY_NULL);
    }

    TEST_CASE("the module list grows past its initial reservation") {
        test_context_fixture ctx;

        // Enough to force at least two doublings off WY_CONTEXT_MODULE_INITIAL
        constexpr wy_uword count = WY_CONTEXT_MODULE_INITIAL * 4;
        wy_module* modules[count] = {};

        for (wy_uword i = 0; i < count; i++) {
            modules[i] = wy_module_new_f(ctx.get_context_ptr());
            REQUIRE_NE(modules[i], WY_NULL);

            wy_uword id = WY_IDX_INVALID;
            REQUIRE_EQ(wy_context_module_register(ctx.get_context_ptr(), modules[i], &id), WY_ERR_NONE);
            REQUIRE_EQ(id, i);
        }

        CHECK_EQ(wy_context_module_count(ctx.get_context_ptr()), count);

        // Every module survives the reallocations behind it
        for (wy_uword i = 0; i < count; i++) {
            CHECK_EQ(wy_context_get_module(ctx.get_context_ptr(), i), modules[i]);
        }
    }

    TEST_CASE("registering rejects null and a null id is allowed") {
        test_context_fixture ctx;

        CHECK_EQ(wy_context_module_register(ctx.get_context_ptr(), WY_NULL, WY_NULL), WY_ERR_INVAL);

        wy_module* module = wy_module_new_f(ctx.get_context_ptr());
        REQUIRE_NE(module, WY_NULL);
        CHECK_EQ(wy_context_module_register(ctx.get_context_ptr(), module, WY_NULL), WY_ERR_NONE);
        CHECK_EQ(wy_context_get_module(ctx.get_context_ptr(), 0), module);
    }
}
