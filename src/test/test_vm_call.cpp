#include <doctest/doctest.h>

#include <wyrm.h>
#include <wyrm/native.h>
#include <wyrm/vm.h>
#include <test_common/test_fiber_fixture.h>

namespace {

wy_error leaf_sum(wy_context*, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    wy_word total = 0;
    for (wy_uword i = 0; i < argc; i++) { total += args[i].data.word; }
    if (nres > 0) { out[0] = wy_value_word(total); }
    for (wy_uword i = 1; i < nres; i++) { out[i] = wy_value_nil(); }
    return WY_ERR_NONE;
}

//! An exec native: writes two results via the reservation convention,
//! regardless of how many the caller actually reserved.
wy_exec_state exec_two_results(wy_context* context, wy_primitive)
{
    wy_value* a = wy_context_value_n(context, 0);
    REQUIRE_NE(a, WY_NULL);
    wy_context_set_result(context, 0, wy_value_word(a->data.word * 2));
    wy_context_set_result(context, 1, wy_value_word(a->data.word * 3));
    return WY_EXEC_DONE;
}

wy_value g_await_out[3];
wy_uword g_await_base_count = 0;
wy_uword g_await_nres = 0;
bool g_await_ran = false;

wy_exec_state exec_after_native(wy_context* context, wy_primitive)
{
    g_await_ran = true;
    REQUIRE_EQ(wy_vm_native_await_complete_f(context, g_await_base_count, g_await_out, g_await_nres), WY_ERR_NONE);
    return WY_EXEC_DONE;
}

} // namespace

TEST_SUITE("vm_call")
{
    TEST_CASE("a leaf native runs inline with no frame") {
        wy_native* native = WY_NULL;
        // Leaf natives are machine-lifetime; a plain heap allocator fixture
        // is unnecessary here since we only exercise wy_vm_call_leaf_f, but
        // wy_native itself is GC-tracked so it still needs a context.
        test_fiber_fixture ctx;
        REQUIRE_EQ(wy_native_leaf_new(ctx.get_context_ptr(), "sum", 0, 4, leaf_sum, &native), WY_ERR_NONE);

        wy_value args[3] = { wy_value_word(1), wy_value_word(2), wy_value_word(3) };
        wy_value out[1] = { wy_value_nil() };
        REQUIRE_EQ(wy_vm_call_leaf_f(ctx.get_context_ptr(), native, args, 3, out, 1), WY_ERR_NONE);
        CHECK_EQ(out[0].data.word, 6);
    }

    TEST_CASE("a leaf native rejects an out-of-range argument count") {
        wy_native* native = WY_NULL;
        test_fiber_fixture ctx;
        REQUIRE_EQ(wy_native_leaf_new(ctx.get_context_ptr(), "sum", 1, 2, leaf_sum, &native), WY_ERR_NONE);

        wy_value out[1] = { wy_value_nil() };
        CHECK_EQ(wy_vm_call_leaf_f(ctx.get_context_ptr(), native, WY_NULL, 0, out, 1), WY_ERR_ARITY);
    }

    TEST_CASE("an exec native's reserved results backfill the window, nres < count") {
        g_await_ran = false;
        wy_native* native = WY_NULL;
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();
        REQUIRE_EQ(wy_native_exec_new(context, "two", 1, 1, wy_exec_fn_create(exec_two_results, wy_primitive_null()), &native), WY_ERR_NONE);

        // One value already on the fiber ("caller's own"), matching
        // base_count in the await-complete contract.
        REQUIRE_EQ(wy_context_push(context, wy_value_word(99)), WY_ERR_NONE);
        g_await_base_count = wy_context_value_count(context);
        g_await_nres = 1;  // caller only wants 1 result, callee writes 2

        wy_value args[1] = { wy_value_word(5) };
        REQUIRE_EQ(wy_vm_call_exec_push_f(context, wy_exec_fn_create(exec_after_native, wy_primitive_null()),
                                           native, args, 1, g_await_nres), WY_ERR_NONE);
        CHECK_EQ(wy_context_exec(context), WY_ERR_NONE);

        REQUIRE(g_await_ran);
        CHECK_EQ(g_await_out[0].data.word, 10);  // 5*2, result 0 - result 1 (15) is truncated
        // The fiber is back to exactly its pre-call state (99 only).
        CHECK_EQ(wy_context_value_count(context), g_await_base_count);
    }

    TEST_CASE("an exec native's unwritten reserved results stay Unset when nres > written count") {
        g_await_ran = false;
        wy_native* native = WY_NULL;
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();
        REQUIRE_EQ(wy_native_exec_new(context, "two", 1, 1, wy_exec_fn_create(exec_two_results, wy_primitive_null()), &native), WY_ERR_NONE);

        g_await_base_count = wy_context_value_count(context);
        g_await_nres = 3;  // caller wants 3, callee only writes 2

        wy_value args[1] = { wy_value_word(4) };
        REQUIRE_EQ(wy_vm_call_exec_push_f(context, wy_exec_fn_create(exec_after_native, wy_primitive_null()),
                                           native, args, 1, g_await_nres), WY_ERR_NONE);
        CHECK_EQ(wy_context_exec(context), WY_ERR_NONE);

        REQUIRE(g_await_ran);
        CHECK_EQ(g_await_out[0].data.word, 8);    // 4*2
        CHECK_EQ(g_await_out[1].data.word, 12);   // 4*3
        // Reserved but never written by exec_two_results - Unset, not nil:
        // this bridge only reads back what's physically reserved, it does
        // not know how many results the callee "meant" to produce the way
        // a bytecode RETURN's explicit count does.
        CHECK(wy_value_is_unset(g_await_out[2]));
    }

    TEST_CASE("an exec native rejects an out-of-range argument count") {
        wy_native* native = WY_NULL;
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();
        REQUIRE_EQ(wy_native_exec_new(context, "two", 2, 2, wy_exec_fn_create(exec_two_results, wy_primitive_null()), &native), WY_ERR_NONE);

        wy_value args[1] = { wy_value_word(1) };
        CHECK_EQ(wy_vm_call_exec_push_f(context, wy_exec_fn_create(exec_after_native, wy_primitive_null()),
                                         native, args, 1, 1), WY_ERR_ARITY);
    }

    TEST_CASE("WY_EXEC_FAULT surfaces as WY_ERR_FAULT with fiber->fault readable") {
        test_fiber_fixture ctx;
        wy_fiber* fiber = ctx.get_fiber_ptr();

        struct local {
            static wy_exec_state raise(wy_context* context, wy_primitive) {
                context->current_fiber->fault = wy_value_word(0xBAD);
                return WY_EXEC_FAULT;
            }
        };
        REQUIRE_EQ(wy_fiber_push_continuation_c_call(fiber, &local::raise, 0), WY_ERR_NONE);
        CHECK_EQ(wy_context_exec(ctx.get_context_ptr()), WY_ERR_FAULT);
        CHECK_EQ(fiber->fault.data.word, 0xBAD);
    }
}
