#include <doctest/doctest.h>

#include <wyrm/wbox.h>
#include <wyrmxx/wcore.h>
#include <test_common/test_context_fixture.h>

TEST_SUITE("wbox") {
    TEST_CASE("construct empty box") {
        test_context_fixture fix;
        wyrm_box* box = WYRM_NULL;
        REQUIRE_EQ(wyrm_box_new_f(fix.context, &box), WYRM_ERR_NONE);

        REQUIRE_NE(box, WYRM_NULL);
        REQUIRE_EQ(wyrm_box_value_f(box), wyrm_value_Unset());
    }
}
