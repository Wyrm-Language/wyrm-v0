#include <doctest/doctest.h>

#include <wyrm.h>
#include <test_common/test_context_fixture.h>
#include <test_common/test_fiber_fixture.h>

static wy_value word_value(wy_uword v)
{
    wy_value value{};
    value.type = WY_TYPE_TAG_WORD;
    value.data.uword = v;
    return value;
}

namespace {

//! Result recorded by exec_record_result, so the exec loop can be observed
wy_uword g_result = 0;
wy_uword g_result_count = 0;

wy_exec_state exec_record_result(wy_state* state)
{
    g_result_count = wy_state_value_count(state);
    wy_value* a = wy_state_value_n(state, 0);
    g_result = (a == WY_NULL) ? 0 : (wy_uword) a->data.word;
    return WY_EXEC_DONE;
}

wy_exec_state exec_mul_int(wy_state* state)
{
    wy_value* a = wy_state_value_n(state, 0);
    wy_value* b = wy_state_value_n(state, 1);
    REQUIRE_NE(a, WY_NULL);
    REQUIRE_NE(b, WY_NULL);

    wy_state_push_return(state, wy_value_word(a->data.word * b->data.word));
    return WY_EXEC_DONE;
}

wy_exec_state exec_call_mul(wy_state* state)
{
    wy_value args[2] = { word_value(8), word_value(32) };
    REQUIRE_EQ(wy_state_call_continue(state, exec_record_result, exec_mul_int, args, 2), WY_ERR_NONE);
    return WY_EXEC_CONTINUE;
}

wy_exec_state exec_tail_call(wy_state* state)
{
    wy_state_push_return(state, wy_value_word(7));
    REQUIRE_EQ(wy_state_set_pending(state, exec_record_result), WY_ERR_NONE);
    return WY_EXEC_TAIL_CALL;
}

wy_state make_state(test_fiber_fixture& ctx)
{
    wy_state state{};
    wy_state_init_from_context_f(&state, ctx.get_context_ptr());
    state.fiber = ctx.get_fiber_ptr();
    return state;
}

}  // namespace

TEST_SUITE("fiber")
{
    TEST_CASE("push_frame_f rebases, pop_continuation_f restores") {
        test_fiber_fixture ctx;
        wy_fiber* fiber = ctx.get_fiber_ptr();

        // Caller value, then a frame: the caller's value sits below the new base
        REQUIRE_EQ(wy_fiber_push_value_f(fiber, word_value(99)), WY_ERR_NONE);
        REQUIRE_EQ(wy_fiber_push_frame_f(fiber, exec_record_result), WY_ERR_NONE);
        CHECK_EQ(wy_fiber_value_count_f(fiber), 0);

        // Two args, a scratch value, then the result to preserve
        REQUIRE_EQ(wy_fiber_push_value_f(fiber, word_value(1)), WY_ERR_NONE);
        REQUIRE_EQ(wy_fiber_push_value_f(fiber, word_value(2)), WY_ERR_NONE);
        CHECK_EQ(wy_fiber_value_n(fiber, 0)->data.uword, 1);
        REQUIRE_EQ(wy_fiber_push_value_f(fiber, word_value(77)), WY_ERR_NONE);
        REQUIRE_EQ(wy_fiber_push_value_f(fiber, word_value(55)), WY_ERR_NONE);

        wy_exec_fn out_fn = WY_NULL;
        CHECK_EQ(wy_fiber_pop_continuation_f(fiber, &out_fn, 1), WY_ERR_NONE);
        CHECK_EQ(out_fn, exec_record_result);

        // Caller sees its own value plus the preserved result
        CHECK_EQ(wy_fiber_value_count_f(fiber), 2);
        CHECK_EQ(wy_fiber_value_n(fiber, 0)->data.uword, 99);
        CHECK_EQ(wy_fiber_value_n(fiber, 1)->data.uword, 55);

        CHECK_EQ(wy_fiber_pop_continuation_f(fiber, &out_fn, 0), WY_ERR_EMPTY);
    }

    TEST_CASE("frame stack overflow is reported, not overrun") {
        test_context_fixture ctx;
        const wy_uword frame_count = 4;
        wy_fiber* fiber = wy_fiber_create(ctx.get_context_ptr(), 64, frame_count);
        REQUIRE_NE(fiber, WY_NULL);
        wy_context_attach_fiber(ctx.get_context_ptr(), fiber);

        for (wy_uword i = 0; i < frame_count; i++) {
            REQUIRE_EQ(wy_fiber_push_frame_f(fiber, exec_record_result), WY_ERR_NONE);
        }
        CHECK_EQ(wy_fiber_push_frame_f(fiber, exec_record_result), WY_ERR_STACK_OVERFLOW);
        CHECK_EQ(wy_fiber_frame_depth_f(fiber), frame_count);
    }

    TEST_CASE("exec_f runs queued continuations to completion") {
        g_result = 0; g_result_count = 0;
        test_fiber_fixture ctx;
        wy_fiber* fiber = ctx.get_fiber_ptr();

        REQUIRE_EQ(wy_fiber_push_continuation(fiber, exec_call_mul), WY_ERR_NONE);

        wy_state state = make_state(ctx);
        CHECK_EQ(wy_fiber_exec_f(fiber, &state), WY_ERR_NONE);

        CHECK_EQ(g_result_count, 1);
        CHECK_EQ(g_result, 8 * 32);
        CHECK_EQ(wy_fiber_frame_depth_f(fiber), 0);
    }

    TEST_CASE("exec_f tail call reuses the active frame") {
        g_result = 0; g_result_count = 0;
        test_fiber_fixture ctx;
        wy_fiber* fiber = ctx.get_fiber_ptr();

        REQUIRE_EQ(wy_fiber_push_continuation(fiber, exec_tail_call), WY_ERR_NONE);

        wy_state state = make_state(ctx);
        CHECK_EQ(wy_fiber_exec_f(fiber, &state), WY_ERR_NONE);

        CHECK_EQ(g_result_count, 1);
        CHECK_EQ(g_result, 7);
        CHECK_EQ(wy_fiber_frame_depth_f(fiber), 0);
    }
}
