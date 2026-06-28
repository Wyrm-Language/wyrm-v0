#include <doctest/doctest.h>

#include <wyrm.h>
#include <test_common/test_allocator_fixture.h>
#include <test_common/test_main_loop_fixture.h>

TEST_SUITE("machine") {
    TEST_CASE("init sets allocator and null context") {
        test_allocator_fixture alloc;
        wyrm_machine machine;
        REQUIRE(wyrm_machine_init_s(&machine, alloc.ptr()) == WYRM_ERR_NONE);
        CHECK(machine.allocator == alloc.ptr());
        CHECK(machine.context == WYRM_NULL);
    }

    TEST_CASE("invalid arguments return INVAL") {
        CHECK(wyrm_machine_attach_context(nullptr, nullptr) == WYRM_ERR_INVAL);
    }

    TEST_CASE("attach_context sets context") {
        test_allocator_fixture alloc;
        test_main_loop_fixture loop;
        wyrm_machine machine;
        wyrm_context ctx;
        REQUIRE(wyrm_machine_init_s(&machine, alloc.ptr()) == WYRM_ERR_NONE);
        REQUIRE(wyrm_context_init_s(&ctx, loop.ptr()) == WYRM_ERR_NONE);
        CHECK(wyrm_machine_attach_context(&machine, &ctx) == WYRM_ERR_NONE);
        CHECK(wyrm_context_get_machine(&ctx) == &machine);
    }
}
