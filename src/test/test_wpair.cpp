#include <doctest/doctest.h>

#include <wyrm.h>
#include <wyrm/pair.h>
#include <wyrmxx/core.h>
#include <test_common/test_context_fixture.h>

TEST_SUITE("wpair") {
    TEST_CASE("empty pair") {
        test_context_fixture fix;
        wyrm_pair* pair = wyrm_pair_new_f(fix.context);

        REQUIRE_NE(pair, WYRM_NULL);
        REQUIRE_EQ(wyrm_pair_car_f(pair), wyrm_value_Unset());
        REQUIRE_EQ(wyrm_pair_cdr_f(pair), wyrm_value_Unset());
    }

    TEST_CASE("cons")
    {
        test_context_fixture fix;
        wyrm_value a = { .type = WYRM_TYPE_TAG_WORD, .data = { .word = 0xfeed } };
        wyrm_value b = { .type = WYRM_TYPE_TAG_WORD, .data = { .word = 0xbeef } };
        wyrm_pair* pair = wyrm_pair_cons_f(fix.context, a, b);

        REQUIRE_NE(pair, WYRM_NULL);
        REQUIRE_EQ(wyrm_pair_car_f(pair), a);
        REQUIRE_EQ(wyrm_pair_cdr_f(pair), b);
    }
}
