#include <doctest/doctest.h>
#include <wyrm/ports/allocator_hosted.h>

TEST_SUITE("allocator_hosted")
{
    TEST_CASE("wyrm_allocator_hosted is valid") {
        wyrm_allocator* hosted = wyrm_allocator_hosted_get_sys();
        CHECK(hosted != nullptr);
    }
}
