#include <doctest/doctest.h>

#include <wyrm.h>
#include <test_common/test_allocator_fixture.h>
#include <test_common/test_main_loop_fixture.h>
#include <test_common/test_machine_fixture.h>

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

    TEST_CASE("insert symbol")
    {
        test_machine_fixture machine;

        wyrm_symtab_entry p{};
        REQUIRE_EQ(wyrm_machine_find_symbol(machine.get_machine_ptr(), "symbol", &p), WYRM_ERR_KEY);

        wyrm_symtab_entry inserted{};
        REQUIRE_EQ(wyrm_machine_insert_symbol(machine.get_machine_ptr(), "symbol", &inserted), WYRM_ERR_NONE);

        wyrm_symtab_entry found{};
        REQUIRE_EQ(wyrm_machine_find_symbol(machine.get_machine_ptr(), "symbol", &found), WYRM_ERR_NONE);
    }

    TEST_CASE("insert symbol, overflow")
    {
        test_machine_fixture machine;
        machine.allocator.set_locked(true);

        for (int i = 0; i < 8192; ++i) {
            char buffer[10];
            sprintf(buffer, "b%d", i);
            wyrm_symtab_entry p{};
            auto err = wyrm_machine_insert_symbol(machine.get_machine_ptr(), buffer, &p);
            if (err != WYRM_ERR_NONE) {
                REQUIRE_EQ(err, WYRM_ERR_NOMEM);
                return;
            }
        }

        throw std::runtime_error{"nomem failed"};
    }
}
