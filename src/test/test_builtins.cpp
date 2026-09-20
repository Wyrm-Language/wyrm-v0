#include <doctest/doctest.h>

#include <wyrm.h>
#include <wyrm/builtins.h>
#include <wyrm/coroutine.h>
#include <wyrm/error.h>
#include <wyrm/function.h>
#include <wyrm/module.h>
#include <wyrm/native.h>
#include <wyrm/opcode.h>
#include <wyrm/slot.h>
#include <wyrm/string.h>
#include <wyrm/value.h>
#include <wyrm/vm.h>

#include <cstring>
#include <string>
#include <vector>

#include <test_common/test_fiber_fixture.h>

namespace {

// Hand-packed bytecode helpers, duplicated from test_coroutine.cpp's
// anonymous namespace (same convention as test_link.cpp/test_wvm.cpp/
// test_dispatch.cpp - no compiler exists in this repo to generate these).
constexpr wy_u32 enc1(wy_u8 op, wy_u8 f, wy_u16 a0)
{
    return (wy_u32(a0) << 16) | (wy_u32(f) << 8) | wy_u32(op);
}
constexpr wy_u32 enc2a(wy_u8 op, wy_u8 f, wy_u16 a0) { return enc1(op, f, a0); }
constexpr wy_u32 enc2b(wy_u16 a1, wy_u16 a2) { return (wy_u32(a1) << 16) | wy_u32(a2); }

wy_function_proto proto_at(wy_u32 offset, wy_u16 nlocals, wy_u8 flags = 0)
{
    wy_function_proto proto = {};
    proto.code_offset = offset;
    proto.nlocals = nlocals;
    proto.flags = flags;
    return proto;
}

wy_module* make_code_module(wy_context* ctx, const wy_u32* code, wy_uword code_len,
    const std::vector<wy_function_proto>& protos, wy_uword nglobals = 0,
    const wy_value* statics = nullptr, wy_uword nstatics = 0)
{
    wy_module* module = wy_module_new_f(ctx);
    module->code = code;
    module->code_len = code_len;

    if (nglobals > 0) {
        module->global_count = nglobals;
        module->globals = (wy_value*) wy_context_gc_alloc(ctx, sizeof(wy_value) * nglobals);
        module->fill_layer = (wy_u8*) wy_context_gc_alloc(ctx, sizeof(wy_u8) * nglobals);
        module->fill_source = (wy_symbol*) wy_context_gc_alloc(ctx, sizeof(wy_symbol) * nglobals);
        for (wy_uword i = 0; i < nglobals; i++) {
            module->globals[i] = wy_value_unset();
            module->fill_layer[i] = 0;
            module->fill_source[i] = WY_NULL;
        }
    }
    if (statics != nullptr && nstatics > 0) {
        auto* heap_statics = (wy_value*) wy_context_gc_alloc(ctx, sizeof(wy_value) * nstatics);
        std::copy(statics, statics + nstatics, heap_statics);
        module->statics = heap_statics;
        module->static_count = nstatics;
    }
    if (!protos.empty()) {
        module->function_count = protos.size();
        auto* heap_protos = (wy_function_proto*) wy_context_gc_alloc(ctx, sizeof(wy_function_proto) * protos.size());
        std::copy(protos.begin(), protos.end(), heap_protos);
        module->functions = heap_protos;
    }
    return module;
}

wy_value builtin_value(wy_context* ctx, wy_module* builtins, const char* name)
{
    wy_symbol sym = WY_NULL;
    (void) wy_context_intern(ctx, name, std::strlen(name), &sym);
    wy_uword slot = wy_slot_dict_get(&builtins->exports, sym);
    REQUIRE_NE(slot, WY_SLOT_INVALID);
    return builtins->globals[slot];
}

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

TEST_SUITE("builtins") {
    TEST_CASE("str converts scalars without writing to the output hook") {
        test_fiber_fixture fix;
        auto* ctx = fix.get_context_ptr();
        ctx->io.write = capture_write;
        g_output.clear();
        wy_module* module;
        REQUIRE_EQ(wy_builtins_new(ctx, &module), WY_ERR_NONE);
        auto* str = find_native(module, ctx, "str");
        wy_string* original;
        REQUIRE_EQ(wy_string_strdup(ctx, "hello", &original), WY_ERR_NONE);
        wy_value values[] = {wy_value_word(-3), wy_value_uword(9), wy_value_float(3.4),
            wy_value_bool(true), wy_value_nil(), wy_value_object(WY_TYPE_TAG_STR, reinterpret_cast<wy_object*>(original))};
        const char* expected[] = {"-3", "9", "3.4", "true", "nil", "hello"};
        for (unsigned i = 0; i < 6; i++) {
            wy_value out;
            REQUIRE_EQ(wy_vm_call_leaf_f(ctx, str, &values[i], 1, &out, 1), WY_ERR_NONE);
            REQUIRE_EQ(out.type, WY_TYPE_TAG_STR);
            CHECK_EQ(std::string(out.data.str->str, out.data.str->len), expected[i]);
            if (i == 5) { CHECK_EQ(out.data.str, original); }
        }
        CHECK(g_output.empty());
    }

    TEST_CASE("range(begin, end) is a bare-name coroutine needing no import (epic 5 M4)") {
        // Hand-packed the same way test_coroutine.cpp does (no compiler in
        // this repo): L0 <- call range(0, 3); then next(L0) three times
        // (0, 1, 2), then a fourth time observing StopIteration.
        test_fiber_fixture fix;
        auto* ctx = fix.get_context_ptr();
        wy_module* builtins = WY_NULL;
        REQUIRE_EQ(wy_builtins_new(ctx, &builtins), WY_ERR_NONE);
        ctx->builtins = builtins;

        wy_value range_fn = builtin_value(ctx, builtins, "range");
        REQUIRE_EQ(range_fn.type, WY_TYPE_TAG_FUNCTION);
        wy_value next_native = builtin_value(ctx, builtins, "next");

        const wy_u32 code[] = {
            enc1(WY_OP_LCONST, 0, 0),                    // L0 <- range fn (static 0)
            enc1(WY_OP_I8, 0, 1),                         // L1 <- 0 (begin)
            enc1(WY_OP_I8, 3, 2),                         // L2 <- 3 (end)
            enc2a(WY_OP_CALL, 2, 0), enc2b(1, 0),         // L0 <- range(L1, L2), argc=2 nres=1
            enc1(WY_OP_LCONST, 10, 1),                    // L10 <- next native (static 1)
            enc1(WY_OP_MOVE, 0, 11),                      // L11 <- L0 (coroutine)
            enc2a(WY_OP_CALL, 1, 10), enc2b(1, 0),        // L10 <- next(L11)
            enc1(WY_OP_MOVE, 10, 3),                      // L3 <- result 1
            enc1(WY_OP_LCONST, 10, 1), enc1(WY_OP_MOVE, 0, 11),
            enc2a(WY_OP_CALL, 1, 10), enc2b(1, 0),
            enc1(WY_OP_MOVE, 10, 4),                      // L4 <- result 2
            enc1(WY_OP_LCONST, 10, 1), enc1(WY_OP_MOVE, 0, 11),
            enc2a(WY_OP_CALL, 1, 10), enc2b(1, 0),
            enc1(WY_OP_MOVE, 10, 5),                      // L5 <- result 3
            enc1(WY_OP_LCONST, 10, 1), enc1(WY_OP_MOVE, 0, 11),
            enc2a(WY_OP_CALL, 1, 10), enc2b(1, 0),
            enc1(WY_OP_MOVE, 10, 6),                      // L6 <- StopIteration
            enc1(WY_OP_RETURN, 4, 3),                     // return L3..L6
        };
        wy_value statics[] = {range_fn, next_native};
        std::vector<wy_function_proto> protos = {proto_at(0, 12, 0)};
        wy_module* module = make_code_module(ctx, code, std::size(code), protos, 0, statics, std::size(statics));
        REQUIRE_EQ(wy_context_module_register(ctx, module, nullptr), WY_ERR_NONE);

        wy_function* fn0 = WY_NULL;
        REQUIRE_EQ(wy_function_new(ctx, module, &module->functions[0], WY_NULL, 0, &fn0), WY_ERR_NONE);

        wy_value out[4];
        wy_error err = wy_vm_call_sync(ctx, wy_value_object(WY_TYPE_TAG_FUNCTION, (wy_object*) fn0),
            WY_NULL, 0, out, 4);
        REQUIRE_EQ(err, WY_ERR_NONE);

        CHECK_EQ(out[0].data.word, 0);
        CHECK_EQ(out[1].data.word, 1);
        CHECK_EQ(out[2].data.word, 2);
        REQUIRE_EQ(out[3].type, WY_TYPE_TAG_ERROR);
        CHECK_EQ(((wy_error_obj*) out[3].data.gc_object)->cls, ctx->stop_iteration_class);
    }
}
