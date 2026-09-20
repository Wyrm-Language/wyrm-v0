#include "vm_internal.h"

#include <wyrm.h>
#include <wyrm/error.h>
#include <wyrm/function.h>
#include <wyrm/native.h>
#include <wyrm/string.h>
#include <wyrm/vm.h>

/**
 * The dispatch loop (design_c_vm.md §2). Entry point and continuation for
 * every bytecode frame; loops across bytecode-to-bytecode calls/returns
 * with no C recursion, yielding back to the fiber trampoline (WY_EXEC_*)
 * only for a native call, a fault, or a GC-abandoned-mid-instruction edge
 * case (none in this milestone).
 */

static wy_value fault_value_f(wy_context* ctx, const char* message)
{
    wy_string* what = WY_NULL;
    if (wy_string_strdup(ctx, message, &what) != WY_ERR_NONE) { return wy_value_word(-1); }
    wy_error_obj* obj = WY_NULL;
    if (wy_error_obj_new(ctx, WY_NULL, what, wy_value_nil(), &obj) != WY_ERR_NONE) { return wy_value_word(-1); }
    return wy_value_object(WY_TYPE_TAG_ERROR, (wy_object*) obj);
}

/** Push a bytecode call frame for `fn` (design_c_vm.md §1.1.2). Fast path only: argc must equal nparams. */
static wy_error push_bytecode_call_f(wy_context* ctx, wy_function* fn, const wy_value* args, wy_uword argc,
    wy_value* ret_dst, wy_uword nres, wy_ret_kind ret_kind)
{
    wy_fiber* fiber = ctx->current_fiber;
    const wy_function_proto* proto = fn->proto;

    if (argc != proto->nparams) { return WY_ERR_ARITY; /* slow-path binding is epic 3 */ }

    wy_frame* new_frame = fiber->current_frame + 1;
    if (!WY_MEM_INFO_TOP_NOT_AT_END(wy_frame, new_frame, &fiber->frame_memory)) { return WY_ERR_STACK_OVERFLOW; }

    wy_uword p_count = (wy_uword) proto->nparams + fn->ncaps;
    wy_uword total = p_count + proto->nlocals;
    if (wy_stack_capacity_remaining_f(&fiber->value_stack) < total) { return WY_ERR_STACK_OVERFLOW; }

    wy_value* p = fiber->value_stack.top;
    wy_value* l = p + p_count;
    fiber->value_stack.top = l + proto->nlocals;

    for (wy_uword i = 0; i < argc; i++) { p[i] = args[i]; }
    for (wy_uword i = 0; i < fn->ncaps; i++) { p[argc + i] = fn->caps[i]; }
    for (wy_uword i = 0; i < proto->nlocals; i++) { l[i] = wy_value_unset(); }

    wy_memset(new_frame, 0, sizeof(wy_frame));
    new_frame->kind = WY_FRAME_BYTECODE;
    new_frame->ret_kind = (wy_u8) ret_kind;
    new_frame->ret_dst = ret_dst;
    new_frame->ret_nres = (wy_u16) nres;
    new_frame->p = p;
    new_frame->l = l;
    new_frame->p_count = (wy_u16) p_count;
    new_frame->ip = fn->module->code + proto->code_offset;
    new_frame->module = fn->module;
    new_frame->proto = proto;

    fiber->current_frame = new_frame;
    return WY_ERR_NONE;
}

/**
 * Unwind every bytecode frame above the nearest native/root frame after a
 * fault, reclaiming their combined stack space in one step. No defer
 * draining (fast path; defers land in epic 3).
 */
static void unwind_on_fault_f(wy_fiber* fiber, wy_value fault)
{
    fiber->fault = fault;
    wy_value* top_after_unwind = WY_NULL;
    while (fiber->current_frame->kind == WY_FRAME_BYTECODE) {
        top_after_unwind = fiber->current_frame->p;
        fiber->current_frame--;
    }
    if (top_after_unwind != WY_NULL) { fiber->value_stack.top = top_after_unwind; }
}

wy_exec_state wy_vm_run(wy_context* ctx, wy_primitive unused)
{
    WY_UNUSED(unused);
    wy_fiber* fb = ctx->current_fiber;

reload:;
    wy_frame* fr = fb->current_frame;
    const wy_u32* ip = fr->ip;
    wy_value* L = fr->l;
    wy_module* mod = fr->module;
    wy_value* G = mod->globals;

    for (;;) {
        if (ctx->gc_pressure > ctx->gc_threshold) {
            fr->ip = ip;
            wy_context_gc_safepoint(ctx);
        }

        wy_u32 w0 = *ip;
        wy_u8 op = (wy_u8) (w0 & 0xffu);
        wy_u8 f = (wy_u8) ((w0 >> 8) & 0xffu);
        wy_u16 a0 = (wy_u16) (w0 >> 16);
        wy_u16 a1 = 0, a2 = 0;
        wy_u32 w1 = 0;
        wy_uword words = (op & WYRM_OP_LONG_START) ? 2u : 1u;
        if (words == 2) {
            w1 = ip[1];
            a1 = (wy_u16) (w1 >> 16);
            a2 = (wy_u16) (w1 & 0xffffu);
        }
        const wy_u32* next_ip = ip + words;

        switch (op) {

        /* -- core one-word ops -- */
        case WY_OP_NOOP:
            ip = next_ip;
            break;

        case WY_OP_TRAP:
            fr->ip = next_ip;
            unwind_on_fault_f(fb, fault_value_f(ctx, f == 0 ? "trap: unreachable code" : "trap: debugger break"));
            return WY_EXEC_FAULT;

        case WY_OP_RETURN: {
            wy_uword base = a0, count = f;
            switch ((wy_ret_kind) fr->ret_kind) {
            case WY_RET_WINDOW:
                wy_vm_backfill_f(fr->ret_dst, fr->ret_nres, &L[base], count);
                break;
            default:
                /* RESERVED/CONSTRUCT/... are not produced by a BYTECODE
                 * frame push in this milestone (epic 3+). */
                break;
            }
            fb->value_stack.top = fr->p;
            fb->current_frame = fr - 1;
            if (fb->current_frame->kind == WY_FRAME_NATIVE) { return WY_EXEC_DONE; }
            goto reload;
        }

        case WY_OP_LNIL:
            *wy_vm_reg_f(fr, a0) = wy_value_nil();
            ip = next_ip;
            break;

        case WY_OP_LBOOL:
            *wy_vm_reg_f(fr, a0) = wy_value_bool(f != 0);
            ip = next_ip;
            break;

        case WY_OP_LUNSET:
            *wy_vm_reg_f(fr, a0) = wy_value_unset();
            ip = next_ip;
            break;

        /* -- pairable ops: compact packs the second operand in f, wide in a1/w1 -- */
        case WY_OP_I8:
            *wy_vm_reg_f(fr, a0) = wy_value_word((int8_t) f);
            ip = next_ip;
            break;
        case WY_OP_I32_WIDE:
            *wy_vm_reg_f(fr, a0) = wy_value_word((wy_word) (wy_i32) w1);
            ip = next_ip;
            break;

        case WY_OP_MOVE:
            *wy_vm_reg_f(fr, a0) = *wy_vm_reg8_f(fr, f);
            ip = next_ip;
            break;
        case WY_OP_MOVE_WIDE:
            *wy_vm_reg_f(fr, a0) = *wy_vm_reg_f(fr, a1);
            ip = next_ip;
            break;

        case WY_OP_GGET: case WY_OP_GGET_WIDE: {
            wy_uword dst = (op == WY_OP_GGET) ? f : a1;
            wy_value g = G[a0];
            if (wy_value_is_unset(g)) {
                unwind_on_fault_f(fb, fault_value_f(ctx, "unbound global"));
                fr->ip = next_ip;
                return WY_EXEC_FAULT;
            }
            *wy_vm_reg8_f(fr, (wy_u8) dst) = g;
            ip = next_ip;
            break;
        }
        case WY_OP_GSET: case WY_OP_GSET_WIDE: {
            wy_uword src = (op == WY_OP_GSET) ? f : a1;
            G[a0] = *wy_vm_reg8_f(fr, (wy_u8) src);
            ip = next_ip;
            break;
        }

        case WY_OP_LSYM: case WY_OP_LSYM_WIDE: {
            wy_uword dst = (op == WY_OP_LSYM) ? f : a1;
            *wy_vm_reg8_f(fr, (wy_u8) dst) = wy_value_symbol(mod->symbols[a0]);
            ip = next_ip;
            break;
        }
        case WY_OP_LCONST: case WY_OP_LCONST_WIDE: {
            wy_uword dst = (op == WY_OP_LCONST) ? f : a1;
            *wy_vm_reg8_f(fr, (wy_u8) dst) = mod->statics[a0];
            ip = next_ip;
            break;
        }

        case WY_OP_NEG: case WY_OP_NEG_WIDE:
        case WY_OP_INV: case WY_OP_INV_WIDE:
        case WY_OP_NOT: case WY_OP_NOT_WIDE: {
            bool wide = (op & WYRM_OP_LONG_START) != 0;
            wy_uword src = wide ? a1 : f;
            wy_u8 base_op = wide ? (wy_u8)(op & ~WYRM_OP_LONG_START) : op;
            wy_value out;
            wy_error err = wy_vm_unary_f(ctx, base_op, *wy_vm_reg8_f(fr, (wy_u8) src), &out);
            if (err != WY_ERR_NONE) {
                fr->ip = next_ip;
                unwind_on_fault_f(fb, fault_value_f(ctx, "unary operator failed"));
                return WY_EXEC_FAULT;
            }
            *wy_vm_reg_f(fr, a0) = out;
            ip = next_ip;
            break;
        }

        case WY_OP_JF: case WY_OP_JF_WIDE:
        case WY_OP_JT: case WY_OP_JT_WIDE:
        case WY_OP_JERR: case WY_OP_JERR_WIDE:
        case WY_OP_JNERR: case WY_OP_JNERR_WIDE: {
            bool wide = (op & WYRM_OP_LONG_START) != 0;
            wy_value cond = wide ? *wy_vm_reg_f(fr, a0) : *wy_vm_reg8_f(fr, f);
            wy_i32 rel = wide ? (wy_i32) w1 : (int16_t) a0;
            wy_u8 base_op = wide ? (wy_u8) (op & ~WYRM_OP_LONG_START) : op;
            bool take;
            switch (base_op) {
            case WY_OP_JF:    take = !wy_value_truthy(cond); break;
            case WY_OP_JT:    take = wy_value_truthy(cond); break;
            case WY_OP_JERR:  take = wy_value_is_error(cond); break;
            case WY_OP_JNERR: take = !wy_value_is_error(cond); break;
            default:          take = false; break;
            }
            ip = take ? (next_ip + rel) : next_ip;
            break;
        }
        case WY_OP_JMP: {
            wy_i32 rel = (int16_t) a0;
            ip = next_ip + rel;
            break;
        }
        case WY_OP_JMP_WIDE: {
            wy_i32 rel = (wy_i32) w1;
            ip = next_ip + rel;
            break;
        }

        /* -- two-word ops -- */
        case WY_OP_F32: {
            wy_u32 bits = w1;
            float value;
            wy_memcpy(&value, &bits, sizeof(value));
            *wy_vm_reg_f(fr, a0) = wy_value_float((double) value);
            ip = next_ip;
            break;
        }

        case WY_OP_ADD: case WY_OP_SUB: case WY_OP_MUL: case WY_OP_DIV: case WY_OP_MOD:
        case WY_OP_POW: case WY_OP_BAND: case WY_OP_BOR: case WY_OP_SHL: case WY_OP_SHR:
        case WY_OP_EQ: case WY_OP_NE: case WY_OP_LT: case WY_OP_LE: case WY_OP_GT: case WY_OP_GE:
        case WY_OP_BXOR: case WY_OP_CMP3: {
            wy_value out;
            wy_error err = wy_vm_binop_f(ctx, op, *wy_vm_reg_f(fr, a1), *wy_vm_reg_f(fr, a2), &out);
            if (err != WY_ERR_NONE) {
                fr->ip = next_ip;
                unwind_on_fault_f(fb, fault_value_f(ctx, "binary operator failed"));
                return WY_EXEC_FAULT;
            }
            *wy_vm_reg_f(fr, a0) = out;
            ip = next_ip;
            break;
        }

        case WY_OP_IS: {
            wy_value out;
            wy_error err = wy_vm_is_f(ctx, *wy_vm_reg_f(fr, a1), *wy_vm_reg_f(fr, a2), &out);
            if (err != WY_ERR_NONE) {
                fr->ip = next_ip;
                unwind_on_fault_f(fb, fault_value_f(ctx, "`is` failed"));
                return WY_EXEC_FAULT;
            }
            *wy_vm_reg_f(fr, a0) = out;
            ip = next_ip;
            break;
        }

        case WY_OP_CLOSURE: {
            if (f != 0) {
                fr->ip = next_ip;
                unwind_on_fault_f(fb, fault_value_f(ctx, "closures with captures are not supported until epic 3"));
                return WY_EXEC_FAULT;
            }
            wy_function* fn = WY_NULL;
            if (wy_function_new(ctx, mod, &mod->functions[a1], WY_NULL, 0, &fn) != WY_ERR_NONE) {
                fr->ip = next_ip;
                unwind_on_fault_f(fb, fault_value_f(ctx, "out of memory"));
                return WY_EXEC_FAULT;
            }
            *wy_vm_reg_f(fr, a0) = wy_value_object(WY_TYPE_TAG_FUNCTION, (wy_object*) fn);
            ip = next_ip;
            break;
        }

        case WY_OP_CALL: {
            wy_uword base = a0, argc = f, nres = a1;
            wy_value callee = L[base];

            if (callee.type == WY_TYPE_TAG_FUNCTION) {
                fr->ip = next_ip;
                wy_error err = push_bytecode_call_f(ctx, (wy_function*) callee.data.gc_object,
                    &L[base + 1], argc, &L[base], nres, WY_RET_WINDOW);
                if (err != WY_ERR_NONE) {
                    unwind_on_fault_f(fb, fault_value_f(ctx,
                        err == WY_ERR_ARITY ? "wrong argument count" : "call failed"));
                    return WY_EXEC_FAULT;
                }
                goto reload;
            }

            if (callee.type == WY_TYPE_TAG_NATIVE) {
                wy_native* native = (wy_native*) callee.data.gc_object;
                if (native->kind != WY_NATIVE_LEAF) {
                    fr->ip = next_ip;
                    unwind_on_fault_f(fb, fault_value_f(ctx,
                        "exec natives called from bytecode are not supported until epic 3+"));
                    return WY_EXEC_FAULT;
                }
                wy_error err = wy_vm_call_leaf_f(ctx, native, &L[base + 1], argc, &L[base], nres);
                if (err != WY_ERR_NONE) {
                    fr->ip = next_ip;
                    unwind_on_fault_f(fb, fault_value_f(ctx,
                        err == WY_ERR_ARITY ? "wrong argument count" : "native call failed"));
                    return WY_EXEC_FAULT;
                }
                /* Backfill nil past whatever the leaf actually wrote, same
                 * as a bytecode return would (leaf natives write directly,
                 * they don't know nres vs. count the way RETURN's operand
                 * does - callers below (println/print) always fill every
                 * reserved slot themselves). */
                ip = next_ip;
                break;
            }

            fr->ip = next_ip;
            unwind_on_fault_f(fb, fault_value_f(ctx, "value is not callable"));
            return WY_EXEC_FAULT;
        }

        default:
            fr->ip = next_ip;
            unwind_on_fault_f(fb, fault_value_f(ctx, "unknown or unimplemented opcode"));
            return WY_EXEC_FAULT;
        }
    }
}

/**
 * Continuation of every bytecode frame after a native exec call. Not
 * reachable from this milestone's own dispatch (WY_OP_CALL only pushes
 * leaf natives; exec natives from bytecode fault), but declared per
 * design_c_vm.md §1.3 for epic 3+ to wire up.
 */
wy_error wy_vm_call_sync(wy_context* ctx, wy_value callee, const wy_value* args, wy_uword argc, wy_value* out, wy_uword nres)
{
    if (ctx == WY_NULL || ctx->current_fiber == WY_NULL) { return WY_ERR_INVAL; }
    if (callee.type != WY_TYPE_TAG_FUNCTION) { return WY_ERR_BAD_TYPE; }

    wy_error err = push_bytecode_call_f(ctx, (wy_function*) callee.data.gc_object, args, argc, out, nres, WY_RET_WINDOW);
    if (err != WY_ERR_NONE) { return err; }

    wy_exec_state state = wy_vm_run(ctx, wy_primitive_null());
    if (state == WY_EXEC_DONE) { return WY_ERR_NONE; }
    if (state == WY_EXEC_FAULT) { return WY_ERR_FAULT; }

    /* A native call is pending (WY_EXEC_CONTINUE): drive it to completion
     * through the ordinary fiber trampoline, which will re-enter wy_vm_run
     * as the continuation once it resolves (epic 3+ machinery; unreached
     * by this milestone's own callers). */
    return wy_fiber_exec_f(ctx->current_fiber, ctx);
}

wy_error wy_vm_call_continue(wy_context* ctx, wy_exec_fn continuation, wy_value callee,
    const wy_value* args, wy_uword argc, wy_uword nres)
{
    WY_UNUSED(ctx); WY_UNUSED(continuation); WY_UNUSED(callee); WY_UNUSED(args); WY_UNUSED(argc); WY_UNUSED(nres);
    return WY_ERR_NOSUPPORT;  /* epic 3+: natives calling back into the VM */
}

/**
 * Stub pending the epic 2 interpreter loop rewrite against the adopted
 * opcode.h encoding (E1/M2): always fails rather than decoding with the
 * retired enum.
 */
wy_error wy_vm_exec_bytecode(wy_context* ctx, size_t pos, const wy_u32 buffer[], size_t len)
{
    WY_UNUSED(ctx);
    WY_UNUSED(pos);
    WY_UNUSED(buffer);
    WY_UNUSED(len);
    return WY_ERR_INVAL;
}


/**
 * Enter bytecode from a callable payload
 *
 * Unpacks the (module id, address) pair the callable carries and resolves
 * the module the address belongs to. The interpreter loop itself is not
 * wired up yet, so this returns immediately.
 *
 * @param context Context whose current fiber holds the call frame
 * @param c_data Packed module id and code address
 * @return Execution state for the fiber loop
 */
wy_exec_state wy_vm_exec_b_code(wy_context* context, wy_primitive c_data)
{
    WY_ASSERT(context != WY_NULL);

    wy_uword module_id = wy_exec_fn_b_code_module_id(c_data);
    wy_uword address = wy_exec_fn_b_code_address(c_data);

    wy_module* module = wy_context_get_module(context, module_id);
    WY_UNUSED(module);
    WY_UNUSED(address);

    return WY_EXEC_DONE;
}
