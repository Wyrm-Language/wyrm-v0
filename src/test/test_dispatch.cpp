#include <doctest/doctest.h>

#include <algorithm>
#include <vector>

#include "vm_internal.h"

#include <wyrm.h>
#include <wyrm/function.h>
#include <wyrm/module.h>
#include <wyrm/opcode.h>
#include <wyrm/vm.h>
#include <test_common/test_context_fixture.h>
#include <test_common/test_fiber_fixture.h>

namespace {

constexpr wy_u32 enc1(wy_u8 op, wy_u8 f, wy_u16 a0)
{
    return (wy_u32(a0) << 16) | (wy_u32(f) << 8) | wy_u32(op);
}
constexpr wy_u32 enc2a(wy_u8 op, wy_u8 f, wy_u16 a0) { return enc1(op, f, a0); }
constexpr wy_u32 enc2b(wy_u16 a1, wy_u16 a2) { return (wy_u32(a1) << 16) | wy_u32(a2); }

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

wy_function_proto proto_msg(const char* name, wy_u32 offset, wy_u16 nlocals, wy_u16 ndispatch)
{
    wy_function_proto proto = {};
    proto.name = name;
    proto.code_offset = offset;
    proto.nlocals = nlocals;
    proto.nparams = 0;
    proto.ndispatch = ndispatch;
    proto.flags = WY_FN_MESSAGE;
    return proto;
}

wy_error run_fn0(wy_context* ctx, wy_module* module, wy_value* out, wy_uword nres)
{
    wy_uword module_id = WY_IDX_INVALID;
    REQUIRE_EQ(wy_context_module_register(ctx, module, &module_id), WY_ERR_NONE);
    wy_function* fn = WY_NULL;
    REQUIRE_EQ(wy_function_new(ctx, module, &module->functions[0], WY_NULL, 0, &fn), WY_ERR_NONE);
    return wy_vm_call_sync(ctx, wy_value_object(WY_TYPE_TAG_FUNCTION, (wy_object*) fn), WY_NULL, 0, out, nres);
}

} // namespace

TEST_SUITE("dispatch resolve") {
    TEST_CASE("multi-dispatch ranking: mixed-specificity receiver pair picks the more specific overload") {
        test_context_fixture fix;
        wy_context* ctx = fix.get_context_ptr();

        wy_class *a = WY_NULL, *a1 = WY_NULL, *b = WY_NULL, *b1 = WY_NULL;
        REQUIRE_EQ(wy_class_new(ctx, &a), WY_ERR_NONE);
        REQUIRE_EQ(wy_class_new(ctx, &a1), WY_ERR_NONE);
        a1->super = a; a1->depth = 1;
        REQUIRE_EQ(wy_class_new(ctx, &b), WY_ERR_NONE);
        REQUIRE_EQ(wy_class_new(ctx, &b1), WY_ERR_NONE);
        b1->super = b; b1->depth = 1;

        wy_instance *recv_a1 = WY_NULL, *recv_b1 = WY_NULL;
        REQUIRE_EQ(wy_instance_new_f(ctx, a1, &recv_a1), WY_ERR_NONE);
        REQUIRE_EQ(wy_instance_new_f(ctx, b1, &recv_b1), WY_ERR_NONE);

        wy_message* msg = WY_NULL;
        REQUIRE_EQ(wy_message_new_f(ctx, "combine", WY_NULL, &msg), WY_ERR_NONE);

        // ov1: (A, B) -> dist (1, 1) against (A1, B1)
        wy_value ov1_types[2] = { wy_value_object(WY_TYPE_TAG_CLASS, (wy_object*) a), wy_value_object(WY_TYPE_TAG_CLASS, (wy_object*) b) };
        REQUIRE_EQ(wy_message_add_overload_f(ctx, msg, 2, ov1_types, wy_value_word(1)), WY_ERR_NONE);
        // ov2: (A1, B) -> dist (0, 1) - most specific at position 0
        wy_value ov2_types[2] = { wy_value_object(WY_TYPE_TAG_CLASS, (wy_object*) a1), wy_value_object(WY_TYPE_TAG_CLASS, (wy_object*) b) };
        REQUIRE_EQ(wy_message_add_overload_f(ctx, msg, 2, ov2_types, wy_value_word(2)), WY_ERR_NONE);
        // ov3: (A, B1) -> dist (1, 0) - most specific at position 1, but position 0 loses to ov2
        wy_value ov3_types[2] = { wy_value_object(WY_TYPE_TAG_CLASS, (wy_object*) a), wy_value_object(WY_TYPE_TAG_CLASS, (wy_object*) b1) };
        REQUIRE_EQ(wy_message_add_overload_f(ctx, msg, 2, ov3_types, wy_value_word(3)), WY_ERR_NONE);

        wy_value receivers[2] = {
            wy_value_object(WY_TYPE_TAG_INSTANCE, (wy_object*) recv_a1),
            wy_value_object(WY_TYPE_TAG_INSTANCE, (wy_object*) recv_b1),
        };
        const wy_overload* chosen = WY_NULL;
        char fault_msg[256];
        REQUIRE_EQ(wy_dispatch_resolve_f(msg, receivers, 2, WY_NULL, &chosen, fault_msg, sizeof(fault_msg)), WY_ERR_NONE);
        REQUIRE_NE(chosen, WY_NULL);
        CHECK_EQ(chosen->body.data.word, 2);  // ov2: (0,1) beats ov3's (1,0) at position 0

        // A genuine tie (another (A1, B) overload) faults ambiguous.
        REQUIRE_EQ(wy_message_add_overload_f(ctx, msg, 2, ov2_types, wy_value_word(4)), WY_ERR_NONE);
        wy_error err = wy_dispatch_resolve_f(msg, receivers, 2, WY_NULL, &chosen, fault_msg, sizeof(fault_msg));
        CHECK_EQ(err, WY_ERR_AMBIGUOUS);
    }

    TEST_CASE("no overload of matching arity faults unbound") {
        test_context_fixture fix;
        wy_context* ctx = fix.get_context_ptr();

        wy_class* a = WY_NULL;
        REQUIRE_EQ(wy_class_new(ctx, &a), WY_ERR_NONE);
        wy_instance* inst = WY_NULL;
        REQUIRE_EQ(wy_instance_new_f(ctx, a, &inst), WY_ERR_NONE);

        wy_message* msg = WY_NULL;
        REQUIRE_EQ(wy_message_new_f(ctx, "lonely", WY_NULL, &msg), WY_ERR_NONE);
        wy_value type_a = wy_value_object(WY_TYPE_TAG_CLASS, (wy_object*) a);
        REQUIRE_EQ(wy_message_add_overload_f(ctx, msg, 1, &type_a, wy_value_word(1)), WY_ERR_NONE);

        wy_value receivers[2] = {
            wy_value_object(WY_TYPE_TAG_INSTANCE, (wy_object*) inst),
            wy_value_object(WY_TYPE_TAG_INSTANCE, (wy_object*) inst),
        };
        const wy_overload* chosen = WY_NULL;
        char fault_msg[256];
        CHECK_EQ(wy_dispatch_resolve_f(msg, receivers, 2, WY_NULL, &chosen, fault_msg, sizeof(fault_msg)), WY_ERR_UNBOUND);
    }

    TEST_CASE("a wildcard overload (nil constraint) matches any receiver but loses to a specific one") {
        // Regression: wy_message_add_overload_f pads unused type slots and
        // wy_dispatch_resolve_f's wildcard check must agree on nil (not
        // Unset) as the wildcard sentinel (design_c_vm.md §7: "nil entry in
        // a type slot = wildcard") - a mismatch here would silently break
        // every message-promotion wildcard (epic 4/M4) while every
        // class-constrained overload kept working, since those never touch
        // the wildcard path at all.
        test_context_fixture fix;
        wy_context* ctx = fix.get_context_ptr();

        wy_class *a = WY_NULL, *b = WY_NULL;
        REQUIRE_EQ(wy_class_new(ctx, &a), WY_ERR_NONE);
        REQUIRE_EQ(wy_class_new(ctx, &b), WY_ERR_NONE);
        wy_instance *inst_a = WY_NULL, *inst_b = WY_NULL;
        REQUIRE_EQ(wy_instance_new_f(ctx, a, &inst_a), WY_ERR_NONE);
        REQUIRE_EQ(wy_instance_new_f(ctx, b, &inst_b), WY_ERR_NONE);

        wy_message* msg = WY_NULL;
        REQUIRE_EQ(wy_message_new_f(ctx, "describe", WY_NULL, &msg), WY_ERR_NONE);
        CHECK_FALSE(msg->has_wildcard_or_ptype_arity1);

        wy_value wildcard_type = wy_value_nil();
        REQUIRE_EQ(wy_message_add_overload_f(ctx, msg, 1, &wildcard_type, wy_value_word(1)), WY_ERR_NONE);
        CHECK(msg->has_wildcard_or_ptype_arity1);

        wy_value type_a = wy_value_object(WY_TYPE_TAG_CLASS, (wy_object*) a);
        REQUIRE_EQ(wy_message_add_overload_f(ctx, msg, 1, &type_a, wy_value_word(2)), WY_ERR_NONE);

        const wy_overload* chosen = WY_NULL;
        char fault_msg[256];
        wy_value recv_b = wy_value_object(WY_TYPE_TAG_INSTANCE, (wy_object*) inst_b);
        REQUIRE_EQ(wy_dispatch_resolve_f(msg, &recv_b, 1, WY_NULL, &chosen, fault_msg, sizeof(fault_msg)), WY_ERR_NONE);
        CHECK_EQ(chosen->body.data.word, 1);  // only the wildcard applies to B

        wy_value recv_a = wy_value_object(WY_TYPE_TAG_INSTANCE, (wy_object*) inst_a);
        REQUIRE_EQ(wy_dispatch_resolve_f(msg, &recv_a, 1, WY_NULL, &chosen, fault_msg, sizeof(fault_msg)), WY_ERR_NONE);
        CHECK_EQ(chosen->body.data.word, 2);  // the specific (A) overload beats the wildcard
    }
}

TEST_SUITE("dispatch super") {
    TEST_CASE("3-deep super chain: grandparent, parent and child each run, one hop at a time") {
        // main: gget G0 (the C instance) -> L0; msg "mark"(); return.
        // GP_mark: G1 <- 1; return.
        // P_mark:  G2 <- 1; super(); return.
        // C_mark:  G3 <- 1; super(); return.
        const wy_u32 code[] = {
            /* 0 main */
            enc1(WY_OP_GGET, 0, 0),
            enc2a(WY_OP_MSG, 0, 0), enc2b(0, 0),
            enc1(WY_OP_RETURN, 0, 0),
            /* 4 GP_mark */
            enc1(WY_OP_I8, 1, 0),
            enc1(WY_OP_GSET, 0, 1),
            enc1(WY_OP_RETURN, 0, 0),
            /* 7 P_mark */
            enc1(WY_OP_I8, 1, 0),
            enc1(WY_OP_GSET, 0, 2),
            enc2a(WY_OP_SUPER, 0, 0), enc2b(0, 0),
            enc1(WY_OP_RETURN, 0, 0),
            /* 12 C_mark */
            enc1(WY_OP_I8, 1, 0),
            enc1(WY_OP_GSET, 0, 3),
            enc2a(WY_OP_SUPER, 0, 0), enc2b(0, 0),
            enc1(WY_OP_RETURN, 0, 0),
        };

        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();

        wy_module* module = make_synthetic_module(context, 4);  // g0,g1,g2 markers, g3 spare
        module->code = code;
        module->code_len = std::size(code);

        std::vector<wy_function_proto> protos = {
            proto_msg("main", 0, 1, 0),
            proto_msg("GP.mark!", 4, 1, 1),
            proto_msg("P.mark!", 7, 1, 1),
            proto_msg("C.mark!", 12, 1, 1),
        };
        module->function_count = protos.size();
        module->functions = (wy_function_proto*) wy_context_gc_alloc(context, sizeof(wy_function_proto) * protos.size());
        std::copy(protos.begin(), protos.end(), module->functions);

        wy_symbol mark_sym = "mark";
        module->symbols = (wy_symbol*) wy_context_gc_alloc(context, sizeof(wy_symbol));
        module->symbols[0] = mark_sym;
        module->symbol_count = 1;

        wy_message* mark_msg = WY_NULL;
        REQUIRE_EQ(wy_module_message_by_name_f(context, module, mark_sym, &mark_msg), WY_ERR_NONE);

        module->messages = (wy_message_ref*) wy_context_gc_alloc(context, sizeof(wy_message_ref));
        module->messages[0].path_len = 1;
        module->messages[0].path = (wy_u16*) wy_context_gc_alloc(context, sizeof(wy_u16));
        module->messages[0].path[0] = 0;
        module->messages[0].bound = WY_NULL;
        module->message_count = 1;

        wy_class *gp = WY_NULL, *p = WY_NULL, *c = WY_NULL;
        REQUIRE_EQ(wy_class_new(context, &gp), WY_ERR_NONE);
        REQUIRE_EQ(wy_class_new(context, &p), WY_ERR_NONE);
        p->super = gp; p->depth = 1;
        REQUIRE_EQ(wy_class_new(context, &c), WY_ERR_NONE);
        c->super = p; c->depth = 2;

        auto wire = [&](wy_class* cls, wy_uword fn_idx) {
            wy_function* fn = WY_NULL;
            REQUIRE_EQ(wy_function_new(context, module, &module->functions[fn_idx], WY_NULL, 0, &fn), WY_ERR_NONE);
            wy_value body = wy_value_object(WY_TYPE_TAG_FUNCTION, (wy_object*) fn);
            cls->msg_map[0].msg = mark_msg;
            cls->msg_map[0].body = body;
            cls->msg_count = 1;
            wy_value type_cls = wy_value_object(WY_TYPE_TAG_CLASS, (wy_object*) cls);
            REQUIRE_EQ(wy_message_add_overload_f(context, mark_msg, 1, &type_cls, body), WY_ERR_NONE);
        };
        wire(gp, 1);
        wire(p, 2);
        wire(c, 3);

        wy_instance* c_inst = WY_NULL;
        REQUIRE_EQ(wy_instance_new_f(context, c, &c_inst), WY_ERR_NONE);
        module->globals[0] = wy_value_object(WY_TYPE_TAG_INSTANCE, (wy_object*) c_inst);

        wy_value out[1];
        REQUIRE_EQ(run_fn0(context, module, out, 0), WY_ERR_NONE);
        CHECK_EQ(module->globals[0].type, WY_TYPE_TAG_INSTANCE);  // untouched receiver slot
        CHECK_EQ(module->globals[1].data.word, 1);  // GP_mark ran (hop 2, via P's super)
        CHECK_EQ(module->globals[2].data.word, 1);  // P_mark ran (hop 1, via C's super)
        CHECK_EQ(module->globals[3].data.word, 1);  // C_mark ran (the initial msg send)
    }
}
