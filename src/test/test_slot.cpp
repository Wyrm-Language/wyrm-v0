#include <doctest/doctest.h>
#include <test_common/test_allocator_fixture.h>

#include <wyrm/slot.h>

TEST_SUITE("slot_dict")
{
    TEST_CASE("basic slots")
    {
        test_allocator_fixture alloc;
        const char bad_sym[] = "bad1";

        wy_slot_dict dict = WY_SLOT_DICT_INITIALIZER;
        REQUIRE_EQ(wy_slot_dict_add_entry(&dict, bad_sym, 15), WYRM_ERR_NOMEM);

        wy_slot_dict_expand_f(&dict, alloc.ptr(), 16);

        REQUIRE_EQ(wy_slot_dict_add_entry(&dict, bad_sym, 15), WYRM_ERR_NONE);
        REQUIRE_EQ(wy_slot_dict_get(&dict, bad_sym), 15);
    }
}
