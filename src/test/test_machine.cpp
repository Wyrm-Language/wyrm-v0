#include <doctest/doctest.h>

#include <wyrm.h>
#include <test_common/test_allocator_fixture.h>
#include <test_common/test_main_loop_fixture.h>
#include <test_common/test_machine_fixture.h>

TEST_SUITE("machine") {
    TEST_CASE("init sets allocator and null context") {
        test_allocator_fixture alloc;
        wy_machine machine;
        REQUIRE(wy_machine_init_s(&machine, alloc.ptr()) == WY_ERR_NONE);
        CHECK(machine.allocator == alloc.ptr());
        CHECK(machine.context == WY_NULL);
    }

    TEST_CASE("invalid arguments return INVAL") {
        CHECK(wy_machine_attach_context(nullptr, nullptr) == WY_ERR_INVAL);
    }

    TEST_CASE("attach_context sets context") {
        test_allocator_fixture alloc;
        test_main_loop_fixture loop;
        wy_machine machine;
        wy_context ctx;
        REQUIRE_EQ(wy_machine_init_s(&machine, alloc.ptr()), WY_ERR_NONE);
        wy_context_init_s(&ctx);
        CHECK_EQ(wy_machine_attach_context(&machine, &ctx), WY_ERR_NONE);
        CHECK_EQ(wy_context_get_machine(&ctx), &machine);
    }

    TEST_CASE("insert symbol")
    {
        test_machine_fixture machine;

        wy_symtab_entry p{};
        REQUIRE_EQ(wy_machine_find_symbol(machine.get_machine_ptr(), "symbol", &p), WY_ERR_KEY);

        wy_symtab_entry inserted{};
        REQUIRE_EQ(wy_machine_insert_symbol(machine.get_machine_ptr(), "symbol", &inserted), WY_ERR_NONE);

        wy_symtab_entry found{};
        REQUIRE_EQ(wy_machine_find_symbol(machine.get_machine_ptr(), "symbol", &found), WY_ERR_NONE);
    }

    TEST_CASE("insert symbol, overflow")
    {
        test_machine_fixture machine;
        machine.allocator.set_locked(true);

        for (int i = 0; i < 8192; ++i) {
            char buffer[10];
            sprintf(buffer, "b%d", i);
            wy_symtab_entry p{};
            auto err = wy_machine_insert_symbol(machine.get_machine_ptr(), buffer, &p);
            if (err != WY_ERR_NONE) {
                REQUIRE_EQ(err, WY_ERR_NOMEM);
                return;
            }
        }

        throw std::runtime_error{"nomem failed"};
    }
}
