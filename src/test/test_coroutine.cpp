#include <doctest/doctest.h>

#include <cstring>
#include <vector>

#include <wyrm.h>
#include <wyrm/builtins.h>
#include <wyrm/coroutine.h>
#include <wyrm/error.h>
#include <wyrm/function.h>
#include <wyrm/module.h>
#include <wyrm/opcode.h>
#include <wyrm/string.h>
#include <wyrm/vm.h>
#include <test_common/test_fiber_fixture.h>

/*
 * Epic 5/M3 (design_c_vm.md §3): coroutines get their own fiber; `next`/
 * `send` are exec natives that switch ctx->current_fiber; `yield`/
 * `yield_from` suspend/delegate. These tests hand-pack bytecode the same
 * way test_wvm.cpp/test_link.cpp do (no compiler exists yet in this repo).
 */

namespace {

constexpr wy_u32 enc1(wy_u8 op, wy_u8 f, wy_u16 a0)
{
    return (wy_u32(a0) << 16) | (wy_u32(f) << 8) | wy_u32(op);
}
constexpr wy_u32 enc2a(wy_u8 op, wy_u8 f, wy_u16 a0) { return enc1(op, f, a0); }
constexpr wy_u32 enc2b(wy_u16 a1, wy_u16 a2) { return (wy_u32(a1) << 16) | wy_u32(a2); }

wy_symbol symbol(wy_context* ctx, const char* text)
{
    wy_symbol out;
    REQUIRE_EQ(wy_context_intern(ctx, text, std::strlen(text), &out), WY_ERR_NONE);
    return out;
}

void name_slot(wy_context* ctx, wy_slot_dict* dict, const char* name, wy_uword slot)
{
    if (dict->capacity == 0) { REQUIRE_EQ(wy_slot_dict_expand_f(dict, ctx->parent->allocator, 32), WY_ERR_NONE); }
    REQUIRE_EQ(wy_slot_dict_add_entry(dict, symbol(ctx, name), slot), WY_ERR_NONE);
}

/** A wy_module owning `code` plus `protos` and `nglobals` Unset globals, all
 * heap-copied since the module's finalizer unconditionally frees every
 * array it doesn't find null. */
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

wy_function_proto proto_at(wy_u32 offset, wy_u16 nlocals, wy_u8 flags = 0)
{
    wy_function_proto proto = {};
    proto.code_offset = offset;
    proto.nlocals = nlocals;
    proto.flags = flags;
    return proto;
}

/** Look up an exec/leaf native by name in the builtins module's exports. */
wy_value builtin_value(wy_context* ctx, wy_module* builtins, const char* name)
{
    wy_symbol sym = symbol(ctx, name);
    wy_uword slot = wy_slot_dict_get(&builtins->exports, sym);
    REQUIRE_NE(slot, WY_SLOT_INVALID);
    return builtins->globals[slot];
}

std::string fault_message(wy_fiber* fiber)
{
    if (fiber->fault.type != WY_TYPE_TAG_ERROR || fiber->fault.data.gc_object == WY_NULL) { return {}; }
    auto* err = (wy_error_obj*) fiber->fault.data.gc_object;
    return std::string(err->what ? err->what->str : "", err->what ? err->what->len : 0);
}

bool is_stop_iteration(wy_context* ctx, wy_value v)
{
    if (!wy_value_is_error(v) || v.data.gc_object == WY_NULL) { return false; }
    auto* err = (wy_error_obj*) v.data.gc_object;
    return err->cls == ctx->stop_iteration_class;
}

} // namespace

TEST_SUITE("coroutine") {
    TEST_CASE("next twice yields two values, then StopIteration idempotently") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        wy_module* builtins = WY_NULL;
        REQUIRE_EQ(wy_builtins_new(ctx, &builtins), WY_ERR_NONE);
        ctx->builtins = builtins;

        const wy_u32 fn0_code[] = {
            enc2a(WY_OP_CLOSURE, 0, 0), enc2b(1, 0),    // L0 <- fn1 (co body)
            enc2a(WY_OP_CALL, 0, 0), enc2b(1, 0),       // L0 <- construct coroutine (argc=0, nres=1)
            enc1(WY_OP_LCONST, 1, 0),                   // L1 <- next native (static 0)
            // call 1
            enc1(WY_OP_MOVE, 1, 10), enc1(WY_OP_MOVE, 0, 11),
            enc2a(WY_OP_CALL, 1, 10), enc2b(1, 0),      // L10 <- next(L0)
            enc1(WY_OP_MOVE, 10, 2),                    // L2 <- result 1
            // call 2
            enc1(WY_OP_MOVE, 1, 10), enc1(WY_OP_MOVE, 0, 11),
            enc2a(WY_OP_CALL, 1, 10), enc2b(1, 0),
            enc1(WY_OP_MOVE, 10, 3),                    // L3 <- result 2
            // call 3 (exhausted)
            enc1(WY_OP_MOVE, 1, 10), enc1(WY_OP_MOVE, 0, 11),
            enc2a(WY_OP_CALL, 1, 10), enc2b(1, 0),
            enc1(WY_OP_MOVE, 10, 4),                    // L4 <- StopIteration
            // call 4 (still exhausted, idempotent)
            enc1(WY_OP_MOVE, 1, 10), enc1(WY_OP_MOVE, 0, 11),
            enc2a(WY_OP_CALL, 1, 10), enc2b(1, 0),
            enc1(WY_OP_MOVE, 10, 5),                    // L5 <- StopIteration
            enc1(WY_OP_RETURN, 4, 2),                   // return L2..L5
        };
        // fn1 body (coroutine): yield 10, yield 20, return.
        const wy_u32 fn1_code[] = {
            enc1(WY_OP_I8, 10, 0),
            enc2a(WY_OP_YIELD, 1, 0), enc2b(0, 0),      // yield L0 (=10)
            enc1(WY_OP_I8, 20, 0),
            enc2a(WY_OP_YIELD, 1, 0), enc2b(0, 0),      // yield L0 (=20)
            enc1(WY_OP_RETURN, 0, 0),
        };
        std::vector<wy_u32> code(std::begin(fn0_code), std::end(fn0_code));
        const wy_uword fn1_offset = code.size();
        code.insert(code.end(), std::begin(fn1_code), std::end(fn1_code));

        wy_value next_native = builtin_value(ctx, builtins, "next");
        std::vector<wy_function_proto> protos = {
            proto_at(0, 12, 0),
            proto_at((wy_u32) fn1_offset, 1, WY_FN_COROUTINE),
        };
        wy_module* module = make_code_module(ctx, code.data(), code.size(), protos, 0, &next_native, 1);
        REQUIRE_EQ(wy_context_module_register(ctx, module, nullptr), WY_ERR_NONE);

        wy_function* fn0 = WY_NULL;
        REQUIRE_EQ(wy_function_new(ctx, module, &module->functions[0], WY_NULL, 0, &fn0), WY_ERR_NONE);

        wy_value out[4];
        wy_error err = wy_vm_call_sync(ctx, wy_value_object(WY_TYPE_TAG_FUNCTION, (wy_object*) fn0),
            WY_NULL, 0, out, 4);
        INFO(fault_message(fix.get_fiber_ptr()));
        REQUIRE_EQ(err, WY_ERR_NONE);

        CHECK_EQ(out[0].data.word, 10);
        CHECK_EQ(out[1].data.word, 20);
        CHECK(is_stop_iteration(ctx, out[2]));
        CHECK(is_stop_iteration(ctx, out[3]));
    }

    TEST_CASE("a coroutine that imports on its first next() runs the dependency inline on its own fiber") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        wy_module* builtins = WY_NULL;
        REQUIRE_EQ(wy_builtins_new(ctx, &builtins), WY_ERR_NONE);
        ctx->builtins = builtins;

        // Dependency "b": a single exported global set to 42.
        const wy_u32 dep_code[] = { enc1(WY_OP_I8, 42, 0), enc1(WY_OP_GSET, 0, 0), enc1(WY_OP_RETURN, 0, 0) };
        wy_module* dep = wy_module_new_f(ctx);
        dep->name = symbol(ctx, "b");
        dep->code = dep_code;
        dep->code_len = std::size(dep_code);
        dep->init_nlocals = 1;
        dep->global_count = 1;
        dep->globals = (wy_value*) wy_context_gc_alloc(ctx, sizeof(wy_value));
        dep->globals[0] = wy_value_unset();
        dep->fill_layer = (wy_u8*) wy_context_gc_alloc(ctx, sizeof(wy_u8));
        dep->fill_layer[0] = 0;
        dep->fill_source = (wy_symbol*) wy_context_gc_alloc(ctx, sizeof(wy_symbol));
        dep->fill_source[0] = WY_NULL;
        name_slot(ctx, &dep->exports, "value", 0);
        REQUIRE_EQ(wy_context_module_register(ctx, dep, nullptr), WY_ERR_NONE);

        // fn1 (coroutine body): import "b" (compact, dst=L1), gget qualified
        // global slot 0 (owned by the enclosing module), yield it, return.
        const wy_u32 fn0_code[] = {
            enc2a(WY_OP_CLOSURE, 0, 0), enc2b(1, 0),    // L0 <- fn1
            enc2a(WY_OP_CALL, 0, 0), enc2b(1, 0),       // L0 <- construct coroutine
            enc1(WY_OP_LCONST, 1, 0),                   // L1 <- next native
            enc1(WY_OP_MOVE, 1, 10), enc1(WY_OP_MOVE, 0, 11),
            enc2a(WY_OP_CALL, 1, 10), enc2b(1, 0),      // L10 <- next(L0)
            enc1(WY_OP_RETURN, 1, 10),                  // return L10
        };
        const wy_u32 fn1_code[] = {
            enc1(WY_OP_IMPORT, 1, 1),                   // L1 <- import "b" (static 1)
            enc1(WY_OP_GGET, 2, 0),                     // L2 <- module global 0 ("b::value")
            enc2a(WY_OP_YIELD, 1, 2), enc2b(0, 0),      // yield L2
            enc1(WY_OP_RETURN, 0, 0),
        };
        std::vector<wy_u32> code(std::begin(fn0_code), std::end(fn0_code));
        const wy_uword fn1_offset = code.size();
        code.insert(code.end(), std::begin(fn1_code), std::end(fn1_code));

        // Two statics shared by both functions: index 0 is fn0's `next`
        // native, index 1 is fn1's import path - import_static() would
        // clobber index 0, so both are built by hand here.
        wy_value next_native = builtin_value(ctx, builtins, "next");
        wy_string* b_path = WY_NULL;
        REQUIRE_EQ(wy_string_strdup(ctx, "b", &b_path), WY_ERR_NONE);
        const wy_value statics[] = { next_native, wy_value_object(WY_TYPE_TAG_STR, (wy_object*) b_path) };

        std::vector<wy_function_proto> protos = {
            proto_at(0, 12, 0),
            proto_at((wy_u32) fn1_offset, 3, WY_FN_COROUTINE),
        };
        wy_module* module = make_code_module(ctx, code.data(), code.size(), protos, 1, statics, std::size(statics));
        name_slot(ctx, &module->free_names, "b::value", 0);
        REQUIRE_EQ(wy_context_module_register(ctx, module, nullptr), WY_ERR_NONE);

        wy_function* fn0 = WY_NULL;
        REQUIRE_EQ(wy_function_new(ctx, module, &module->functions[0], WY_NULL, 0, &fn0), WY_ERR_NONE);

        wy_value out[1];
        wy_error err = wy_vm_call_sync(ctx, wy_value_object(WY_TYPE_TAG_FUNCTION, (wy_object*) fn0),
            WY_NULL, 0, out, 1);
        INFO(fault_message(fix.get_fiber_ptr()));
        REQUIRE_EQ(err, WY_ERR_NONE);

        CHECK_EQ(out[0].data.word, 42);
        CHECK_EQ(dep->state, WY_MODULE_READY);
    }

    TEST_CASE("an abandoned suspended coroutine is collected along with its fiber's stack") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        wy_module* builtins = WY_NULL;
        REQUIRE_EQ(wy_builtins_new(ctx, &builtins), WY_ERR_NONE);
        ctx->builtins = builtins;

        const wy_u32 fn0_code[] = {
            enc2a(WY_OP_CLOSURE, 0, 0), enc2b(1, 0),    // L0 <- fn1
            enc2a(WY_OP_CALL, 0, 0), enc2b(1, 0),       // L0 <- construct coroutine
            enc1(WY_OP_LCONST, 1, 0),                   // L1 <- next native
            enc1(WY_OP_MOVE, 1, 10), enc1(WY_OP_MOVE, 0, 11),
            enc2a(WY_OP_CALL, 1, 10), enc2b(1, 0),      // L10 <- next(L0) - suspends the coroutine
            // Deliberately never store L0 anywhere else and never call
            // next() again: once this frame returns, nothing references the
            // coroutine or its fiber any more.
            enc1(WY_OP_RETURN, 1, 10),
        };
        // fn1 body: yield once, forever suspended thereafter.
        const wy_u32 fn1_code[] = {
            enc1(WY_OP_I8, 7, 0),
            enc2a(WY_OP_YIELD, 1, 0), enc2b(0, 0),
            enc1(WY_OP_RETURN, 0, 0),
        };
        std::vector<wy_u32> code(std::begin(fn0_code), std::end(fn0_code));
        const wy_uword fn1_offset = code.size();
        code.insert(code.end(), std::begin(fn1_code), std::end(fn1_code));

        wy_value next_native = builtin_value(ctx, builtins, "next");
        std::vector<wy_function_proto> protos = {
            proto_at(0, 12, 0),
            proto_at((wy_u32) fn1_offset, 1, WY_FN_COROUTINE),
        };
        wy_module* module = make_code_module(ctx, code.data(), code.size(), protos, 0, &next_native, 1);
        REQUIRE_EQ(wy_context_module_register(ctx, module, nullptr), WY_ERR_NONE);

        wy_function* fn0 = WY_NULL;
        REQUIRE_EQ(wy_function_new(ctx, module, &module->functions[0], WY_NULL, 0, &fn0), WY_ERR_NONE);

        wy_value out[1];
        wy_error err = wy_vm_call_sync(ctx, wy_value_object(WY_TYPE_TAG_FUNCTION, (wy_object*) fn0),
            WY_NULL, 0, out, 1);
        INFO(fault_message(fix.get_fiber_ptr()));
        REQUIRE_EQ(err, WY_ERR_NONE);
        CHECK_EQ(out[0].data.word, 7);

        // The suspended coroutine's fiber is reachable from nowhere now: not
        // ctx->current_fiber (that's the root fiber again), not
        // ctx->fiber_list (only the root fiber is ever linked there), and no
        // register on the root fiber's stack still holds the coroutine value
        // (this frame already returned, popping it). A full GC must reclaim
        // it. wy_gc_full_run has no "did this get freed" signal by itself,
        // so this only demonstrates the run completes without asserting/
        // crashing under a debug allocator; the epic's own GC-stress golden
        // suite (WY_TEST_GC_THRESHOLD=0) is what actually exercises use-
        // after-free on this path across every other fixture.
        wy_context_gc_full_run(ctx);
        CHECK_EQ(wy_context_exec(ctx), WY_ERR_NONE);
    }
}
