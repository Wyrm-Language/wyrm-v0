#include <doctest/doctest.h>

#include <wyrm.h>
#include <test_common/test_context_fixture.h>

TEST_SUITE("wclass")
{
    TEST_CASE("init basic class")
    {
        test_context_fixture fix;

        wyrm_class* cls = WYRM_NULL;

        REQUIRE_EQ(wyrm_class_new(fix.get_context_ptr(), &cls), WYRM_ERR_NONE);
        REQUIRE_NE(cls, WYRM_NULL);
    }

    TEST_CASE("slot accessors")
    {
        test_context_fixture fix;

        wyrm_class* cls = WYRM_NULL;
        REQUIRE_EQ(wyrm_class_new(fix.get_context_ptr(), &cls), WYRM_ERR_NONE);

        wyrm_primitive name {};
        wyrm_primitive age {};
        wyrm_machine_insert_symbol(fix.get_machine_ptr(), "name", &name);
        wyrm_machine_insert_symbol(fix.get_machine_ptr(), "age", &age);

        wyrm_class_add_slot_f(cls, name, WYRM_TYPE_TAG_STR);
        wyrm_class_add_slot_f(cls, age, WYRM_TYPE_TAG_WORD);

        auto slot_name = wyrm_class_get_slot_selector_f(cls, name);
        auto slot_age = wyrm_class_get_slot_selector_f(cls, age);

        REQUIRE_NE(slot_name, WYRM_BAD_SLOT);
        REQUIRE_NE(slot_age, WYRM_BAD_SLOT);
        REQUIRE_NE(slot_name, slot_age);
    }
}
