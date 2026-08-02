#include <doctest/doctest.h>

#include <wyrm.h>
#include <test_common/test_allocator_fixture.h>
#include <test_common/test_main_loop_fixture.h>
#include <test_common/test_machine_fixture.h>
#include <test_common/test_context_fixture.h>
#include <test_common/test_state_fixture.h>

TEST_SUITE("table") {
    TEST_CASE("string get/set")
    {
        test_state_fixture state;

        wyrm_dict* new_dict = nullptr;
        REQUIRE_EQ(wyrm_dict_new(state.context, &new_dict), WYRM_ERR_NONE);
        REQUIRE_NE(new_dict, nullptr);

        wyrm_dict& dict = *new_dict;;

        wyrm_primitive s1{};
        wyrm_primitive s2{};

        REQUIRE_EQ(wyrm_string_strdup(state.get_context_ptr(), "dog", &s1.str), WYRM_ERR_NONE);
        REQUIRE_EQ(wyrm_string_strdup(state.get_context_ptr(), "cat", &s2.str), WYRM_ERR_NONE);

        wyrm_dict_set(&state, &dict, WYRM_TYPE_TAG_STR, s1, WYRM_TYPE_TAG_WORD, wyrm_primitive_int(0xfeed));
        wyrm_dict_set(&state, &dict, WYRM_TYPE_TAG_STR, s2, WYRM_TYPE_TAG_NIL, wyrm_primitive_null());

        wyrm_value* v1 = wyrm_dict_get(&state, &dict, WYRM_TYPE_TAG_STR, s1);
        wyrm_value* v2 = wyrm_dict_get(&state, &dict, WYRM_TYPE_TAG_STR, s2);

        REQUIRE((v1 != nullptr));
        REQUIRE((v2 != nullptr));

        REQUIRE_EQ(v1->type, WYRM_TYPE_TAG_WORD);
        REQUIRE_EQ(v2->type, WYRM_TYPE_TAG_NIL);

        REQUIRE_EQ(v1->data.word, 0xfeed);
        wyrm_primitive nil = wyrm_primitive_null();
        REQUIRE_EQ(wyrm_memcmp(&v2->data, &nil, sizeof(wyrm_primitive)), 0);
    }

    TEST_CASE("simple uword get/set") {
        test_state_fixture state;

        wyrm_dict* new_dict = nullptr;
        REQUIRE_EQ(wyrm_dict_new(state.context, &new_dict), WYRM_ERR_NONE);
        REQUIRE_NE(new_dict, nullptr);

        wyrm_dict& dict = *new_dict;;

        REQUIRE_EQ(wyrm_dict_get(&state, &dict, WYRM_TYPE_TAG_UWORD, {.uword=5}), WYRM_NULL);
        REQUIRE_EQ(wyrm_dict_set(&state, &dict, WYRM_TYPE_TAG_UWORD, {.uword=5}, WYRM_TYPE_TAG_UWORD, {.uword=0xfeed}), WYRM_ERR_NONE);

        auto res = wyrm_dict_get(&state, &dict, WYRM_TYPE_TAG_UWORD, {.uword=5});
        REQUIRE_NE(res, WYRM_NULL);
        REQUIRE_EQ(res->data.uword, 0xfeed);
    }
}
