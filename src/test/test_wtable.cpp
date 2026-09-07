#include <doctest/doctest.h>

#include <wyrm.h>
#include <wyrm/dict.h>
#include <wyrm/string.h>
#include <test_common/test_allocator_fixture.h>
#include <test_common/test_main_loop_fixture.h>
#include <test_common/test_machine_fixture.h>
#include <test_common/test_context_fixture.h>
#include <test_common/test_context_fixture.h>

TEST_SUITE("table") {
    TEST_CASE("string get/set")
    {
        test_context_fixture state;

        wy_dict* new_dict = nullptr;
        REQUIRE_EQ(wy_dict_new(state.get_context_ptr(), &new_dict), WY_ERR_NONE);
        REQUIRE_NE(new_dict, nullptr);

        wy_dict& dict = *new_dict;;

        wy_primitive s1{};
        wy_primitive s2{};

        REQUIRE_EQ(wy_string_strdup(state.get_context_ptr(), "dog", &s1.str), WY_ERR_NONE);
        REQUIRE_EQ(wy_string_strdup(state.get_context_ptr(), "cat", &s2.str), WY_ERR_NONE);

        wy_dict_set(state.get_context_ptr(), &dict, WY_TYPE_TAG_STR, s1, WY_TYPE_TAG_WORD, wy_primitive_int(0xfeed));
        wy_dict_set(state.get_context_ptr(), &dict, WY_TYPE_TAG_STR, s2, WY_TYPE_TAG_NIL, wy_primitive_null());

        wy_value* v1 = wy_dict_get(state.get_context_ptr(), &dict, WY_TYPE_TAG_STR, s1);
        wy_value* v2 = wy_dict_get(state.get_context_ptr(), &dict, WY_TYPE_TAG_STR, s2);

        REQUIRE((v1 != nullptr));
        REQUIRE((v2 != nullptr));

        REQUIRE_EQ(v1->type, WY_TYPE_TAG_WORD);
        REQUIRE_EQ(v2->type, WY_TYPE_TAG_NIL);

        REQUIRE_EQ(v1->data.word, 0xfeed);
        wy_primitive nil = wy_primitive_null();
        REQUIRE_EQ(wy_memcmp(&v2->data, &nil, sizeof(wy_primitive)), 0);
    }

    TEST_CASE("simple uword get/set") {
        test_context_fixture state;

        wy_dict* new_dict = nullptr;
        REQUIRE_EQ(wy_dict_new(state.get_context_ptr(), &new_dict), WY_ERR_NONE);
        REQUIRE_NE(new_dict, nullptr);

        wy_dict& dict = *new_dict;;

        REQUIRE_EQ(wy_dict_get(state.get_context_ptr(), &dict, WY_TYPE_TAG_UWORD, {.uword=5}), WY_NULL);
        REQUIRE_EQ(wy_dict_set(state.get_context_ptr(), &dict, WY_TYPE_TAG_UWORD, {.uword=5}, WY_TYPE_TAG_UWORD, {.uword=0xfeed}), WY_ERR_NONE);

        auto res = wy_dict_get(state.get_context_ptr(), &dict, WY_TYPE_TAG_UWORD, {.uword=5});
        REQUIRE_NE(res, WY_NULL);
        REQUIRE_EQ(res->data.uword, 0xfeed);
    }
}
