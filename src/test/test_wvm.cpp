#include <doctest/doctest.h>

#include <algorithm>
#include <cstring>

#include <wyrm.h>
#include <wyrm/function.h>
#include <wyrm/opcode.h>
#include <wyrm/vm.h>
#include <wyrm/module.h>
#include <test_common/test_main_loop_fixture.h>
#include <test_common/test_context_fixture.h>
#include <test_common/test_fiber_fixture.h>

namespace {

/* Hand-packed instruction words (pypoc/doc/wyc-format.md §5), substituting
 * for the design_c_vm.md §9a `packed_ops.h`/`generate_c_fixtures.py`
 * pipeline this milestone does not build (see epic_2_report.md). */
constexpr wy_u32 enc1(wy_u8 op, wy_u8 f, wy_u16 a0)
{
    return (wy_u32(a0) << 16) | (wy_u32(f) << 8) | wy_u32(op);
}
constexpr wy_u32 enc2a(wy_u8 op, wy_u8 f, wy_u16 a0) { return enc1(op, f, a0); }
constexpr wy_u32 enc2b(wy_u16 a1, wy_u16 a2) { return (wy_u32(a1) << 16) | wy_u32(a2); }

/** A wy_module with just enough set up to run hand-packed `code`. */
wy_module* make_synthetic_module(wy_context* ctx, wy_uword nglobals)
{
    wy_module* module = wy_module_new_f(ctx);
    module->global_count = nglobals;
    if (nglobals > 0) {
        module->globals = (wy_value*) wy_context_gc_alloc(ctx, sizeof(wy_value) * nglobals);
        for (wy_uword i = 0; i < nglobals; i++) { module->globals[i] = wy_value_unset(); }
    }
    return module;
}

/** Wrap `code` as a zero-param, zero-capture function body and run it synchronously. */
wy_error run_synthetic(wy_context* ctx, const wy_u32* code, wy_uword code_len, wy_u16 nlocals, wy_uword nglobals,
    const wy_value* args, wy_uword argc, wy_value* out, wy_uword nres,
    const wy_value* statics = nullptr, wy_uword nstatics = 0,
    const wy_symbol* symbols = nullptr, wy_uword nsymbols = 0)
{
    // wy_module's finalizer unconditionally wy_context_gc_free()s a non-null
    // statics/symbols pointer, so these must be heap-allocated through the
    // same allocator - pointing at a caller's stack array would crash the
    // fixture's teardown GC pass.
    wy_module* module = make_synthetic_module(ctx, nglobals);
    module->code = code;
    module->code_len = code_len;
    if (statics != nullptr && nstatics > 0) {
        auto* heap_statics = (wy_value*) wy_context_gc_alloc(ctx, sizeof(wy_value) * nstatics);
        std::copy(statics, statics + nstatics, heap_statics);
        module->statics = heap_statics;
        module->static_count = nstatics;
    }
    if (symbols != nullptr && nsymbols > 0) {
        auto* heap_symbols = (wy_symbol*) wy_context_gc_alloc(ctx, sizeof(wy_symbol) * nsymbols);
        std::copy(symbols, symbols + nsymbols, heap_symbols);
        module->symbols = heap_symbols;
        module->symbol_count = nsymbols;
    }

    wy_function_proto proto = {};
    proto.code_offset = 0;
    proto.nlocals = nlocals;
    proto.nparams = (wy_u16) argc;

    wy_function* fn = WY_NULL;
    REQUIRE_EQ(wy_function_new(ctx, module, &proto, WY_NULL, 0, &fn), WY_ERR_NONE);

    return wy_vm_call_sync(ctx, wy_value_object(WY_TYPE_TAG_FUNCTION, (wy_object*) fn), args, argc, out, nres);
}

} // namespace

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

TEST_SUITE("vm dispatch loop") {
    TEST_CASE("lnil/lbool/lunset load and return backfills the window") {
        test_fiber_fixture ctx;
        const wy_u32 code[] = {
            enc1(WY_OP_LNIL, 0, 0),
            enc1(WY_OP_LBOOL, 1, 1),
            enc1(WY_OP_LUNSET, 0, 2),
            enc1(WY_OP_RETURN, 3, 0),  // base=0, count=3
        };
        wy_value out[3];
        REQUIRE_EQ(run_synthetic(ctx.get_context_ptr(), code, std::size(code), 3, 0, WY_NULL, 0, out, 3), WY_ERR_NONE);
        CHECK_EQ(out[0].type, WY_TYPE_TAG_NIL);
        CHECK_EQ(out[1].type, WY_TYPE_TAG_BOOL);
        CHECK_EQ(out[1].data.flag, true);
        CHECK(wy_value_is_unset(out[2]));
    }

    TEST_CASE("backfill nil-fills nres past what return actually produced") {
        test_fiber_fixture ctx;
        const wy_u32 code[] = {
            enc1(WY_OP_LBOOL, 1, 0),
            enc1(WY_OP_RETURN, 1, 0),  // base=0, count=1
        };
        wy_value out[3] = { wy_value_word(-1), wy_value_word(-1), wy_value_word(-1) };
        REQUIRE_EQ(run_synthetic(ctx.get_context_ptr(), code, std::size(code), 1, 0, WY_NULL, 0, out, 3), WY_ERR_NONE);
        CHECK_EQ(out[0].data.flag, true);
        CHECK_EQ(out[1].type, WY_TYPE_TAG_NIL);
        CHECK_EQ(out[2].type, WY_TYPE_TAG_NIL);
    }

    TEST_CASE("return truncates when the caller reserves fewer than count") {
        test_fiber_fixture ctx;
        const wy_u32 code[] = {
            enc1(WY_OP_I8, 11, 0),
            enc1(WY_OP_I8, 22, 1),
            enc1(WY_OP_I8, 33, 2),
            enc1(WY_OP_RETURN, 3, 0),  // base=0, count=3
        };
        wy_value out[1];
        REQUIRE_EQ(run_synthetic(ctx.get_context_ptr(), code, std::size(code), 3, 0, WY_NULL, 0, out, 1), WY_ERR_NONE);
        CHECK_EQ(out[0].data.word, 11);
    }

    TEST_CASE("i8 (compact), i32 (wide) and move round-trip") {
        test_fiber_fixture ctx;
        const wy_u32 code[] = {
            enc1(WY_OP_I8, (wy_u8) (wy_i32) -5, 0),
            enc2a(WY_OP_I32_WIDE, 0, 1), enc2b(0, 0), // placeholder, patched below
            enc1(WY_OP_MOVE, 0, 2),
            enc1(WY_OP_RETURN, 3, 0),
        };
        wy_u32 patched[std::size(code)];
        std::copy(std::begin(code), std::end(code), patched);
        wy_i32 big = 100000;
        std::memcpy(&patched[2], &big, sizeof(wy_u32));  // I32_WIDE's word1 (index 2, not 3)

        wy_value out[3];
        REQUIRE_EQ(run_synthetic(ctx.get_context_ptr(), patched, std::size(patched), 3, 0, WY_NULL, 0, out, 3), WY_ERR_NONE);
        CHECK_EQ(out[0].data.word, -5);
        CHECK_EQ(out[1].data.word, 100000);
        CHECK_EQ(out[2].data.word, -5);
    }

    TEST_CASE("gset/gget round-trip a module global") {
        test_fiber_fixture ctx;
        const wy_u32 code[] = {
            enc1(WY_OP_I8, 42, 0),
            enc1(WY_OP_GSET, 0, 0),   // g0 <- L0
            enc1(WY_OP_GGET, 1, 0),   // L1 <- g0
            enc1(WY_OP_RETURN, 1, 1), // base=1, count=1
        };
        wy_value out[1];
        REQUIRE_EQ(run_synthetic(ctx.get_context_ptr(), code, std::size(code), 2, 1, WY_NULL, 0, out, 1), WY_ERR_NONE);
        CHECK_EQ(out[0].data.word, 42);
    }

    TEST_CASE("gget on an unfilled global faults, fiber->fault set") {
        test_fiber_fixture ctx;
        const wy_u32 code[] = {
            enc1(WY_OP_GGET, 0, 0),   // L0 <- g0 (never written)
            enc1(WY_OP_RETURN, 1, 0),
        };
        wy_value out[1];
        wy_error result = run_synthetic(ctx.get_context_ptr(), code, std::size(code), 1, 1, WY_NULL, 0, out, 1);
        CHECK_EQ(result, WY_ERR_FAULT);
        CHECK(wy_value_is_error(ctx.get_fiber_ptr()->fault));
    }

    TEST_CASE("trap 0 faults with WY_ERR_FAULT") {
        test_fiber_fixture ctx;
        const wy_u32 code[] = { enc1(WY_OP_TRAP, 0, 0) };
        wy_value out[1];
        CHECK_EQ(run_synthetic(ctx.get_context_ptr(), code, std::size(code), 0, 0, WY_NULL, 0, out, 0), WY_ERR_FAULT);
        CHECK(wy_value_is_error(ctx.get_fiber_ptr()->fault));
    }

    TEST_CASE("arithmetic: add/sub/mul/div/mod/pow") {
        test_fiber_fixture ctx;
        const wy_u32 code[] = {
            enc1(WY_OP_I8, 7, 0),
            enc1(WY_OP_I8, 2, 1),
            enc2a(WY_OP_ADD, 0, 2), enc2b(0, 1),
            enc2a(WY_OP_SUB, 0, 3), enc2b(0, 1),
            enc2a(WY_OP_MUL, 0, 4), enc2b(0, 1),
            enc2a(WY_OP_DIV, 0, 5), enc2b(0, 1),
            enc2a(WY_OP_MOD, 0, 6), enc2b(0, 1),
            enc2a(WY_OP_POW, 0, 7), enc2b(0, 1),
            enc1(WY_OP_RETURN, 6, 2),  // base=2, count=6
        };
        wy_value out[6];
        REQUIRE_EQ(run_synthetic(ctx.get_context_ptr(), code, std::size(code), 8, 0, WY_NULL, 0, out, 6), WY_ERR_NONE);
        CHECK_EQ(out[0].data.word, 9);
        CHECK_EQ(out[1].data.word, 5);
        CHECK_EQ(out[2].data.word, 14);
        CHECK_EQ(out[3].type, WY_TYPE_TAG_FLOAT);
        CHECK_EQ(out[3].data.fp, doctest::Approx(3.5));
        CHECK_EQ(out[4].data.word, 1);
        CHECK_EQ(out[5].data.word, 49);
    }

    TEST_CASE("division by zero produces an error value, not a fault") {
        test_fiber_fixture ctx;
        const wy_u32 code[] = {
            enc1(WY_OP_I8, 5, 0),
            enc1(WY_OP_I8, 0, 1),
            enc2a(WY_OP_DIV, 0, 2), enc2b(0, 1),
            enc1(WY_OP_RETURN, 1, 2),
        };
        wy_value out[1];
        REQUIRE_EQ(run_synthetic(ctx.get_context_ptr(), code, std::size(code), 3, 0, WY_NULL, 0, out, 1), WY_ERR_NONE);
        CHECK(wy_value_is_error(out[0]));
    }

    TEST_CASE("comparisons and cmp3") {
        test_fiber_fixture ctx;
        const wy_u32 code[] = {
            enc1(WY_OP_I8, 3, 0),
            enc1(WY_OP_I8, 5, 1),
            enc2a(WY_OP_LT, 0, 2), enc2b(0, 1),
            enc2a(WY_OP_EQ, 0, 3), enc2b(0, 1),
            enc2a(WY_OP_CMP3, 0, 4), enc2b(0, 1),
            enc1(WY_OP_RETURN, 3, 2),
        };
        wy_value out[3];
        REQUIRE_EQ(run_synthetic(ctx.get_context_ptr(), code, std::size(code), 5, 0, WY_NULL, 0, out, 3), WY_ERR_NONE);
        CHECK_EQ(out[0].data.flag, true);   // 3 < 5
        CHECK_EQ(out[1].data.flag, false);  // 3 == 5
        CHECK_EQ(out[2].data.word, -1);     // 3 <=> 5
    }

    TEST_CASE("neg/inv/not") {
        test_fiber_fixture ctx;
        const wy_u32 code[] = {
            enc1(WY_OP_I8, 3, 0),
            enc1(WY_OP_LBOOL, 1, 1),
            enc1(WY_OP_NEG, 0, 2),
            enc1(WY_OP_INV, 0, 3),
            enc1(WY_OP_NOT, 1, 4),
            enc1(WY_OP_RETURN, 3, 2),
        };
        wy_value out[3];
        REQUIRE_EQ(run_synthetic(ctx.get_context_ptr(), code, std::size(code), 5, 0, WY_NULL, 0, out, 3), WY_ERR_NONE);
        CHECK_EQ(out[0].data.word, -3);
        CHECK_EQ(out[1].data.word, -4);   // ~3
        CHECK_EQ(out[2].data.flag, false); // not true
    }

    TEST_CASE("jf/jt/jmp branch correctly (compact)") {
        test_fiber_fixture ctx;
        //   0: lbool L1 <- false
        //   1: jf L1, +2          -> if false, skip to word 4 (the else branch)
        //   2: i8 L0 <- 1
        //   3: jmp +1             -> skip the else branch
        //   4: i8 L0 <- 2
        //   5: return base=0, count=1
        const wy_u32 code[] = {
            enc1(WY_OP_LBOOL, 0, 1),
            enc1(WY_OP_JF, 1, 2),
            enc1(WY_OP_I8, 1, 0),
            enc1(WY_OP_JMP, 0, 1),
            enc1(WY_OP_I8, 2, 0),
            enc1(WY_OP_RETURN, 1, 0),
        };
        wy_value out[1];
        REQUIRE_EQ(run_synthetic(ctx.get_context_ptr(), code, std::size(code), 2, 0, WY_NULL, 0, out, 1), WY_ERR_NONE);
        CHECK_EQ(out[0].data.word, 2);  // cond false -> else branch
    }

    TEST_CASE("jt takes the branch when truthy (wide form)") {
        test_fiber_fixture ctx;
        //   0: lbool L0 <- true
        //   1-2: jt (wide) L0, +2 -> if true, jump to word 5
        //   3: i8 L1 <- 1
        //   4: return base=1, count=1
        //   5: i8 L1 <- 9
        //   6: return base=1, count=1
        const wy_u32 code[] = {
            enc1(WY_OP_LBOOL, 1, 0),
            enc2a(WY_OP_JT_WIDE, 0, 0), enc2b(0, 0),  // patched: a1(a0-in-wide)=0 via 'a2' field below
            enc1(WY_OP_I8, 1, 1),
            enc1(WY_OP_RETURN, 1, 1),
            enc1(WY_OP_I8, 9, 1),
            enc1(WY_OP_RETURN, 1, 1),
        };
        wy_u32 patched[std::size(code)];
        std::copy(std::begin(code), std::end(code), patched);
        // wide jf/jt: a0 = cond register (full reg ref), w1 = rel i32.
        patched[1] = enc1(WY_OP_JT_WIDE, 0, 0);  // a0 = cond reg (L0)
        wy_i32 rel = 2;  // next_ip is word index 3; target word index 5 => rel = 5 - 3
        std::memcpy(&patched[2], &rel, sizeof(wy_u32));

        wy_value out[1];
        REQUIRE_EQ(run_synthetic(ctx.get_context_ptr(), patched, std::size(patched), 2, 0, WY_NULL, 0, out, 1), WY_ERR_NONE);
        CHECK_EQ(out[0].data.word, 9);
    }

    TEST_CASE("lconst/lsym read the statics/symbols tables") {
        test_fiber_fixture ctx;
        wy_value statics[1] = { wy_value_word(777) };
        const char* sym_text = "ready";
        wy_symbol symbols[1] = { sym_text };

        const wy_u32 code[] = {
            enc1(WY_OP_LCONST, 0, 0),
            enc1(WY_OP_LSYM, 1, 0),
            enc1(WY_OP_RETURN, 2, 0),
        };
        wy_value out[2];
        REQUIRE_EQ(run_synthetic(ctx.get_context_ptr(), code, std::size(code), 2, 0, WY_NULL, 0, out, 2,
                                  statics, 1, symbols, 1), WY_ERR_NONE);
        CHECK_EQ(out[0].data.word, 777);
        CHECK_EQ(out[1].type, WY_TYPE_TAG_SYMBOL);
        CHECK_EQ(out[1].data.symtab_entry, sym_text);
    }

    TEST_CASE("call/return across a nested function, with a closure of 0 captures") {
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();

        // callee: fn double(a): return a*2   (nparams=1, nlocals=1)
        const wy_u32 callee_code[] = {
            enc1(WY_OP_I8, 2, 1),                       // L1 <- 2
            enc2a(WY_OP_MUL, 0, 1), enc2b(0x8000, 1),    // L1 <- P0 * L1  (P0 is the argument)
            enc1(WY_OP_RETURN, 1, 1),
        };

        // module->functions must be heap-allocated: the finalizer
        // unconditionally wy_context_gc_free()s it (see run_synthetic's
        // comment on the same pitfall for statics/symbols).
        wy_module* module = make_synthetic_module(context, 0);
        module->function_count = 1;
        auto* callee_proto = (wy_function_proto*) wy_context_gc_alloc(context, sizeof(wy_function_proto));
        *callee_proto = wy_function_proto{};
        callee_proto->code_offset = 0;
        callee_proto->nparams = 1;
        callee_proto->nlocals = 2;
        module->functions = callee_proto;

        // caller: closure L0 <- fn#0 (0 caps); call L0(5) -> L0, nres=1; return L0, 1
        const wy_u32 caller_code[] = {
            enc2a(WY_OP_CLOSURE, 0, 0), enc2b(0, 0),   // a1=function index 0, f(=ncaps)=0
            enc1(WY_OP_I8, 5, 1),                       // L1 <- 5 (argument)
            enc2a(WY_OP_CALL, 1, 0), enc2b(1, 0),        // base=0, argc=1, nres=1
            enc1(WY_OP_RETURN, 1, 0),
        };
        // callee_code is placed right after caller_code in the same module image.
        wy_u32 combined[std::size(caller_code) + std::size(callee_code)];
        std::copy(std::begin(caller_code), std::end(caller_code), combined);
        std::copy(std::begin(callee_code), std::end(callee_code), combined + std::size(caller_code));
        module->code = combined;
        module->code_len = std::size(combined);
        callee_proto->code_offset = (wy_u32) std::size(caller_code);

        wy_function_proto caller_proto = {};
        caller_proto.code_offset = 0;
        caller_proto.nlocals = 2;

        wy_function* fn = WY_NULL;
        REQUIRE_EQ(wy_function_new(context, module, &caller_proto, WY_NULL, 0, &fn), WY_ERR_NONE);

        wy_value out[1];
        REQUIRE_EQ(wy_vm_call_sync(context, wy_value_object(WY_TYPE_TAG_FUNCTION, (wy_object*) fn), WY_NULL, 0, out, 1), WY_ERR_NONE);
        CHECK_EQ(out[0].data.word, 10);
    }

    TEST_CASE("a wrong argument count faults rather than binding") {
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();

        const wy_u32 callee_code[] = { enc1(WY_OP_RETURN, 0, 0) };
        wy_module* module = make_synthetic_module(context, 0);
        module->code = callee_code;
        module->code_len = std::size(callee_code);

        wy_function_proto proto = {};
        proto.nparams = 2;  // expects 2 args
        wy_function* fn = WY_NULL;
        REQUIRE_EQ(wy_function_new(context, module, &proto, WY_NULL, 0, &fn), WY_ERR_NONE);

        wy_value args[1] = { wy_value_word(1) };
        wy_value out[1];
        CHECK_EQ(wy_vm_call_sync(context, wy_value_object(WY_TYPE_TAG_FUNCTION, (wy_object*) fn), args, 1, out, 1), WY_ERR_ARITY);
    }
}
