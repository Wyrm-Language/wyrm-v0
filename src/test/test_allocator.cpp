#include <doctest/doctest.h>

#include <wyrmxx/allocator.h>
#include <test_common/test_allocator_fixture.h>

TEST_SUITE("allocator")
{
    TEST_CASE("allocator basics") {
        test_allocator_fixture fixture;
        auto alloc = fixture.get();

        void* d = wy_allocator_alloc(alloc, 5);
        wy_allocator_free(alloc, d);

        fixture.check();
    }
}
