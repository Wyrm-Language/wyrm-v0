#include <doctest/doctest.h>
#include <wyrm/sys/atomics.h>

TEST_SUITE("atomics")
{
    TEST_CASE("wyrm_ref increments and wyrm_deref decrements") {
        wyrm_atomic_word a = 0;

        wyrm_ref(&a);
        wyrm_ref(&a);

        CHECK(wyrm_deref(&a) == false);
        CHECK(wyrm_deref(&a) == true);
    }
}
