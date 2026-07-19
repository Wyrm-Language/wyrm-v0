#include <doctest/doctest.h>

#include <wyrm.h>
#include <test_common/test_allocator_fixture.h>
#include <test_common/test_main_loop_fixture.h>
#include <test_common/test_machine_fixture.h>
#include <test_common/test_context_fixture.h>

TEST_SUITE("table") {
    TEST_CASE("string get/set")
    {
        test_context_fixture ctx;
        wyrm_table dict{};

        wyrm_table_init_f(&dict, ctx.allocator.ptr());

        wyrm_primitive s1{};
        wyrm_primitive s2{};

        REQUIRE_EQ(wyrm_string_strdup(ctx.get_context_ptr(), "dog", &s1.str), WYRM_ERR_NONE);
        REQUIRE_EQ(wyrm_string_strdup(ctx.get_context_ptr(), "cat", &s2.str), WYRM_ERR_NONE);

        wyrm_table_set(&dict, WYRM_TYPE_TAG_STR, s1, WYRM_TYPE_TAG_WORD, wyrm_primitive_int(0xfeed));
        wyrm_table_set(&dict, WYRM_TYPE_TAG_STR, s2, WYRM_TYPE_TAG_NIL, wyrm_primitive_null());

        wyrm_value* v1 = wyrm_table_get(&dict, WYRM_TYPE_TAG_STR, s1);
        wyrm_value* v2 = wyrm_table_get(&dict, WYRM_TYPE_TAG_STR, s2);

        REQUIRE((v1 != nullptr));
        REQUIRE((v2 != nullptr));

        REQUIRE_EQ(v1->type, WYRM_TYPE_TAG_WORD);
        REQUIRE_EQ(v2->type, WYRM_TYPE_TAG_NIL);

        REQUIRE_EQ(v1->data.word, 0xfeed);
        wyrm_primitive nil = wyrm_primitive_null();
        REQUIRE_EQ(wyrm_memcmp(&v2->data, &nil, sizeof(wyrm_primitive)), 0);
    }

    TEST_CASE("simple uword get/set") {
        test_allocator_fixture alloc;
        wyrm_table dict{};

        wyrm_table_init_f(&dict, alloc.ptr());

        REQUIRE_EQ(wyrm_table_get(&dict, WYRM_TYPE_TAG_UWORD, {.uword=5}), WYRM_NULL);
        REQUIRE_EQ(wyrm_table_set(&dict, WYRM_TYPE_TAG_UWORD, {.uword=5}, WYRM_TYPE_TAG_UWORD, {.uword=0xfeed}), WYRM_ERR_NONE);

        auto res = wyrm_table_get(&dict, WYRM_TYPE_TAG_UWORD, {.uword=5});
        REQUIRE_NE(res, WYRM_NULL);
        REQUIRE_EQ(res->data.uword, 0xfeed);

        wyrm_table_finalize_f(&dict);
        alloc.check();
    }
}
