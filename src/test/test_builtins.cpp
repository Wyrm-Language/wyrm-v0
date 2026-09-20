#include <doctest/doctest.h>

#include <wyrm.h>
#include <wyrm/builtins.h>
#include <wyrm/module.h>
#include <wyrm/native.h>
#include <wyrm/slot.h>
#include <wyrm/string.h>
#include <wyrm/value.h>
#include <wyrm/vm.h>

#include <string>

#include <test_common/test_fiber_fixture.h>

namespace {

//! Output sink: appends every write to a file-scope buffer, following the
//! g_result capture idiom used by test_fiber.cpp.
std::string g_output;

void capture_write(wy_context*, const char* bytes, wy_uword len, void*)
{
    g_output.append(bytes, len);
}

wy_native* find_native(wy_module* module, wy_context* context, const char* name)
{
    wy_symbol sym = WY_NULL;
    REQUIRE_EQ(wy_context_intern(context, name, wy_strlen_f(name), &sym), WY_ERR_NONE);
    wy_uword slot = wy_slot_dict_get(&module->exports, sym);
    REQUIRE_NE(slot, WY_SLOT_INVALID);
    REQUIRE_LT(slot, module->global_count);
    wy_value v = module->globals[slot];
    REQUIRE_EQ(v.type, WY_TYPE_TAG_NATIVE);
    return (wy_native*) v.data.gc_object;
}

} // namespace

TEST_SUITE("builtins")
{
    TEST_CASE("wy_builtins_new exports println, print, and nil") {
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();

        wy_module* module = WY_NULL;
        REQUIRE_EQ(wy_builtins_new(context, &module), WY_ERR_NONE);
        REQUIRE_NE(module, WY_NULL);
        CHECK_EQ(module->state, WY_MODULE_BUILTIN);

        wy_native* println = find_native(module, context, "println");
        CHECK_EQ(println->kind, WY_NATIVE_LEAF);
        wy_native* print = find_native(module, context, "print");
        CHECK_EQ(print->kind, WY_NATIVE_LEAF);

        wy_symbol nil_sym = WY_NULL;
        REQUIRE_EQ(wy_context_intern(context, "nil", 3, &nil_sym), WY_ERR_NONE);
        wy_uword nil_slot = wy_slot_dict_get(&module->exports, nil_sym);
        REQUIRE_NE(nil_slot, WY_SLOT_INVALID);
        CHECK_EQ(module->globals[nil_slot].type, WY_TYPE_TAG_NIL);
    }

    TEST_CASE("println formats each value kind and space-joins with a trailing newline") {
        g_output.clear();
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();
        context->io.write = capture_write;

        wy_module* module = WY_NULL;
        REQUIRE_EQ(wy_builtins_new(context, &module), WY_ERR_NONE);
        wy_native* println = find_native(module, context, "println");

        wy_string* hi = WY_NULL;
        REQUIRE_EQ(wy_string_strdup(context, "hi", &hi), WY_ERR_NONE);

        wy_value args[6] = {
            wy_value_word(-3),
            wy_value_uword(9),
            wy_value_float(3.4),
            wy_value_bool(true),
            wy_value_nil(),
            wy_value_object(WY_TYPE_TAG_STR, (wy_object*) hi),
        };
        wy_value out[1];
        REQUIRE_EQ(wy_vm_call_leaf_f(context, println, args, 6, out, 1), WY_ERR_NONE);
        CHECK_EQ(g_output, "-3 9 3.4 true nil hi\n");
    }

    TEST_CASE("float formatting is shortest-round-trip: 3.4 and 1.0") {
        g_output.clear();
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();
        context->io.write = capture_write;

        wy_module* module = WY_NULL;
        REQUIRE_EQ(wy_builtins_new(context, &module), WY_ERR_NONE);
        wy_native* println = find_native(module, context, "println");

        wy_value args[1] = { wy_value_float(3.4) };
        wy_value out[1];
        REQUIRE_EQ(wy_vm_call_leaf_f(context, println, args, 1, out, 1), WY_ERR_NONE);
        CHECK_EQ(g_output, "3.4\n");

        g_output.clear();
        args[0] = wy_value_float(1.0);
        REQUIRE_EQ(wy_vm_call_leaf_f(context, println, args, 1, out, 1), WY_ERR_NONE);
        CHECK_EQ(g_output, "1.0\n");
    }

    TEST_CASE("multiret.out's int modulo result prints without a decimal point") {
        g_output.clear();
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();
        context->io.write = capture_write;

        wy_module* module = WY_NULL;
        REQUIRE_EQ(wy_builtins_new(context, &module), WY_ERR_NONE);
        wy_native* println = find_native(module, context, "println");

        wy_value args[1] = { wy_value_word(2) };
        wy_value out[1];
        REQUIRE_EQ(wy_vm_call_leaf_f(context, println, args, 1, out, 1), WY_ERR_NONE);
        CHECK_EQ(g_output, "2\n");
    }

    TEST_CASE("print space-joins with no trailing newline, matching hello_3.out byte-for-byte") {
        g_output.clear();
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();
        context->io.write = capture_write;

        wy_module* module = WY_NULL;
        REQUIRE_EQ(wy_builtins_new(context, &module), WY_ERR_NONE);
        wy_native* print = find_native(module, context, "print");

        wy_string* prefix = WY_NULL;
        REQUIRE_EQ(wy_string_strdup(context, "Magic Number: ", &prefix), WY_ERR_NONE);
        wy_string* newline = WY_NULL;
        REQUIRE_EQ(wy_string_strdup(context, "\n", &newline), WY_ERR_NONE);

        wy_value args[3] = {
            wy_value_object(WY_TYPE_TAG_STR, (wy_object*) prefix),
            wy_value_word(3),
            wy_value_object(WY_TYPE_TAG_STR, (wy_object*) newline),
        };
        wy_value out[1];
        REQUIRE_EQ(wy_vm_call_leaf_f(context, print, args, 3, out, 1), WY_ERR_NONE);
        // test/bytecode/hello_3.out, byte for byte:
        CHECK_EQ(g_output, "Magic Number:  3 \n");
    }

    TEST_CASE("print with no args writes nothing") {
        g_output.clear();
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();
        context->io.write = capture_write;

        wy_module* module = WY_NULL;
        REQUIRE_EQ(wy_builtins_new(context, &module), WY_ERR_NONE);
        wy_native* print = find_native(module, context, "print");

        wy_value out[1];
        REQUIRE_EQ(wy_vm_call_leaf_f(context, print, WY_NULL, 0, out, 1), WY_ERR_NONE);
        CHECK_EQ(g_output, "");
    }

    TEST_CASE("a NULL io.write hook is a silent no-op") {
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();
        context->io.write = WY_NULL;

        wy_module* module = WY_NULL;
        REQUIRE_EQ(wy_builtins_new(context, &module), WY_ERR_NONE);
        wy_native* println = find_native(module, context, "println");

        wy_value args[1] = { wy_value_word(42) };
        wy_value out[1];
        CHECK_EQ(wy_vm_call_leaf_f(context, println, args, 1, out, 1), WY_ERR_NONE);
    }
}
