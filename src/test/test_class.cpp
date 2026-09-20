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

    TEST_CASE("slot lookup walks super, base-first")
    {
        test_context_fixture fix;
        wy_context* ctx = fix.get_context_ptr();

        wy_symtab_entry name {};
        wy_symtab_entry age {};
        wy_machine_insert_symbol(fix.get_machine_ptr(), "name", &name);
        wy_machine_insert_symbol(fix.get_machine_ptr(), "age", &age);

        wy_class* base = WY_NULL;
        REQUIRE_EQ(wy_class_new(ctx, &base), WY_ERR_NONE);
        base->slots = (wy_class_slot*) wy_context_gc_alloc(ctx, sizeof(wy_class_slot));
        base->slots[0] = wy_class_slot { name, wy_value_nil(), wy_value_unset(), wy_value_unset() };
        base->slot_count = 1;

        wy_class* derived = WY_NULL;
        REQUIRE_EQ(wy_class_new(ctx, &derived), WY_ERR_NONE);
        derived->super = base;
        derived->depth = 1;
        derived->slots = (wy_class_slot*) wy_context_gc_alloc(ctx, sizeof(wy_class_slot) * 2);
        derived->slots[0] = base->slots[0];
        derived->slots[1] = wy_class_slot { age, wy_value_nil(), wy_value_unset(), wy_value_unset() };
        derived->slot_count = 2;

        wy_uword name_idx = WY_UWORD_MAX;
        wy_uword age_idx = WY_UWORD_MAX;
        wy_class_slot* found_name = wy_class_find_slot_f(derived, name, &name_idx);
        wy_class_slot* found_age = wy_class_find_slot_f(derived, age, &age_idx);

        REQUIRE_NE(found_name, WY_NULL);
        REQUIRE_NE(found_age, WY_NULL);
        CHECK_EQ(name_idx, 0u);
        CHECK_EQ(age_idx, 1u);
        CHECK_EQ(wy_class_distance_f(derived, base), 1u);
        CHECK_EQ(wy_class_distance_f(derived, derived), 0u);
        CHECK_EQ(wy_class_distance_f(base, derived), WY_WILDCARD_DISTANCE);
    }

    TEST_CASE("new_instance on a 2-level hierarchy: base-first defaults, getslot/setslot round-trip") {
        test_context_fixture fix;
        wy_context* ctx = fix.get_context_ptr();

        wy_symtab_entry x {};
        wy_symtab_entry y {};
        wy_machine_insert_symbol(fix.get_machine_ptr(), "x", &x);
        wy_machine_insert_symbol(fix.get_machine_ptr(), "y", &y);

        wy_class* base = WY_NULL;
        REQUIRE_EQ(wy_class_new(ctx, &base), WY_ERR_NONE);
        base->slots = (wy_class_slot*) wy_context_gc_alloc(ctx, sizeof(wy_class_slot));
        base->slots[0] = wy_class_slot { x, wy_value_word(1), wy_value_unset(), wy_value_unset() };
        base->slot_count = 1;

        wy_class* derived = WY_NULL;
        REQUIRE_EQ(wy_class_new(ctx, &derived), WY_ERR_NONE);
        derived->super = base;
        derived->depth = 1;
        derived->slots = (wy_class_slot*) wy_context_gc_alloc(ctx, sizeof(wy_class_slot) * 2);
        derived->slots[0] = base->slots[0];
        derived->slots[1] = wy_class_slot { y, wy_value_word(2), wy_value_unset(), wy_value_unset() };
        derived->slot_count = 2;

        wy_instance* inst = WY_NULL;
        REQUIRE_EQ(wy_instance_new_f(ctx, derived, &inst), WY_ERR_NONE);
        REQUIRE_EQ(inst->cls, derived);
        CHECK_EQ(inst->slots[0].data.word, 1);
        CHECK_EQ(inst->slots[1].data.word, 2);

        inst->slots[0] = wy_value_word(10);
        inst->slots[1] = wy_value_word(20);
        CHECK_EQ(inst->slots[0].data.word, 10);
        CHECK_EQ(inst->slots[1].data.word, 20);
    }

    TEST_CASE("class realisation: a base declared after the derived class in source order") {
        /* wyc-format.md §8.6: "the slot is read at the moment the class is
         * realised, which is what lets a base defined further down the
         * same file work" - realise class_protos[1] (derived, super_slot 0)
         * before global slot 0 (the base) is filled, then realise the base
         * and confirm the derived's super link only exists once expected;
         * here we realise base first (as a real `class` op sequence would)
         * and confirm the global-slot indirection resolves it regardless of
         * proto array order. */
        test_context_fixture fix;
        wy_context* ctx = fix.get_context_ptr();

        wy_module* module = wy_module_new_f(ctx);
        REQUIRE_NE(module, WY_NULL);

        wy_symtab_entry base_sym {}, derived_sym {}, x_sym {};
        wy_machine_insert_symbol(fix.get_machine_ptr(), "Base", &base_sym);
        wy_machine_insert_symbol(fix.get_machine_ptr(), "Derived", &derived_sym);
        wy_machine_insert_symbol(fix.get_machine_ptr(), "x", &x_sym);

        module->global_count = 2;
        module->globals = (wy_value*) wy_context_gc_alloc(ctx, sizeof(wy_value) * 2);
        module->globals[0] = wy_value_unset();
        module->globals[1] = wy_value_unset();

        /* class_protos[0] = Derived (declared first in the file), whose
         * super_slot (0) names Base - defined by class_protos[1], further
         * down the same file, exactly the risk case this test covers. */
        wy_class_proto* protos = (wy_class_proto*) wy_context_gc_alloc(ctx, sizeof(wy_class_proto) * 2);
        REQUIRE_NE(protos, WY_NULL);
        protos[0] = wy_class_proto {};
        protos[0].name = derived_sym;
        protos[0].super_slot = 0;
        protos[0].init_fn = -1;
        protos[0].nslots = 0;
        protos[0].nmsgs = 0;
        protos[0].nstatics = 0;

        protos[1] = wy_class_proto {};
        protos[1].name = base_sym;
        protos[1].super_slot = -1;
        protos[1].init_fn = -1;
        protos[1].nslots = 0;
        protos[1].nmsgs = 0;
        protos[1].nstatics = 0;

        module->class_protos = protos;
        module->class_count = 2;

        /* Realising Derived (index 0) before Base's global is filled faults -
         * the global slot genuinely isn't a class yet. */
        wy_class* derived = WY_NULL;
        CHECK_EQ(wy_class_realise_f(ctx, module, 0, &derived), WY_ERR_BAD_TYPE);

        /* Realise Base (index 1) and publish it to its global slot, the way
         * the `class`/`gset` op pair in Base's own init code would. */
        wy_class* base = WY_NULL;
        REQUIRE_EQ(wy_class_realise_f(ctx, module, 1, &base), WY_ERR_NONE);
        module->globals[0] = wy_value_object(WY_TYPE_TAG_CLASS, (wy_object*) base);

        /* Now Derived realises cleanly against the filled slot. */
        REQUIRE_EQ(wy_class_realise_f(ctx, module, 0, &derived), WY_ERR_NONE);
        CHECK_EQ(derived->super, base);
        CHECK_EQ(derived->depth, 1);

        /* Idempotent: a second `class` op on the same index returns the
         * cached object rather than re-realising. */
        wy_class* derived_again = WY_NULL;
        REQUIRE_EQ(wy_class_realise_f(ctx, module, 0, &derived_again), WY_ERR_NONE);
        CHECK_EQ(derived_again, derived);
    }
}
