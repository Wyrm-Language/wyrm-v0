#include <doctest/doctest.h>
#include <wyrm/sys/atomics.h>

TEST_SUITE("atomics")
{
    TEST_CASE("wy_ref increments and wy_deref decrements") {
        wy_atomic_word a = 0;

        wy_ref(&a);
        wy_ref(&a);

        CHECK(wy_deref(&a) == false);
        CHECK(wy_deref(&a) == true);
    }
}
