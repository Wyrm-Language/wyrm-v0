#include <doctest/doctest.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include <wyrm.h>
#include <wyrm/builtins.h>
#include <wyrm/class.h>
#include <wyrm/dict.h>
#include <wyrm/error.h>
#include <wyrm/function.h>
#include <wyrm/opcode.h>
#include <wyrm/pair.h>
#include <wyrm/string.h>
#include <wyrm/tuple.h>
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

/**
 * A module owning `code` plus several `functions[]` protos and optional
 * statics/symbols tables, all heap-copied because the module's finalizer
 * unconditionally frees every array it does not find null (see the note on
 * make_synthetic_module). The caller still owns `proto.params`/strings it
 * allocated itself - the finalizer frees `functions[].params`, so those must
 * also be allocator-owned.
 */
wy_module* make_code_module(wy_context* ctx, const wy_u32* code, wy_uword code_len,
    const std::vector<wy_function_proto>& protos,
    const wy_value* statics = nullptr, wy_uword nstatics = 0,
    const wy_symbol* symbols = nullptr, wy_uword nsymbols = 0)
{
    wy_module* module = make_synthetic_module(ctx, 0);
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
    if (!protos.empty()) {
        module->function_count = protos.size();
        auto* heap_protos = (wy_function_proto*) wy_context_gc_alloc(ctx, sizeof(wy_function_proto) * protos.size());
        std::copy(protos.begin(), protos.end(), heap_protos);
        module->functions = heap_protos;
    }
    return module;
}

/** A parameter table entry: name plus no default. */
wy_param param_named(const char* name)
{
    wy_param p = {};
    p.name = name;
    p.default_static = -1;
    return p;
}

/** The fault's message text, or an empty std::string when it is no fault. */
std::string fault_message(wy_fiber* fiber)
{
    if (fiber->fault.type != WY_TYPE_TAG_ERROR || fiber->fault.data.gc_object == WY_NULL) { return {}; }
    auto* err = (wy_error_obj*) fiber->fault.data.gc_object;
    return std::string(err->what ? err->what->str : "", err->what ? err->what->len : 0);
}

/** Give a make_code_module module `n` Unset globals (allocator-owned, see
 * make_synthetic_module). */
void give_globals(wy_context* ctx, wy_module* module, wy_uword n)
{
    module->global_count = n;
    module->globals = (wy_value*) wy_context_gc_alloc(ctx, sizeof(wy_value) * n);
    for (wy_uword i = 0; i < n; i++) { module->globals[i] = wy_value_unset(); }
}

/** A proto for functions[] at `offset` with `nparams` named parameters. */
wy_function_proto proto_at(wy_context* ctx, const char* name, wy_u32 offset, wy_u16 nlocals, wy_u16 nparams = 0)
{
    wy_function_proto proto = {};
    proto.name = name;
    proto.code_offset = offset;
    proto.nlocals = nlocals;
    proto.nparams = nparams;
    if (nparams > 0) {
        auto* params = (wy_param*) wy_context_gc_alloc(ctx, sizeof(wy_param) * nparams);
        for (wy_u16 i = 0; i < nparams; i++) { params[i] = param_named("p"); }
        proto.params = params;
    }
    return proto;
}

/** Call `module->functions[0]` synchronously, the module registered so a
 * stress-mode collection mid-run keeps it (frames do not root modules). */
wy_error run_fn0(wy_context* ctx, wy_module* module, const wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    wy_uword module_id = WY_IDX_INVALID;
    REQUIRE_EQ(wy_context_module_register(ctx, module, &module_id), WY_ERR_NONE);
    wy_function* fn = WY_NULL;
    REQUIRE_EQ(wy_function_new(ctx, module, &module->functions[0], WY_NULL, 0, &fn), WY_ERR_NONE);
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

    TEST_CASE("gget on an unfilled free slot faults naming it; an unassigned own global reads Unset") {
        const wy_u32 code[] = {
            enc1(WY_OP_GGET, 0, 0),   // L0 <- g0 (never written)
            enc1(WY_OP_RETURN, 1, 0),
        };
        for (bool free_slot : { true, false }) {
            test_fiber_fixture ctx;
            wy_context* context = ctx.get_context_ptr();
            wy_module* module = make_code_module(context, code, std::size(code), { proto_at(context, "f", 0, 1) });
            give_globals(context, module, 1);
            if (free_slot) {
                REQUIRE_EQ(wy_slot_dict_expand_f(&module->free_names, wy_context_get_machine(context)->allocator, 4),
                    WY_ERR_NONE);
                REQUIRE_EQ(wy_slot_dict_add_entry(&module->free_names, "missing", 0), WY_ERR_NONE);
            }
            wy_value out[1];
            CAPTURE(free_slot);
            wy_error result = run_fn0(context, module, WY_NULL, 0, out, 1);
            if (free_slot) {
                CHECK_EQ(result, WY_ERR_FAULT);
                CHECK_EQ(fault_message(ctx.get_fiber_ptr()), "unbound global 'missing'");
            } else {
                REQUIRE_EQ(result, WY_ERR_NONE);
                CHECK(wy_value_is_unset(out[0]));
            }
        }
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
        // a params table is mandatory once nparams > 0 (the binder asserts it)
        auto* callee_params = (wy_param*) wy_context_gc_alloc(context, sizeof(wy_param));
        *callee_params = param_named("a");
        callee_proto->params = callee_params;
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

    TEST_CASE("a call with too few arguments faults naming the parameter") {
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();

        const wy_u32 callee_code[] = { enc1(WY_OP_RETURN, 0, 0) };
        wy_module* module = make_synthetic_module(context, 0);
        module->code = callee_code;
        module->code_len = std::size(callee_code);

        // functions[].params is freed by the module finalizer, so this must
        // be allocator-owned (see make_synthetic_module's note).
        auto* params = (wy_param*) wy_context_gc_alloc(context, 2 * sizeof(wy_param));
        params[0] = param_named("a");
        params[1] = param_named("b");

        wy_function_proto proto = {};
        proto.name = "f";
        proto.nparams = 2;
        proto.params = params;
        wy_function* fn = WY_NULL;
        REQUIRE_EQ(wy_function_new(context, module, &proto, WY_NULL, 0, &fn), WY_ERR_NONE);

        wy_value args[1] = { wy_value_word(1) };
        wy_value out[1];
        REQUIRE_EQ(wy_vm_call_sync(context, wy_value_object(WY_TYPE_TAG_FUNCTION, (wy_object*) fn), args, 1, out, 1),
            WY_ERR_FAULT);
        CHECK_EQ(fault_message(ctx.get_fiber_ptr()), "f() missing required argument: 'b'");
    }

    TEST_CASE("closure copies captured locals by value") {
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();

        // caller: L1 <- 5 (the captured amount); closure L0 <- fn#1, 1 cap
        // from L1; L1 <- 2 (the argument lives at base+1 in the call
        // window); call base=L0 argc=1 nres=1; return L0.
        const wy_u32 caller_code[] = {
            enc1(WY_OP_I8, 5, 1),
            enc2a(WY_OP_CLOSURE, 1, 0), enc2b(1, 1),
            enc1(WY_OP_I8, 2, 1),
            enc2a(WY_OP_CALL, 1, 0), enc2b(1, 0),
            enc1(WY_OP_RETURN, 1, 0),
        };
        // fn#1: add L0 <- P0 + P1  (P0 = value, P1 = captured amount); return L0
        const wy_u32 callee_code[] = {
            enc2a(WY_OP_ADD, 0, 0), enc2b(0x8000, 0x8001),
            enc1(WY_OP_RETURN, 1, 0),
        };

        wy_u32 combined[std::size(caller_code) + std::size(callee_code)];
        std::copy(std::begin(caller_code), std::end(caller_code), combined);
        std::copy(std::begin(callee_code), std::end(callee_code), combined + std::size(caller_code));

        wy_function_proto callee_proto = {};
        callee_proto.name = "<lambda>";
        callee_proto.code_offset = (wy_u32) std::size(caller_code);
        callee_proto.nparams = 1;
        callee_proto.ncaptures = 1;
        callee_proto.nlocals = 1;
        auto* callee_params = (wy_param*) wy_context_gc_alloc(context, sizeof(wy_param));
        *callee_params = param_named("value");
        callee_proto.params = callee_params;

        wy_function_proto caller_proto = {};
        caller_proto.nlocals = 3;

        wy_module* module = make_code_module(context, combined, std::size(combined),
            { caller_proto, callee_proto });

        wy_function* fn = WY_NULL;
        REQUIRE_EQ(wy_function_new(context, module, &module->functions[0], WY_NULL, 0, &fn), WY_ERR_NONE);

        wy_value out[1];
        REQUIRE_EQ(wy_vm_call_sync(context, wy_value_object(WY_TYPE_TAG_FUNCTION, (wy_object*) fn), WY_NULL, 0, out, 1),
            WY_ERR_NONE);
        CHECK_EQ(out[0].data.word, 7);
    }

    TEST_CASE("closure captures a shared mutable cell") {
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();

        // The cell is a one-element list both closures of this test would
        // share if it ran twice; a single closure is enough to prove the
        // capture aliases the list (bump's second call sees 11's write).
        //
        // caller: L4 <- 0 (cell index); L2 <- 10; L2 <- list(L2);
        //   closure L0 <- fn#1, 1 cap from L2; L5 <- L0 (keep the callee);
        //   L1 <- 1; call base=L0 argc=1 nres=1; L6 <- L0;
        //   L0 <- L5; L1 <- 1; call base=L0 argc=1 nres=1; L7 <- L0;
        //   return L6, L7.
        const wy_u32 caller_code[] = {
            enc1(WY_OP_I8, 0, 4),
            enc1(WY_OP_I8, 10, 2),
            enc2a(WY_OP_LIST, 1, 2), enc2b(2, 0),
            enc2a(WY_OP_CLOSURE, 1, 0), enc2b(1, 2),
            enc1(WY_OP_MOVE, 0, 5),
            enc1(WY_OP_I8, 1, 1),
            enc2a(WY_OP_CALL, 1, 0), enc2b(1, 0),
            enc1(WY_OP_MOVE, 0, 6),
            enc1(WY_OP_MOVE, 5, 0),
            enc1(WY_OP_I8, 1, 1),
            enc2a(WY_OP_CALL, 1, 0), enc2b(1, 0),
            enc1(WY_OP_MOVE, 0, 7),
            enc1(WY_OP_RETURN, 2, 6),
        };
        // fn#1 (bump): the captured cell is P1, the integer step P0.
        //   L0 <- 0; L1 <- P1[L0]; L2 <- P0 + L1; P1[L0] <- L2;
        //   L3 <- P1[L0]; return L3
        const wy_u32 callee_code[] = {
            enc1(WY_OP_I8, 0, 0),
            enc2a(WY_OP_GETIDX, 0, 1), enc2b(0x8001, 0),
            enc2a(WY_OP_ADD, 0, 2), enc2b(0x8000, 1),
            enc2a(WY_OP_SETIDX, 0, 0x8001), enc2b(0, 2),
            enc2a(WY_OP_GETIDX, 0, 3), enc2b(0x8001, 0),
            enc1(WY_OP_RETURN, 1, 3),
        };

        wy_u32 combined[std::size(caller_code) + std::size(callee_code)];
        std::copy(std::begin(caller_code), std::end(caller_code), combined);
        std::copy(std::begin(callee_code), std::end(callee_code), combined + std::size(caller_code));

        wy_function_proto callee_proto = {};
        callee_proto.name = "bump";
        callee_proto.code_offset = (wy_u32) std::size(caller_code);
        callee_proto.nparams = 1;
        callee_proto.ncaptures = 1;
        callee_proto.nlocals = 4;
        auto* callee_params = (wy_param*) wy_context_gc_alloc(context, sizeof(wy_param));
        *callee_params = param_named("step");
        callee_proto.params = callee_params;

        wy_function_proto caller_proto = {};
        caller_proto.nlocals = 8;

        wy_module* module = make_code_module(context, combined, std::size(combined),
            { caller_proto, callee_proto });

        wy_function* fn = WY_NULL;
        REQUIRE_EQ(wy_function_new(context, module, &module->functions[0], WY_NULL, 0, &fn), WY_ERR_NONE);

        wy_value out[2];
        REQUIRE_EQ(wy_vm_call_sync(context, wy_value_object(WY_TYPE_TAG_FUNCTION, (wy_object*) fn), WY_NULL, 0, out, 2),
            WY_ERR_NONE);
        CHECK_EQ(out[0].data.word, 11);
        CHECK_EQ(out[1].data.word, 12);
    }

    TEST_CASE("call_va unpacks the positional tuple and an empty dict") {
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();

        // caller: closure L0 <- fn#1, 0 caps; L3 <- 3; L1 <- tuple(L3);
        //   L2 <- {}; call_va base=L0 nres=1 (a1=nres); return L0
        const wy_u32 caller_code[] = {
            enc2a(WY_OP_CLOSURE, 0, 0), enc2b(1, 0),
            enc1(WY_OP_I8, 3, 3),
            enc2a(WY_OP_TUPLE, 1, 1), enc2b(3, 0),
            enc2a(WY_OP_NEW_PRIMITIVE, (wy_u8) WY_TYPE_TAG_TABLE, 2), enc2b(0, 0),
            enc2a(WY_OP_CALL_VA, 0, 0), enc2b(1, 0),
            enc1(WY_OP_RETURN, 1, 0),
        };
        // fn#1: L1 <- 2; mul L1 <- P0 * L1; return L1
        const wy_u32 callee_code[] = {
            enc1(WY_OP_I8, 2, 1),
            enc2a(WY_OP_MUL, 0, 1), enc2b(0x8000, 1),
            enc1(WY_OP_RETURN, 1, 1),
        };

        wy_u32 combined[std::size(caller_code) + std::size(callee_code)];
        std::copy(std::begin(caller_code), std::end(caller_code), combined);
        std::copy(std::begin(callee_code), std::end(callee_code), combined + std::size(caller_code));

        wy_function_proto callee_proto = {};
        callee_proto.name = "double";
        callee_proto.code_offset = (wy_u32) std::size(caller_code);
        callee_proto.nparams = 1;
        callee_proto.nlocals = 2;
        auto* callee_params = (wy_param*) wy_context_gc_alloc(context, sizeof(wy_param));
        *callee_params = param_named("x");
        callee_proto.params = callee_params;

        wy_function_proto caller_proto = {};
        caller_proto.nlocals = 4;

        wy_module* module = make_code_module(context, combined, std::size(combined),
            { caller_proto, callee_proto });

        wy_function* fn = WY_NULL;
        REQUIRE_EQ(wy_function_new(context, module, &module->functions[0], WY_NULL, 0, &fn), WY_ERR_NONE);

        wy_value out[1];
        REQUIRE_EQ(wy_vm_call_sync(context, wy_value_object(WY_TYPE_TAG_FUNCTION, (wy_object*) fn), WY_NULL, 0, out, 1),
            WY_ERR_NONE);
        CHECK_EQ(out[0].data.word, 6);
    }

    TEST_CASE("call_va binds trailing parameters by keyword and collects **kwargs") {
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();

        // fn#1 (f(a, b, **kw)): return P0, P1, P2  (a, b, the leftover dict)
        const wy_u32 callee_code[] = { enc1(WY_OP_RETURN, 3, 0x8000) };

        // caller: closure L0 <- fn#1, 0 caps; L3 <- 1; L1 <- tuple(L3);
        //   kw = {b: 2, x: 3} as interleaved pairs L4..L7 (keys are STR
        //   statics, matching the real compiler's call_va encoding - a
        //   keyword dict's keys are plain strings, not interned symbols,
        //   confirmed against pypoc/wypoc/compiler_bc's actual output
        //   during epic 9); L2 <- dict(L4); call_va base=L0 nres=3;
        //   return L0, 3
        const wy_u32 caller_code[] = {
            enc2a(WY_OP_CLOSURE, 0, 0), enc2b(1, 0),
            enc1(WY_OP_I8, 1, 3),
            enc2a(WY_OP_TUPLE, 1, 1), enc2b(3, 0),
            enc1(WY_OP_LCONST, 4, 0),
            enc1(WY_OP_I8, 2, 5),
            enc1(WY_OP_LCONST, 6, 1),
            enc1(WY_OP_I8, 3, 7),
            enc2a(WY_OP_DICT, 2, 2), enc2b(4, 0),
            enc2a(WY_OP_CALL_VA, 0, 0), enc2b(3, 0),
            enc1(WY_OP_RETURN, 3, 0),
        };

        wy_u32 combined[std::size(caller_code) + std::size(callee_code)];
        std::copy(std::begin(caller_code), std::end(caller_code), combined);
        std::copy(std::begin(callee_code), std::end(callee_code), combined + std::size(caller_code));

        wy_symbol sym_a = WY_NULL, sym_b = WY_NULL, sym_kw = WY_NULL;
        REQUIRE_EQ(wy_context_intern(context, "a", 1, &sym_a), WY_ERR_NONE);
        REQUIRE_EQ(wy_context_intern(context, "b", 1, &sym_b), WY_ERR_NONE);
        REQUIRE_EQ(wy_context_intern(context, "kw", 2, &sym_kw), WY_ERR_NONE);

        wy_string *str_b = WY_NULL, *str_x = WY_NULL;
        REQUIRE_EQ(wy_string_new(context, "b", 1, &str_b), WY_ERR_NONE);
        REQUIRE_EQ(wy_string_new(context, "x", 1, &str_x), WY_ERR_NONE);
        const wy_value statics[] = {
            wy_value_object(WY_TYPE_TAG_STR, (wy_object*) str_b),
            wy_value_object(WY_TYPE_TAG_STR, (wy_object*) str_x),
        };

        // Param names are the interned symbols: keyword lookup matches by
        // symtab pointer, exactly as a loaded module's params always are.
        auto* bounds = (wy_param*) wy_context_gc_alloc(context, 3 * sizeof(wy_param));
        bounds[0].name = sym_a;
        bounds[0].default_static = -1;
        bounds[1].name = sym_b;
        bounds[1].default_static = -1;
        bounds[2].name = sym_kw;
        bounds[2].default_static = -1;

        wy_function_proto callee_proto = {};
        callee_proto.name = "f";
        callee_proto.code_offset = (wy_u32) std::size(caller_code);
        callee_proto.nparams = 3;
        callee_proto.nlocals = 0;
        callee_proto.flags = WY_FN_KWARGS;
        callee_proto.params = bounds;

        wy_function_proto caller_proto = {};
        caller_proto.nlocals = 8;

        wy_module* module = make_code_module(context, combined, std::size(combined),
            { caller_proto, callee_proto }, statics, std::size(statics));

        wy_function* fn = WY_NULL;
        REQUIRE_EQ(wy_function_new(context, module, &module->functions[0], WY_NULL, 0, &fn), WY_ERR_NONE);

        wy_value out[3];
        REQUIRE_EQ(wy_vm_call_sync(context, wy_value_object(WY_TYPE_TAG_FUNCTION, (wy_object*) fn), WY_NULL, 0, out, 3),
            WY_ERR_NONE);
        CHECK_EQ(out[0].data.word, 1);
        CHECK_EQ(out[1].data.word, 2);
        REQUIRE_EQ(out[2].type, WY_TYPE_TAG_TABLE);
        wy_dict* kw = (wy_dict*) out[2].data.gc_object;
        wy_value* x = wy_dict_get(context, kw, WY_TYPE_TAG_STR, wy_value_object(WY_TYPE_TAG_STR, (wy_object*) str_x).data);
        REQUIRE_NE(x, WY_NULL);
        CHECK_EQ(x->data.word, 3);
    }

    TEST_CASE("a declared default binds when no positional or keyword fills it") {
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();

        // fn: add L0 <- P0 + P1; return L0  (b defaults to statics[0] = 10)
        const wy_u32 callee_code[] = {
            enc2a(WY_OP_ADD, 0, 0), enc2b(0x8000, 0x8001),
            enc1(WY_OP_RETURN, 1, 0),
        };

        auto* params = (wy_param*) wy_context_gc_alloc(context, 2 * sizeof(wy_param));
        params[0] = param_named("a");
        params[1] = param_named("b");
        params[1].default_static = 0;

        wy_function_proto proto = {};
        proto.name = "f";
        proto.nparams = 2;
        proto.nlocals = 1;
        proto.params = params;

        const wy_value statics[] = { wy_value_word(10) };
        wy_module* module = make_code_module(context, callee_code, std::size(callee_code),
            { proto }, statics, std::size(statics));

        wy_function* fn = WY_NULL;
        REQUIRE_EQ(wy_function_new(context, module, &module->functions[0], WY_NULL, 0, &fn), WY_ERR_NONE);

        wy_value args[1] = { wy_value_word(5) };
        wy_value out[1];
        REQUIRE_EQ(wy_vm_call_sync(context, wy_value_object(WY_TYPE_TAG_FUNCTION, (wy_object*) fn), args, 1, out, 1),
            WY_ERR_NONE);
        CHECK_EQ(out[0].data.word, 15);
    }

    TEST_CASE("*args collects the leftover positional values as a tuple") {
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();

        // fn(a, *rest): return P1 (the rest tuple), 1
        const wy_u32 callee_code[] = { enc1(WY_OP_RETURN, 1, 0x8001) };

        auto* params = (wy_param*) wy_context_gc_alloc(context, 2 * sizeof(wy_param));
        params[0] = param_named("a");
        params[1] = param_named("rest");

        wy_function_proto proto = {};
        proto.name = "f";
        proto.nparams = 2;
        proto.nlocals = 0;
        proto.flags = WY_FN_VARARGS;
        proto.params = params;

        wy_module* module = make_code_module(context, callee_code, std::size(callee_code), { proto });

        wy_function* fn = WY_NULL;
        REQUIRE_EQ(wy_function_new(context, module, &module->functions[0], WY_NULL, 0, &fn), WY_ERR_NONE);

        const wy_value args[] = { wy_value_word(1), wy_value_word(2), wy_value_word(3) };
        wy_value out[1];
        REQUIRE_EQ(wy_vm_call_sync(context, wy_value_object(WY_TYPE_TAG_FUNCTION, (wy_object*) fn), args, 3, out, 1),
            WY_ERR_NONE);
        REQUIRE_EQ(out[0].type, WY_TYPE_TAG_TUPLE);
        auto* rest = (wy_tuple*) out[0].data.gc_object;
        REQUIRE_EQ(rest->count, 2u);
        CHECK_EQ(rest->items[0].data.word, 2);
        CHECK_EQ(rest->items[1].data.word, 3);
    }

    TEST_CASE("an unexpected keyword argument faults with sorted names") {
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();

        // fn#1 (f(a)): body is unreachable - the call faults in binding
        const wy_u32 callee_code[] = { enc1(WY_OP_RETURN, 0, 0) };

        // caller: closure L0 <- fn#1, 0 caps; L1 <- () (empty tuple);
        //   kw = {c: 1, a: 9, b: 2} as interleaved pairs L4..L9 (STR keys,
        //   matching the real compiler's call_va encoding - see the
        //   sibling kwargs test above); L2 <- dict(L4); call_va base=L0 nres=1
        const wy_u32 caller_code[] = {
            enc2a(WY_OP_CLOSURE, 0, 0), enc2b(1, 0),
            enc2a(WY_OP_TUPLE, 0, 1), enc2b(3, 0),
            enc1(WY_OP_LCONST, 4, 0),
            enc1(WY_OP_I8, 1, 5),
            enc1(WY_OP_LCONST, 6, 1),
            enc1(WY_OP_I8, 9, 7),
            enc1(WY_OP_LCONST, 8, 2),
            enc1(WY_OP_I8, 2, 9),
            enc2a(WY_OP_DICT, 3, 2), enc2b(4, 0),
            enc2a(WY_OP_CALL_VA, 0, 0), enc2b(1, 0),
            enc1(WY_OP_RETURN, 0, 0),
        };

        wy_u32 combined[std::size(caller_code) + std::size(callee_code)];
        std::copy(std::begin(caller_code), std::end(caller_code), combined);
        std::copy(std::begin(callee_code), std::end(callee_code), combined + std::size(caller_code));

        wy_symbol sym_a = WY_NULL;
        REQUIRE_EQ(wy_context_intern(context, "a", 1, &sym_a), WY_ERR_NONE);

        wy_string *str_c = WY_NULL, *str_a = WY_NULL, *str_b = WY_NULL;
        REQUIRE_EQ(wy_string_new(context, "c", 1, &str_c), WY_ERR_NONE);
        REQUIRE_EQ(wy_string_new(context, "a", 1, &str_a), WY_ERR_NONE);
        REQUIRE_EQ(wy_string_new(context, "b", 1, &str_b), WY_ERR_NONE);
        const wy_value statics[] = {
            wy_value_object(WY_TYPE_TAG_STR, (wy_object*) str_c),
            wy_value_object(WY_TYPE_TAG_STR, (wy_object*) str_a),
            wy_value_object(WY_TYPE_TAG_STR, (wy_object*) str_b),
        };

        wy_function_proto callee_proto = {};
        callee_proto.name = "f";
        callee_proto.code_offset = (wy_u32) std::size(caller_code);
        callee_proto.nparams = 1;
        callee_proto.nlocals = 0;
        auto* callee_params = (wy_param*) wy_context_gc_alloc(context, sizeof(wy_param));
        *callee_params = param_named(sym_a);
        callee_proto.params = callee_params;

        wy_function_proto caller_proto = {};
        caller_proto.nlocals = 10;

        wy_module* module = make_code_module(context, combined, std::size(combined),
            { caller_proto, callee_proto }, statics, std::size(statics));

        wy_function* fn = WY_NULL;
        REQUIRE_EQ(wy_function_new(context, module, &module->functions[0], WY_NULL, 0, &fn), WY_ERR_NONE);

        wy_value out[1];
        REQUIRE_EQ(wy_vm_call_sync(context, wy_value_object(WY_TYPE_TAG_FUNCTION, (wy_object*) fn), WY_NULL, 0, out, 1),
            WY_ERR_FAULT);
        CHECK_EQ(fault_message(ctx.get_fiber_ptr()), "f() got unexpected keyword argument(s): b, c");
    }
}

TEST_SUITE("wvm milestone 1") {
    TEST_CASE("tuple and list construction and getidx") {
        test_fiber_fixture ctx;
        const wy_u32 code[] = {
            enc1(WY_OP_I8, 10, 0),
            enc1(WY_OP_I8, 20, 1),
            enc2a(WY_OP_TUPLE, 2, 2), enc2b(0, 0),    // L2 <- tuple(L0, L1)
            enc2a(WY_OP_LIST, 2, 3), enc2b(0, 0),     // L3 <- list(L0, L1)
            enc1(WY_OP_I8, 0, 4),
            enc1(WY_OP_I8, 1, 5),
            enc2a(WY_OP_GETIDX, 0, 6), enc2b(2, 4),   // L6 <- L2[0] = 10 (a0=6, a1=2, a2=4)
            enc2a(WY_OP_GETIDX, 0, 7), enc2b(3, 5),   // L7 <- L3[1] = 20 (a0=7, a1=3, a2=5)
            enc1(WY_OP_RETURN, 2, 6),
        };
        wy_value out[2];
        REQUIRE_EQ(run_synthetic(ctx.get_context_ptr(), code, std::size(code), 8, 0, WY_NULL, 0, out, 2), WY_ERR_NONE);
        CHECK_EQ(out[0].data.word, 10);
        CHECK_EQ(out[1].data.word, 20);
    }

    TEST_CASE("dict construction and getidx/setidx") {
        test_fiber_fixture ctx;
        const wy_u32 code[] = {
            enc1(WY_OP_I8, 1, 0),                     // L0 <- 1 (key)
            enc1(WY_OP_I8, 100, 1),                   // L1 <- 100 (val)
            enc1(WY_OP_I8, 2, 2),                     // L2 <- 2 (key)
            enc1(WY_OP_I8, (wy_u8)200, 3),            // L3 <- 200 (val)
            enc2a(WY_OP_DICT, 2, 4), enc2b(0, 0),     // L4 <- dict({1: 100, 2: 200})
            enc2a(WY_OP_GETIDX, 0, 5), enc2b(4, 0),   // L5 <- L4[1] = 100 (a1=4, a2=0)
            enc1(WY_OP_I8, 123, 6),                   // L6 <- 123
            enc2a(WY_OP_SETIDX, 0, 4), enc2b(0, 6),   // L4[1] <- L6 (a0=4, a1=0, a2=6)
            enc2a(WY_OP_GETIDX, 0, 7), enc2b(4, 0),   // L7 <- L4[1] = 123
            enc1(WY_OP_RETURN, 2, 5),                 // return L5, L7
        };
        wy_value out[2];
        REQUIRE_EQ(run_synthetic(ctx.get_context_ptr(), code, std::size(code), 8, 0, WY_NULL, 0, out, 2), WY_ERR_NONE);
        CHECK_EQ(out[0].data.word, 100);
        CHECK_EQ(out[1].data.word, 123);
    }

    TEST_CASE("plist construction and PAIR getidx 0") {
        test_fiber_fixture ctx;
        const wy_u32 code[] = {
            enc1(WY_OP_I8, 11, 0),
            enc1(WY_OP_I8, 22, 1),
            enc2a(WY_OP_PLIST, 2, 2), enc2b(0, 0),    // L2 <- (11 . (22 . nil))
            enc1(WY_OP_I8, 0, 3),
            enc2a(WY_OP_GETIDX, 0, 4), enc2b(2, 3),   // L4 <- L2[0] = 11 (a0=4, a1=2, a2=3)
            enc1(WY_OP_I8, 99, 5),
            enc2a(WY_OP_SETIDX, 0, 2), enc2b(3, 5),   // L2[0] <- 99 (a0=2, a1=3, a2=5)
            enc2a(WY_OP_GETIDX, 0, 6), enc2b(2, 3),   // L6 <- L2[0] = 99
            enc1(WY_OP_RETURN, 2, 4),
        };
        wy_value out[2];
        REQUIRE_EQ(run_synthetic(ctx.get_context_ptr(), code, std::size(code), 7, 0, WY_NULL, 0, out, 2), WY_ERR_NONE);
        CHECK_EQ(out[0].data.word, 11);
        CHECK_EQ(out[1].data.word, 99);
    }

    TEST_CASE("iteration over list") {
        test_fiber_fixture ctx;
        const wy_u32 code[] = {
            enc1(WY_OP_I8, 5, 0),
            enc1(WY_OP_I8, 6, 1),
            enc2a(WY_OP_LIST, 2, 2), enc2b(0, 0),     // L2 <- [5, 6]
            enc2a(WY_OP_ITER, 0, 3), enc2b(2, 0),     // L3 <- iter(L2) (a0=3, a1=2)
            enc2a(WY_OP_ITNEXT, 0, 4), enc2b(3, 3),   // L4 <- next(L3), if exhausted jump +3 (a0=4, a1=3)
            enc2a(WY_OP_ITNEXT, 0, 5), enc2b(3, 1),   // L5 <- next(L3), if exhausted jump +1
            enc1(WY_OP_JMP, 0, 1),                    // skip exhausted case
            enc1(WY_OP_I8, -1, 4),                    // (not reached)
            enc1(WY_OP_RETURN, 2, 4),                 // return L4, L5
        };
        wy_value out[2];
        REQUIRE_EQ(run_synthetic(ctx.get_context_ptr(), code, std::size(code), 6, 0, WY_NULL, 0, out, 2), WY_ERR_NONE);
        CHECK_EQ(out[0].data.word, 5);
        CHECK_EQ(out[1].data.word, 6);
    }

    TEST_CASE("unpack list success") {
        test_fiber_fixture ctx;
        const wy_u32 code[] = {
            enc1(WY_OP_I8, 7, 0),
            enc1(WY_OP_I8, 8, 1),
            enc2a(WY_OP_LIST, 2, 2), enc2b(0, 0),     // L2 <- [7, 8]
            enc2a(WY_OP_UNPACK, 2, 3), enc2b(2, 0),   // L3, L4 <- unpack(L2) (a0=3, f=2, a1=2)
            enc1(WY_OP_RETURN, 2, 3),
        };
        wy_value out[2];
        REQUIRE_EQ(run_synthetic(ctx.get_context_ptr(), code, std::size(code), 5, 0, WY_NULL, 0, out, 2), WY_ERR_NONE);
        CHECK_EQ(out[0].data.word, 7);
        CHECK_EQ(out[1].data.word, 8);
    }

    TEST_CASE("unpack list mismatch") {
        test_fiber_fixture ctx;
        const wy_u32 code[] = {
            enc1(WY_OP_I8, 7, 0),
            enc2a(WY_OP_LIST, 1, 1), enc2b(0, 0),     // L1 <- [7]
            enc2a(WY_OP_UNPACK, 2, 2), enc2b(1, 0),   // L2, L3 <- unpack(L1) (a0=2, f=2, a1=1)
            enc1(WY_OP_RETURN, 2, 2),
        };
        wy_value out[2];
        REQUIRE_EQ(run_synthetic(ctx.get_context_ptr(), code, std::size(code), 4, 0, WY_NULL, 0, out, 2), WY_ERR_NONE);
        CHECK(wy_value_is_error(out[0]));
        CHECK(wy_value_is_error(out[1]));
    }

    TEST_CASE("string getidx decodes a UTF-8 codepoint, not a raw byte") {
        test_fiber_fixture ctx;
        wy_string* str = WY_NULL;
        // "h\xC3\xA9llo" = "héllo": 5 codepoints, 6 bytes ('é' is 2 bytes).
        REQUIRE_EQ(wy_string_new(ctx.get_context_ptr(), "h\xC3\xA9llo", 6, &str), WY_ERR_NONE);
        const wy_value statics[] = { wy_value_object(WY_TYPE_TAG_STR, (wy_object*) str) };
        const wy_u32 code[] = {
            enc1(WY_OP_LCONST, 0, 0),                 // L0 <- "héllo"
            enc1(WY_OP_I8, 1, 1),
            enc2a(WY_OP_GETIDX, 0, 2), enc2b(0, 1),   // L2 <- L0[1] = codepoint of 'é' (0xE9)
            enc1(WY_OP_I8, 4, 3),
            enc2a(WY_OP_GETIDX, 0, 3), enc2b(0, 3),   // L3 <- L0[4] = 'o' (in-range only by codepoint count)
            enc1(WY_OP_RETURN, 2, 2),
        };
        wy_value out[2];
        REQUIRE_EQ(run_synthetic(ctx.get_context_ptr(), code, std::size(code), 5, 0, WY_NULL, 0, out, 2,
            statics, std::size(statics)), WY_ERR_NONE);
        CHECK_EQ(out[0].type, WY_TYPE_TAG_WORD);
        CHECK_EQ(out[0].data.word, 0xE9);
        CHECK_EQ(out[1].data.word, 'o');
    }

    TEST_CASE("string iter/unpack yield one-character substrings, not byte codes") {
        test_fiber_fixture ctx;
        wy_string* str = WY_NULL;
        REQUIRE_EQ(wy_string_new(ctx.get_context_ptr(), "h\xC3\xA9llo", 6, &str), WY_ERR_NONE);
        const wy_value statics[] = { wy_value_object(WY_TYPE_TAG_STR, (wy_object*) str) };
        const wy_u32 code[] = {
            enc1(WY_OP_LCONST, 0, 0),                 // L0 <- "héllo"
            enc2a(WY_OP_ITER, 0, 1), enc2b(0, 0),     // L1 <- iter(L0)
            enc2a(WY_OP_ITNEXT, 0, 2), enc2b(1, 1),   // L2 <- next(L1) = "h"
            enc2a(WY_OP_ITNEXT, 0, 3), enc2b(1, 1),   // L3 <- next(L1) = "é" (2-byte codepoint, one item)
            enc2a(WY_OP_UNPACK, 5, 4), enc2b(0, 0),   // L4..L8 <- unpack(L0) into 5 one-char strings
            enc1(WY_OP_RETURN, 3, 2),                 // return L2, L3, L4
        };
        wy_value out[3];
        REQUIRE_EQ(run_synthetic(ctx.get_context_ptr(), code, std::size(code), 9, 0, WY_NULL, 0, out, 3,
            statics, std::size(statics)), WY_ERR_NONE);
        REQUIRE_EQ(out[0].type, WY_TYPE_TAG_STR);
        CHECK_EQ(std::string(out[0].data.str->str, out[0].data.str->len), "h");
        REQUIRE_EQ(out[1].type, WY_TYPE_TAG_STR);
        CHECK_EQ(std::string(out[1].data.str->str, out[1].data.str->len), "\xC3\xA9");
        REQUIRE_EQ(out[2].type, WY_TYPE_TAG_STR);
        CHECK_EQ(std::string(out[2].data.str->str, out[2].data.str->len), "h");
    }

    TEST_CASE("in operator") {
        test_fiber_fixture ctx;
        const wy_u32 code[] = {
            enc1(WY_OP_I8, 5, 0),
            enc1(WY_OP_I8, 6, 1),
            enc2a(WY_OP_LIST, 2, 2), enc2b(0, 0),     // L2 <- [5, 6]
            enc2a(WY_OP_IN, 0, 3), enc2b(0, 2),       // L3 <- 5 in L2 (true) (a0=3, a1=0, a2=2)
            enc1(WY_OP_I8, 7, 0),
            enc2a(WY_OP_IN, 0, 4), enc2b(0, 2),       // L4 <- 7 in L2 (false)
            enc1(WY_OP_RETURN, 2, 3),
        };
        wy_value out[2];
        REQUIRE_EQ(run_synthetic(ctx.get_context_ptr(), code, std::size(code), 5, 0, WY_NULL, 0, out, 2), WY_ERR_NONE);
        CHECK_EQ(out[0].data.flag, true);
        CHECK_EQ(out[1].data.flag, false);
    }

    TEST_CASE("jerr/jnerr branch correctly on an error value and a non-error value") {
        test_fiber_fixture ctx;
        //   0: lunset L0 (an error value)
        //   1: jerr L0, +2   -> taken, skip to word 4
        //   2: i8 L1 <- 1    (not reached)
        //   3: jmp +1
        //   4: i8 L1 <- 2
        //   5: i8 L3 <- 5    (a non-error value)
        //   6: jnerr L3, +2  -> taken, skip to word 9
        //   7: i8 L2 <- 1    (not reached)
        //   8: jmp +1
        //   9: i8 L2 <- 2
        //  10: return L1, L2
        const wy_u32 code[] = {
            enc1(WY_OP_LUNSET, 0, 0),
            enc1(WY_OP_JERR, 0, 2),
            enc1(WY_OP_I8, 1, 1),
            enc1(WY_OP_JMP, 0, 1),
            enc1(WY_OP_I8, 2, 1),
            enc1(WY_OP_I8, 5, 3),
            enc1(WY_OP_JNERR, 3, 2),
            enc1(WY_OP_I8, 1, 2),
            enc1(WY_OP_JMP, 0, 1),
            enc1(WY_OP_I8, 2, 2),
            enc1(WY_OP_RETURN, 2, 1),
        };
        wy_value out[2];
        REQUIRE_EQ(run_synthetic(ctx.get_context_ptr(), code, std::size(code), 4, 0, WY_NULL, 0, out, 2), WY_ERR_NONE);
        CHECK_EQ(out[0].data.word, 2);
        CHECK_EQ(out[1].data.word, 2);
    }

    TEST_CASE("error(\"msg\") constructs a value is_error accepts, and `is` sees the class chain") {
        test_fiber_fixture ctx;
        wy_context* c = ctx.get_context_ptr();
        wy_module* builtins = WY_NULL;
        REQUIRE_EQ(wy_builtins_new(c, &builtins), WY_ERR_NONE);

        wy_symbol error_sym, oom_sym;
        REQUIRE_EQ(wy_context_intern(c, "error", 5, &error_sym), WY_ERR_NONE);
        REQUIRE_EQ(wy_context_intern(c, "OutOfMemory", 11, &oom_sym), WY_ERR_NONE);
        wy_uword error_slot = wy_slot_dict_get(&builtins->exports, error_sym);
        wy_uword oom_slot = wy_slot_dict_get(&builtins->exports, oom_sym);
        REQUIRE_NE(error_slot, WY_SLOT_INVALID);
        REQUIRE_NE(oom_slot, WY_SLOT_INVALID);
        wy_value error_class = builtins->globals[error_slot];
        wy_value oom_class = builtins->globals[oom_slot];
        REQUIRE_EQ(error_class.type, WY_TYPE_TAG_CLASS);
        CHECK_EQ((wy_class*) error_class.data.gc_object, c->error_class);

        wy_string* msg = WY_NULL;
        REQUIRE_EQ(wy_string_new(c, "boom", 4, &msg), WY_ERR_NONE);
        const wy_value statics[] = {
            error_class,
            wy_value_object(WY_TYPE_TAG_STR, (wy_object*) msg),
            oom_class,
        };
        const wy_u32 code[] = {
            enc1(WY_OP_LCONST, 0, 0),                 // L0 <- error class
            enc1(WY_OP_LCONST, 1, 1),                 // L1 <- "boom"
            enc1(WY_OP_LCONST, 2, 2),                 // L2 <- OutOfMemory class
            enc1(WY_OP_MOVE, 0, 3),                   // L3 <- L0 (call window: L3=callee, L4=arg)
            enc1(WY_OP_MOVE, 1, 4),                   // L4 <- L1
            enc2a(WY_OP_CALL, 1, 3), enc2b(1, 0),     // L3 <- L3(L4) (f=argc=1, a0=base=3, a1=nres=1)
            enc1(WY_OP_MOVE, 3, 4),                   // L4 <- L3 (save the result out of the call window)
            enc2a(WY_OP_IS, 0, 5), enc2b(4, 0),       // L5 <- L4 is L0 (the error class)
            enc2a(WY_OP_IS, 0, 6), enc2b(4, 2),       // L6 <- L4 is OutOfMemory (false)
            enc1(WY_OP_RETURN, 3, 4),                 // return L4, L5, L6
        };
        wy_value out[3];
        REQUIRE_EQ(run_synthetic(c, code, std::size(code), 7, 0, WY_NULL, 0, out, 3,
            statics, std::size(statics)), WY_ERR_NONE);
        CHECK(wy_value_is_error(out[0]));
        CHECK_EQ(((wy_error_obj*) out[0].data.gc_object)->cls, c->error_class);
        CHECK_EQ(std::string(((wy_error_obj*) out[0].data.gc_object)->what->str,
            ((wy_error_obj*) out[0].data.gc_object)->what->len), "boom");
        CHECK_EQ(out[1].data.flag, true);
        CHECK_EQ(out[2].data.flag, false);
    }

    TEST_CASE("new_primitive") {
        test_fiber_fixture ctx;
        const wy_u32 code[] = {
            enc2a(WY_OP_NEW_PRIMITIVE, (wy_u8) WY_TYPE_TAG_LIST, 0), enc2b(0, 0),
            enc2a(WY_OP_NEW_PRIMITIVE, (wy_u8) WY_TYPE_TAG_TABLE, 1), enc2b(0, 0),
            enc1(WY_OP_RETURN, 2, 0),
        };
        wy_value out[2];
        REQUIRE_EQ(run_synthetic(ctx.get_context_ptr(), code, std::size(code), 2, 0, WY_NULL, 0, out, 2), WY_ERR_NONE);
        CHECK_EQ(out[0].type, WY_TYPE_TAG_LIST);
        CHECK_EQ(out[1].type, WY_TYPE_TAG_TABLE);
    }
}

TEST_SUITE("vm defers and fault unwinding") {
    TEST_CASE("return drains defers most recent first, by mode") {
        // fn#0(p): arms d1(mode 0), d3(mode 1), d4(mode 2), d2(mode 0); returns p.
        // d1: g3 <- 1, g0 <- 1   d2: g3 <- 2   d3: g1 <- 1   d4: g2 <- 1
        const wy_u32 code[] = {
            /* 0 fn#0 */
            enc2a(WY_OP_CLOSURE, 0, 0), enc2b(1, 0),
            enc2a(WY_OP_DEFER_REG, 0, 0), enc2b(0, 0),
            enc2a(WY_OP_CLOSURE, 0, 0), enc2b(3, 0),
            enc2a(WY_OP_DEFER_REG, 1, 0), enc2b(0, 0),
            enc2a(WY_OP_CLOSURE, 0, 0), enc2b(4, 0),
            enc2a(WY_OP_DEFER_REG, 2, 0), enc2b(0, 0),
            enc2a(WY_OP_CLOSURE, 0, 0), enc2b(2, 0),
            enc2a(WY_OP_DEFER_REG, 0, 0), enc2b(0, 0),
            enc1(WY_OP_RETURN, 1, 0x8000),
            /* 17 d1 */
            enc1(WY_OP_I8, 1, 0), enc1(WY_OP_GSET, 0, 3), enc1(WY_OP_GSET, 0, 0), enc1(WY_OP_RETURN, 0, 0),
            /* 21 d2 */
            enc1(WY_OP_I8, 2, 0), enc1(WY_OP_GSET, 0, 3), enc1(WY_OP_RETURN, 0, 0),
            /* 24 d3 */
            enc1(WY_OP_I8, 1, 0), enc1(WY_OP_GSET, 0, 1), enc1(WY_OP_RETURN, 0, 0),
            /* 27 d4 */
            enc1(WY_OP_I8, 1, 0), enc1(WY_OP_GSET, 0, 2), enc1(WY_OP_RETURN, 0, 0),
        };
        for (int variant = 0; variant < 3; variant++) {
            test_fiber_fixture ctx;
            wy_context* context = ctx.get_context_ptr();
            wy_module* module = make_code_module(context, code, std::size(code), {
                proto_at(context, "f", 0, 1, 1), proto_at(context, "d1", 17, 1),
                proto_at(context, "d2", 21, 1), proto_at(context, "d3", 24, 1), proto_at(context, "d4", 27, 1) });
            give_globals(context, module, 4);

            wy_value arg = variant == 0 ? wy_value_word(5) : variant == 1 ? wy_value_nil() : wy_value_unset();
            wy_value out[1];
            CAPTURE(variant);
            REQUIRE_EQ(run_fn0(context, module, &arg, 1, out, 1), WY_ERR_NONE);
            CHECK_EQ(out[0].type, arg.type);
            // d2 armed last runs first, then d1 overwrites g3: order is newest first.
            CHECK_EQ(module->globals[3].data.word, 1);
            CHECK_EQ(module->globals[0].data.word, 1);
            // mode 1 runs only for an error result (Unset counts, as in is_error)
            CHECK_EQ(wy_value_is_unset(module->globals[1]), variant != 2);
            // mode 2 runs for an error or nil result
            CHECK_EQ(wy_value_is_unset(module->globals[2]), variant == 0);
            CHECK_EQ(ctx.get_fiber_ptr()->current_frame->kind, WY_FRAME_NATIVE);
        }
    }

    TEST_CASE("a trap two frames deep runs the inner then the outer on-error defer, then faults to C") {
        // fn#0 outer: arms d_outer (mode 1), calls fn#1.
        // fn#1 inner: arms d_inner (mode 2), traps.
        // d_inner: g1 <- 1.  d_outer: g0 <- g1 + 10 (gget faults if d_inner had not run).
        const wy_u32 code[] = {
            /* 0 outer */
            enc2a(WY_OP_CLOSURE, 0, 0), enc2b(3, 0),
            enc2a(WY_OP_DEFER_REG, 1, 0), enc2b(0, 0),
            enc2a(WY_OP_CLOSURE, 0, 1), enc2b(1, 0),
            enc2a(WY_OP_CALL, 0, 1), enc2b(1, 0),
            enc1(WY_OP_RETURN, 1, 1),
            /* 9 inner */
            enc2a(WY_OP_CLOSURE, 0, 0), enc2b(2, 0),
            enc2a(WY_OP_DEFER_REG, 2, 0), enc2b(0, 0),
            enc1(WY_OP_TRAP, 0, 0),
            /* 14 d_inner */
            enc1(WY_OP_I8, 1, 0), enc1(WY_OP_GSET, 0, 1), enc1(WY_OP_RETURN, 0, 0),
            /* 17 d_outer */
            enc1(WY_OP_GGET, 0, 1), enc1(WY_OP_I8, 10, 1),
            enc2a(WY_OP_ADD, 0, 0), enc2b(0, 1),
            enc1(WY_OP_GSET, 0, 0), enc1(WY_OP_RETURN, 0, 0),
        };
        for (wy_uword threshold : { (wy_uword) -1, (wy_uword) 0 }) {
            test_fiber_fixture ctx;
            wy_context* context = ctx.get_context_ptr();
            if (threshold == 0) { context->gc_threshold = 0; context->gc_growth_factor = 0; }
            wy_module* module = make_code_module(context, code, std::size(code), {
                proto_at(context, "outer", 0, 2), proto_at(context, "inner", 9, 1),
                proto_at(context, "d_inner", 14, 1), proto_at(context, "d_outer", 17, 2) });
            give_globals(context, module, 2);
            wy_value* base_top = ctx.get_fiber_ptr()->value_stack.top;

            wy_value out[1];
            CAPTURE(threshold);
            REQUIRE_EQ(run_fn0(context, module, WY_NULL, 0, out, 1), WY_ERR_FAULT);
            CHECK_EQ(module->globals[0].data.word, 11);
            CHECK_EQ(fault_message(ctx.get_fiber_ptr()),
                "unreachable code reached - or a function body the compiler could not lower");
            CHECK_EQ(ctx.get_fiber_ptr()->current_frame->kind, WY_FRAME_NATIVE);
            CHECK_EQ(ctx.get_fiber_ptr()->value_stack.top, base_top);
        }
    }

    TEST_CASE("defer drains under WY_PHASE_FAILING do not recurse in C: 121 frames unwind through their defers") {
        // fn#0: g0 <- 0; g1 <- rec; rec(120)
        // fn#1 rec(n): arm count (mode 0); if !n trap; rec(n - 1)
        // fn#2 count: g0 <- g0 + 1
        const wy_u32 code[] = {
            /* 0 */
            enc1(WY_OP_I8, 0, 2), enc1(WY_OP_GSET, 2, 0),
            enc2a(WY_OP_CLOSURE, 0, 0), enc2b(1, 0),
            enc1(WY_OP_GSET, 0, 1),
            enc1(WY_OP_I8, 120, 1),
            enc2a(WY_OP_CALL, 1, 0), enc2b(1, 0),
            enc1(WY_OP_RETURN, 1, 0),
            /* 9 rec */
            enc2a(WY_OP_CLOSURE, 0, 0), enc2b(2, 0),
            enc2a(WY_OP_DEFER_REG, 0, 0), enc2b(0, 0),
            enc1(WY_OP_JT, 0x80, 1),
            enc1(WY_OP_TRAP, 1, 0),
            enc1(WY_OP_GGET, 0, 1), enc1(WY_OP_I8, 1, 2),
            enc2a(WY_OP_SUB, 0, 1), enc2b(0x8000, 2),
            enc2a(WY_OP_CALL, 1, 0), enc2b(1, 0),
            enc1(WY_OP_RETURN, 1, 0),
            /* 22 count */
            enc1(WY_OP_GGET, 0, 0), enc1(WY_OP_I8, 1, 1),
            enc2a(WY_OP_ADD, 0, 0), enc2b(0, 1),
            enc1(WY_OP_GSET, 0, 0), enc1(WY_OP_RETURN, 0, 0),
        };
        for (wy_uword threshold : { (wy_uword) -1, (wy_uword) 0 }) {
            test_fiber_fixture ctx;
            wy_context* context = ctx.get_context_ptr();
            if (threshold == 0) { context->gc_threshold = 0; context->gc_growth_factor = 0; }
            wy_module* module = make_code_module(context, code, std::size(code), {
                proto_at(context, "main", 0, 3), proto_at(context, "rec", 9, 3, 1), proto_at(context, "count", 22, 2) });
            give_globals(context, module, 2);

            wy_value out[1];
            CAPTURE(threshold);
            REQUIRE_EQ(run_fn0(context, module, WY_NULL, 0, out, 1), WY_ERR_FAULT);
            CHECK_EQ(module->globals[0].data.word, 121);
            CHECK_EQ(fault_message(ctx.get_fiber_ptr()), "debugger break");
            CHECK_EQ(ctx.get_fiber_ptr()->current_frame->kind, WY_FRAME_NATIVE);
        }
    }

    TEST_CASE("a defer that faults during return fails the frame; the rest run as on-error") {
        // fn#0: arm d_mark (mode 1), arm d_trap (mode 0), return 5.
        const wy_u32 code[] = {
            /* 0 */
            enc2a(WY_OP_CLOSURE, 0, 0), enc2b(1, 0),
            enc2a(WY_OP_DEFER_REG, 1, 0), enc2b(0, 0),
            enc2a(WY_OP_CLOSURE, 0, 0), enc2b(2, 0),
            enc2a(WY_OP_DEFER_REG, 0, 0), enc2b(0, 0),
            enc1(WY_OP_I8, 5, 1),
            enc1(WY_OP_RETURN, 1, 1),
            /* 10 d_mark */
            enc1(WY_OP_I8, 1, 0), enc1(WY_OP_GSET, 0, 0), enc1(WY_OP_RETURN, 0, 0),
            /* 13 d_trap */
            enc1(WY_OP_TRAP, 1, 0),
        };
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();
        wy_module* module = make_code_module(context, code, std::size(code), {
            proto_at(context, "f", 0, 2), proto_at(context, "d_mark", 10, 1), proto_at(context, "d_trap", 13, 0) });
        give_globals(context, module, 1);

        wy_value out[1] = { wy_value_word(99) };
        REQUIRE_EQ(run_fn0(context, module, WY_NULL, 0, out, 1), WY_ERR_FAULT);
        CHECK_EQ(module->globals[0].data.word, 1);
        CHECK_EQ(fault_message(ctx.get_fiber_ptr()), "debugger break");
        CHECK_EQ(out[0].data.word, 99);  // the return window was never delivered
    }

    TEST_CASE("defer_reg on a non-function faults") {
        test_fiber_fixture ctx;
        const wy_u32 code[] = {
            enc1(WY_OP_I8, 1, 0),
            enc2a(WY_OP_DEFER_REG, 0, 0), enc2b(0, 0),
            enc1(WY_OP_RETURN, 0, 0),
        };
        wy_value out[1];
        REQUIRE_EQ(run_synthetic(ctx.get_context_ptr(), code, std::size(code), 1, 0, WY_NULL, 0, out, 0), WY_ERR_FAULT);
        CHECK_EQ(fault_message(ctx.get_fiber_ptr()), "defer_reg: not a function");
    }
}

TEST_SUITE("vm construct-on-call and message identities") {
    // main: gget G0 (the class) -> L0; call L0() nres=1; return L0.
    const wy_u32 construct_code[] = {
        /* 0 main */
        enc1(WY_OP_GGET, 0, 0),
        enc2a(WY_OP_CALL, 0, 0), enc2b(1, 0),
        enc1(WY_OP_RETURN, 1, 0),
        /* 4 init(): this = P0; setattr(this, symbols[0], statics[0]); return */
        enc1(WY_OP_LCONST, 1, 0),
        enc2a(WY_OP_SETATTR, 0, 0x8000), enc2b(0, 1),
        enc1(WY_OP_RETURN, 0, 0),
    };

    TEST_CASE("a class with a zero-arg init that sets a slot: calling it produces an instance with that slot set") {
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();

        wy_symbol x_sym = "x";
        const wy_value statics[] = { wy_value_word(42) };
        wy_module* module = make_code_module(context, construct_code, std::size(construct_code),
            { proto_at(context, "main", 0, 1, 0), proto_at(context, "init!", 4, 2, 0) },
            statics, 1, &x_sym, 1);
        give_globals(context, module, 1);

        wy_class* cls = WY_NULL;
        REQUIRE_EQ(wy_class_new(context, &cls), WY_ERR_NONE);
        cls->module = module;
        cls->slots = (wy_class_slot*) wy_context_gc_alloc(context, sizeof(wy_class_slot));
        cls->slots[0] = wy_class_slot { x_sym, wy_value_unset(), wy_value_unset(), wy_value_unset() };
        cls->slot_count = 1;
        wy_function* init_fn = WY_NULL;
        REQUIRE_EQ(wy_function_new(context, module, &module->functions[1], WY_NULL, 0, &init_fn), WY_ERR_NONE);
        cls->init = wy_value_object(WY_TYPE_TAG_FUNCTION, (wy_object*) init_fn);
        module->globals[0] = wy_value_object(WY_TYPE_TAG_CLASS, (wy_object*) cls);

        wy_value out[1];
        REQUIRE_EQ(run_fn0(context, module, WY_NULL, 0, out, 1), WY_ERR_NONE);
        REQUIRE_EQ(out[0].type, WY_TYPE_TAG_INSTANCE);
        auto* inst = (wy_instance*) out[0].data.gc_object;
        CHECK_EQ(inst->cls, cls);
        CHECK_EQ(inst->slots[0].data.word, 42);
    }

    TEST_CASE("a class with no init: calling it produces an instance with defaults untouched, no fault") {
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();

        wy_symbol x_sym = "x";
        // Only functions[0] (main) is used; there is no init function at all.
        wy_module* module = make_code_module(context,
            construct_code, 4 /* just the main body, words 0..3 */,
            { proto_at(context, "main", 0, 1, 0) }, WY_NULL, 0, &x_sym, 1);
        give_globals(context, module, 1);

        wy_class* cls = WY_NULL;
        REQUIRE_EQ(wy_class_new(context, &cls), WY_ERR_NONE);
        cls->module = module;
        cls->slots = (wy_class_slot*) wy_context_gc_alloc(context, sizeof(wy_class_slot));
        cls->slots[0] = wy_class_slot { x_sym, wy_value_word(7), wy_value_unset(), wy_value_unset() };
        cls->slot_count = 1;
        // cls->init stays Unset (wy_class_new's default).
        module->globals[0] = wy_value_object(WY_TYPE_TAG_CLASS, (wy_object*) cls);

        wy_value out[1];
        REQUIRE_EQ(run_fn0(context, module, WY_NULL, 0, out, 1), WY_ERR_NONE);
        REQUIRE_EQ(out[0].type, WY_TYPE_TAG_INSTANCE);
        auto* inst = (wy_instance*) out[0].data.gc_object;
        CHECK_EQ(inst->cls, cls);
        CHECK_EQ(inst->slots[0].data.word, 7);  // untouched default, not clobbered
    }

    TEST_CASE("a class with no init faults if called with an argument") {
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();
        // main: gget G0 -> L0; call L0(L1=3) nres=1; return L0.
        const wy_u32 code[] = {
            enc1(WY_OP_GGET, 0, 0),
            enc1(WY_OP_I8, 3, 1),
            enc2a(WY_OP_CALL, 1, 0), enc2b(1, 0),
            enc1(WY_OP_RETURN, 1, 0),
        };
        wy_module* module = make_code_module(context, code, std::size(code),
            { proto_at(context, "main", 0, 2, 0) });
        give_globals(context, module, 1);

        wy_class* cls = WY_NULL;
        REQUIRE_EQ(wy_class_new(context, &cls), WY_ERR_NONE);
        module->globals[0] = wy_value_object(WY_TYPE_TAG_CLASS, (wy_object*) cls);

        wy_value out[1] = { wy_value_word(99) };
        REQUIRE_EQ(run_fn0(context, module, WY_NULL, 0, out, 1), WY_ERR_FAULT);
        CHECK(fault_message(ctx.get_fiber_ptr()).find("no applicable 'init'") != std::string::npos);
    }

    TEST_CASE("reg_msg registers an overload, resolved and cached through module->message_table") {
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();

        wy_symbol greet_sym = "greet";
        // main: closure L0 <- functions[1] (0 caps); tuple L1 <- () (0 types);
        // reg_msg messages[0] <- (L1, L0); return.
        const wy_u32 code[] = {
            /* 0 main */
            enc2a(WY_OP_CLOSURE, 0, 0), enc2b(1, 0),
            enc2a(WY_OP_TUPLE, 0, 1), enc2b(0, 0),
            enc2a(WY_OP_REG_MSG, 0, 0), enc2b(0, 1),
            enc1(WY_OP_RETURN, 0, 0),
            /* 5 greeted() */
            enc1(WY_OP_RETURN, 0, 0),
        };
        wy_module* module = make_code_module(context, code, std::size(code),
            { proto_at(context, "main", 0, 2, 0), proto_at(context, "greeted!", 5, 0, 0) });

        wy_message_ref ref {};
        ref.path_len = 1;
        ref.path = (wy_u16*) wy_context_gc_alloc(context, sizeof(wy_u16));
        ref.path[0] = 0;
        ref.bound = WY_NULL;
        module->messages = (wy_message_ref*) wy_context_gc_alloc(context, sizeof(wy_message_ref));
        module->messages[0] = ref;
        module->message_count = 1;
        module->symbols = (wy_symbol*) wy_context_gc_alloc(context, sizeof(wy_symbol));
        module->symbols[0] = greet_sym;
        module->symbol_count = 1;

        wy_value out[1];
        REQUIRE_EQ(run_fn0(context, module, WY_NULL, 0, out, 0), WY_ERR_NONE);

        wy_message* msg = WY_NULL;
        REQUIRE_EQ(wy_module_resolve_message_f(context, module, 0, &msg), WY_ERR_NONE);
        REQUIRE_NE(msg, WY_NULL);
        CHECK_EQ(msg->name, greet_sym);
        REQUIRE_EQ(msg->overload_count, 1u);
        CHECK_EQ(msg->overloads[0].arity, 0);
        CHECK_EQ(msg->overloads[0].body.type, WY_TYPE_TAG_FUNCTION);
        CHECK_EQ(((wy_function*) msg->overloads[0].body.data.gc_object)->proto, &module->functions[1]);
        CHECK_EQ(module->messages[0].bound, msg);  // bound on first read, cached

        // A second registration on the same message identity appends, it
        // doesn't replace.
        wy_value another_body = wy_value_object(WY_TYPE_TAG_FUNCTION, msg->overloads[0].body.data.gc_object);
        REQUIRE_EQ(wy_message_add_overload_f(context, msg, 0, WY_NULL, another_body), WY_ERR_NONE);
        CHECK_EQ(msg->overload_count, 2u);
    }
}
