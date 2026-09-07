#include <doctest/doctest.h>

#include <wyrm.h>
#include <wyrm/pair.h>
#include <wyrmxx/core.h>
#include <test_common/test_context_fixture.h>

TEST_SUITE("wpair") {
    TEST_CASE("empty pair") {
        test_context_fixture fix;
        wy_pair* pair = wy_pair_new_f(fix.context);

        REQUIRE_NE(pair, WY_NULL);
        REQUIRE_EQ(wy_pair_car_f(pair), wy_value_Unset());
        REQUIRE_EQ(wy_pair_cdr_f(pair), wy_value_Unset());
    }

    TEST_CASE("cons")
    {
        test_context_fixture fix;
        wy_value a = { .type = WY_TYPE_TAG_WORD, .data = { .word = 0xfeed } };
        wy_value b = { .type = WY_TYPE_TAG_WORD, .data = { .word = 0xbeef } };
        wy_pair* pair = wy_pair_cons_f(fix.context, a, b);

        REQUIRE_NE(pair, WY_NULL);
        REQUIRE_EQ(wy_pair_car_f(pair), a);
        REQUIRE_EQ(wy_pair_cdr_f(pair), b);
    }
}
