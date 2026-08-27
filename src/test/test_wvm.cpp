#include <doctest/doctest.h>

#include <wyrm.h>
#include <wyrm/wopcode.h>
#include <wyrm/wvm.h>
#include <test_common/test_main_loop_fixture.h>
#include <test_common/test_context_fixture.h>
#include <test_common/test_fiber_fixture.h>

TEST_SUITE("wvm") {
    TEST_CASE("bad opcode causes error")
    {
        wy_u32 bad_instruction[] = { 0xffffffffu };
        test_fiber_fixture ctx;

        wyrm_error result = wy_vm_exec_bytecode(ctx.context, 0, bad_instruction, std::size(bad_instruction));
        REQUIRE_EQ(result, WYRM_ERR_INVAL);

        wyrm_value value = *wy_fiber_accum(ctx);
        wyrm_value un = wyrm_value_Unset();
        REQUIRE_EQ(value.type, un.type);
        REQUIRE_EQ(value.data.error, un.data.error);
    }

    TEST_CASE("noop leaves accumulator unchanged")
    {
        wy_u32 buffer[] = { wy_opcode_p0(WYRM_OP_NOOP)};
        test_fiber_fixture ctx;

        wyrm_error result = wy_vm_exec_bytecode(ctx.context, 0, buffer, std::size(buffer));
        REQUIRE_EQ(result, WYRM_ERR_NONE);

        wyrm_value value = *wy_fiber_accum(ctx);
        wyrm_value un = wyrm_value_Unset();
        REQUIRE_EQ(value.type, un.type);
        REQUIRE_EQ(value.data.error, un.data.error);
    }

    TEST_CASE("pass has evaluates to nil")
    {
        wy_u32 buffer[] = { wy_opcode_p0(WYRM_OP_PASS)};
        test_fiber_fixture ctx;

        wyrm_error result = wy_vm_exec_bytecode(ctx.context, 0, buffer, std::size(buffer));
        REQUIRE_EQ(result, WYRM_ERR_NONE);

        wyrm_value value = *wy_fiber_accum(ctx);
        wyrm_value nil = wyrm_value_nil();
        REQUIRE_EQ(value.type, nil.type);
        REQUIRE_EQ(value.data.error, nil.data.error);
    }
}
