#include <doctest/doctest.h>

#include <wyrm.h>
#include <wyrm/vm.h>
#include <wyrm/module.h>
#include <test_common/test_main_loop_fixture.h>
#include <test_common/test_context_fixture.h>
#include <test_common/test_fiber_fixture.h>

TEST_SUITE("wvm") {
    TEST_CASE("exec_bytecode is a stub pending the epic 2 interpreter loop")
    {
        wy_u32 buffer[] = { 0x00000000u };
        test_fiber_fixture ctx;

        wy_error result = wy_vm_exec_bytecode(ctx.context, 0, buffer, std::size(buffer));
        REQUIRE_EQ(result, WY_ERR_INVAL);
    }

    TEST_CASE("the packed payload round trips both halves")
    {
        // Distinct values in each half, so a swapped or overlapping field shows
        const wy_uword module_id = 0xABC;
        const wy_uword address = 0x12345;

        wy_primitive packed = wy_exec_fn_b_code_pack(module_id, address);

        CHECK_EQ(wy_exec_fn_b_code_module_id(packed), module_id);
        CHECK_EQ(wy_exec_fn_b_code_address(packed), address);
    }

    TEST_CASE("each half spans its full range without touching the other")
    {
        const wy_uword max_module = WY_EXEC_FN_MODULE_MAX - 1;
        const wy_uword max_address = WY_EXEC_FN_ADDR_MAX - 1;

        wy_primitive only_module = wy_exec_fn_b_code_pack(max_module, 0);
        CHECK_EQ(wy_exec_fn_b_code_module_id(only_module), max_module);
        CHECK_EQ(wy_exec_fn_b_code_address(only_module), 0);

        wy_primitive only_address = wy_exec_fn_b_code_pack(0, max_address);
        CHECK_EQ(wy_exec_fn_b_code_module_id(only_address), 0);
        CHECK_EQ(wy_exec_fn_b_code_address(only_address), max_address);

        wy_primitive both = wy_exec_fn_b_code_pack(max_module, max_address);
        CHECK_EQ(wy_exec_fn_b_code_module_id(both), max_module);
        CHECK_EQ(wy_exec_fn_b_code_address(both), max_address);

        // 12 + 20 fits a 32 bit primitive with nothing above it
        CHECK_EQ(both.uword, 0xFFFFFFFFu);
    }

    TEST_CASE("the address field spans exactly the largest code array")
    {
        // The encoding must reach every slot WY_MAX_ARRAY_LEN permits
        CHECK_EQ(WY_EXEC_FN_ADDR_MAX, WY_MAX_ARRAY_LEN);
    }

    TEST_CASE("a bytecode callable resolves its module and returns")
    {
        test_fiber_fixture ctx;

        wy_module* module = wy_module_new_f(ctx.get_context_ptr());
        REQUIRE_NE(module, WY_NULL);

        wy_uword module_id = WY_IDX_INVALID;
        REQUIRE_EQ(wy_context_module_register(ctx.get_context_ptr(), module, &module_id), WY_ERR_NONE);

        wy_exec_fn fn = wy_exec_fn_create_b_code(module_id, 0);
        CHECK_EQ(fn.fn, wy_vm_exec_b_code);
        CHECK_EQ(wy_exec_fn_b_code_module_id(fn.c_data), module_id);

        // Runs through the fiber loop like any other callable
        REQUIRE_EQ(wy_fiber_push_continuation(ctx.get_fiber_ptr(), fn, 0), WY_ERR_NONE);
        CHECK_EQ(wy_context_exec(ctx.get_context_ptr()), WY_ERR_NONE);
    }
}
