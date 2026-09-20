#include <doctest/doctest.h>

#include <wyrm.h>
#include <wyrm/module.h>
#include <wyrm/value.h>
#include <test_common/test_context_fixture.h>
#include <test_common/test_fiber_fixture.h>

namespace {

//! Result recorded by exec_record_result, so the exec loop can be observed
wy_uword g_result = 0;
wy_uword g_result_count = 0;

wy_exec_state exec_record_result(wy_context* context, wy_primitive)
{
    g_result_count = wy_context_value_count(context);
    wy_value* a = wy_context_value_n(context, 0);
    g_result = (a == WY_NULL) ? 0 : (wy_uword) a->data.word;
    return WY_EXEC_DONE;
}

wy_exec_state exec_mul_int(wy_context* context, wy_primitive)
{
    wy_value* a = wy_context_value_n(context, 0);
    wy_value* b = wy_context_value_n(context, 1);
    REQUIRE_NE(a, WY_NULL);
    REQUIRE_NE(b, WY_NULL);

    wy_context_set_result(context, 0, wy_value_word(a->data.word * b->data.word));
    return WY_EXEC_DONE;
}

wy_exec_state exec_call_mul(wy_context* context, wy_primitive)
{
    wy_value args[2] = { wy_value_word(8), wy_value_word(32) };
    REQUIRE_EQ(wy_context_call_continue(context, exec_record_result, exec_mul_int, args, 2, 1), WY_ERR_NONE);
    return WY_EXEC_CONTINUE;
}

//! Returns three results regardless of how many the caller reserved
wy_exec_state exec_three_results(wy_context* context, wy_primitive)
{
    wy_context_set_result(context, 0, wy_value_word(11));
    wy_context_set_result(context, 1, wy_value_word(22));
    wy_context_set_result(context, 2, wy_value_word(33));
    return WY_EXEC_DONE;
}

wy_exec_state exec_call_three(wy_context* context, wy_primitive)
{
    REQUIRE_EQ(wy_context_call_continue(context, exec_record_result, exec_three_results, WY_NULL, 0, 1), WY_ERR_NONE);
    return WY_EXEC_CONTINUE;
}

wy_exec_state exec_double(wy_context* context, wy_primitive)
{
    wy_value* a = wy_context_value_n(context, 0);
    REQUIRE_NE(a, WY_NULL);
    wy_context_set_result(context, 0, wy_value_word(a->data.word * 2));
    return WY_EXEC_DONE;
}

//! Replaces its own arguments and tail calls, inheriting the reserved results
wy_exec_state exec_tail_to_double(wy_context* context, wy_primitive)
{
    REQUIRE_EQ(wy_context_push(context, wy_value_word(7)), WY_ERR_NONE);
    REQUIRE_EQ(wy_context_tail_call(context, exec_double, 1), WY_ERR_NONE);
    return WY_EXEC_TAIL_CALL;
}

wy_exec_state exec_call_tail(wy_context* context, wy_primitive)
{
    REQUIRE_EQ(wy_context_call_continue(context, exec_record_result, exec_tail_to_double, WY_NULL, 0, 1), WY_ERR_NONE);
    return WY_EXEC_CONTINUE;
}

//! Records the c_data it was handed, so threading can be observed
wy_primitive g_seen_data = wy_primitive_null();

struct exec_payload
{
    wy_uword factor;
};

wy_exec_state exec_record_data(wy_context* context, wy_primitive c_data)
{
    WY_UNUSED(context);
    g_seen_data = c_data;
    return WY_EXEC_DONE;
}

//! Scales its argument by the factor carried in c_data
wy_exec_state exec_scale_by_data(wy_context* context, wy_primitive c_data)
{
    exec_payload* payload = WY_PRIMITIVE_PTR(exec_payload, c_data);
    REQUIRE_NE(payload, WY_NULL);

    wy_value* a = wy_context_value_n(context, 0);
    REQUIRE_NE(a, WY_NULL);

    wy_context_set_result(context, 0, wy_value_word((wy_word) (a->data.uword * payload->factor)));
    return WY_EXEC_DONE;
}

//! Scales its argument by a factor carried inline in the primitive
wy_exec_state exec_scale_by_word(wy_context* context, wy_primitive c_data)
{
    wy_value* a = wy_context_value_n(context, 0);
    REQUIRE_NE(a, WY_NULL);

    wy_context_set_result(context, 0, wy_value_word((wy_word) (a->data.uword * c_data.uword)));
    return WY_EXEC_DONE;
}

}  // namespace

TEST_SUITE("exec_fn")
{
    TEST_CASE("an empty callable cannot be scheduled") {
        test_fiber_fixture ctx;
        wy_fiber* fiber = ctx.get_fiber_ptr();

        CHECK_EQ(wy_fiber_push_continuation(fiber, wy_exec_fn_create_empty(), 0), WY_ERR_INVAL);
        CHECK_EQ(wy_fiber_push_continuation_c_call(fiber, WY_NULL, 0), WY_ERR_INVAL);
        CHECK_EQ(wy_fiber_tail_call_f(fiber, wy_exec_fn_create_empty(), 0), WY_ERR_INVAL);
        CHECK_EQ(wy_context_set_pending(ctx.get_context_ptr(), wy_exec_fn_create_empty()), WY_ERR_INVAL);
    }

    TEST_CASE("a callable keeps both its function and its data") {
        exec_payload payload{ 3 };
        wy_exec_fn fn = wy_exec_fn_create(exec_scale_by_data, wy_primitive_ptr(&payload));

        // The two fields coexist; neither write clobbers the other
        CHECK_EQ(fn.fn, exec_scale_by_data);
        CHECK_EQ(fn.c_data.ptr, &payload);
        CHECK_FALSE(wy_exec_fn_is_empty(&fn));

        // A callable with data but no function is still empty
        wy_exec_fn no_fn = wy_exec_fn_create(WY_NULL, wy_primitive_ptr(&payload));
        CHECK(wy_exec_fn_is_empty(&no_fn));
    }

    TEST_CASE("c_data reaches the callable through the exec loop") {
        g_seen_data = wy_primitive_null();
        exec_payload payload{ 7 };

        test_fiber_fixture ctx;
        wy_fiber* fiber = ctx.get_fiber_ptr();

        REQUIRE_EQ(wy_fiber_push_continuation(fiber, wy_exec_fn_create(exec_record_data, wy_primitive_ptr(&payload)), 0), WY_ERR_NONE);
        CHECK_EQ(wy_context_exec(ctx.get_context_ptr()), WY_ERR_NONE);

        CHECK_EQ(g_seen_data.ptr, &payload);
    }

    TEST_CASE("a primitive c_data survives whole, not just its pointer") {
        g_seen_data = wy_primitive_null();

        test_fiber_fixture ctx;
        wy_fiber* fiber = ctx.get_fiber_ptr();

        // A word payload needs no backing storage at all
        REQUIRE_EQ(wy_fiber_push_continuation(fiber, wy_exec_fn_create(exec_record_data, wy_primitive_uword(0xABCDEF)), 0), WY_ERR_NONE);
        CHECK_EQ(wy_context_exec(ctx.get_context_ptr()), WY_ERR_NONE);

        CHECK_EQ(g_seen_data.uword, 0xABCDEF);
    }

    TEST_CASE("a C-only callable is handed a null c_data") {
        g_seen_data = wy_primitive_uword(0xDEAD);

        test_fiber_fixture ctx;
        wy_fiber* fiber = ctx.get_fiber_ptr();

        REQUIRE_EQ(wy_fiber_push_continuation_c_call(fiber, exec_record_data, 0), WY_ERR_NONE);
        CHECK_EQ(wy_context_exec(ctx.get_context_ptr()), WY_ERR_NONE);

        CHECK_EQ(g_seen_data.ptr, WY_NULL);
    }

    TEST_CASE("a called function uses its own data, not the continuation's") {
        exec_payload callee_payload{ 5 };
        g_result = 0; g_result_count = 0;

        test_fiber_fixture ctx;
        wy_fiber* fiber = ctx.get_fiber_ptr();

        // exec_scale_by_data reads its factor from c_data, so a wrong
        // pointer here would scale by garbage rather than by 5
        wy_value args[1] = { wy_value_word(9) };
        REQUIRE_EQ(wy_fiber_exec_continue_f(fiber,
                                            wy_exec_fn_create(exec_record_result, wy_primitive_null()),
                                            wy_exec_fn_create(exec_scale_by_data, wy_primitive_ptr(&callee_payload)),
                                            args, 1, 1), WY_ERR_NONE);
        CHECK_EQ(wy_context_exec(ctx.get_context_ptr()), WY_ERR_NONE);

        CHECK_EQ(g_result_count, 1);
        CHECK_EQ(g_result, 45);
    }

    TEST_CASE("an inline word payload reaches a called function") {
        g_result = 0; g_result_count = 0;

        test_fiber_fixture ctx;
        wy_fiber* fiber = ctx.get_fiber_ptr();

        wy_value args[1] = { wy_value_word(6) };
        REQUIRE_EQ(wy_fiber_exec_continue_f(fiber,
                                            wy_exec_fn_create(exec_record_result, wy_primitive_null()),
                                            wy_exec_fn_create(exec_scale_by_word, wy_primitive_uword(4)),
                                            args, 1, 1), WY_ERR_NONE);
        CHECK_EQ(wy_context_exec(ctx.get_context_ptr()), WY_ERR_NONE);

        CHECK_EQ(g_result_count, 1);
        CHECK_EQ(g_result, 24);
    }
}

TEST_SUITE("fiber")
{
    TEST_CASE("push_frame_f reserves results below the new base") {
        test_fiber_fixture ctx;
        wy_fiber* fiber = ctx.get_fiber_ptr();

        // Caller value, then a call reserving two results
        REQUIRE_EQ(wy_fiber_push_value_f(fiber, wy_value_word(99)), WY_ERR_NONE);
        REQUIRE_EQ(wy_fiber_push_frame_c_call_f(fiber, exec_record_result, 2), WY_ERR_NONE);

        // The reserved slots sit below base, so the frame starts empty
        CHECK_EQ(wy_fiber_value_count_f(fiber), 0);
        CHECK_EQ(wy_fiber_result_count_f(fiber), 2);

        // Unwritten slots read back as unset
        CHECK_EQ(wy_fiber_result_n(fiber, 0)->type, wy_value_unset().type);
        CHECK_EQ(wy_fiber_result_n(fiber, 1)->type, wy_value_unset().type);
        CHECK_EQ(wy_fiber_result_n(fiber, 2), WY_NULL);
    }

    TEST_CASE("results land in the reserved slots, in reverse order") {
        test_fiber_fixture ctx;
        wy_fiber* fiber = ctx.get_fiber_ptr();

        REQUIRE_EQ(wy_fiber_push_value_f(fiber, wy_value_word(99)), WY_ERR_NONE);
        REQUIRE_EQ(wy_fiber_push_frame_c_call_f(fiber, exec_record_result, 2), WY_ERR_NONE);

        // Arguments and scratch above base, results written below it
        REQUIRE_EQ(wy_fiber_push_value_f(fiber, wy_value_word(1)), WY_ERR_NONE);
        REQUIRE_EQ(wy_fiber_push_value_f(fiber, wy_value_word(77)), WY_ERR_NONE);
        REQUIRE(wy_fiber_set_result_f(fiber, 0, wy_value_word(55)));
        REQUIRE(wy_fiber_set_result_f(fiber, 1, wy_value_word(66)));

        // Result 0 is base[-1], result 1 is base[-2]
        CHECK_EQ(wy_fiber_result_n(fiber, 0)->data.uword, 55);
        CHECK_EQ(wy_fiber_result_n(fiber, 1)->data.uword, 66);

        wy_exec_fn out_fn = wy_exec_fn_create_empty();
        CHECK_EQ(wy_fiber_pop_continuation_f(fiber, &out_fn), WY_ERR_NONE);
        CHECK_EQ(out_fn.fn, exec_record_result);

        // Caller sees its own value plus the results, lowest slot first
        CHECK_EQ(wy_fiber_value_count_f(fiber), 3);
        CHECK_EQ(wy_fiber_value_n(fiber, 0)->data.uword, 99);
        CHECK_EQ(wy_fiber_value_n(fiber, 1)->data.uword, 66);
        CHECK_EQ(wy_fiber_value_n(fiber, 2)->data.uword, 55);

        CHECK_EQ(wy_fiber_pop_continuation_f(fiber, &out_fn), WY_ERR_EMPTY);
    }

    TEST_CASE("unreserved result slots are refused") {
        test_fiber_fixture ctx;
        wy_fiber* fiber = ctx.get_fiber_ptr();

        REQUIRE_EQ(wy_fiber_push_frame_c_call_f(fiber, exec_record_result, 1), WY_ERR_NONE);

        // Only slot 1 exists; the rest are refused rather than written
        REQUIRE_NE(wy_fiber_result_n(fiber, 0), WY_NULL);
        CHECK_EQ(wy_fiber_result_n(fiber, 1), WY_NULL);
        CHECK_EQ(wy_fiber_result_n(fiber, 2), WY_NULL);

        // set_result reports which of those it stored
        CHECK(wy_fiber_set_result_f(fiber, 0, wy_value_word(1)));
        CHECK_FALSE(wy_fiber_set_result_f(fiber, 1, wy_value_word(2)));
        CHECK_EQ(wy_fiber_result_n(fiber, 0)->data.uword, 1);

        wy_exec_fn out_fn = wy_exec_fn_create_empty();
        REQUIRE_EQ(wy_fiber_pop_continuation_f(fiber, &out_fn), WY_ERR_NONE);
        CHECK_EQ(wy_fiber_value_count_f(fiber), 1);
        CHECK_EQ(wy_fiber_value_n(fiber, 0)->data.uword, 1);
    }

    TEST_CASE("a call reserving no results exposes no slots") {
        test_fiber_fixture ctx;
        wy_fiber* fiber = ctx.get_fiber_ptr();

        REQUIRE_EQ(wy_fiber_push_frame_c_call_f(fiber, exec_record_result, 0), WY_ERR_NONE);
        CHECK_EQ(wy_fiber_result_count_f(fiber), 0);
        CHECK_EQ(wy_fiber_result_n(fiber, 0), WY_NULL);
        CHECK_FALSE(wy_fiber_set_result_f(fiber, 0, wy_value_word(42)));

        wy_exec_fn out_fn = wy_exec_fn_create_empty();
        REQUIRE_EQ(wy_fiber_pop_continuation_f(fiber, &out_fn), WY_ERR_NONE);
        CHECK_EQ(wy_fiber_value_count_f(fiber), 0);
    }

    TEST_CASE("collection walks a frame holding unwritten result slots") {
        test_fiber_fixture ctx;
        wy_fiber* fiber = ctx.get_fiber_ptr();

        // Unset carries an object tag, so an unguarded iterator would hand
        // the collector a null child here.
        REQUIRE_EQ(wy_fiber_push_frame_c_call_f(fiber, exec_record_result, 3), WY_ERR_NONE);
        REQUIRE(wy_fiber_set_result_f(fiber, 0, wy_value_word(5)));
        REQUIRE_EQ(wy_fiber_push_value_f(fiber, wy_value_word(6)), WY_ERR_NONE);

        ctx.run_gc();

        CHECK_EQ(wy_fiber_result_n(fiber, 0)->data.uword, 5);
        CHECK_EQ(wy_fiber_result_n(fiber, 1)->type, wy_value_unset().type);
        CHECK_EQ(wy_fiber_value_count_f(fiber), 1);
    }

    TEST_CASE("frame stack overflow is reported, not overrun") {
        test_context_fixture ctx;
        const wy_uword frame_count = 4;
        wy_fiber* fiber = wy_fiber_create(ctx.get_context_ptr(), 64, frame_count);
        REQUIRE_NE(fiber, WY_NULL);
        wy_context_attach_fiber(ctx.get_context_ptr(), fiber);

        for (wy_uword i = 0; i < frame_count; i++) {
            REQUIRE_EQ(wy_fiber_push_frame_c_call_f(fiber, exec_record_result, 1), WY_ERR_NONE);
        }
        CHECK_EQ(wy_fiber_push_frame_c_call_f(fiber, exec_record_result, 1), WY_ERR_STACK_OVERFLOW);
        CHECK_EQ(wy_fiber_frame_depth_f(fiber), frame_count);
    }

    TEST_CASE("reserving more results than the stack holds fails cleanly") {
        test_context_fixture ctx;
        wy_fiber* fiber = wy_fiber_create(ctx.get_context_ptr(), 4, 8);
        REQUIRE_NE(fiber, WY_NULL);
        wy_context_attach_fiber(ctx.get_context_ptr(), fiber);

        CHECK_EQ(wy_fiber_push_frame_c_call_f(fiber, exec_record_result, 5), WY_ERR_STACK_OVERFLOW);
        CHECK_EQ(wy_fiber_frame_depth_f(fiber), 0);
        CHECK_EQ(wy_fiber_value_count_f(fiber), 0);
    }

    TEST_CASE("exec_f runs queued continuations to completion") {
        g_result = 0; g_result_count = 0;
        test_fiber_fixture ctx;
        wy_fiber* fiber = ctx.get_fiber_ptr();

        REQUIRE_EQ(wy_fiber_push_continuation_c_call(fiber, exec_call_mul, 0), WY_ERR_NONE);
        CHECK_EQ(wy_context_exec(ctx.get_context_ptr()), WY_ERR_NONE);

        CHECK_EQ(g_result_count, 1);
        CHECK_EQ(g_result, 8 * 32);
        CHECK_EQ(wy_fiber_frame_depth_f(fiber), 0);
    }

    TEST_CASE("exec_f truncates a callee that overproduces") {
        g_result = 0; g_result_count = 0;
        test_fiber_fixture ctx;
        wy_fiber* fiber = ctx.get_fiber_ptr();

        REQUIRE_EQ(wy_fiber_push_continuation_c_call(fiber, exec_call_three, 0), WY_ERR_NONE);
        CHECK_EQ(wy_context_exec(ctx.get_context_ptr()), WY_ERR_NONE);

        // One slot was reserved, so only the first result reaches the caller
        CHECK_EQ(g_result_count, 1);
        CHECK_EQ(g_result, 11);
        CHECK_EQ(wy_fiber_frame_depth_f(fiber), 0);
    }

    TEST_CASE("a tail call returns through the frame it reused") {
        g_result = 0; g_result_count = 0;
        test_fiber_fixture ctx;
        wy_fiber* fiber = ctx.get_fiber_ptr();

        REQUIRE_EQ(wy_fiber_push_continuation_c_call(fiber, exec_call_tail, 0), WY_ERR_NONE);
        CHECK_EQ(wy_context_exec(ctx.get_context_ptr()), WY_ERR_NONE);

        // exec_double's result reaches exec_call_tail's continuation
        CHECK_EQ(g_result_count, 1);
        CHECK_EQ(g_result, 14);
        CHECK_EQ(wy_fiber_frame_depth_f(fiber), 0);
    }
}
