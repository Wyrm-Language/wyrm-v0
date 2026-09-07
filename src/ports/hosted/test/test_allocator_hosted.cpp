#include <doctest/doctest.h>
#include <wyrm/ports/allocator_hosted.h>

TEST_SUITE("allocator_hosted")
{
    TEST_CASE("wy_allocator_hosted is valid") {
        wy_allocator* hosted = wy_allocator_hosted_get_sys();
        CHECK(hosted != nullptr);
    }
}
