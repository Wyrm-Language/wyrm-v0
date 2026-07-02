#include <doctest/doctest.h>

#include <wyrm.h>
#include <test_common/test_allocator_fixture.h>
#include <test_common/test_main_loop_fixture.h>

TEST_SUITE("dict") {
    TEST_CASE("simple uword get/set") {
        test_allocator_fixture alloc;
        wyrm_dict dict{};

        wyrm_dict_init_f(&dict, alloc.ptr());

        REQUIRE_EQ(wyrm_dict_get(&dict, WYRM_TYPE_TAG_UWORD, {.uword=5}), WYRM_NULL);
        REQUIRE_EQ(wyrm_dict_set(&dict, WYRM_TYPE_TAG_UWORD, {.uword=5}, WYRM_TYPE_TAG_UWORD, {.uword=0xfeed}), WYRM_ERR_NONE);

        auto res = wyrm_dict_get(&dict, WYRM_TYPE_TAG_UWORD, {.uword=5});
        REQUIRE_NE(res, WYRM_NULL);
        REQUIRE_EQ(res->data.uword, 0xfeed);

        wyrm_dict_finalize_f(&dict);
        alloc.check();
    }
}
