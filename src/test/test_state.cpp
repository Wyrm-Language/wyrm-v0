#include <doctest/doctest.h>

#include <wyrm.h>
#include <wyrmxx/except.h>
#include <wyrmxx/state.h>
#include <test_common/test_allocator_fixture.h>

TEST_SUITE("wstate")
{
    TEST_CASE("wmryxx state init")
    {
        wyrmxx::state_ s;
        REQUIRE_EQ(s.context, nullptr);
    }

    TEST_CASE("wyrmxx throws on nomem")
    {
        test_allocator_fixture test_fixture;
        test_fixture.set_locked(true);

        CHECK_THROWS_AS(wyrmxx::state{test_fixture.get()}, wyrmxx::out_of_memory);
    }

    TEST_CASE("basic machine allocation") {
        test_allocator_fixture alloc_fixture;


        auto state = wyrm_state_new(alloc_fixture.ptr());
        REQUIRE_NE(state, nullptr);

        wyrm_state_delete(state);

        alloc_fixture.check();
    }
}
