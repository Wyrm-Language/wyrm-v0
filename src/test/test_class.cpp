#include <doctest/doctest.h>

#include <wyrm.h>
#include <test_common/test_context_fixture.h>

TEST_SUITE("wclass")
{
    TEST_CASE("init basic class")
    {
        test_context_fixture fix;

        wy_class* cls = WY_NULL;

        REQUIRE_EQ(wy_class_new(fix.get_context_ptr(), &cls), WY_ERR_NONE);
        REQUIRE_NE(cls, WY_NULL);
    }

    TEST_CASE("slot accessors")
    {
        test_context_fixture fix;

        wy_class* cls = WY_NULL;
        REQUIRE_EQ(wy_class_new(fix.get_context_ptr(), &cls), WY_ERR_NONE);

        wy_symtab_entry name {};
        wy_symtab_entry age {};
        wy_machine_insert_symbol(fix.get_machine_ptr(), "name", &name);
        wy_machine_insert_symbol(fix.get_machine_ptr(), "age", &age);

        wy_class_add_slot_f(cls, name, WY_SLOT_DEFAULTS);
        wy_class_add_slot_f(cls, age, WY_SLOT_DEFAULTS);

        auto slot_name = wy_class_get_slot_selector_f(cls, name);
        auto slot_age = wy_class_get_slot_selector_f(cls, age);

        REQUIRE_NE(slot_name, WY_BAD_SLOT);
        REQUIRE_NE(slot_age, WY_BAD_SLOT);
        REQUIRE_NE(slot_name, slot_age);
    }
}
