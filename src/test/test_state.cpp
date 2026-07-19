#include <doctest/doctest.h>

#include <wyrm.h>
#include <test_common/test_allocator_fixture.h>

TEST_SUITE("wstate")
{
    TEST_CASE("basic machine allocation") {
        test_allocator_fixture alloc_fixture;


        auto state = wyrm_state_new(alloc_fixture.ptr());
        REQUIRE_NE(state, nullptr);

        wyrm_state_delete(state);

        alloc_fixture.check();
    }
}
