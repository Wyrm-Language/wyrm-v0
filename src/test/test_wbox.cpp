#include <doctest/doctest.h>

#include <wyrm/box.h>
#include <wyrmxx/core.h>
#include <test_common/test_context_fixture.h>

TEST_SUITE("wbox") {
    TEST_CASE("construct empty box") {
        test_context_fixture fix;
        wy_box* box = WY_NULL;
        REQUIRE_EQ(wy_box_new_f(fix.context, &box), WY_ERR_NONE);

        REQUIRE_NE(box, WY_NULL);
        REQUIRE_EQ(wy_box_value_f(box), wy_value_Unset());
    }
}
