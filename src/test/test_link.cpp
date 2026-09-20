#include <doctest/doctest.h>

#include <iterator>

#include <wyrm.h>
#include <wyrm/allocator.h>
#include <wyrm/builtins.h>
#include <wyrm/link.h>
#include <wyrm/module.h>
#include <wyrm/opcode.h>
#include <wyrm/value.h>
#include <test_common/test_fiber_fixture.h>

namespace {

constexpr wy_u32 enc1(wy_u8 op, wy_u8 f, wy_u16 a0)
{
    return (wy_u32(a0) << 16) | (wy_u32(f) << 8) | wy_u32(op);
}

/** A wy_module with `nglobals` Unset globals and empty fill bookkeeping, ready for a free_names entry to be added. */
wy_module* make_module_with_globals(wy_context* ctx, wy_uword nglobals)
{
    wy_module* module = wy_module_new_f(ctx);
    module->global_count = nglobals;
    if (nglobals > 0) {
        module->globals = (wy_value*) wy_context_gc_alloc(ctx, sizeof(wy_value) * nglobals);
        module->fill_layer = (wy_u8*) wy_context_gc_alloc(ctx, sizeof(wy_u8) * nglobals);
        module->fill_source = (wy_symbol*) wy_context_gc_alloc(ctx, sizeof(wy_symbol) * nglobals);
        for (wy_uword i = 0; i < nglobals; i++) {
            module->globals[i] = wy_value_unset();
            module->fill_layer[i] = 0;
            module->fill_source[i] = WY_NULL;
        }
    }
    return module;
}

} // namespace

TEST_SUITE("link") {
    TEST_CASE("layer 3 fill matches a free name against a builtins export") {
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();

        wy_module* builtins = WY_NULL;
        REQUIRE_EQ(wy_builtins_new(context, &builtins), WY_ERR_NONE);

        wy_module* module = make_module_with_globals(context, 1);
        wy_allocator* allocator = wy_context_get_machine(context)->allocator;
        REQUIRE_EQ(wy_slot_dict_expand_f(&module->free_names, allocator, 4), WY_ERR_NONE);

        wy_symbol println_sym;
        REQUIRE_EQ(wy_context_intern(context, "println", 7, &println_sym), WY_ERR_NONE);
        REQUIRE_EQ(wy_slot_dict_add_entry(&module->free_names, println_sym, 0), WY_ERR_NONE);

        REQUIRE_EQ(wy_link_fill_from_builtins(context, module, builtins), WY_ERR_NONE);

        CHECK_EQ(module->fill_layer[0], 3);
        wy_uword builtins_slot = wy_slot_dict_get(&builtins->exports, println_sym);
        REQUIRE_NE(builtins_slot, WY_SLOT_INVALID);
        // filled with the exact same callable builtins exports (correct + callable: a NATIVE object)
        CHECK_EQ(module->globals[0].type, WY_TYPE_TAG_NATIVE);
        CHECK_EQ(module->globals[0].data.gc_object, builtins->globals[builtins_slot].data.gc_object);
    }

    TEST_CASE("a free name builtins doesn't supply stays Unset after the fill") {
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();

        wy_module* builtins = WY_NULL;
        REQUIRE_EQ(wy_builtins_new(context, &builtins), WY_ERR_NONE);

        wy_module* module = make_module_with_globals(context, 1);
        wy_allocator* allocator = wy_context_get_machine(context)->allocator;
        REQUIRE_EQ(wy_slot_dict_expand_f(&module->free_names, allocator, 4), WY_ERR_NONE);

        wy_symbol missing_sym;
        REQUIRE_EQ(wy_context_intern(context, "does_not_exist", 15, &missing_sym), WY_ERR_NONE);
        REQUIRE_EQ(wy_slot_dict_add_entry(&module->free_names, missing_sym, 0), WY_ERR_NONE);

        REQUIRE_EQ(wy_link_fill_from_builtins(context, module, builtins), WY_ERR_NONE);

        CHECK_EQ(module->fill_layer[0], 0);
        CHECK(wy_value_is_unset(module->globals[0]));
    }

    TEST_CASE("wy_module_run_init on a trivial init runs it and marks the module ready") {
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();

        const wy_u32 code[] = { enc1(WY_OP_RETURN, 0, 0) };
        wy_module* module = make_module_with_globals(context, 0);
        module->code = code;
        module->code_len = std::size(code);
        module->init_nlocals = 0;

        CHECK_EQ(wy_module_run_init(context, module), WY_ERR_NONE);
        CHECK_EQ(module->state, WY_MODULE_READY);
    }

    TEST_CASE("wy_module_run_init on trapping init code faults and marks the module failed") {
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();

        const wy_u32 code[] = { enc1(WY_OP_TRAP, 0, 0) };
        wy_module* module = make_module_with_globals(context, 0);
        module->code = code;
        module->code_len = std::size(code);
        module->init_nlocals = 0;

        CHECK_EQ(wy_module_run_init(context, module), WY_ERR_FAULT);
        CHECK_EQ(module->state, WY_MODULE_FAILED);
        CHECK(wy_value_is_error(context->current_fiber->fault));
    }
}
